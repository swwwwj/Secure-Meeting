#pragma once



#include "services/AIProcessor.h"



#include <QElapsedTimer>

#include <QFutureWatcher>

#include <QHash>

#include <QJsonObject>

#include <QNetworkAccessManager>

#include <QNetworkReply>

#include <QRectF>

#include <QUrl>

#include <QVector>



class HttpAIProcessor : public AIProcessor

{

    Q_OBJECT

public:

    explicit HttpAIProcessor(const QUrl &endpoint,

                             int timeoutMs,

                             int maxInFlightRequests,

                             const QString &modelVersion,

                             const QString &policyVersion,

                             int minFrameIntervalMs = 180,

                             int transportMaxEdge = 384,

                             int transportJpegQuality = 65,

                             QObject *parent = nullptr);



    void setEnabled(bool enabled) override;

    bool isEnabled() const override;

    void processFrame(const QImage &frame) override;



    void setPrivacyContext(const QString &roomId,

                           const QStringList &whitelistUserIds,

                           bool facePrivacyEnabled,

                           bool objectDetectionEnabled) override;

    void clearPrivacyContext() override;

    void enrollFace(const QString &userId, const QImage &frame) override;

    QList<FaceProfileSummary> enrollFaces(const QString &labelPrefix, const QImage &frame) override;


private:

    struct EncodedFrame {
        QByteArray imageBase64;
        QSize transportSize;
        QString requestId;
        QString traceId;
        qint64 frameSequence = 0;
    };

    struct InFlightInfo {

        QElapsedTimer timer;

        QString requestId;

        QString traceId;

        bool facePrivacyEnabled = false;

        qint64 frameSequence = 0;

        QSize transportSize;
    };



    QUrl apiUrl(const QString &path) const;

    QByteArray imageToBase64(const QImage &frame, int maxEdge, int jpegQuality, QSize *encodedSize = nullptr) const;

    void updateCachedPrivacyRegions(const QJsonObject &response, const QSize &transportSize);

    void logEvent(const QString &event, const QJsonObject &extra = {}) const;

    void logMetricsIfNeeded() const;

    QString newId() const;

    void postEnroll(const QString &userId, const QImage &frame);

    void postProcessRequest(const EncodedFrame &encoded);



    QNetworkAccessManager m_network;

    QUrl m_processEndpoint;

    QUrl m_apiBase;

    bool m_enabled = false;

    int m_timeoutMs = 1200;

    int m_maxInFlightRequests = 2;

    int m_minFrameIntervalMs = 180;

    int m_transportMaxEdge = 384;

    int m_transportJpegQuality = 65;

    QString m_modelVersion;

    QString m_policyVersion;

    QString m_roomId;

    QStringList m_whitelistUserIds;

    bool m_facePrivacyEnabled = false;

    bool m_objectDetectionEnabled = true;

    mutable qint64 m_startedMs = 0;

    mutable int m_framesIn = 0;

    mutable int m_framesOut = 0;

    mutable int m_failed = 0;

    mutable int m_dropped = 0;

    mutable double m_totalLatencyMs = 0.0;

    mutable double m_totalInferenceMs = 0.0;

    mutable QHash<QNetworkReply *, InFlightInfo> m_inFlight;

    QVector<QRectF> m_cachedPrivacyRegions;

    bool m_havePrivacyMetadata = false;

    qint64 m_lastFrameSentMs = 0;

    qint64 m_nextFrameSequence = 0;

    bool m_encodeInFlight = false;

};


