#include "ui/MeetingWindow.h"

#include "ui/VideoWidget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

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

    m_grid->setSpacing(12);
    m_grid->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_gridHost, 1);

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
    root->addWidget(protectionPanel);

    m_arcfacePanel = new QWidget(this);
    m_arcfacePanel->setObjectName("panelCard");
    auto *ah = new QHBoxLayout(m_arcfacePanel);
    ah->setContentsMargins(14, 10, 14, 10);
    m_arcfaceStatusLabel = new QLabel("ArcFace: 关闭", m_arcfacePanel);
    m_arcfaceStatusLabel->setObjectName("mutedText");
    m_enrollUserCombo = new QComboBox(m_arcfacePanel);
    m_enrollUserCombo->setObjectName("inputField");
    m_enrollUserCombo->setMinimumWidth(160);
    m_enrollFaceButton = new QPushButton("录入当前画面人脸", m_arcfacePanel);
    m_enrollFaceButton->setObjectName("secondaryButton");
    m_enrollFaceButton->setEnabled(false);
    ah->addWidget(m_arcfaceStatusLabel);
    ah->addWidget(m_enrollUserCombo, 1);
    ah->addWidget(m_enrollFaceButton);
    root->addWidget(m_arcfacePanel);

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
        const QString userId = m_enrollUserCombo->currentText().trimmed();
        if (!userId.isEmpty()) {
            emit enrollFaceRequested(userId);
        }
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

void MeetingWindow::setArcFaceEnabled(bool enabled)
{
    m_arcfacePanel->setVisible(enabled);
    m_arcfaceStatusLabel->setText(enabled ? QStringLiteral("ArcFace: 已启用")
                                         : QStringLiteral("ArcFace: 关闭"));
    m_enrollFaceButton->setEnabled(enabled && m_enrollUserCombo->count() > 0);
}

void MeetingWindow::setEnrollableUsers(const QStringList &users)
{
    m_enrollUserCombo->clear();
    for (const QString &user : users) {
        if (!user.trimmed().isEmpty()) {
            m_enrollUserCombo->addItem(user.trimmed());
        }
    }
    m_enrollFaceButton->setEnabled(m_enrollUserCombo->count() > 0 && m_arcfacePanel->isVisible());
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
        if (i > 0) tile->setMediaState(true, true);
        m_tiles.append(tile);
        m_grid->addWidget(tile, i / cols, i % cols);
    }
}

int MeetingWindow::columnsForCount(int count)
{
    if (count <= 1) return 1;
    if (count == 2) return 2;
    if (count <= 4) return 2;
    return 3;
}
