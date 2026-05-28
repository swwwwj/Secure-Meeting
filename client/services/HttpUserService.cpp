#include "services/HttpUserService.h"

#include <QBuffer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QRegularExpression>

namespace {
// #region debug-point A:http-user-service
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

HttpUserService::HttpUserService(const QUrl &baseUrl, int timeoutMs)
    : m_baseUrl(baseUrl)
    , m_timeoutMs(timeoutMs)
{
}

bool HttpUserService::registerUser(const QString &userId, const QString &password)
{
    m_lastError.clear();
    bool ok = false;
    const QJsonObject body{{"username", userId}, {"password", password}};
    postJson("/api/v1/auth/register", body, false, &ok);
    if (!ok) return false;
    return true;
}

bool HttpUserService::login(const QString &userId, const QString &password)
{
    m_lastError.clear();
    bool ok = false;
    const QJsonObject body{{"username", userId}, {"password", password}};
    const QJsonObject out = postJson("/api/v1/auth/login", body, false, &ok);
    if (!ok) return false;
    m_token = out.value("session_token").toString();
    m_userName = out.value("username").toString(userId);
    if (m_token.isEmpty()) {
        m_lastError = "登录响应缺少会话 token。";
        return false;
    }
    return true;
}

bool HttpUserService::logout()
{
    if (m_token.isEmpty()) return true;
    bool ok = false;
    postJson("/api/v1/auth/logout", QJsonObject{}, true, &ok);
    if (ok) {
        m_token.clear();
        m_userName.clear();
    }
    return ok;
}

bool HttpUserService::uploadMyFaceProfile(const QString &label, const QImage &image)
{
    m_lastError.clear();
    // #region debug-point A:upload-face-profile
    postDebugEvent("A",
                   "client/services/HttpUserService.cpp:uploadMyFaceProfile",
                   "[DEBUG] upload face profile invoked",
                   QJsonObject{{"image_null", image.isNull()}, {"token_empty", m_token.isEmpty()}, {"username", m_userName}});
    // #endregion
    if (image.isNull()) {
        m_lastError = "请选择有效的人脸照片。";
        return false;
    }
    bool ok = false;
    const QJsonObject body{{"label", label.trimmed()}, {"image", QString::fromLatin1(imageToBase64(image))}};
    postJson("/api/v1/face-profiles/me", body, true, &ok);
    return ok;
}

QList<FaceProfileSummary> HttpUserService::listFaceProfiles()
{
    m_lastError.clear();
    // #region debug-point C:list-face-profiles
    postDebugEvent("C",
                   "client/services/HttpUserService.cpp:listFaceProfiles",
                   "[DEBUG] list face profiles invoked",
                   QJsonObject{{"token_empty", m_token.isEmpty()}, {"username", m_userName}});
    // #endregion
    bool ok = false;
    const QJsonObject out = getJson("/api/v1/face-profiles", true, &ok);
    QList<FaceProfileSummary> profiles;
    if (!ok) return profiles;

    const QJsonArray items = out.value("profiles").toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const QString profileKey = obj.value("profile_key").toString().trimmed();
        const QString username = obj.value("username").toString().trimmed();
        const QString label = obj.value("label").toString().trimmed();
        if (profileKey.isEmpty() || username.isEmpty()) continue;
        profiles.append(FaceProfileSummary{profileKey, username, label, obj.value("updated_at").toString()});
    }
    return profiles;
}

QList<FaceProfileRecord> HttpUserService::fetchFaceProfiles(const QStringList &profileKeys)
{
    m_lastError.clear();
    QList<FaceProfileRecord> profiles;
    QJsonArray keys;
    for (const QString &profileKey : profileKeys) {
        const QString trimmed = profileKey.trimmed();
        if (!trimmed.isEmpty()) {
            keys.append(trimmed);
        }
    }
    bool ok = false;
    const QJsonObject out = postJson("/api/v1/face-profiles/query", QJsonObject{{"profile_keys", keys}}, true, &ok);
    if (!ok) return profiles;

    const QJsonArray items = out.value("profiles").toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const QString profileKey = obj.value("profile_key").toString().trimmed();
        const QString username = obj.value("username").toString().trimmed();
        const QString label = obj.value("label").toString().trimmed();
        const QString imageBase64 = obj.value("image").toString();
        if (profileKey.isEmpty() || username.isEmpty() || imageBase64.isEmpty()) continue;
        profiles.append(FaceProfileRecord{profileKey, username, label, imageBase64, obj.value("updated_at").toString()});
    }
    return profiles;
}

QStringList HttpUserService::getParticipants() const
{
    if (m_userName.isEmpty()) return {"You"};
    return {m_userName};
}

QString HttpUserService::currentUserName() const
{
    return m_userName;
}

QString HttpUserService::sessionToken() const
{
    return m_token;
}

QString HttpUserService::lastError() const
{
    return m_lastError;
}

