#include "controller/MainController.h"

#include "services/AIProcessor.h"
#include "services/CameraPermissionService.h"
#include "services/MeetingService.h"
#include "services/NetworkService.h"
#include "services/UserService.h"
#include "ui/JoinMeetingWindow.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/MeetingWindow.h"
#include "ui/RegisterWindow.h"
#include "video/VideoSource.h"

#include <QByteArray>

MainController::MainController(VideoSource *videoSource,
                               CameraPermissionService *cameraPermissionService,
                               AIProcessor *aiProcessor,
                               MeetingService *meetingService,
                               NetworkService *networkService,
                               UserService *userService,
                               QObject *parent)
    : QObject(parent)
    , m_videoSource(videoSource)
    , m_cameraPermissionService(cameraPermissionService)
    , m_aiProcessor(aiProcessor)
    , m_meetingService(meetingService)
    , m_networkService(networkService)
    , m_userService(userService)
{
    connect(m_videoSource, &VideoSource::frameReady, this, &MainController::onRawFrameReady);
    connect(m_videoSource, &VideoSource::sourceWarning, this, &MainController::updateMeetingStatus);
    connect(m_aiProcessor, &AIProcessor::frameProcessed, this, &MainController::onProcessedFrameReady);
    connect(m_meetingService, &MeetingService::meetingStateChanged, this, &MainController::onMeetingStateChanged);
    if (m_cameraPermissionService) {
        connect(m_cameraPermissionService, &CameraPermissionService::cameraAccessResolved,
                this, &MainController::onCameraPermissionResolved);
    }
}

void MainController::bindView(MainWindow *view)
{
    m_view = view;
    auto *login = view->loginWindow();
    auto *reg = view->registerWindow();
    auto *join = view->joinWindow();
    auto *meeting = view->meetingWindow();

    connect(login, &LoginWindow::goToRegisterRequested, this, &MainController::onGoToRegisterRequested);
    connect(login, &LoginWindow::loginRequested, this, &MainController::onLoginRequested);
    connect(reg, &RegisterWindow::registerSubmitRequested, this, &MainController::onRegisterSubmitRequested);
    connect(reg, &RegisterWindow::backToLoginRequested, this, &MainController::onBackToLoginFromRegisterRequested);
    connect(join, &JoinMeetingWindow::joinMeetingRequested, this, &MainController::onJoinMeetingRequested);
    connect(join, &JoinMeetingWindow::faceProfileEnrollRequested, this, &MainController::onFaceProfileEnrollRequested);
    connect(join, &JoinMeetingWindow::refreshFaceProfilesRequested, this, &MainController::onRefreshFaceProfilesRequested);
    connect(join, &JoinMeetingWindow::backToLoginRequested, this, &MainController::onBackToLoginRequested);

    connect(meeting, &MeetingWindow::leaveClicked, this, &MainController::onLeaveClicked);
    connect(meeting, &MeetingWindow::cameraToggled, this, &MainController::onCameraToggled);
    connect(meeting, &MeetingWindow::microphoneToggled, this, &MainController::onMicrophoneToggled);
    connect(meeting, &MeetingWindow::aiToggled, this, &MainController::onAIToggled);
    connect(meeting, &MeetingWindow::protectionLevelChanged, this, &MainController::onProtectionLevelChanged);
    connect(meeting, &MeetingWindow::enrollFacesRequested, this, &MainController::onEnrollFacesRequested);
    connect(meeting, &MeetingWindow::roomWhitelistChanged, this, &MainController::onMeetingWhitelistChanged);

    meeting->setAIEnabled(m_aiProcessor->isEnabled());
}

void MainController::onGoToRegisterRequested()
{
    if (!m_view) return;
    m_view->loginWindow()->setErrorMessage(QString());
    m_view->registerWindow()->setMessage(QString());
    m_view->showRegisterPage();
}

void MainController::onRegisterSubmitRequested(const QString &username,
                                               const QString &password,
                                               const QString &confirmPassword)
{
    if (!m_view) return;
    const QString name = username.trimmed();
    if (name.isEmpty() || password.isEmpty() || confirmPassword.isEmpty()) {
        m_view->registerWindow()->setMessage("请完整填写用户名和密码。");
        return;
    }
    if (password.size() < 6) {
        m_view->registerWindow()->setMessage("密码长度至少 6 位。");
        return;
    }
    if (password != confirmPassword) {
        m_view->registerWindow()->setMessage("两次输入的密码不一致。");
        return;
    }
    if (!m_userService->registerUser(name, password)) {
        const QString err = m_userService->lastError();
        m_view->registerWindow()->setMessage(err.isEmpty() ? "注册失败，请稍后重试" : err);
        return;
    }
    m_view->registerWindow()->setMessage(QString());
    m_view->loginWindow()->setErrorMessage("注册成功，请登录");
    m_view->showLoginPage();
}

