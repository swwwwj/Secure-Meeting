#pragma once

#include "video/VideoSource.h"

#include <QImage>

class QCamera;
class QVideoSink;
class QMediaCaptureSession;
class QVideoFrame;

class CameraVideoSource : public VideoSource
{
    Q_OBJECT
public:
    explicit CameraVideoSource(QObject *parent = nullptr);
    ~CameraVideoSource() override;

    bool isAvailable() const;
    void start() override;
    void stop() override;
    QImage getFrame() const override;

private slots:
    void onFrameChanged(const QVideoFrame &frame);

private:
    QCamera *m_camera = nullptr;
    QVideoSink *m_sink = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QImage m_lastFrame;
};
