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

namespace {
QRect normalizedToImageRect(const QRectF &normalized, const QSize &size)
{
    return QRect(qRound(normalized.left() * size.width()),
                 qRound(normalized.top() * size.height()),
                 qRound(normalized.width() * size.width()),
                 qRound(normalized.height() * size.height()))
        .normalized()
        .intersected(QRect(QPoint(0, 0), size));
}

QRect stateToImageRect(const QPointF &center, const QSizeF &stateSize, const QSize &imageSize)
{
    const double w = qBound(0.01, stateSize.width(), 1.0);
    const double h = qBound(0.01, stateSize.height(), 1.0);
    return normalizedToImageRect(
        QRectF(qBound(0.0, center.x() - w / 2.0, 1.0 - w),
               qBound(0.0, center.y() - h / 2.0, 1.0 - h),
               w,
               h),
        imageSize);
}

qint64 sadForRects(const QImage &a, const QRect &aRect, const QImage &b, const QRect &bRect)
{
    qint64 sad = 0;
    for (int y = 0; y < aRect.height(); ++y) {
        const uchar *pa = a.constScanLine(aRect.top() + y) + aRect.left();
        const uchar *pb = b.constScanLine(bRect.top() + y) + bRect.left();
        for (int x = 0; x < aRect.width(); ++x) {
            sad += qAbs(int(pa[x]) - int(pb[x]));
        }
    }
    return sad;
}
}

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
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_useKalmanTracking || regions.isEmpty()) {
        m_privacyRegions = regions;
        m_kalmanStates.clear();
        m_lastKalmanUpdateMs = nowMs;
        m_lastPrivacyMeasureMs = regions.isEmpty() ? 0 : nowMs;
        refreshFrame();
        return;
    }

    if (m_kalmanStates.size() != regions.size()) {
        m_kalmanStates.clear();
        m_kalmanStates.reserve(regions.size());
        for (const QRectF &reg : regions) {
            KalmanState ks;
            ks.position = reg.center();
            ks.size = reg.size();
            ks.velocity = QPointF(0, 0);
            ks.lastPredictMs = nowMs;
            ks.lastMeasureMs = nowMs;
            ks.valid = true;
            m_kalmanStates.append(ks);
        }
    } else {
        for (int i = 0; i < regions.size(); ++i) {
            KalmanState &ks = m_kalmanStates[i];
            const QPointF measuredCenter = regions[i].center();
            const QSizeF measuredSize = regions[i].size();
            const double dt = ks.lastMeasureMs > 0
                ? qBound(0.03, (nowMs - ks.lastMeasureMs) / 1000.0, 1.0)
                : 0.1;
            const QPointF measuredVelocity((measuredCenter.x() - ks.position.x()) / dt,
                                           (measuredCenter.y() - ks.position.y()) / dt);
            const double maxVelocity = 4.5;

            ks.velocity = QPointF(
                qBound(-maxVelocity, ks.velocity.x() * 0.25 + measuredVelocity.x() * 0.75, maxVelocity),
                qBound(-maxVelocity, ks.velocity.y() * 0.25 + measuredVelocity.y() * 0.75, maxVelocity));
            ks.position = QPointF(ks.position.x() * 0.05 + measuredCenter.x() * 0.95,
                                  ks.position.y() * 0.05 + measuredCenter.y() * 0.95);
            ks.size = QSizeF(ks.size.width() * 0.18 + measuredSize.width() * 0.82,
                             ks.size.height() * 0.18 + measuredSize.height() * 0.82);
            ks.lastMeasureMs = nowMs;
            ks.lastPredictMs = nowMs;
            ks.valid = true;
        }
    }

    QVector<QRectF> corrected;
    corrected.reserve(m_kalmanStates.size());
    for (const KalmanState &ks : m_kalmanStates) {
        const double w = qBound(0.01, ks.size.width(), 1.0);
        const double h = qBound(0.01, ks.size.height(), 1.0);
        corrected.append(QRectF(qBound(0.0, ks.position.x() - w / 2.0, 1.0 - w),
                                qBound(0.0, ks.position.y() - h / 2.0, 1.0 - h),
                                w,
                                h));
    }
    m_privacyRegions = corrected;
    m_lastKalmanUpdateMs = nowMs;
    m_lastPrivacyMeasureMs = nowMs;
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
    const QImage scaled = m_lastFrame.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    const QRect visibleSource(
        qMax(0, (scaled.width() - size.width()) / 2),
        qMax(0, (scaled.height() - size.height()) / 2),
        qMin(size.width(), scaled.width()),
        qMin(size.height(), scaled.height())
    );
    QImage preview = scaled.copy(visibleSource);
    if (!m_privacyRegions.isEmpty()) {
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int i = 0; i < m_privacyRegions.size(); ++i) {
            const QRectF &normalized = m_privacyRegions[i];
            QRect rect(qRound(normalized.left() * scaled.width()) - visibleSource.left(),
                       qRound(normalized.top() * scaled.height()) - visibleSource.top(),
                       qRound(normalized.width() * scaled.width()),
                       qRound(normalized.height() * scaled.height()));
            const int margin = qMax(qMax(16, m_blurRadius), qMin(rect.width(), rect.height()) / 4);
            const QPointF velocity = i < m_kalmanStates.size() ? m_kalmanStates[i].velocity : QPointF();
            const int leadX = qRound(qBound(-0.28, velocity.x(), 0.28) * scaled.width());
            const int leadY = qRound(qBound(-0.20, velocity.y(), 0.20) * scaled.height());
            rect = rect.normalized().adjusted(
                -margin + qMin(0, leadX),
                -margin + qMin(0, leadY),
                margin + qMax(0, leadX),
                margin + qMax(0, leadY)).intersected(preview.rect());
            if (!rect.isEmpty()) {
                // Strong local fallback blur. The server frame is preferred when fresh;
                // this path must still hide details when the server result is stale.
                const QImage roi = preview.copy(rect);
                const int divisor = qBound(6, m_blurRadius / 2, 24);
                const QSize blurSize(qMax(1, rect.width() / divisor),
                                     qMax(1, rect.height() / divisor));
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
    if (nextFrame.isNull() || m_lastFrame.isNull() || m_privacyRegions.isEmpty()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_lastKalmanUpdateMs > 0
        ? qBound(0.001, (nowMs - m_lastKalmanUpdateMs) / 1000.0, 0.12)
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
            ks.lastPredictMs = nowMs;
            ks.lastMeasureMs = nowMs;
            ks.valid = true;
            m_kalmanStates.append(ks);
        }
        m_lastKalmanUpdateMs = nowMs;
        return;
    }

    constexpr int trackWidth = 192;
    const int trackHeight = qMax(1, trackWidth * nextFrame.height() / qMax(1, nextFrame.width()));
    const QSize trackSize(trackWidth, trackHeight);
    const QImage previousGray = m_lastFrame.scaled(trackSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                    .convertToFormat(QImage::Format_Grayscale8);
    const QImage nextGray = nextFrame.scaled(trackSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                .convertToFormat(QImage::Format_Grayscale8);

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
        ks.velocity *= 0.96;
        ks.position.setX(qBound(0.0, ks.position.x(), 1.0));
        ks.position.setY(qBound(0.0, ks.position.y(), 1.0));

        // 测量步：在缩略图（192px宽）上用 SAD 块匹配做视觉追踪
        const QRect predictedRect = stateToImageRect(ks.position, ks.size, trackSize);
        if (!predictedRect.isEmpty()) {
            const int templateEdge = qBound(12, qMin(predictedRect.width(), predictedRect.height()) / 2, 48);
            const QRect templateRect = QRect(QPoint(0, 0), QSize(templateEdge, templateEdge))
                                           .translated(predictedRect.center() - QPoint(templateEdge / 2, templateEdge / 2))
                                           .intersected(nextGray.rect());
            const int candidateW = templateRect.width();
            const int candidateH = templateRect.height();
            const int searchRadius = qBound(6, qMax(predictedRect.width(), predictedRect.height()) / 3, 20);
            const QRect searchRect = predictedRect.adjusted(-searchRadius, -searchRadius,
                                                            searchRadius, searchRadius)
                                         .intersected(nextGray.rect());
            if (templateRect.width() >= 8
                && templateRect.height() >= 8
                && searchRect.width() >= candidateW
                && searchRect.height() >= candidateH) {
                qint64 bestSad = std::numeric_limits<qint64>::max();
                QPoint bestTopLeft = searchRect.topLeft();
                const int step = qMax(2, templateEdge / 8);
                for (int y = searchRect.top(); y <= searchRect.bottom() - candidateH + 1; y += step) {
                    for (int x = searchRect.left(); x <= searchRect.right() - candidateW + 1; x += step) {
                        const QRect candidate(QPoint(x, y), QSize(candidateW, candidateH));
                        const qint64 sad = sadForRects(previousGray, templateRect, nextGray, candidate);
                        if (sad < bestSad) {
                            bestSad = sad;
                            bestTopLeft = QPoint(x, y);
                        }
                    }
                }

                // 防漂移检查：根据改善度动态决定信任度
                const qint64 zeroSad = sadForRects(previousGray, templateRect, nextGray, templateRect);
                const int pixelCount = templateRect.width() * templateRect.height();
                const double improvementPerPixel = static_cast<double>(zeroSad - bestSad) / qMax(1, pixelCount);
                const double avgBestSad = static_cast<double>(bestSad) / qMax(1, pixelCount);
                const bool lostTrack = avgBestSad > 35.0;
                const double confidence = lostTrack ? 0.0
                                        : improvementPerPixel > 1.0 ? 0.45
                                        : improvementPerPixel > 0.3 ? 0.30
                                        : improvementPerPixel > 0.1 ? 0.15
                                        : 0.0;
                if (confidence > 0.0) {
                    const QRect measuredRect(bestTopLeft, QSize(candidateW, candidateH));
                    const QPointF measuredCenter(
                        (measuredRect.center().x() + 0.5) / trackSize.width(),
                        (measuredRect.center().y() + 0.5) / trackSize.height());
                    const QPointF measuredVelocity(
                        (measuredCenter.x() - ks.position.x()) / qMax(dt, 0.001),
                        (measuredCenter.y() - ks.position.y()) / qMax(dt, 0.001));
                    const double maxVelocity = 4.5;
                    ks.velocity = QPointF(
                        qBound(-maxVelocity,
                               ks.velocity.x() * (1.0 - confidence) + measuredVelocity.x() * confidence,
                               maxVelocity),
                        qBound(-maxVelocity,
                               ks.velocity.y() * (1.0 - confidence) + measuredVelocity.y() * confidence,
                               maxVelocity));
                    ks.position = QPointF(
                        ks.position.x() * (1.0 - confidence) + measuredCenter.x() * confidence,
                        ks.position.y() * (1.0 - confidence) + measuredCenter.y() * confidence);
                } else if (lostTrack) {
                    // 跟丢了：急停，等待下一次 server bbox 重新定位
                    ks.velocity = QPointF(0, 0);
                } else {
                    // 无改善但没丢：快速衰减速度，防止惯性漂移
                    ks.velocity *= 0.5;
                }
            }
        }

        ks.lastPredictMs = nowMs;

        const double newX = ks.position.x();
        const double newY = ks.position.y();
        const double newW = qBound(0.01, ks.size.width(), 1.0);
        const double newH = qBound(0.01, ks.size.height(), 1.0);

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
    Q_UNUSED(frame);
}

void VideoWidget::setBlurRadius(int radius)
{
    m_blurRadius = qMax(9, radius | 1);
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
