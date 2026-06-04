#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <QWidget>

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
        QPointF position;
        QPointF velocity;
        QSizeF size;
        qint64 lastPredictMs = 0;
        qint64 lastMeasureMs = 0;
        bool valid = false;
    };

    QLabel *m_canvas;
    QLabel *m_nameChip;
    QLabel *m_camChip;
    QLabel *m_micChip;
    QImage m_lastFrame;
    QVector<QRectF> m_privacyRegions;
    QVector<KalmanState> m_kalmanStates;
    qint64 m_lastKalmanUpdateMs = 0;
    qint64 m_lastPrivacyMeasureMs = 0;
    bool m_useKalmanTracking = true;
    int m_maxProcessedFrameAgeMs = 150;
    int m_blurRadius = 15;
};
