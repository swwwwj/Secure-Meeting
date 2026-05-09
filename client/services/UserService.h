#pragma once

#include <QString>
#include <QStringList>

// User/meeting-management abstraction placeholder.
// Later this can connect to account systems and persistent meeting metadata.
class UserService
{
public:
    virtual ~UserService() = default;
    virtual bool registerUser(const QString &userId, const QString &password) = 0;
    virtual bool login(const QString &userId, const QString &password) = 0;
    virtual bool logout() = 0;
    virtual QStringList getParticipants() const = 0;
    virtual QString sessionToken() const = 0;
    virtual QString lastError() const = 0;
};
