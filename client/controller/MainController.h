#pragma once

#include <QObject>
#include <QImage>
#include <QStringList>

class VideoSource;
class AIProcessor;
class MeetingService;
class NetworkService;
class UserService;
class MainWindow;

class MainController : public QObject
{
    Q_OBJECT
public:
    MainController(VideoSource *videoSource,
                   AIProcessor *aiProcessor,
                   MeetingService *meetingService,
                   NetworkService *networkService,
                   UserService *userService,
                   QObject *parent = nullptr);

    void bindView(MainWindow *view);

private slots:
    void onGoToRegisterRequested();
    void onRegisterSubmitRequested(const QString &username, const QString &password, const QString &confirmPassword);
    void onBackToLoginFromRegisterRequested();
    void onLoginRequested(const QString &username, const QString &password);
    void onJoinMeetingRequested(const QString &meetingId,
                                const QString &displayName,
                                bool cameraOn,
                                bool microphoneOn,
                                const QStringList &whitelist);
    void onBackToLoginRequested();
    void onLeaveClicked();
    void onCameraToggled(bool enabled);
    void onMicrophoneToggled(bool enabled);
    void onAIToggled(bool enabled);
    void onProtectionLevelChanged(const QString &level);
    void onRawFrameReady(const QImage &frame);
    void onProcessedFrameReady(const QImage &frame);
    void onMeetingStateChanged(bool joined, const QString &message);

private:
    void updateMeetingStatus(const QString &message) const;

    VideoSource *m_videoSource;
    AIProcessor *m_aiProcessor;
    MeetingService *m_meetingService;
    NetworkService *m_networkService;
    UserService *m_userService;
    MainWindow *m_view = nullptr;

    bool m_joined = false;
    bool m_cameraEnabled = true;
    bool m_microphoneEnabled = true;
    QString m_userName;
    QString m_meetingId;
    QStringList m_whitelist;
};
