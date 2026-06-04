#include "ui/MeetingWindow.h"

#include "ui/VideoWidget.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

MeetingWindow::MeetingWindow(QWidget *parent)
    : QWidget(parent)
    , m_gridHost(new QWidget(this))
    , m_grid(new QGridLayout(m_gridHost))
    , m_titleLabel(new QLabel("会议号 --", this))
    , m_statusLabel(new QLabel(this))
    , m_cameraButton(new QToolButton(this))
    , m_microphoneButton(new QToolButton(this))
    , m_aiButton(new QToolButton(this))
    , m_leaveButton(new QPushButton("离开会议", this))
    , m_protectionGroup(new QButtonGroup(this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto *header = new QWidget(this);
    header->setObjectName("topBar");
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(16, 10, 16, 10);
    m_titleLabel->setObjectName("meetingTitle");
    m_statusLabel->setObjectName("mutedText");
    h->addWidget(m_titleLabel);
    h->addStretch();
    h->addWidget(m_statusLabel);
    root->addWidget(header);

    m_grid->setSpacing(10);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_gridHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *protectionPanel = new QWidget(this);
    protectionPanel->setObjectName("panelCard");
    auto *ph = new QHBoxLayout(protectionPanel);
    ph->setContentsMargins(14, 10, 14, 10);
    ph->addWidget(new QLabel("AI Protection", protectionPanel));
    const QStringList levels = {"关闭", "低", "中", "高"};
    for (int i = 0; i < levels.size(); ++i) {
        auto *btn = new QToolButton(protectionPanel);
        btn->setText(levels[i]);
        btn->setCheckable(true);
        btn->setObjectName("segButton");
        if (i == 1) btn->setChecked(true);
        m_protectionGroup->addButton(btn, i);
        ph->addWidget(btn);
    }
    ph->addStretch();
    m_arcfacePanel = new QWidget(this);
    m_arcfacePanel->setObjectName("panelCard");
    auto *ah = new QVBoxLayout(m_arcfacePanel);
    ah->setContentsMargins(14, 10, 14, 10);
    ah->setSpacing(10);
    m_arcfaceStatusLabel = new QLabel("ArcFace: 关闭", m_arcfacePanel);
    m_arcfaceStatusLabel->setObjectName("mutedText");
    m_enrollFaceNameEdit = new QLineEdit(m_arcfacePanel);
    m_enrollFaceNameEdit->setObjectName("inputField");
    m_enrollFaceNameEdit->setPlaceholderText("当前画面人脸命名前缀，例如 前排嘉宾");
    m_enrollFaceButton = new QPushButton("录入当前画面人脸", m_arcfacePanel);
    m_enrollFaceButton->setObjectName("secondaryButton");
    m_enrollFaceButton->setEnabled(false);
    m_roomFaceList = new QListWidget(m_arcfacePanel);
    m_roomFaceList->setSelectionMode(QAbstractItemView::NoSelection);
    m_roomFaceList->setMinimumHeight(180);
    auto *row = new QHBoxLayout();
    row->addWidget(m_enrollFaceNameEdit, 1);
    row->addWidget(m_enrollFaceButton);
    ah->addWidget(m_arcfaceStatusLabel);
    ah->addLayout(row);
    ah->addWidget(new QLabel("会议内可见人脸条目", m_arcfacePanel));
    ah->addWidget(m_roomFaceList);

    auto *content = new QWidget(this);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *sideBar = new QWidget(content);
    sideBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    sideBar->setMinimumWidth(320);
    sideBar->setMaximumWidth(360);
    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);
    sideLayout->addWidget(protectionPanel);
    sideLayout->addWidget(m_arcfacePanel, 1);

    contentLayout->addWidget(m_gridHost, 1);
    contentLayout->addWidget(sideBar);
    root->addWidget(content, 1);

    auto *floating = new QWidget(this);
    floating->setObjectName("floatingBar");
    auto *fh = new QHBoxLayout(floating);
    fh->setContentsMargins(18, 12, 18, 12);
    fh->setSpacing(10);

    m_cameraButton->setText("摄像头");
    m_cameraButton->setCheckable(true);
    m_cameraButton->setChecked(true);
    m_cameraButton->setObjectName("floatingButton");
    m_microphoneButton->setText("麦克风");
    m_microphoneButton->setCheckable(true);
    m_microphoneButton->setChecked(true);
    m_microphoneButton->setObjectName("floatingButton");
    m_aiButton->setText("AI");
    m_aiButton->setCheckable(true);
    m_aiButton->setObjectName("floatingButton");
    m_leaveButton->setObjectName("dangerButton");

    fh->addStretch();
    fh->addWidget(m_cameraButton);
    fh->addWidget(m_microphoneButton);
    fh->addWidget(m_aiButton);
    fh->addSpacing(8);
    fh->addWidget(m_leaveButton);
    fh->addStretch();
    root->addWidget(floating);

    connect(m_leaveButton, &QPushButton::clicked, this, &MeetingWindow::leaveClicked);
    connect(m_cameraButton, &QToolButton::toggled, this, &MeetingWindow::cameraToggled);
    connect(m_microphoneButton, &QToolButton::toggled, this, &MeetingWindow::microphoneToggled);
    connect(m_aiButton, &QToolButton::toggled, this, &MeetingWindow::aiToggled);
    connect(m_protectionGroup, &QButtonGroup::idClicked, this, [this, levels](int id) {
        emit protectionLevelChanged(levels.value(id, "低"));
    });
    connect(m_enrollFaceButton, &QPushButton::clicked, this, [this]() {
        emit enrollFacesRequested(m_enrollFaceNameEdit->text().trimmed());
    });
    connect(m_roomFaceList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
        QStringList selected;
        for (int i = 0; i < m_roomFaceList->count(); ++i) {
            QListWidgetItem *item = m_roomFaceList->item(i);
            if (item && item->checkState() == Qt::Checked) {
                selected << item->data(Qt::UserRole).toString();
            }
        }
        emit roomWhitelistChanged(selected);
    });
}

