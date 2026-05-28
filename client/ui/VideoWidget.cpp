#include "ui/VideoWidget.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>

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
    QImage preview = m_lastFrame.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    if (!m_privacyRegions.isEmpty()) {
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (const QRectF &normalized : m_privacyRegions) {
            QRect rect(qRound(normalized.left() * preview.width()),
                       qRound(normalized.top() * preview.height()),
                       qRound(normalized.width() * preview.width()),
                       qRound(normalized.height() * preview.height()));
            rect = rect.normalized().adjusted(-8, -8, 8, 8).intersected(preview.rect());
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
