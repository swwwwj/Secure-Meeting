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
    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString &errorString) {
                Q_UNUSED(error);
                if (!errorString.isEmpty()) {
                    emit sourceWarning(QStringLiteral("摄像头启动失败：%1").arg(errorString));
                }
            });
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