void MeetingWindow::setMeetingInfo(const QString &meetingId, const QString &userName)
{
    m_titleLabel->setText(QString("会议号 %1  ·  %2").arg(meetingId, userName));
}

void MeetingWindow::setStatusMessage(const QString &message)
{
    m_statusLabel->setText(message);
}

void MeetingWindow::setParticipants(const QStringList &participants)
{
    rebuildGrid(participants);
}

void MeetingWindow::setPrimaryFrame(const QImage &frame)
{
    if (!m_tiles.isEmpty()) {
        m_tiles.first()->setFrame(frame);
    }
}

void MeetingWindow::setLocalMediaState(bool cameraOn, bool microphoneOn)
{
    m_cameraButton->setChecked(cameraOn);
    m_microphoneButton->setChecked(microphoneOn);
    if (!m_tiles.isEmpty()) {
        m_tiles.first()->setMediaState(cameraOn, microphoneOn);
    }
}

void MeetingWindow::clearPrimaryFrame()
{
    if (!m_tiles.isEmpty()) {
        m_tiles.first()->clearFrame();
    }
}

void MeetingWindow::setAIEnabled(bool enabled)
{
    m_aiButton->setChecked(enabled);
}

void MeetingWindow::setPrimaryPrivacyRegions(const QVector<QRectF> &regions)
{
    if (!m_tiles.isEmpty()) {
        m_tiles.first()->setPrivacyRegions(regions);
    }
}

void MeetingWindow::setPrimaryProcessedFrame(const QImage &frame)
{
    if (!m_tiles.isEmpty()) {
        m_tiles.first()->setProcessedFrame(frame);
    }
}

void MeetingWindow::setVideoPrivacyOptions(int blurRadius, bool useKalmanTracking, int maxProcessedFrameAgeMs)
{
    m_blurRadius = blurRadius;
    m_useKalmanTracking = useKalmanTracking;
    m_maxProcessedFrameAgeMs = maxProcessedFrameAgeMs;
    for (VideoWidget *tile : m_tiles) {
        if (!tile) continue;
        tile->setBlurRadius(m_blurRadius);
        tile->setUseKalmanTracking(m_useKalmanTracking);
        tile->setMaxProcessedFrameAgeMs(m_maxProcessedFrameAgeMs);
    }
}

void MeetingWindow::setArcFaceEnabled(bool enabled)
{
    m_arcfacePanel->setVisible(enabled);
    m_arcfaceStatusLabel->setText(enabled ? QStringLiteral("ArcFace: 已启用")
                                         : QStringLiteral("ArcFace: 关闭"));
    m_enrollFaceButton->setEnabled(enabled);
}

void MeetingWindow::setMeetingFaceProfiles(const QList<FaceProfileSummary> &profiles, const QStringList &selectedProfileKeys)
{
    m_roomFaceList->blockSignals(true);
    m_roomFaceList->clear();
    for (const FaceProfileSummary &profile : profiles) {
        if (profile.profileKey.trimmed().isEmpty()) continue;
        auto *item = new QListWidgetItem(profile.displayName(), m_roomFaceList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, profile.profileKey);
        item->setCheckState(selectedProfileKeys.contains(profile.profileKey) ? Qt::Checked : Qt::Unchecked);
    }
    m_roomFaceList->blockSignals(false);
    m_enrollFaceButton->setEnabled(m_arcfacePanel->isVisible());
}

void MeetingWindow::rebuildGrid(const QStringList &participants)
{
    while (m_grid->count() > 0) {
        delete m_grid->takeAt(0);
    }
    qDeleteAll(m_tiles);
    m_tiles.clear();

    const int count = participants.size();
    if (count == 0) return;
    const int cols = columnsForCount(count);

    for (int i = 0; i < count; ++i) {
        auto *tile = new VideoWidget(participants[i], m_gridHost);
        tile->setBlurRadius(m_blurRadius);
        tile->setUseKalmanTracking(m_useKalmanTracking);
        tile->setMaxProcessedFrameAgeMs(m_maxProcessedFrameAgeMs);
        if (i > 0) tile->setMediaState(true, true);
        m_tiles.append(tile);
        m_grid->addWidget(tile, i / cols, i % cols);
    }
    const int rows = (count + cols - 1) / cols;
    for (int col = 0; col < cols; ++col) {
        m_grid->setColumnStretch(col, 1);
    }
    for (int row = 0; row < rows; ++row) {
        m_grid->setRowStretch(row, 1);
    }
}

int MeetingWindow::columnsForCount(int count)
{
    if (count <= 1) return 1;
    if (count == 2) return 2;
    if (count <= 4) return 2;
    return 3;
}