void MainController::onBackToLoginFromRegisterRequested()
{
    if (!m_view) return;
    m_view->registerWindow()->setMessage(QString());
    m_view->showLoginPage();
}

void MainController::onLoginRequested(const QString &username, const QString &password)
{
    if (!m_view) return;
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        m_view->loginWindow()->setErrorMessage("请输入用户名和密码。");
        return;
    }
    if (!m_userService->login(username.trimmed(), password)) {
        const QString err = m_userService->lastError();
        m_view->loginWindow()->setErrorMessage(err.isEmpty() ? "用户名或密码错误" : err);
        return;
    }
    m_userName = username.trimmed();
    m_view->loginWindow()->setErrorMessage(QString());
    m_view->joinWindow()->setDisplayName(m_userName);
    refreshFaceProfiles();
    m_view->showJoinPage();
}

void MainController::onJoinMeetingRequested(const QString &meetingId,
                                            const QString &displayName,
                                            bool cameraOn,
                                            bool microphoneOn,
                                            const QStringList &whitelist,
                                            bool arcfaceEnabled)
{
    if (!m_view) return;
    m_meetingId = meetingId;
    m_userName = displayName.isEmpty() ? "User" : displayName;
    m_cameraEnabled = cameraOn;
    m_microphoneEnabled = microphoneOn;
    m_whitelist = whitelist;
    m_arcfaceEnabled = arcfaceEnabled;
    m_pendingSelfEnroll = false;
    m_hasProcessedAiFrame = false;

    if (!m_meetingService->joinMeeting(m_meetingId, m_userName)) {
        const QString err = m_meetingService->lastError();
        m_view->joinWindow()->setNotice(err.isEmpty() ? "加入会议失败。" : err);
        return;
    }

    QStringList participants = m_userService->getParticipants();
    if (participants.isEmpty() || participants.first() != m_userName) {
        participants.prepend(m_userName);
    }
    m_view->meetingWindow()->setParticipants(participants);
    m_view->meetingWindow()->setMeetingInfo(m_meetingId, m_userName);
    m_view->meetingWindow()->setLocalMediaState(m_cameraEnabled, m_microphoneEnabled);
    if (m_arcfaceEnabled && !m_aiProcessor->isEnabled()) {
        m_aiProcessor->setEnabled(true);
    }
    m_view->meetingWindow()->setAIEnabled(m_aiProcessor->isEnabled());

    m_meetingFaceProfiles = m_availableFaceProfiles;
    m_view->meetingWindow()->setMeetingFaceProfiles(m_meetingFaceProfiles, m_whitelist);
    m_view->meetingWindow()->setArcFaceEnabled(m_arcfaceEnabled);

    m_aiProcessor->clearPrivacyContext();
    m_aiProcessor->setPrivacyContext(m_meetingId, m_whitelist, m_arcfaceEnabled, true);
    if (m_arcfaceEnabled && !m_availableFaceProfiles.isEmpty()) {
        QStringList profileKeys;
        for (const FaceProfileSummary &summary : m_availableFaceProfiles) {
            profileKeys << summary.profileKey;
        }
        const QList<FaceProfileRecord> profiles = m_userService->fetchFaceProfiles(profileKeys);
        for (const FaceProfileRecord &profile : profiles) {
            const QImage image = imageFromBase64(profile.imageBase64);
            if (!image.isNull()) {
                m_aiProcessor->enrollFace(profile.profileKey, image);
            }
        }
    }

    m_view->meetingWindow()->setStatusMessage(
        m_arcfaceEnabled
            ? (m_whitelist.isEmpty()
                   ? "已加入会议。ArcFace 已启用，AI 已自动开启；当前未选择任何可见人脸，检测到的人脸将自动打码。"
                   : QString("已加入会议。ArcFace 已启用，AI 已自动开启，当前允许显示 %1 个命名人脸条目。").arg(m_whitelist.size()))
            : "已加入会议。ArcFace 未启用。");
    m_view->showMeetingPage();

    if (m_cameraEnabled) {
        startCameraIfAllowed();
    }
}

void MainController::onBackToLoginRequested()
{
    if (!m_view) return;
    m_view->joinWindow()->setNotice("可创建或加入会议。");
    m_view->showLoginPage();
}