QJsonObject HttpUserService::getJson(const QString &path, bool withAuthHeader, bool *ok)
{
    *ok = false;
    QUrl url = m_baseUrl;
    url.setPath(path);

    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    if (withAuthHeader && !m_token.isEmpty()) {
        request.setRawHeader("x-session-token", m_token.toUtf8());
    }
    // #region debug-point C:get-json
    postDebugEvent("C",
                   "client/services/HttpUserService.cpp:getJson",
                   "[DEBUG] getJson request prepared",
                   QJsonObject{{"path", path}, {"with_auth", withAuthHeader}, {"token_empty", m_token.isEmpty()}});
    // #endregion

    QNetworkReply *reply = m_network.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(m_timeoutMs + 100);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        m_lastError = "连接服务器失败，请检查服务是否启动";
        reply->deleteLater();
        return {};
    }

    const QByteArray bytes = reply->readAll();
    const QJsonObject obj = QJsonDocument::fromJson(bytes).object();
    if (reply->error() != QNetworkReply::NoError) {
        // #region debug-point C:get-json-error
        postDebugEvent("C",
                       "client/services/HttpUserService.cpp:getJson:error",
                       "[DEBUG] getJson request failed",
                       QJsonObject{{"path", path}, {"network_error", static_cast<int>(reply->error())},
                                   {"error_string", reply->errorString()},
                                   {"error_code", obj.value("error").toObject().value("code").toString()}});
        // #endregion
        m_lastError = extractError(obj);
        if (m_lastError.isEmpty()) {
            m_lastError = mapNetworkError(reply->error());
        }
        reply->deleteLater();
        return obj;
    }

    *ok = true;
    // #region debug-point C:get-json-ok
    postDebugEvent("C",
                   "client/services/HttpUserService.cpp:getJson:ok",
                   "[DEBUG] getJson request succeeded",
                   QJsonObject{{"path", path},
                               {"keys", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact).left(200))}});
    // #endregion
    reply->deleteLater();
    return obj;
}

QJsonObject HttpUserService::postJson(const QString &path, const QJsonObject &body, bool withAuthHeader, bool *ok)
{
    *ok = false;
    QUrl url = m_baseUrl;
    url.setPath(path);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);
    if (withAuthHeader && !m_token.isEmpty()) {
        request.setRawHeader("x-session-token", m_token.toUtf8());
    }
    // #region debug-point B:post-json
    postDebugEvent("B",
                   "client/services/HttpUserService.cpp:postJson",
                   "[DEBUG] postJson request prepared",
                   QJsonObject{{"path", path}, {"with_auth", withAuthHeader}, {"token_empty", m_token.isEmpty()},
                               {"body_keys", QStringList(body.keys()).join(",")}});
    // #endregion

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
        m_lastError = "连接服务器失败，请检查服务是否启动";
        reply->deleteLater();
        return {};
    }

    const QByteArray bytes = reply->readAll();
    const QJsonObject obj = QJsonDocument::fromJson(bytes).object();
    if (reply->error() != QNetworkReply::NoError) {
        // #region debug-point B:post-json-error
        postDebugEvent("B",
                       "client/services/HttpUserService.cpp:postJson:error",
                       "[DEBUG] postJson request failed",
                       QJsonObject{{"path", path}, {"network_error", static_cast<int>(reply->error())},
                                   {"error_string", reply->errorString()},
                                   {"error_code", obj.value("error").toObject().value("code").toString()}});
        // #endregion
        m_lastError = extractError(obj);
        if (m_lastError.isEmpty()) {
            m_lastError = mapNetworkError(reply->error());
        }
        reply->deleteLater();
        return obj;
    }

    *ok = true;
    // #region debug-point B:post-json-ok
    postDebugEvent("B",
                   "client/services/HttpUserService.cpp:postJson:ok",
                   "[DEBUG] postJson request succeeded",
                   QJsonObject{{"path", path},
                               {"keys", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact).left(200))}});
    // #endregion
    reply->deleteLater();
    return obj;
}

QString HttpUserService::extractError(const QJsonObject &obj) const
{
    const QJsonObject errorObj = obj.value("error").toObject();
    const QString code = errorObj.value("code").toString();
    const QString mapped = mapErrorCode(code);
    if (!mapped.isEmpty()) {
        return mapped;
    }
    const QString message = errorObj.value("message").toString();
    if (!message.isEmpty()) {
        return "操作失败，请稍后重试";
    }
    return QString();
}

QString HttpUserService::mapErrorCode(const QString &code) const
{
    if (code == "INVALID_CREDENTIALS") return "用户名或密码错误";
    if (code == "USERNAME_EXISTS") return "用户名已存在，请更换";
    if (code == "UNAUTHORIZED") return "登录状态失效，请重新登录";
    if (code == "MEETING_SERVER_DISABLED") return "会议服务当前不可用";
    if (code == "INVALID_FACE_PROFILE") return "人脸资料图片无效，请重新选择。";
    return {};
}

QString HttpUserService::mapNetworkError(QNetworkReply::NetworkError error) const
{
    switch (error) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionRefusedError:
        return "连接服务器失败，请检查服务是否启动";
    default:
        return "请求失败，请稍后重试";
    }
}

QByteArray HttpUserService::imageToBase64(const QImage &image) const
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes.toBase64();
}
