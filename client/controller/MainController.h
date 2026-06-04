#pragma once

#include <QObject>
#include <QImage>
#include <QStringList>

#include "services/UserService.h"

class VideoSource;
class AIProcessor;
class CameraPermissionService;
class MeetingService;
class NetworkService;
class UserService;
class MainWindow;

class MainController : public QObject
{
    Q_OBJECT
public:
    MainController(VideoSource *videoSource,
                   CameraPermissionService *cameraPermissionService,
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
                                const QStringList &whitelistProfileKeys,
                                bool arcfaceEnabled,
                                bool adversarialPerturbationEnabled);
    void onFaceProfileEnrollRequested(const QString &label, const QImage &image);
    void onRefreshFaceProfilesRequested();
    void onBackToLoginRequested();
    void onLeaveClicked();
    void onCameraToggled(bool enabled);
    void onMicrophoneToggled(bool enabled);
    void onAIToggled(bool enabled);
    void onProtectionLevelChanged(const QString &level);
    void onEnrollFacesRequested(const QString &labelPrefix);
    void onMeetingWhitelistChanged(const QStringList &selectedProfileKeys);
    void onRawFrameReady(const QImage &frame);
    void onProcessedFrameReady(const QImage &frame);
    void onMeetingStateChanged(bool joined, const QString &message);
    void onCameraPermissionResolved(bool granted, const QString &message);

private:
    void startCameraIfAllowed();
    void updateMeetingStatus(const QString &message) const;
    void refreshFaceProfiles(const QString &statusMessage = QString());
    static QImage imageFromBase64(const QString &imageBase64);

    VideoSource *m_videoSource;
    CameraPermissionService *m_cameraPermissionService;
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
    QList<FaceProfileSummary> m_availableFaceProfiles;
    QList<FaceProfileSummary> m_meetingFaceProfiles;
    bool m_arcfaceEnabled = false;
    bool m_perturbationEnabled = false;
    bool m_pendingSelfEnroll = false;
    bool m_hasProcessedAiFrame = false;
};
