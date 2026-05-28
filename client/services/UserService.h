#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

struct FaceProfileSummary {
    QString profileKey;
    QString username;
    QString label;
    QString updatedAt;
    QString displayName() const
    {
        if (username.isEmpty()) return label;
        if (label.isEmpty()) return username;
        return QString("%1 / %2").arg(username, label);
    }
};

struct FaceProfileRecord {
    QString profileKey;
    QString username;
    QString label;
    QString imageBase64;
    QString updatedAt;
    QString displayName() const
    {
        if (username.isEmpty()) return label;
        if (label.isEmpty()) return username;
        return QString("%1 / %2").arg(username, label);
    }
};

// User/meeting-management abstraction placeholder.
// Later this can connect to account systems and persistent meeting metadata.
class UserService
{
public:
    virtual ~UserService() = default;
    virtual bool registerUser(const QString &userId, const QString &password) = 0;
    virtual bool login(const QString &userId, const QString &password) = 0;
    virtual bool logout() = 0;
    virtual bool uploadMyFaceProfile(const QString &label, const QImage &image) = 0;
    virtual QList<FaceProfileSummary> listFaceProfiles() = 0;
    virtual QList<FaceProfileRecord> fetchFaceProfiles(const QStringList &profileKeys) = 0;
    virtual QStringList getParticipants() const = 0;
    virtual QString currentUserName() const = 0;
    virtual QString sessionToken() const = 0;
    virtual QString lastError() const = 0;
};
