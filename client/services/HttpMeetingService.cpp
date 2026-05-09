#include "services/HttpMeetingService.h"

#include "services/UserService.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

HttpMeetingService::HttpMeetingService(const QUrl &baseUrl, int timeoutMs, UserService *userService, QObject *parent)
    : MeetingService(parent)
    , m_baseUrl(baseUrl)
    , m_timeoutMs(timeoutMs)
    , m_userService(userService)
{
}

bool HttpMeetingService::joinMeeting(const QString &meetingId, const QString &userId)
{
    Q_UNUSED(userId);
    m_lastError.clear();
    bool ok = false;
    const QJsonObject createResp = postJson("/api/v1/rooms/create", QJsonObject{{"room_code", meetingId}}, &ok);
    if (!ok) {
        const QString code = createResp.value("error").toObject().value("code").toString();
        if (code != "ROOM_EXISTS") {
            emit meetingStateChanged(false, QString("建房失败: %1").arg(m_lastError));
            return false;
        }
    }

    const QJsonObject joinResp = postJson("/api/v1/rooms/join", QJsonObject{{"room_code", meetingId}}, &ok);
    if (!ok) {
        emit meetingStateChanged(false, QString("入会失败: %1").arg(m_lastError));
        return false;
    }

    const QString status = joinResp.value("status").toString();
    m_activeMeetingId = meetingId;
    emit meetingStateChanged(true, status == "already_joined" ? "已在会议中。" : "已加入会议。");
    return true;
}

void HttpMeetingService::leaveMeeting()
{
    m_lastError.clear();
    if (m_activeMeetingId.isEmpty()) {
        emit meetingStateChanged(false, "当前未加入会议。");
        return;
    }
    bool ok = false;
    postJson("/api/v1/rooms/leave", QJsonObject{{"room_code", m_activeMeetingId}}, &ok);
    if (!ok) {
        emit meetingStateChanged(true, QString("离会失败: %1").arg(m_lastError));
        return;
    }
    m_activeMeetingId.clear();
    emit meetingStateChanged(false, "已离开会议。");
}

QString HttpMeetingService::lastError() const
{
    return m_lastError;
}

QJsonObject HttpMeetingService::postJson(const QString &path, const QJsonObject &body, bool *ok)
{
    *ok = false;
    if (!m_userService || m_userService->sessionToken().isEmpty()) {
        m_lastError = "缺少会话 token，请先登录。";
        return {};
    }

    QUrl url = m_baseUrl;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(m_timeoutMs);
    request.setRawHeader("x-session-token", m_userService->sessionToken().toUtf8());

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
        m_lastError = "请求 meeting_server 超时。";
        reply->deleteLater();
        return {};
    }

    const QByteArray bytes = reply->readAll();
    const QJsonObject obj = QJsonDocument::fromJson(bytes).object();
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = extractError(obj);
        if (m_lastError.isEmpty()) m_lastError = reply->errorString();
        reply->deleteLater();
        return obj;
    }

    *ok = true;
    reply->deleteLater();
    return obj;
}

QString HttpMeetingService::extractError(const QJsonObject &obj) const
{
    return obj.value("error").toObject().value("message").toString();
}
