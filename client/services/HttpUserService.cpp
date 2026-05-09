#include "services/HttpUserService.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

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

QStringList HttpUserService::getParticipants() const
{
    if (m_userName.isEmpty()) return {"You"};
    return {m_userName};
}

QString HttpUserService::sessionToken() const
{
    return m_token;
}

QString HttpUserService::lastError() const
{
    return m_lastError;
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
        m_lastError = extractError(obj);
        if (m_lastError.isEmpty()) {
            m_lastError = mapNetworkError(reply->error());
        }
        reply->deleteLater();
        return obj;
    }

    *ok = true;
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
