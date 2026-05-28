#include "services/MockUserService.h"

#include <QBuffer>

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

bool MockUserService::uploadMyFaceProfile(const QString &label, const QImage &image)
{
    m_lastError.clear();
    if (m_currentUser.isEmpty() || image.isNull()) {
        m_lastError = "请选择有效的人脸照片。";
        return false;
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    const QString normalizedLabel = label.trimmed().isEmpty() ? QStringLiteral("默认") : label.trimmed();
    const QString profileKey = QStringLiteral("%1::%2").arg(m_currentUser, normalizedLabel);
    m_faceImages.insert(profileKey, QString::fromLatin1(bytes.toBase64()));
    m_faceSummaries.insert(profileKey, FaceProfileSummary{profileKey, m_currentUser, normalizedLabel, QStringLiteral("mock")});
    return true;
}

QList<FaceProfileSummary> MockUserService::listFaceProfiles()
{
    QList<FaceProfileSummary> profiles;
    const auto values = m_faceSummaries.values();
    for (const FaceProfileSummary &summary : values) profiles.append(summary);
    return profiles;
}

QList<FaceProfileRecord> MockUserService::fetchFaceProfiles(const QStringList &profileKeys)
{
    QList<FaceProfileRecord> profiles;
    for (const QString &profileKey : profileKeys) {
        const QString trimmed = profileKey.trimmed();
        if (trimmed.isEmpty() || !m_faceImages.contains(trimmed) || !m_faceSummaries.contains(trimmed)) continue;
        const FaceProfileSummary summary = m_faceSummaries.value(trimmed);
        profiles.append(FaceProfileRecord{summary.profileKey,
                                          summary.username,
                                          summary.label,
                                          m_faceImages.value(trimmed),
                                          QStringLiteral("mock")});
    }
    return profiles;
}

QStringList MockUserService::getParticipants() const
{
    if (m_currentUser.isEmpty()) {
        return {"You"};
    }
    return {m_currentUser};
}

QString MockUserService::currentUserName() const
{
    return m_currentUser;
}

QString MockUserService::sessionToken() const
{
    return m_token;
}

QString MockUserService::lastError() const
{
    return m_lastError;
}
