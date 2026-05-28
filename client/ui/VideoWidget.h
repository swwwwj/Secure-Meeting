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

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshFrame();
    void trackPrivacyRegions(const QImage &nextFrame);

    QLabel *m_canvas;
    QLabel *m_nameChip;
    QLabel *m_camChip;
    QLabel *m_micChip;
    QImage m_lastFrame;
    QVector<QRectF> m_privacyRegions;
};
