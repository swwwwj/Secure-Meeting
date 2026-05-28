#include "ui/JoinMeetingWindow.h"

#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QFileInfo>

JoinMeetingWindow::JoinMeetingWindow(QWidget *parent)
    : QWidget(parent)
    , m_displayNameEdit(new QLineEdit(this))
    , m_meetingIdEdit(new QLineEdit(this))
    , m_faceNameEdit(new QLineEdit(this))
    , m_cameraCheck(new QCheckBox("进入会议后开启摄像头", this))
    , m_microphoneCheck(new QCheckBox("进入会议后开启麦克风", this))
    , m_arcfaceCheck(new QCheckBox("启用 ArcFace 无关人员模糊", this))
    , m_faceProfileStatusLabel(new QLabel(this))
    , m_uploadFaceButton(new QPushButton("选择照片录入我的人脸", this))
    , m_refreshProfilesButton(new QPushButton("刷新已录入用户", this))
    , m_faceProfileList(new QListWidget(this))
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
    m_faceNameEdit->setObjectName("inputField");
    m_faceNameEdit->setPlaceholderText("人脸命名（单张直接使用；多张时作为前缀，可留空使用文件名）");
    m_faceNameEdit->setFixedHeight(40);

    m_cameraCheck->setChecked(true);
    m_microphoneCheck->setChecked(true);
    m_arcfaceCheck->setChecked(true);

    auto *profileTitle = new QLabel("我的 ArcFace 人脸资料", card);
    profileTitle->setObjectName("sectionTitle");
    auto *profileRow = new QHBoxLayout();
    m_faceProfileStatusLabel->setObjectName("mutedText");
    m_faceProfileStatusLabel->setText("未录入，请先上传一张清晰正脸照片。");
    m_uploadFaceButton->setObjectName("secondaryButton");
    m_uploadFaceButton->setFixedHeight(38);
    m_refreshProfilesButton->setObjectName("textButton");
    profileRow->addWidget(m_faceProfileStatusLabel, 1);
    profileRow->addWidget(m_uploadFaceButton);
    profileRow->addWidget(m_refreshProfilesButton);

    auto *wlTitle = new QLabel("会议可见人脸名单（仅选中的已录入用户不打码）", card);
    wlTitle->setObjectName("sectionTitle");
    m_faceProfileList->setSelectionMode(QAbstractItemView::NoSelection);

    m_noticeLabel->setObjectName("mutedText");
    m_noticeLabel->setText("登录后先录入自己的人脸资料；入会时勾选允许正常显示的人脸用户，其他人脸将自动打码。");

    cardLayout->addWidget(m_displayNameEdit);
    cardLayout->addWidget(m_meetingIdEdit);
    cardLayout->addWidget(m_cameraCheck);
    cardLayout->addWidget(m_microphoneCheck);
    cardLayout->addWidget(m_arcfaceCheck);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(profileTitle);
    cardLayout->addWidget(m_faceNameEdit);
    cardLayout->addLayout(profileRow);
    cardLayout->addSpacing(4);
    cardLayout->addWidget(wlTitle);
    cardLayout->addWidget(m_faceProfileList);
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

    connect(m_uploadFaceButton, &QPushButton::clicked, this, [this]() {
        const QStringList filePaths = QFileDialog::getOpenFileNames(
            this,
            QStringLiteral("选择一个或多个人脸照片"),
            QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)")
        );
        if (filePaths.isEmpty()) return;
        const QString baseLabel = m_faceNameEdit->text().trimmed();
        for (int i = 0; i < filePaths.size(); ++i) {
            const QString &filePath = filePaths[i];
            QImage image(filePath);
            if (image.isNull()) {
                continue;
            }
            QString label = QFileInfo(filePath).completeBaseName().trimmed();
            if (!baseLabel.isEmpty()) {
                label = filePaths.size() == 1 ? baseLabel : QStringLiteral("%1-%2").arg(baseLabel).arg(i + 1);
            }
            emit faceProfileEnrollRequested(label, image);
        }
    });
    connect(m_refreshProfilesButton, &QPushButton::clicked, this, &JoinMeetingWindow::refreshFaceProfilesRequested);
    connect(back, &QPushButton::clicked, this, &JoinMeetingWindow::backToLoginRequested);
    connect(m_joinButton, &QPushButton::clicked, this, [this]() {
        QString room = m_meetingIdEdit->text().trimmed();
        if (room.isEmpty()) room = "secure-room-001";
        emit joinMeetingRequested(room,
                                  m_displayNameEdit->text().trimmed(),
                                  m_cameraCheck->isChecked(),
                                  m_microphoneCheck->isChecked(),
                                  selectedFaceUsers(),
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

void JoinMeetingWindow::setFaceProfileStatus(bool enrolled, const QString &message)
{
    m_faceProfileStatusLabel->setText(enrolled ? QStringLiteral("我的资料：已录入。%1").arg(message)
                                               : QStringLiteral("我的资料：未录入。%1").arg(message));
}

void JoinMeetingWindow::setAvailableFaceUsers(const QList<FaceProfileSummary> &profiles, const QStringList &selectedProfileKeys)
{
    QStringList effectiveSelected = selectedProfileKeys;
    if (effectiveSelected.isEmpty()) {
        for (int i = 0; i < m_faceProfileList->count(); ++i) {
            QListWidgetItem *item = m_faceProfileList->item(i);
            if (item && item->checkState() == Qt::Checked) {
                effectiveSelected << item->data(Qt::UserRole).toString();
            }
        }
    }

    m_faceProfileList->clear();
    for (const FaceProfileSummary &profile : profiles) {
        if (profile.profileKey.trimmed().isEmpty()) continue;
        auto *item = new QListWidgetItem(profile.displayName(), m_faceProfileList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, profile.profileKey);
        item->setCheckState(effectiveSelected.contains(profile.profileKey) ? Qt::Checked : Qt::Unchecked);
    }
}

QStringList JoinMeetingWindow::selectedFaceUsers() const
{
    QStringList out;
    for (int i = 0; i < m_faceProfileList->count(); ++i) {
        QListWidgetItem *item = m_faceProfileList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            out << item->data(Qt::UserRole).toString();
        }
    }
    return out;
}
