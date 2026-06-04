#include "ui/VideoWidget.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QtMath>

#include <limits>

VideoWidget::VideoWidget(const QString &participant, QWidget *parent)
    : QWidget(parent)
    , m_canvas(new QLabel(this))
    , m_nameChip(new QLabel(participant, this))
    , m_camChip(new QLabel("Cam", this))
    , m_micChip(new QLabel("Mic", this))
{
    setObjectName("videoCard");
    setMinimumSize(320, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);
    m_canvas->setObjectName("videoCanvas");
    m_canvas->setAlignment(Qt::AlignCenter);
    root->addWidget(m_canvas, 1);

    auto *bottom = new QHBoxLayout();
    m_nameChip->setObjectName("nameChip");
    m_camChip->setObjectName("statusChip");
    m_micChip->setObjectName("statusChip");
    bottom->addWidget(m_nameChip);
    bottom->addStretch();
    bottom->addWidget(m_camChip);
    bottom->addWidget(m_micChip);
    root->addLayout(bottom);

    clearFrame();
}

void VideoWidget::setParticipantName(const QString &name)
{
    m_nameChip->setText(name);
}

void VideoWidget::setFrame(const QImage &frame)
{
    if (!m_lastFrame.isNull() && !m_privacyRegions.isEmpty()) {
        trackPrivacyRegions(frame);
    }
    m_lastFrame = frame;
    refreshFrame();
}

void VideoWidget::clearFrame()
{
    m_lastFrame = QImage();
    m_canvas->setText("Camera Off");
    m_canvas->setPixmap(QPixmap());
}

void VideoWidget::setMediaState(bool cameraOn, bool microphoneOn)
{
    m_camChip->setText(cameraOn ? "Cam" : "Cam Off");
    m_micChip->setText(microphoneOn ? "Mic" : "Muted");
}

void VideoWidget::setPrivacyRegions(const QVector<QRectF> &regions)
{
    m_privacyRegions = regions;
    refreshFrame();
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    refreshFrame();
}

