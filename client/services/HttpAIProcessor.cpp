#include "services/HttpAIProcessor.h"

#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

namespace {
// #region debug-point F:http-ai-processor
void postDebugEvent(const char *hypothesisId, const char *location, const QString &message, const QJsonObject &data)
{
    QString url = QStringLiteral("http://127.0.0.1:7777/event");
    QString sessionId = QStringLiteral("face-profile-upload");
    QFile envFile(QStringLiteral(".dbg/face-profile-upload.env"));
    if (envFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(envFile.readAll());
        const QRegularExpression urlRe(QStringLiteral("^DEBUG_SERVER_URL=(.+)$"), QRegularExpression::MultilineOption);
        const QRegularExpression sessionRe(QStringLiteral("^DEBUG_SESSION_ID=(.+)$"), QRegularExpression::MultilineOption);
        const QRegularExpressionMatch urlMatch = urlRe.match(content);
        const QRegularExpressionMatch sessionMatch = sessionRe.match(content);
        if (urlMatch.hasMatch()) url = urlMatch.captured(1).trimmed();
        if (sessionMatch.hasMatch()) sessionId = sessionMatch.captured(1).trimmed();
    }
    static QNetworkAccessManager *manager = nullptr;
    if (!manager) manager = new QNetworkAccessManager();
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject payload{
        {"sessionId", sessionId},
        {"runId", "pre-fix"},
        {"hypothesisId", hypothesisId},
        {"location", location},
        {"msg", message},
        {"data", data}
    };
    manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}
// #endregion
}

HttpAIProcessor::HttpAIProcessor(const QUrl &endpoint,
                                 int timeoutMs,
                                 int maxInFlightRequests,
                                 const QString &modelVersion,
                                 const QString &policyVersion,
                                 QObject *parent)
    : AIProcessor(parent)
    , m_processEndpoint(endpoint)
    , m_timeoutMs(timeoutMs)
    , m_maxInFlightRequests(maxInFlightRequests)
    , m_modelVersion(modelVersion)
    , m_policyVersion(policyVersion)
{
    m_apiBase = endpoint;
    const QString path = endpoint.path();
    const int cut = path.lastIndexOf(QStringLiteral("/process_frame"));
    if (cut > 0) {
        m_apiBase.setPath(path.left(cut));
    }
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
}

QUrl HttpAIProcessor::apiUrl(const QString &path) const
{
    QUrl url = m_apiBase;
    const QString basePath = url.path();
    const QString joined = basePath.endsWith(QLatin1Char('/'))
        ? basePath + path
        : basePath + QLatin1Char('/') + path;
    url.setPath(joined);
    return url;
}

void HttpAIProcessor::setPrivacyContext(const QString &roomId,
                                        const QStringList &whitelistUserIds,
                                        bool facePrivacyEnabled,
                                        bool objectDetectionEnabled,
                                        const QString &privacyProtectMode)
{
    m_roomId = roomId.trimmed();
    m_whitelistUserIds = whitelistUserIds;
    m_facePrivacyEnabled = facePrivacyEnabled;
    m_objectDetectionEnabled = objectDetectionEnabled;
    m_privacyProtectMode = privacyProtectMode.trimmed().isEmpty() ? QStringLiteral("blur") : privacyProtectMode.trimmed();
    logEvent("privacy_context_set",
             {{"room_id", m_roomId},
              {"whitelist_count", m_whitelistUserIds.size()},
              {"face_privacy", facePrivacyEnabled},
              {"object_detection", objectDetectionEnabled},
              {"privacy_protect_mode", m_privacyProtectMode}});
    // #region debug-point F:privacy-context
    postDebugEvent("F",
                   "client/services/HttpAIProcessor.cpp:setPrivacyContext",
                   "[DEBUG] privacy context set",
                   QJsonObject{{"room_id", m_roomId},
                               {"whitelist_count", m_whitelistUserIds.size()},
                               {"face_privacy", facePrivacyEnabled},
                               {"object_detection", objectDetectionEnabled},
                               {"privacy_protect_mode", m_privacyProtectMode}});
    // #endregion
}

