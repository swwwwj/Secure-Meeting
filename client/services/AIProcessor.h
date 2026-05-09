#pragma once

#include <QImage>
#include <QObject>

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

signals:
    void frameProcessed(const QImage &frame);
};