void VideoWidget::refreshFrame()
{
    if (m_lastFrame.isNull()) return;
    const QSize size = m_canvas->size();
    if (size.isEmpty()) return;
    m_canvas->setText(QString());

    // 混合渲染：选择显示源
    // 若服务端处理帧新鲜（< maxAge），优先显示（含服务端高斯模糊）
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool useServerFrame = m_hasProcessedFrame
        && !m_lastProcessedFrame.isNull()
        && (nowMs - m_lastProcessedFrameMs) < m_maxProcessedFrameAgeMs;
    const QImage &source = useServerFrame ? m_lastProcessedFrame : m_lastFrame;
    const bool needLocalBlur = !m_privacyRegions.isEmpty() && !useServerFrame;

    const QImage scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    const QRect visibleSource(
        qMax(0, (scaled.width() - size.width()) / 2),
        qMax(0, (scaled.height() - size.height()) / 2),
        qMin(size.width(), scaled.width()),
        qMin(size.height(), scaled.height())
    );
    QImage preview = scaled.copy(visibleSource);
    if (needLocalBlur) {
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (const QRectF &normalized : m_privacyRegions) {
            QRect rect(qRound(normalized.left() * scaled.width()) - visibleSource.left(),
                       qRound(normalized.top() * scaled.height()) - visibleSource.top(),
                       qRound(normalized.width() * scaled.width()),
                       qRound(normalized.height() * scaled.height()));
            const int margin = qMax(12, qMin(rect.width(), rect.height()) / 6);
            rect = rect.normalized().adjusted(-margin, -margin, margin, margin).intersected(preview.rect());
            if (!rect.isEmpty()) {
                // 高斯模糊：缩放到1/4 + 平滑上采样，近似高斯模糊效果
                const QImage roi = preview.copy(rect);
                const QSize blurSize(qMax(1, rect.width() / 4),
                                     qMax(1, rect.height() / 4));
                const QImage blurred = roi.scaled(blurSize, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
                painter.drawImage(rect, blurred.scaled(rect.size(), Qt::IgnoreAspectRatio,
                                                        Qt::SmoothTransformation));
            }
        }
    }
    m_canvas->setPixmap(QPixmap::fromImage(preview));
}

void VideoWidget::trackPrivacyRegions(const QImage &nextFrame)
{
    if (nextFrame.isNull() || m_privacyRegions.isEmpty()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_lastKalmanUpdateMs > 0
        ? qBound(0.001, (nowMs - m_lastKalmanUpdateMs) / 1000.0, 0.1)
        : 0.033;

    if (!m_useKalmanTracking) {
        // 降级为直接使用最近的 bbox（不做帧间追踪）
        m_lastKalmanUpdateMs = nowMs;
        return;
    }

    // 初始化或重置卡尔曼状态
    if (m_kalmanStates.isEmpty() || m_kalmanStates.size() != m_privacyRegions.size()) {
        m_kalmanStates.clear();
        for (const QRectF &reg : m_privacyRegions) {
            KalmanState ks;
            ks.position = reg.center();
            ks.size = reg.size();
            ks.velocity = QPointF(0, 0);
            ks.lastUpdateMs = nowMs;
            ks.valid = true;
            m_kalmanStates.append(ks);
        }
        m_lastKalmanUpdateMs = nowMs;
        return;
    }

    QVector<QRectF> tracked;
    tracked.reserve(m_privacyRegions.size());

    for (int i = 0; i < m_privacyRegions.size(); ++i) {
        if (i >= m_kalmanStates.size()) {
            tracked.append(m_privacyRegions[i]);
            continue;
        }

        KalmanState &ks = m_kalmanStates[i];

        // 预测步：恒速模型，带阻尼
        ks.position += ks.velocity * dt;
        ks.velocity *= 0.95;
        ks.position.setX(qBound(0.0, ks.position.x(), 1.0));
        ks.position.setY(qBound(0.0, ks.position.y(), 1.0));

        // 测量步：使用 server 返回的 bbox，应用卡尔曼增益
        const QRectF &measured = m_privacyRegions[i];
        const double measX = measured.center().x();
        const double measY = measured.center().y();
        const double measW = measured.width();
        const double measH = measured.height();

        const double ageSec = (nowMs - ks.lastUpdateMs) / 1000.0;
        const double gain = qBound(0.1, exp(-ageSec * 5.0), 0.95);

        const double newX = ks.position.x() * (1.0 - gain) + measX * gain;
        const double newY = ks.position.y() * (1.0 - gain) + measY * gain;
        const double newW = ks.size.width() * (1.0 - gain) + measW * gain;
        const double newH = ks.size.height() * (1.0 - gain) + measH * gain;

        const double vx = (newX - ks.position.x()) / qMax(dt, 0.001);
        const double vy = (newY - ks.position.y()) / qMax(dt, 0.001);
        const double maxVel = 2.0;
        ks.velocity = QPointF(qBound(-maxVel, vx, maxVel), qBound(-maxVel, vy, maxVel));

        ks.position = QPointF(newX, newY);
        ks.size = QSizeF(newW, newH);
        ks.lastUpdateMs = nowMs;

        QRectF predicted(
            qBound(0.0, newX - newW / 2.0, 1.0 - newW),
            qBound(0.0, newY - newH / 2.0, 1.0 - newH),
            qMin(newW, 1.0),
            qMin(newH, 1.0)
        );
        tracked.append(predicted);
    }

    m_privacyRegions = tracked;
    m_lastKalmanUpdateMs = nowMs;
}

void VideoWidget::setProcessedFrame(const QImage &frame)
{
    m_lastProcessedFrame = frame;
    m_lastProcessedFrameMs = QDateTime::currentMSecsSinceEpoch();
    m_hasProcessedFrame = true;
}

void VideoWidget::setBlurRadius(int radius)
{
    m_blurRadius = qMax(3, radius | 1);
}

void VideoWidget::setUseKalmanTracking(bool enabled)
{
    m_useKalmanTracking = enabled;
    if (!enabled) {
        m_kalmanStates.clear();
    }
}

void VideoWidget::setMaxProcessedFrameAgeMs(int ms)
{
    m_maxProcessedFrameAgeMs = qMax(0, ms);
}
