#include "services/MockUserService.h"

bool MockUserService::registerUser(const QString &userId, const QString &password)
{
    m_lastError.clear();
    const QString username = userId.trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        m_lastError = "用户名或密码不能为空。";
        return false;
    }
    if (m_users.contains(username)) {
        m_lastError = "用户名已存在，请直接登录或更换用户名";
        return false;
    }
    m_users.insert(username, password);
    return true;
}

bool MockUserService::login(const QString &userId, const QString &password)
{
    m_lastError.clear();
    const QString username = userId.trimmed();
    if (username.isEmpty() || password.isEmpty()) {
        m_lastError = "用户名或密码不能为空。";
        return false;
    }
    if (!m_users.contains(username) || m_users.value(username) != password) {
        m_lastError = "用户名或密码错误";
        return false;
    }
    m_currentUser = username;
    m_token = "mock-token-" + username;
    return true;
}

bool MockUserService::logout()
{
    m_token.clear();
    m_currentUser.clear();
    return true;
}

QStringList MockUserService::getParticipants() const
{
    if (m_currentUser.isEmpty()) {
        return {"You"};
    }
    return {m_currentUser};
}

QString MockUserService::sessionToken() const
{
    return m_token;
}

QString MockUserService::lastError() const
{
    return m_lastError;
}