void HttpAIProcessor::clearPrivacyContext()
{
    if (m_roomId.isEmpty()) {
        m_roomId.clear();
        m_whitelistUserIds.clear();
        m_lastProcessedFrame = QImage();
        return;
    }
    const QString roomId = m_roomId;
    QJsonObject body{{"room_id", roomId}};
    QNetworkRequest request(apiUrl(QStringLiteral("face/clear")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    m_roomId.clear();
    m_whitelistUserIds.clear();
    m_lastProcessedFrame = QImage();
    logEvent("privacy_context_cleared", {{"room_id", roomId}});
}

void HttpAIProcessor::postEnroll(const QString &userId, const QImage &frame)
{
    if (m_roomId.isEmpty() || userId.trimmed().isEmpty() || frame.isNull()) {
        return;
    }
    QJsonObject body;
    body.insert("room_id", m_roomId);
    body.insert("user_id", userId.trimmed());
    body.insert("image", QString::fromLatin1(imageToBase64(frame)));
    body.insert("request_id", newId());
    body.insert("trace_id", newId());

    QNetworkRequest request(apiUrl(QStringLiteral("face/enroll")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        logEvent(ok ? "face_enroll_ok" : "face_enroll_fail",
                 {{"user_id", userId}, {"error", reply->errorString()}});
        reply->deleteLater();
    });
}

void HttpAIProcessor::enrollFace(const QString &userId, const QImage &frame)
{
    postEnroll(userId, frame);
}

QList<FaceProfileSummary> HttpAIProcessor::enrollFaces(const QString &labelPrefix, const QImage &frame)
{
    QList<FaceProfileSummary> enrolled;
    if (m_roomId.isEmpty() || frame.isNull()) {
        return enrolled;
    }

    QJsonObject body;
    body.insert("room_id", m_roomId);
    body.insert("label_prefix", labelPrefix.trimmed());
    body.insert("image", QString::fromLatin1(imageToBase64(frame)));
    body.insert("request_id", newId());
    body.insert("trace_id", newId());

    QNetworkRequest request(apiUrl(QStringLiteral("face/enroll_many")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(m_timeoutMs + 100);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return enrolled;
    }

    if (reply->error() == QNetworkReply::NoError) {
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray items = obj.value("entries").toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString faceId = item.value("face_id").toString().trimmed();
            const QString label = item.value("label").toString().trimmed();
            if (faceId.isEmpty() || label.isEmpty()) continue;
            enrolled.append(FaceProfileSummary{faceId, QString(), label, QStringLiteral("live")});
        }
    }
    reply->deleteLater();
    return enrolled;
}

void HttpAIProcessor::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_lastProcessedFrame = QImage();
    }
    logEvent("ai_toggle", {{"enabled", enabled}});
    // #region debug-point G:ai-toggle
    postDebugEvent("G",
                   "client/services/HttpAIProcessor.cpp:setEnabled",
                   "[DEBUG] ai toggle changed",
                   QJsonObject{{"enabled", enabled}});
    // #endregion
}

bool HttpAIProcessor::isEnabled() const
{
    return m_enabled;
}

