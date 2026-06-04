#pragma once

#include "services/UserService.h"

#include <QImage>
#include <QObject>
#include <QStringList>

// Abstract AI processing interface.
// Future implementations can route to local model runtime, gRPC AI cluster, etc.
class AIProcessor : public QObject
{
    Q_OBJECT
public:
    explicit AIProcessor(QObject *parent = nullptr) : QObject(parent) {}
    ~AIProcessor() override = default;

    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    virtual void processFrame(const QImage &frame) = 0;

    virtual void setPrivacyContext(const QString &roomId,
                                   const QStringList &whitelistUserIds,
                                   bool facePrivacyEnabled,
                                   bool objectDetectionEnabled) {
        Q_UNUSED(roomId);
        Q_UNUSED(whitelistUserIds);
        Q_UNUSED(facePrivacyEnabled);
        Q_UNUSED(objectDetectionEnabled);
    }
    virtual void clearPrivacyContext() {}
    virtual void enrollFace(const QString &userId, const QImage &frame) {
        Q_UNUSED(userId);
        Q_UNUSED(frame);
    }
    virtual QList<FaceProfileSummary> enrollFaces(const QString &labelPrefix, const QImage &frame) {
        Q_UNUSED(labelPrefix);
        Q_UNUSED(frame);
        return {};
    }

signals:
    void frameProcessed(const QImage &frame);
    void privacyRegionsUpdated(const QVector<QRectF> &regions);
};
