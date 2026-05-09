#include "video/CameraVideoSource.h"

#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

CameraVideoSource::CameraVideoSource(QObject *parent)
    : VideoSource(parent)
{
    if (QMediaDevices::videoInputs().isEmpty()) {
        return;
    }
    m_camera = new QCamera(QMediaDevices::defaultVideoInput(), this);
    m_sink = new QVideoSink(this);
    m_session = new QMediaCaptureSession(this);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &CameraVideoSource::onFrameChanged);
}

CameraVideoSource::~CameraVideoSource() = default;

bool CameraVideoSource::isAvailable() const
{
    return m_camera != nullptr;
}

void CameraVideoSource::start()
{
    if (m_camera) m_camera->start();
}

void CameraVideoSource::stop()
{
    if (m_camera) m_camera->stop();
}

QImage CameraVideoSource::getFrame() const
{
    return m_lastFrame;
}

void CameraVideoSource::onFrameChanged(const QVideoFrame &frame)
{
    const QImage image = frame.toImage();
    if (image.isNull()) return;
    m_lastFrame = image.convertToFormat(QImage::Format_RGB32);
    emit frameReady(m_lastFrame);
}
