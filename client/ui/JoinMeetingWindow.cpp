#include "ui/JoinMeetingWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QCheckBox>

JoinMeetingWindow::JoinMeetingWindow(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_meetingIdEdit(new QLineEdit(this))
    , m_cameraCheck(new QCheckBox("进入会议后开启摄像头", this))
    , m_microphoneCheck(new QCheckBox("进入会议后开启麦克风", this))
    , m_arcfaceCheck(new QCheckBox("启用 ArcFace 无关人员模糊", this))
    , m_whitelistInput(new QLineEdit(this))
    , m_whitelistList(new QListWidget(this))
    , m_noticeLabel(new QLabel(this))
    , m_joinButton(new QPushButton("加入会议", this))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addSpacing(36);

    auto *content = new QWidget(this);
    content->setMaximumWidth(920);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(32, 20, 32, 20);
    contentLayout->setSpacing(16);

    auto *title = new QLabel("加入会议", this);
    title->setObjectName("pageTitle");
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel("设置入会身份与隐私保护白名单", this);
    subtitle->setObjectName("pageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(title);
    contentLayout->addWidget(subtitle);

    auto *card = new QWidget(this);
    card->setObjectName("panelCard");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 22, 24, 22);
    cardLayout->setSpacing(12);

    m_displayNameEdit->setObjectName("inputField");
    m_displayNameEdit->setPlaceholderText("入会名称");
    m_displayNameEdit->setFixedHeight(40);
    m_meetingIdEdit->setObjectName("inputField");
    m_meetingIdEdit->setPlaceholderText("会议号（默认 secure-room-001）");
    m_meetingIdEdit->setFixedHeight(40);

    m_cameraCheck->setChecked(true);
    m_microphoneCheck->setChecked(true);
    m_arcfaceCheck->setChecked(true);

    auto *wlTitle = new QLabel("隐私保护白名单（已录入人脸的用户不模糊）", card);
    wlTitle->setObjectName("sectionTitle");

    auto *wlRow = new QHBoxLayout();
    m_whitelistInput->setObjectName("inputField");
    m_whitelistInput->setPlaceholderText("输入用户名后添加");
    m_whitelistInput->setFixedHeight(38);
    auto *addButton = new QPushButton("添加", card);
    addButton->setObjectName("secondaryButton");
    addButton->setFixedHeight(38);
    wlRow->addWidget(m_whitelistInput, 1);
    wlRow->addWidget(addButton);

    auto *removeButton = new QPushButton("移除选中", card);
    removeButton->setObjectName("textButton");

    m_noticeLabel->setObjectName("mutedText");
    m_noticeLabel->setText("入会后在会议页点击「录入人脸」；白名单用户需先录入当前画面人脸。");

    cardLayout->addWidget(m_displayNameEdit);
    cardLayout->addWidget(m_meetingIdEdit);
    cardLayout->addWidget(m_cameraCheck);
    cardLayout->addWidget(m_microphoneCheck);
    cardLayout->addWidget(m_arcfaceCheck);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(wlTitle);
    cardLayout->addLayout(wlRow);
    cardLayout->addWidget(m_whitelistList);
    cardLayout->addWidget(removeButton, 0, Qt::AlignLeft);
    cardLayout->addWidget(m_noticeLabel);

    contentLayout->addWidget(card, 1);

    auto *bottom = new QHBoxLayout();
    auto *back = new QPushButton("返回登录", this);
    back->setObjectName("textButton");
    m_joinButton->setObjectName("primaryButton");
    m_joinButton->setFixedHeight(42);
    bottom->addWidget(back);
    bottom->addStretch();
    bottom->addWidget(m_joinButton);
    contentLayout->addLayout(bottom);

    root->addWidget(content, 1, Qt::AlignHCenter);
    root->addSpacing(24);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString user = m_whitelistInput->text().trimmed();
        if (user.isEmpty()) return;
        if (m_whitelistList->findItems(user, Qt::MatchExactly).isEmpty()) {
            m_whitelistList->addItem(user);
        }
        m_whitelistInput->clear();
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        delete m_whitelistList->takeItem(m_whitelistList->currentRow());
    });
    connect(back, &QPushButton::clicked, this, &JoinMeetingWindow::backToLoginRequested);
    connect(m_joinButton, &QPushButton::clicked, this, [this]() {
        QString room = m_meetingIdEdit->text().trimmed();
        if (room.isEmpty()) room = "secure-room-001";
        emit joinMeetingRequested(room,
                                  m_displayNameEdit->text().trimmed(),
                                  m_cameraCheck->isChecked(),
                                  m_microphoneCheck->isChecked(),
                                  whitelistUsers(),
                                  m_arcfaceCheck->isChecked());
    });
}

void JoinMeetingWindow::setDisplayName(const QString &name)
{
    m_displayNameEdit->setText(name);
}

void JoinMeetingWindow::setNotice(const QString &message)
{
    m_noticeLabel->setText(message);
}

QStringList JoinMeetingWindow::whitelistUsers() const
{
    QStringList out;
    for (int i = 0; i < m_whitelistList->count(); ++i) {
        out << m_whitelistList->item(i)->text();
    }
    return out;
}
