#include "services/HttpAIProcessor.h"

#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUuid>

HttpAIProcessor::HttpAIProcessor(const QUrl &endpoint,
                                 int timeoutMs,
                                 int maxInFlightRequests,
                                 const QString &modelVersion,
                                 const QString &policyVersion,
                                 QObject *parent)
    : AIProcessor(parent)
    , m_endpoint(endpoint)
    , m_timeoutMs(timeoutMs)
    , m_maxInFlightRequests(maxInFlightRequests)
    , m_modelVersion(modelVersion)
    , m_policyVersion(policyVersion)
{
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
}

void HttpAIProcessor::setEnabled(bool enabled)
{
    m_enabled = enabled;
    logEvent("ai_toggle", {{"enabled", enabled}});
}

bool HttpAIProcessor::isEnabled() const
{
    return m_enabled;
}

void HttpAIProcessor::processFrame(const QImage &frame)
{
    m_framesIn += 1;

    if (!m_enabled || !m_endpoint.isValid()) {
        if (!m_enabled) {
            logEvent("ai_bypass", {{"reason", "disabled"}});
        }
        emit frameProcessed(frame);
        return;
    }

    if (m_inFlight.size() >= m_maxInFlightRequests) {
        m_dropped += 1;
        logEvent("frame_dropped", {{"reason", "backpressure"}, {"in_flight", m_inFlight.size()}});
        emit frameProcessed(frame);
        logMetricsIfNeeded();
        return;
    }

    const QString requestId = newId();
    const QString traceId = newId();
    const QByteArray encoded = imageToBase64(frame);
    QJsonObject body;
    body.insert("image", QString::fromLatin1(encoded));
    body.insert("request_id", requestId);
    body.insert("trace_id", traceId);
    body.insert("model_version", m_modelVersion);
    body.insert("policy_version", m_policyVersion);

    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    InFlightInfo info;
    info.timer.start();
    info.requestId = requestId;
    info.traceId = traceId;
    m_inFlight.insert(reply, info);
    logEvent("frame_send", {{"request_id", requestId}, {"trace_id", traceId}, {"in_flight", m_inFlight.size()}});

    connect(reply, &QNetworkReply::finished, this, [this, reply, frame]() {
        const QByteArray payload = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const InFlightInfo info = m_inFlight.value(reply);
        const double latencyMs = info.timer.isValid() ? static_cast<double>(info.timer.elapsed()) : 0.0;
        m_totalLatencyMs += latencyMs;
        m_inFlight.remove(reply);

        QImage output = frame;
        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QJsonObject obj = doc.object();
            const QByteArray outBase64 = obj.value("image").toString().toLatin1();
            m_totalInferenceMs += obj.value("latency_ms").toDouble(0.0);
            const QImage decoded = base64ToImage(outBase64);
            if (!decoded.isNull()) {
                output = decoded;
            }
            m_framesOut += 1;
            logEvent("frame_ok",
                     {{"request_id", info.requestId},
                      {"trace_id", info.traceId},
                      {"latency_ms", latencyMs},
                      {"server_latency_ms", obj.value("latency_ms").toDouble(0.0)}});
        } else {
            m_failed += 1;
            logEvent("frame_fail",
                     {{"request_id", info.requestId},
                      {"trace_id", info.traceId},
                      {"latency_ms", latencyMs},
                      {"error", reply->errorString()}});
        }

        emit frameProcessed(output);
        logMetricsIfNeeded();
        reply->deleteLater();
    });
}

QByteArray HttpAIProcessor::imageToBase64(const QImage &frame) const
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "PNG");
    return bytes.toBase64();
}

QImage HttpAIProcessor::base64ToImage(const QByteArray &base64) const
{
    const QByteArray bytes = QByteArray::fromBase64(base64);
    QImage image;
    image.loadFromData(bytes, "PNG");
    return image;
}

void HttpAIProcessor::logEvent(const QString &event, const QJsonObject &extra) const
{
    QJsonObject root{
        {"ts_ms", QDateTime::currentMSecsSinceEpoch()},
        {"service", "client"},
        {"component", "http_ai_processor"},
        {"event", event}
    };
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        root.insert(it.key(), it.value());
    }
    qInfo().noquote() << QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void HttpAIProcessor::logMetricsIfNeeded() const
{
    const int every = 30;
    const int total = m_framesOut + m_failed + m_dropped;
    if (total == 0 || total % every != 0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double durationSec = static_cast<double>(now - m_startedMs) / 1000.0;
    const double fps = durationSec > 0.0 ? static_cast<double>(m_framesOut) / durationSec : 0.0;
    const double failRate = m_framesIn > 0 ? static_cast<double>(m_failed) / m_framesIn : 0.0;
    const double dropRate = m_framesIn > 0 ? static_cast<double>(m_dropped) / m_framesIn : 0.0;
    const double avgE2e = total > 0 ? m_totalLatencyMs / total : 0.0;
    const double avgInference = m_framesOut > 0 ? m_totalInferenceMs / m_framesOut : 0.0;
    logEvent("metrics_snapshot",
             {{"fps", fps},
              {"avg_e2e_latency_ms", avgE2e},
              {"avg_ai_latency_ms", avgInference},
              {"drop_rate", dropRate},
              {"failure_rate", failRate},
              {"frames_in", m_framesIn},
              {"frames_out", m_framesOut}});
}

QString HttpAIProcessor::newId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