void HttpAIProcessor::processFrame(const QImage &frame)
{
    m_framesIn += 1;

    if (!m_enabled) {
        // #region debug-point G:process-bypass-disabled
        postDebugEvent("G",
                       "client/services/HttpAIProcessor.cpp:processFrame",
                       "[DEBUG] processFrame bypassed because AI disabled",
                       QJsonObject{{"room_id", m_roomId}, {"face_privacy", m_facePrivacyEnabled}});
        // #endregion
        emit frameProcessed(frame);
        return;
    }

    if (!m_processEndpoint.isValid()) {
        emit frameProcessed(frame);
        return;
    }

    const bool preferProcessedFrame = m_facePrivacyEnabled;
    const int maxInFlight = preferProcessedFrame ? 1 : m_maxInFlightRequests;
    if (m_inFlight.size() >= maxInFlight) {
        m_dropped += 1;
        logEvent("frame_dropped",
                 {{"reason", "backpressure"},
                  {"in_flight", m_inFlight.size()},
                  {"prefer_processed_frame", preferProcessedFrame}});
        emit frameProcessed(preferProcessedFrame && !m_lastProcessedFrame.isNull() ? m_lastProcessedFrame : frame);
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
    if (!m_roomId.isEmpty()) {
        body.insert("room_id", m_roomId);
    }
    if (!m_whitelistUserIds.isEmpty()) {
        QJsonArray users;
        for (const QString &name : m_whitelistUserIds) {
            users.append(name);
        }
        body.insert("whitelist_user_ids", users);
    }
    body.insert("enable_face_privacy", m_facePrivacyEnabled);
    body.insert("enable_object_detection", m_objectDetectionEnabled);
    if (m_facePrivacyEnabled && !m_privacyProtectMode.isEmpty()) {
        body.insert("privacy_protect_mode", m_privacyProtectMode);
    }
    // #region debug-point H:frame-send
    postDebugEvent("H",
                   "client/services/HttpAIProcessor.cpp:processFrame:send",
                   "[DEBUG] processFrame request prepared",
                   QJsonObject{{"room_id", m_roomId},
                               {"whitelist_count", m_whitelistUserIds.size()},
                               {"face_privacy", m_facePrivacyEnabled},
                               {"object_detection", m_objectDetectionEnabled}});
    // #endregion

    QNetworkRequest request(m_processEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    InFlightInfo info;
    info.timer.start();
    info.requestId = requestId;
    info.traceId = traceId;
    info.facePrivacyEnabled = m_facePrivacyEnabled;
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
                m_lastProcessedFrame = decoded;
            } else if (info.facePrivacyEnabled && !m_lastProcessedFrame.isNull()) {
                output = m_lastProcessedFrame;
            }
            m_framesOut += 1;
            logEvent("frame_ok",
                     {{"request_id", info.requestId},
                      {"trace_id", info.traceId},
                      {"latency_ms", latencyMs},
                      {"server_latency_ms", obj.value("latency_ms").toDouble(0.0)}});
            // #region debug-point H:frame-ok
            postDebugEvent("H",
                           "client/services/HttpAIProcessor.cpp:processFrame:ok",
                           "[DEBUG] processFrame response received",
                           QJsonObject{{"request_id", info.requestId},
                                       {"faces_detected", obj.value("faces_detected").toInt()},
                                       {"faces_blurred", obj.value("faces_blurred").toInt()},
                                       {"blurred_count", obj.value("blurred_count").toInt()},
                                       {"server_latency_ms", obj.value("latency_ms").toDouble(0.0)}});
            // #endregion
        } else {
            m_failed += 1;
            if (info.facePrivacyEnabled && !m_lastProcessedFrame.isNull()) {
                output = m_lastProcessedFrame;
            }
            logEvent("frame_fail",
                     {{"request_id", info.requestId},
                      {"trace_id", info.traceId},
                      {"latency_ms", latencyMs},
                      {"error", reply->errorString()}});
            // #region debug-point H:frame-fail
            postDebugEvent("H",
                           "client/services/HttpAIProcessor.cpp:processFrame:fail",
                           "[DEBUG] processFrame request failed",
                           QJsonObject{{"request_id", info.requestId},
                                       {"error", reply->errorString()}});
            // #endregion
        }

        emit frameProcessed(output);
        logMetricsIfNeeded();
        reply->deleteLater();
    });
}

QByteArray HttpAIProcessor::imageToBase64(const QImage &frame) const
{
    QImage transportFrame = frame;
    const QSize size = frame.size();
    const int longestEdge = qMax(size.width(), size.height());
    if (longestEdge > 640) {
        transportFrame = frame.scaled(640,
                                      640,
                                      Qt::KeepAspectRatio,
                                      Qt::FastTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!transportFrame.save(&buffer, "JPEG", 75)) {
        bytes.clear();
        buffer.close();
        buffer.setBuffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        transportFrame.save(&buffer, "PNG");
    }
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
