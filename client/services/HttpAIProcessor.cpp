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
#include <QtConcurrent/QtConcurrent>

namespace {
// #region debug-point F:http-ai-processor
void postDebugEvent(const char *hypothesisId, const char *location, const QString &message, const QJsonObject &data)
{
    if (!qEnvironmentVariableIsSet("SM_DEBUG_EVENTS")) {
        return;
    }
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

QByteArray encodeImageToBase64(const QImage &frame, int maxEdge, int jpegQuality, QSize *encodedSize)
{
    QImage transportFrame = frame;
    const QSize size = frame.size();
    const int longestEdge = qMax(size.width(), size.height());
    const int edge = qMax(64, maxEdge);
    if (longestEdge > edge) {
        transportFrame = frame.scaled(edge, edge, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    if (encodedSize) {
        *encodedSize = transportFrame.size();
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const int quality = qMax(1, qMin(95, jpegQuality));
    if (!transportFrame.save(&buffer, "JPEG", quality)) {
        bytes.clear();
        buffer.close();
        buffer.setBuffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        transportFrame.save(&buffer, "PNG");
    }
    return bytes.toBase64();
}
}

HttpAIProcessor::HttpAIProcessor(const QUrl &endpoint,
                                 int timeoutMs,
                                 int maxInFlightRequests,
                                 const QString &modelVersion,
                                 const QString &policyVersion,
                                 int minFrameIntervalMs,
                                 int transportMaxEdge,
                                 int transportJpegQuality,
                                 QObject *parent)
    : AIProcessor(parent)
    , m_processEndpoint(endpoint)
    , m_timeoutMs(timeoutMs)
    , m_maxInFlightRequests(qMax(1, maxInFlightRequests))
    , m_minFrameIntervalMs(qMax(0, minFrameIntervalMs))
    , m_transportMaxEdge(qMax(64, transportMaxEdge))
    , m_transportJpegQuality(qMax(1, qMin(95, transportJpegQuality)))
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
                                        bool objectDetectionEnabled)
{
    m_roomId = roomId.trimmed();
    m_whitelistUserIds = whitelistUserIds;
    m_facePrivacyEnabled = facePrivacyEnabled;
    m_objectDetectionEnabled = objectDetectionEnabled;
    m_cachedPrivacyRegions.clear();
    m_havePrivacyMetadata = false;
    emit privacyRegionsUpdated(m_cachedPrivacyRegions);
    logEvent("privacy_context_set",
             {{"room_id", m_roomId},
              {"whitelist_count", m_whitelistUserIds.size()},
              {"face_privacy", facePrivacyEnabled},
              {"object_detection", objectDetectionEnabled}});
    // #region debug-point F:privacy-context
    postDebugEvent("F",
                   "client/services/HttpAIProcessor.cpp:setPrivacyContext",
                   "[DEBUG] privacy context set",
                   QJsonObject{{"room_id", m_roomId},
                               {"whitelist_count", m_whitelistUserIds.size()},
                               {"face_privacy", facePrivacyEnabled},
                               {"object_detection", objectDetectionEnabled}});
    // #endregion
}

void HttpAIProcessor::clearPrivacyContext()
{
    if (m_roomId.isEmpty()) {
        m_roomId.clear();
        m_whitelistUserIds.clear();
        m_cachedPrivacyRegions.clear();
        m_havePrivacyMetadata = false;
        emit privacyRegionsUpdated(m_cachedPrivacyRegions);
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
    m_cachedPrivacyRegions.clear();
    m_havePrivacyMetadata = false;
    emit privacyRegionsUpdated(m_cachedPrivacyRegions);
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
    body.insert("image", QString::fromLatin1(imageToBase64(frame, 640, 75)));
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
    body.insert("image", QString::fromLatin1(imageToBase64(frame, 640, 75)));
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
        m_cachedPrivacyRegions.clear();
        m_havePrivacyMetadata = false;
        emit privacyRegionsUpdated(m_cachedPrivacyRegions);
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

    const int maxInFlight = qMax(1, m_maxInFlightRequests);
    if (m_encodeInFlight || m_inFlight.size() >= maxInFlight) {
        m_dropped += 1;
        if (m_dropped % 30 == 1) {
            logEvent("frame_dropped",
                     {{"reason", "backpressure"},
                      {"in_flight", m_inFlight.size()},
                      {"encode_in_flight", m_encodeInFlight},
                      {"max_in_flight", maxInFlight}});
        }
        logMetricsIfNeeded();
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 throttleBaseMs = qMax(m_lastFrameSentMs, m_lastFrameCompletedMs);
    if (m_minFrameIntervalMs > 0 && throttleBaseMs > 0 && (nowMs - throttleBaseMs) < m_minFrameIntervalMs) {
        m_dropped += 1;
        if (m_dropped % 30 == 1) {
            logEvent("frame_dropped",
                     {{"reason", "rate_limited"},
                      {"min_frame_interval_ms", m_minFrameIntervalMs},
                      {"elapsed_ms", nowMs - throttleBaseMs}});
        }
        logMetricsIfNeeded();
        return;
    }

    const QString requestId = newId();
    const QString traceId = newId();
    const qint64 frameSequence = ++m_nextFrameSequence;
    const int transportMaxEdge = m_transportMaxEdge;
    const int transportJpegQuality = m_transportJpegQuality;
    m_lastFrameSentMs = nowMs;
    m_encodeInFlight = true;

    auto *watcher = new QFutureWatcher<EncodedFrame>(this);
    connect(watcher, &QFutureWatcher<EncodedFrame>::finished, this, [this, watcher]() {
        m_encodeInFlight = false;
        const EncodedFrame encoded = watcher->result();
        watcher->deleteLater();
        if (!m_enabled || encoded.imageBase64.isEmpty()) {
            return;
        }
        if (m_inFlight.size() >= qMax(1, m_maxInFlightRequests)) {
            m_dropped += 1;
            logMetricsIfNeeded();
            return;
        }
        postProcessRequest(encoded);
    });
    watcher->setFuture(QtConcurrent::run([frame, transportMaxEdge, transportJpegQuality, requestId, traceId, frameSequence]() {
        EncodedFrame out;
        out.requestId = requestId;
        out.traceId = traceId;
        out.frameSequence = frameSequence;
        out.imageBase64 = encodeImageToBase64(frame, transportMaxEdge, transportJpegQuality, &out.transportSize);
        return out;
    }));
}

void HttpAIProcessor::postProcessRequest(const EncodedFrame &encoded)
{
    QJsonObject body;
    body.insert("image", QString::fromLatin1(encoded.imageBase64));
    body.insert("request_id", encoded.requestId);
    body.insert("trace_id", encoded.traceId);
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
    body.insert("return_image", false);
    // #region debug-point H:frame-send
    postDebugEvent("H",
                   "client/services/HttpAIProcessor.cpp:processFrame:send",
                   "[DEBUG] processFrame request prepared",
                   QJsonObject{{"room_id", m_roomId},
                               {"whitelist_count", m_whitelistUserIds.size()},
                               {"face_privacy", m_facePrivacyEnabled},
                               {"object_detection", m_objectDetectionEnabled},
                               {"transport_max_edge", m_transportMaxEdge},
                               {"transport_jpeg_quality", m_transportJpegQuality}});
    // #endregion

    QNetworkRequest request(m_processEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    InFlightInfo info;
    info.timer.start();
    info.requestId = encoded.requestId;
    info.traceId = encoded.traceId;
    info.facePrivacyEnabled = m_facePrivacyEnabled;
    info.frameSequence = encoded.frameSequence;
    info.transportSize = encoded.transportSize;
    m_inFlight.insert(reply, info);
    if (m_framesIn % 30 == 0) {
        logEvent("frame_send", {{"request_id", encoded.requestId}, {"trace_id", encoded.traceId}, {"in_flight", m_inFlight.size()}});
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray payload = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        const InFlightInfo info = m_inFlight.value(reply);
        const double latencyMs = info.timer.isValid() ? static_cast<double>(info.timer.elapsed()) : 0.0;
        m_totalLatencyMs += latencyMs;
        m_inFlight.remove(reply);
        m_lastFrameCompletedMs = QDateTime::currentMSecsSinceEpoch();

        if (!m_enabled) {
            reply->deleteLater();
            return;
        }

        if (ok) {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QJsonObject obj = doc.object();
            m_totalInferenceMs += obj.value("latency_ms").toDouble(0.0);
            updateCachedPrivacyRegions(obj, info.transportSize);
            m_framesOut += 1;
            if (m_framesOut % 30 == 0) {
                logEvent("frame_ok",
                         {{"request_id", info.requestId},
                          {"trace_id", info.traceId},
                          {"latency_ms", latencyMs},
                          {"server_latency_ms", obj.value("latency_ms").toDouble(0.0)}});
            }
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

        logMetricsIfNeeded();
        reply->deleteLater();
    });
}

QByteArray HttpAIProcessor::imageToBase64(const QImage &frame, int maxEdge, int jpegQuality, QSize *encodedSize) const
{
    return encodeImageToBase64(frame, maxEdge, jpegQuality, encodedSize);
}

void HttpAIProcessor::updateCachedPrivacyRegions(const QJsonObject &response, const QSize &imageSize)
{
    QVector<QRectF> nextRegions;
    m_havePrivacyMetadata = true;
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        return;
    }

    const auto appendBox = [&nextRegions, &imageSize](const QJsonArray &bbox,
                                                      double padX,
                                                      double padTop,
                                                      double padBottom) {
        if (bbox.size() < 4) {
            return;
        }
        const double x1 = bbox.at(0).toDouble();
        const double y1 = bbox.at(1).toDouble();
        const double x2 = bbox.at(2).toDouble();
        const double y2 = bbox.at(3).toDouble();
        const double boxWidth = qMax(1.0, x2 - x1);
        const double boxHeight = qMax(1.0, y2 - y1);
        QRectF normalized(QPointF((x1 - boxWidth * padX) / imageSize.width(),
                                  (y1 - boxHeight * padTop) / imageSize.height()),
                          QPointF((x2 + boxWidth * padX) / imageSize.width(),
                                  (y2 + boxHeight * padBottom) / imageSize.height()));
        normalized = normalized.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
        if (normalized.width() > 0.0 && normalized.height() > 0.0) {
            nextRegions.append(normalized);
        }
    };

    const QJsonArray detections = response.value("detections").toArray();
    for (const QJsonValue &value : detections) {
        const QJsonObject det = value.toObject();
        if (det.value("blurred").toBool(false) || det.value("sensitive").toBool(false)) {
            appendBox(det.value("bbox").toArray(), 0.08, 0.08, 0.08);
        }
    }

    const QJsonArray faces = response.value("faces").toArray();
    for (const QJsonValue &value : faces) {
        const QJsonObject face = value.toObject();
        if (face.value("blurred").toBool(false)) {
            appendBox(face.value("bbox").toArray(), 0.38, 0.45, 0.35);
        }
    }

    m_cachedPrivacyRegions = nextRegions;

    emit privacyRegionsUpdated(m_cachedPrivacyRegions);
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
