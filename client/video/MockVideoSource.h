#pragma once

#include "video/VideoSource.h"

#include <QTimer>

class MockVideoSource : public VideoSource
{
    Q_OBJECT
public:
    explicit MockVideoSource(QObject *parent = nullptr);

    void start() override;
    void stop() override;
    QImage getFrame() const override;

private slots:
    void produceFrame();

private:
    QTimer m_timer;
    QImage m_lastFrame;
    int m_frameIndex = 0;
};
