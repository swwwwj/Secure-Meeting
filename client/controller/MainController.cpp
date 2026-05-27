#include "controller/MainController.h"

#include "services/AIProcessor.h"
#include "services/MeetingService.h"
#include "services/NetworkService.h"
#include "services/UserService.h"
#include "ui/JoinMeetingWindow.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/MeetingWindow.h"
#include "ui/RegisterWindow.h"
#include "video/VideoSource.h"

MainController::MainController(VideoSource *videoSource,
                               AIProcessor *aiProcessor,
                               MeetingService *meetingService,
                               NetworkService *networkService,
                               UserService *userService,
                               QObject *parent)
    : QObject(parent)
    , m_videoSource(videoSource)
    , m_aiProcessor(aiProcessor)
    , m_meetingService(meetingService)
    , m_networkService(networkService)
    , m_userService(userService)
{
    connect(m_videoSource, &VideoSource::frameReady, this, &MainController::onRawFrameReady);
    connect(m_aiProcessor, &AIProcessor::frameProcessed, this, &MainController::onProcessedFrameReady);
    connect(m_meetingService, &MeetingService::meetingStateChanged, this, &MainController::onMeetingStateChanged);
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
    connect(join, &JoinMeetingWindow::backToLoginRequested, this, &MainController::onBackToLoginRequested);

    connect(meeting, &MeetingWindow::leaveClicked, this, &MainController::onLeaveClicked);
    connect(meeting, &MeetingWindow::cameraToggled, this, &MainController::onCameraToggled);
    connect(meeting, &MeetingWindow::microphoneToggled, this, &MainController::onMicrophoneToggled);
    connect(meeting, &MeetingWindow::aiToggled, this, &MainController::onAIToggled);
    connect(meeting, &MeetingWindow::protectionLevelChanged, this, &MainController::onProtectionLevelChanged);
    connect(meeting, &MeetingWindow::enrollFaceRequested, this, &MainController::onEnrollFaceRequested);

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
    m_pendingSelfEnroll = arcfaceEnabled;

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

    QStringList enrollUsers = m_whitelist;
    if (!enrollUsers.contains(m_userName)) {
        enrollUsers.prepend(m_userName);
    }
    m_view->meetingWindow()->setEnrollableUsers(enrollUsers);
    m_view->meetingWindow()->setArcFaceEnabled(m_arcfaceEnabled);

    m_aiProcessor->setPrivacyContext(m_meetingId, m_whitelist, m_arcfaceEnabled, true);

    m_view->meetingWindow()->setStatusMessage(
        m_arcfaceEnabled
            ? (m_whitelist.isEmpty()
                   ? "已加入会议。请为本人录入人脸；未在白名单的外人将模糊。"
                   : QString("已加入会议。ArcFace 已启用，白名单: %1").arg(m_whitelist.join(", ")))
            : "已加入会议。ArcFace 未启用。");
    m_view->showMeetingPage();

    if (m_cameraEnabled) {
        m_videoSource->start();
    }
}

void MainController::onBackToLoginRequested()
{
    if (!m_view) return;
    m_view->joinWindow()->setNotice("可创建或加入会议。");
    m_view->showLoginPage();
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
        m_videoSource->start();
    } else {
        m_videoSource->stop();
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
    updateMeetingStatus(enabled ? "AI 处理已开启（含物体检测与 ArcFace）。" : "AI 处理已关闭。");
}

void MainController::onEnrollFaceRequested(const QString &userId)
{
    if (!m_view || userId.trimmed().isEmpty()) return;
    const QImage frame = m_videoSource->getFrame();
    if (frame.isNull()) {
        updateMeetingStatus("无法录入：当前没有可用画面，请开启摄像头。");
        return;
    }
    m_aiProcessor->enrollFace(userId.trimmed(), frame);
    updateMeetingStatus(QString("已向 AI 服务录入用户 %1 的人脸模板。").arg(userId.trimmed()));
}

void MainController::onProtectionLevelChanged(const QString &level)
{
    updateMeetingStatus(QString("AI Protection: %1。").arg(level));
}

void MainController::onRawFrameReady(const QImage &frame)
{
    if (!m_joined || !m_cameraEnabled) return;
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
    if (m_view) {
        m_view->meetingWindow()->setPrimaryFrame(frame);
    }
}

void MainController::onMeetingStateChanged(bool joined, const QString &message)
{
    m_joined = joined;
    updateMeetingStatus(message);
}

void MainController::updateMeetingStatus(const QString &message) const
{
    if (m_view) {
        m_view->meetingWindow()->setStatusMessage(message);
    }
}
