#include "config/AppConfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcessEnvironment>

namespace {
QString readEnvOrDefault(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}
}

AppConfig AppConfig::load()
{
    AppConfig cfg;
    cfg.env = readEnvOrDefault("SM_ENV", "dev");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString configPath = appDir + "/config/" + cfg.env + ".json";
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QByteArray raw = file.readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning().noquote()
                << QString("[AppConfig] config parse failed env=%1 path=%2 fallback_defaults=true error=%3")
                       .arg(cfg.env, configPath, parseError.errorString());
        } else {
        const QJsonObject obj = doc.object();
        if (obj.contains("ai_endpoint")) cfg.aiEndpoint = QUrl(obj.value("ai_endpoint").toString());
        if (obj.contains("meeting_server_endpoint")) cfg.meetingServerEndpoint = QUrl(obj.value("meeting_server_endpoint").toString());
        if (obj.contains("use_mock_services")) cfg.useMockServices = obj.value("use_mock_services").toBool(cfg.useMockServices);
        if (obj.contains("ai_enabled_by_default")) cfg.aiEnabledByDefault = obj.value("ai_enabled_by_default").toBool();
        if (obj.contains("ai_timeout_ms")) cfg.aiTimeoutMs = obj.value("ai_timeout_ms").toInt(cfg.aiTimeoutMs);
        if (obj.contains("meeting_timeout_ms")) cfg.meetingTimeoutMs = obj.value("meeting_timeout_ms").toInt(cfg.meetingTimeoutMs);
        if (obj.contains("max_in_flight_requests")) cfg.maxInFlightRequests = obj.value("max_in_flight_requests").toInt(cfg.maxInFlightRequests);
        if (obj.contains("model_version")) cfg.modelVersion = obj.value("model_version").toString(cfg.modelVersion);
        if (obj.contains("policy_version")) cfg.policyVersion = obj.value("policy_version").toString(cfg.policyVersion);
        }
    } else {
        qWarning().noquote()
            << QString("[AppConfig] config open failed env=%1 path=%2 fallback_defaults=true error=%3")
                   .arg(cfg.env, configPath, file.errorString());
    }

    const QString endpointOverride = qEnvironmentVariable("SM_AI_ENDPOINT");
    if (!endpointOverride.isEmpty()) cfg.aiEndpoint = QUrl(endpointOverride);
    const QString meetingEndpointOverride = qEnvironmentVariable("SM_MEETING_ENDPOINT");
    if (!meetingEndpointOverride.isEmpty()) cfg.meetingServerEndpoint = QUrl(meetingEndpointOverride);
    const QString modelOverride = qEnvironmentVariable("SM_MODEL_VERSION");
    if (!modelOverride.isEmpty()) cfg.modelVersion = modelOverride;
    const QString policyOverride = qEnvironmentVariable("SM_POLICY_VERSION");
    if (!policyOverride.isEmpty()) cfg.policyVersion = policyOverride;
    const QString useMockOverride = qEnvironmentVariable("SM_USE_MOCK_SERVICES");
    if (!useMockOverride.isEmpty()) {
        cfg.useMockServices = (useMockOverride.compare("true", Qt::CaseInsensitive) == 0
                               || useMockOverride == "1");
    }

    qInfo().noquote()
        << QString("[AppConfig] resolved env=%1 path=%2 mode=%3 ai=%4 meeting=%5")
               .arg(cfg.env,
                    configPath,
                    cfg.useMockServices ? "Mock" : "Real",
                    cfg.aiEndpoint.toString(),
                    cfg.meetingServerEndpoint.toString());

    return cfg;
}