void MainController::onFaceProfileEnrollRequested(const QString &label, const QImage &image)
{
    if (!m_view) return;
    if (!m_userService->uploadMyFaceProfile(label, image)) {
        const QString err = m_userService->lastError();
        m_view->joinWindow()->setNotice(err.isEmpty() ? "人脸资料录入失败。" : err);
        return;
    }
    refreshFaceProfiles("我的人脸资料已更新，可在会议中勾选允许显示的人脸用户。");
}

void MainController::onRefreshFaceProfilesRequested()
{
    refreshFaceProfiles("已刷新可选人脸资料列表。");
}

void MainController::onLeaveClicked()
{
    m_videoSource->stop();
    if (m_joined) {
        m_meetingService->leaveMeeting();
    }
    m_userService->logout();
    m_aiProcessor->clearPrivacyContext();
    m_joined = false;
    m_meetingId.clear();
    m_whitelist.clear();
    m_arcfaceEnabled = false;
    m_pendingSelfEnroll = false;
    m_hasProcessedAiFrame = false;
    if (m_view) {
        m_view->meetingWindow()->clearPrimaryFrame();
        m_view->loginWindow()->setErrorMessage(QString());
        m_view->showLoginPage();
    }
}

void MainController::onCameraToggled(bool enabled)
{
    m_cameraEnabled = enabled;
    if (enabled) {
        m_hasProcessedAiFrame = false;
        startCameraIfAllowed();
    } else {
        m_videoSource->stop();
        m_hasProcessedAiFrame = false;
        if (m_view) m_view->meetingWindow()->clearPrimaryFrame();
    }
    if (m_view) m_view->meetingWindow()->setLocalMediaState(m_cameraEnabled, m_microphoneEnabled);
    updateMeetingStatus(enabled ? "摄像头已开启。" : "摄像头已关闭。");
}

void MainController::onMicrophoneToggled(bool enabled)
{
    m_microphoneEnabled = enabled;
    if (m_view) m_view->meetingWindow()->setLocalMediaState(m_cameraEnabled, m_microphoneEnabled);
    updateMeetingStatus(enabled ? "麦克风已开启（演示模式）。" : "麦克风已静音（演示模式）。");
}

void MainController::onAIToggled(bool enabled)
{
    m_aiProcessor->setEnabled(enabled);
    if (enabled) {
        m_hasProcessedAiFrame = false;
    }
    updateMeetingStatus(enabled ? "AI 处理已开启（含物体检测与 ArcFace）。" : "AI 处理已关闭。");
}

void MainController::onEnrollFacesRequested(const QString &labelPrefix)
{
    if (!m_view) return;
    const QImage frame = m_videoSource->getFrame();
    if (frame.isNull()) {
        updateMeetingStatus("无法录入：当前没有可用画面，请开启摄像头。");
        return;
    }
    const QList<FaceProfileSummary> enrolled = m_aiProcessor->enrollFaces(labelPrefix, frame);
    if (enrolled.isEmpty()) {
        updateMeetingStatus("当前画面未检测到可录入的人脸。");
        return;
    }
    QStringList selected = m_whitelist;
    for (const FaceProfileSummary &summary : enrolled) {
        bool exists = false;
        for (const FaceProfileSummary &item : m_meetingFaceProfiles) {
            if (item.profileKey == summary.profileKey) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_meetingFaceProfiles.append(summary);
        }
        if (!selected.contains(summary.profileKey)) {
            selected.append(summary.profileKey);
        }
    }
    m_whitelist = selected;
    m_view->meetingWindow()->setMeetingFaceProfiles(m_meetingFaceProfiles, m_whitelist);
    m_aiProcessor->setPrivacyContext(m_meetingId, m_whitelist, m_arcfaceEnabled, true);
    updateMeetingStatus(QString("已从当前画面录入 %1 张人脸，可在列表里勾选是否显示。").arg(enrolled.size()));
}

void MainController::onMeetingWhitelistChanged(const QStringList &selectedProfileKeys)
{
    m_whitelist = selectedProfileKeys;
    m_aiProcessor->setPrivacyContext(m_meetingId, m_whitelist, m_arcfaceEnabled, true);
    if (m_view) {
        m_view->meetingWindow()->setStatusMessage(
            m_whitelist.isEmpty()
                ? QStringLiteral("当前未勾选可见人脸，检测到的人脸将自动打码。")
                : QStringLiteral("当前允许显示 %1 个命名人脸条目。").arg(m_whitelist.size()));
    }
}

