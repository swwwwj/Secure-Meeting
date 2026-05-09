#pragma once

#include "services/AIProcessor.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

class HttpAIProcessor : public AIProcessor
{
    Q_OBJECT
public:
    explicit HttpAIProcessor(const QUrl &endpoint,
                             int timeoutMs,
                             int maxInFlightRequests,
                             const QString &modelVersion,
                             const QString &policyVersion,
                             QObject *parent = nullptr);

    void setEnabled(bool enabled) override;
    bool isEnabled() const override;
    void processFrame(const QImage &frame) override;

private:
    struct InFlightInfo {
        QElapsedTimer timer;
        QString requestId;
        QString traceId;
    };

    QByteArray imageToBase64(const QImage &frame) const;
    QImage base64ToImage(const QByteArray &base64) const;
    void logEvent(const QString &event, const QJsonObject &extra = {}) const;
    void logMetricsIfNeeded() const;
    QString newId() const;

    QNetworkAccessManager m_network;
    QUrl m_endpoint;
    bool m_enabled = false;
    int m_timeoutMs = 1200;
    int m_maxInFlightRequests = 2;
    QString m_modelVersion;
    QString m_policyVersion;
    mutable qint64 m_startedMs = 0;
    mutable int m_framesIn = 0;
    mutable int m_framesOut = 0;
    mutable int m_failed = 0;
    mutable int m_dropped = 0;
    mutable double m_totalLatencyMs = 0.0;
    mutable double m_totalInferenceMs = 0.0;
    mutable QHash<QNetworkReply *, InFlightInfo> m_inFlight;
};
