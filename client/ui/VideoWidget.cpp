#include "ui/VideoWidget.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>

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
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (const QRectF &normalized : m_privacyRegions) {
            QRect rect(qRound(normalized.left() * scaled.width()) - visibleSource.left(),
                       qRound(normalized.top() * scaled.height()) - visibleSource.top(),
                       qRound(normalized.width() * scaled.width()),
                       qRound(normalized.height() * scaled.height()));
            const int margin = qMax(12, qMin(rect.width(), rect.height()) / 6);
            rect = rect.normalized().adjusted(-margin, -margin, margin, margin).intersected(preview.rect());
            if (!rect.isEmpty()) {
                const QImage roi = preview.copy(rect);
                const QSize tinySize(qMax(1, rect.width() / 16), qMax(1, rect.height() / 16));
                const QImage tiny = roi.scaled(tinySize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
                painter.drawImage(rect, tiny.scaled(rect.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
            }
        }
    }
    m_canvas->setPixmap(QPixmap::fromImage(preview));
}

void VideoWidget::trackPrivacyRegions(const QImage &nextFrame)
{
    if (nextFrame.isNull() || m_lastFrame.size().isEmpty()) {
        return;
    }

    constexpr int trackWidth = 192;
    const int trackHeight = qMax(1, trackWidth * nextFrame.height() / qMax(1, nextFrame.width()));
    const QSize trackSize(trackWidth, trackHeight);
    const QImage prev = m_lastFrame.scaled(trackSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                            .convertToFormat(QImage::Format_Grayscale8);
    const QImage curr = nextFrame.scaled(trackSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                            .convertToFormat(QImage::Format_Grayscale8);

    QVector<QRectF> tracked;
    tracked.reserve(m_privacyRegions.size());
    const QRect bounds(0, 0, trackWidth, trackHeight);

    for (const QRectF &normalized : m_privacyRegions) {
        QRect templateRect(qRound(normalized.left() * trackWidth),
                           qRound(normalized.top() * trackHeight),
                           qRound(normalized.width() * trackWidth),
                           qRound(normalized.height() * trackHeight));
        templateRect = templateRect.normalized().intersected(bounds);
        if (templateRect.width() < 8 || templateRect.height() < 8) {
            tracked.append(normalized);
            continue;
        }

        QRect matchRect = templateRect.adjusted(templateRect.width() / 4,
                                                templateRect.height() / 4,
                                                -templateRect.width() / 4,
                                                -templateRect.height() / 4);
        if (matchRect.width() < 8 || matchRect.height() < 8) {
            matchRect = templateRect;
        }

        const int searchRadius = qBound(6, qMin(templateRect.width(), templateRect.height()) / 3, 20);
        const int sampleStep = qBound(2, qMin(matchRect.width(), matchRect.height()) / 10, 5);
        qint64 bestScore = std::numeric_limits<qint64>::max();
        qint64 zeroScore = std::numeric_limits<qint64>::max();
        QPoint bestDelta(0, 0);

        for (int dy = -searchRadius; dy <= searchRadius; dy += 2) {
            for (int dx = -searchRadius; dx <= searchRadius; dx += 2) {
                const QRect candidate = matchRect.translated(dx, dy);
                if (!bounds.contains(candidate)) {
                    continue;
                }
                qint64 score = 0;
                int samples = 0;
                for (int y = 0; y < matchRect.height(); y += sampleStep) {
                    const uchar *prevLine = prev.constScanLine(matchRect.top() + y) + matchRect.left();
                    const uchar *currLine = curr.constScanLine(candidate.top() + y) + candidate.left();
                    for (int x = 0; x < matchRect.width(); x += sampleStep) {
                        score += qAbs(int(prevLine[x]) - int(currLine[x]));
                        ++samples;
                    }
                }
                if (samples > 0) {
                    score /= samples;
                }
                if (dx == 0 && dy == 0) {
                    zeroScore = score;
                }
                if (score < bestScore) {
                    bestScore = score;
                    bestDelta = QPoint(dx, dy);
                }
            }
        }

        if (zeroScore == std::numeric_limits<qint64>::max()
            || bestScore > 50
            || (zeroScore - bestScore) < 3) {
            tracked.append(normalized);
            continue;
        }

        QRectF moved = normalized.translated(static_cast<double>(bestDelta.x()) * 1.0 / trackWidth,
                                             static_cast<double>(bestDelta.y()) * 1.0 / trackHeight);
        moved = moved.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
        tracked.append(moved.isEmpty() ? normalized : moved);
    }

    m_privacyRegions = tracked;
}