void MainController::onProtectionLevelChanged(const QString &level)
{
    updateMeetingStatus(QString("AI Protection: %1。").arg(level));
}

void MainController::onRawFrameReady(const QImage &frame)
{
    if (!m_joined || !m_cameraEnabled) return;
    const bool preferProcessedPreview = m_aiProcessor->isEnabled() && m_arcfaceEnabled;
    if (m_view && (!preferProcessedPreview || !m_hasProcessedAiFrame)) {
        m_view->meetingWindow()->setPrimaryFrame(frame);
    }
    if (m_pendingSelfEnroll && m_arcfaceEnabled && !m_userName.isEmpty()) {
        m_aiProcessor->enrollFace(m_userName, frame);
        m_pendingSelfEnroll = false;
        updateMeetingStatus(QString("已自动录入 %1 的人脸；可用「录入当前画面人脸」补充白名单。").arg(m_userName));
    }
    m_networkService->sendFrame("frame");
    m_aiProcessor->processFrame(frame);
}

void MainController::onProcessedFrameReady(const QImage &frame)
{
    m_hasProcessedAiFrame = true;
    if (m_view) {
        m_view->meetingWindow()->setPrimaryFrame(frame);
    }
}

void MainController::onMeetingStateChanged(bool joined, const QString &message)
{
    m_joined = joined;
    updateMeetingStatus(message);
}

void MainController::onCameraPermissionResolved(bool granted, const QString &message)
{
    if (!m_cameraEnabled) {
        return;
    }
    if (granted) {
        m_videoSource->start();
        if (!message.isEmpty()) {
            updateMeetingStatus(message);
        }
        return;
    }

    m_cameraEnabled = false;
    if (m_view) {
        m_view->meetingWindow()->clearPrimaryFrame();
        m_view->meetingWindow()->setLocalMediaState(m_cameraEnabled, m_microphoneEnabled);
    }
    updateMeetingStatus(message.isEmpty() ? QStringLiteral("摄像头权限未授予，已关闭本地摄像头。")
                                          : message);
}

void MainController::startCameraIfAllowed()
{
    if (!m_cameraPermissionService) {
        m_videoSource->start();
        return;
    }

    QString permissionMessage;
    const CameraPermissionService::AccessState state =
        m_cameraPermissionService->ensureCameraAccess(&permissionMessage);
    switch (state) {
    case CameraPermissionService::AccessState::Granted:
        m_videoSource->start();
        break;
    case CameraPermissionService::AccessState::Pending:
        if (!permissionMessage.isEmpty()) {
            updateMeetingStatus(permissionMessage);
        }
        break;
    case CameraPermissionService::AccessState::Denied:
        m_cameraEnabled = false;
        if (m_view) {
            m_view->meetingWindow()->clearPrimaryFrame();
            m_view->meetingWindow()->setLocalMediaState(m_cameraEnabled, m_microphoneEnabled);
        }
        updateMeetingStatus(permissionMessage.isEmpty()
                                ? QStringLiteral("摄像头权限未授予，已关闭本地摄像头。")
                                : permissionMessage);
        break;
    }
}

void MainController::updateMeetingStatus(const QString &message) const
{
    if (m_view) {
        m_view->meetingWindow()->setStatusMessage(message);
    }
}

void MainController::refreshFaceProfiles(const QString &statusMessage)
{
    if (!m_view) return;
    const QList<FaceProfileSummary> profiles = m_userService->listFaceProfiles();
    m_availableFaceProfiles = profiles;
    bool selfEnrolled = false;
    QStringList selectedKeys;
    for (const FaceProfileSummary &profile : profiles) {
        if (profile.username == m_userName) {
            selfEnrolled = true;
            selectedKeys << profile.profileKey;
        }
    }
    m_view->joinWindow()->setAvailableFaceUsers(profiles, selectedKeys);
    m_view->joinWindow()->setFaceProfileStatus(
        selfEnrolled,
        selfEnrolled ? QStringLiteral("已录入命名人脸，可在入会时勾选允许显示的条目。")
                     : QStringLiteral("请先上传一张或多张清晰正脸照片。")
    );
    if (!statusMessage.isEmpty()) {
        m_view->joinWindow()->setNotice(statusMessage);
    }
}

QImage MainController::imageFromBase64(const QString &imageBase64)
{
    QImage image;
    image.loadFromData(QByteArray::fromBase64(imageBase64.toLatin1()), "PNG");
    return image;
}
