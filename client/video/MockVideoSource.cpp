#include "video/MockVideoSource.h"

#include <QColor>
#include <QDateTime>
#include <QPainter>

MockVideoSource::MockVideoSource(QObject *parent)
    : VideoSource(parent)
{
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &MockVideoSource::produceFrame);
}

void MockVideoSource::start()
{
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void MockVideoSource::stop()
{
    m_timer.stop();
}

QImage MockVideoSource::getFrame() const
{
    return m_lastFrame;
}

void MockVideoSource::produceFrame()
{
    constexpr int width = 640;
    constexpr int height = 360;
    QImage frame(width, height, QImage::Format_RGB32);

    const int phase = (m_frameIndex * 3) % 255;
    frame.fill(QColor(20 + (phase / 4), 60, 120 + (phase / 3)));

    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 18, QFont::Bold));
    painter.drawText(QRect(0, 0, width, height), Qt::AlignCenter, "Local Camera (Mock)");

    painter.setFont(QFont("Segoe UI", 11));
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    painter.drawText(16, height - 20, QString("frame #%1  %2").arg(m_frameIndex).arg(timestamp));

    m_lastFrame = frame;
    emit frameReady(m_lastFrame);
    ++m_frameIndex;
}
