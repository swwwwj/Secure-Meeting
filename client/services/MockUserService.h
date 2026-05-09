#pragma once

#include "services/UserService.h"
#include <QHash>

class MockUserService : public UserService
{
public:
    bool registerUser(const QString &userId, const QString &password) override;
    bool login(const QString &userId, const QString &password) override;
    bool logout() override;
    QStringList getParticipants() const override;
    QString sessionToken() const override;
    QString lastError() const override;

private:
    QHash<QString, QString> m_users;
    QString m_token;
    QString m_currentUser;
    QString m_lastError;
};
