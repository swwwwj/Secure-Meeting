#pragma once

#include <QImage>
#include <QObject>

// Abstract video source interface.
// Future implementations can wrap camera SDK, RTC stream, or recorded media.
class VideoSource : public QObject
{
    Q_OBJECT
public:
    explicit VideoSource(QObject *parent = nullptr) : QObject(parent) {}
    ~VideoSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual QImage getFrame() const = 0;

signals:
    void frameReady(const QImage &frame);
};
