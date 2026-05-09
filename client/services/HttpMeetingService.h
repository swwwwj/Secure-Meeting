#pragma once

#include "services/MeetingService.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>

class UserService;

class HttpMeetingService : public MeetingService
{
    Q_OBJECT
public:
    explicit HttpMeetingService(const QUrl &baseUrl, int timeoutMs, UserService *userService, QObject *parent = nullptr);

    bool joinMeeting(const QString &meetingId, const QString &userId) override;
    void leaveMeeting() override;
    QString lastError() const override;

private:
    QJsonObject postJson(const QString &path, const QJsonObject &body, bool *ok);
    QString extractError(const QJsonObject &obj) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    int m_timeoutMs = 1500;
    UserService *m_userService = nullptr;
    QString m_activeMeetingId;
    QString m_lastError;
};
