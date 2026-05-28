#pragma once

#include "services/UserService.h"
#include <QHash>

class MockUserService : public UserService
{
public:
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
    QHash<QString, QString> m_users;
    QHash<QString, QString> m_faceImages;
    QHash<QString, FaceProfileSummary> m_faceSummaries;
    QString m_token;
    QString m_currentUser;
    QString m_lastError;
};
