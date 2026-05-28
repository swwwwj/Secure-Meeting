#pragma once

#include "services/UserService.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

class HttpUserService : public UserService
{
public:
    explicit HttpUserService(const QUrl &baseUrl, int timeoutMs);

    bool registerUser(const QString &userId, const QString &password) override;
    bool login(const QString &userId, const QString &password) override;
    bool logout() override;
    bool uploadMyFaceProfile(const QString &label, const QImage &image) override;
    QList<FaceProfileSummary> listFaceProfiles() override;
    QList<FaceProfileRecord> fetchFaceProfiles(const QStringList &profileKeys) override;
    QStringList getParticipants() const override;
    QString currentUserName() const override;
    QString sessionToken() const override;
    QString lastError() const override;

private:
    QJsonObject getJson(const QString &path, bool withAuthHeader, bool *ok);
    QJsonObject postJson(const QString &path, const QJsonObject &body, bool withAuthHeader, bool *ok);
    QString extractError(const QJsonObject &obj) const;
    QString mapErrorCode(const QString &code) const;
    QString mapNetworkError(QNetworkReply::NetworkError error) const;
    QByteArray imageToBase64(const QImage &image) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    int m_timeoutMs = 1500;
    QString m_token;
    QString m_userName;
    QString m_lastError;
};
