#pragma once

#include <QWidget>
#include <QImage>
#include <QRectF>
#include <QVector>

class QLabel;
class QResizeEvent;

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(const QString &participant, QWidget *parent = nullptr);

    void setParticipantName(const QString &name);
    void setFrame(const QImage &frame);
    void clearFrame();
    void setMediaState(bool cameraOn, bool microphoneOn);
    void setPrivacyRegions(const QVector<QRectF> &regions);

    void setProcessedFrame(const QImage &frame);
    void setBlurRadius(int radius);
    void setUseKalmanTracking(bool enabled);
    void setMaxProcessedFrameAgeMs(int ms);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshFrame();
    void trackPrivacyRegions(const QImage &nextFrame);

    struct KalmanState {
        QPointF position;      // 中心点（归一化）
        QPointF velocity;      // 速度（归一化/秒）
        QSizeF size;           // 宽高（归一化）
        qint64 lastUpdateMs = 0;
        bool valid = false;
    };

    QLabel *m_canvas;
    QLabel *m_nameChip;
    QLabel *m_camChip;
    QLabel *m_micChip;
    QImage m_lastFrame;
    QVector<QRectF> m_privacyRegions;

    // 卡尔曼追踪状态
    QVector<KalmanState> m_kalmanStates;
    qint64 m_lastKalmanUpdateMs = 0;
    bool m_useKalmanTracking = true;

    // 混合渲染：服务端帧缓存
    QImage m_lastProcessedFrame;
    qint64 m_lastProcessedFrameMs = 0;
    bool m_hasProcessedFrame = false;
    int m_maxProcessedFrameAgeMs = 150;

    // 高斯模糊参数
    int m_blurRadius = 15;
};
