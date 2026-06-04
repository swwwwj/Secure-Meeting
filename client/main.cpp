#include "controller/MainController.h"
#include "config/AppConfig.h"
#include "services/HttpAIProcessor.h"
#include "services/CameraPermissionService.h"
#include "services/HttpMeetingService.h"
#include "services/HttpUserService.h"
#include "services/MockMeetingService.h"
#include "services/MockNetworkService.h"
#include "services/MockUserService.h"
#include "ui/MainWindow.h"
#include "ui/MeetingWindow.h"
#include "ui/LoginWindow.h"
#include "ui/RegisterWindow.h"
#include "video/CameraVideoSource.h"
#include "video/MockVideoSource.h"

#include <QApplication>
#include <QJsonDocument>
#include <memory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const AppConfig cfg = AppConfig::load();
    CameraVideoSource cameraSource;
    MockVideoSource mockSource;
    CameraPermissionService cameraPermissionService;
    VideoSource *videoSource = cameraSource.isAvailable()
        ? static_cast<VideoSource *>(&cameraSource)
        : static_cast<VideoSource *>(&mockSource);

    HttpAIProcessor aiProcessor(cfg.aiEndpoint,
                                 cfg.aiTimeoutMs,
                                 cfg.maxInFlightRequests,
                                 cfg.modelVersion,
                                 cfg.policyVersion,
                                 cfg.aiMinFrameIntervalMs,
                                 cfg.aiTransportMaxEdge,
                                 cfg.aiTransportJpegQuality);
    aiProcessor.setEnabled(cfg.aiEnabledByDefault);
    std::unique_ptr<UserService> userService;
    std::unique_ptr<MeetingService> meetingService;
    if (cfg.useMockServices) {
        userService = std::make_unique<MockUserService>();
        meetingService = std::make_unique<MockMeetingService>();
    } else {
        userService = std::make_unique<HttpUserService>(cfg.meetingServerEndpoint, cfg.meetingTimeoutMs);
        meetingService = std::make_unique<HttpMeetingService>(cfg.meetingServerEndpoint,
                                                              cfg.meetingTimeoutMs,
                                                              userService.get());
    }
    MockNetworkService networkService;

    const QString smokeFlag = qEnvironmentVariable("SM_CLIENT_SMOKE_TEST");
    const bool smokeTest = (smokeFlag == "1" || smokeFlag.compare("true", Qt::CaseInsensitive) == 0);
    if (smokeTest) {
        const QString user = qEnvironmentVariable("SM_CLIENT_TEST_USER", "smoke_user");
        const QString pass = qEnvironmentVariable("SM_CLIENT_TEST_PASSWORD", "smoke_pass_123");
        const QString room = qEnvironmentVariable("SM_CLIENT_TEST_ROOM", "smoke-room");

        bool regOk = userService->registerUser(user, pass);
        if (!regOk && !userService->lastError().contains("exists", Qt::CaseInsensitive)) {
            qCritical().noquote() << "client_smoke register failed:" << userService->lastError();
            return 2;
        }
        if (!userService->login(user, pass)) {
            qCritical().noquote() << "client_smoke login failed:" << userService->lastError();
            return 3;
        }
        if (!meetingService->joinMeeting(room, user)) {
            qCritical().noquote() << "client_smoke join failed:" << meetingService->lastError();
            return 4;
        }
        meetingService->leaveMeeting();
        if (!userService->logout()) {
            qCritical().noquote() << "client_smoke logout failed:" << userService->lastError();
            return 5;
        }
        qInfo().noquote() << "client_smoke success";
        return 0;
    }

    QJsonObject startup{
        {"service", "client"},
        {"event", "startup"},
        {"env", cfg.env},
        {"runtime_mode", cfg.useMockServices ? "Mock" : "Real"},
        {"ai_endpoint", cfg.aiEndpoint.toString()},
        {"meeting_server_endpoint", cfg.meetingServerEndpoint.toString()},
        {"use_mock_services", cfg.useMockServices},
        {"ai_enabled_by_default", cfg.aiEnabledByDefault},
        {"ai_timeout_ms", cfg.aiTimeoutMs},
        {"meeting_timeout_ms", cfg.meetingTimeoutMs},
        {"max_in_flight_requests", cfg.maxInFlightRequests},
        {"model_version", cfg.modelVersion},
        {"policy_version", cfg.policyVersion}
    };
    qInfo().noquote() << QJsonDocument(startup).toJson(QJsonDocument::Compact);

    MainController controller(videoSource,
                              &cameraPermissionService,
                              &aiProcessor,
                              meetingService.get(),
                              &networkService,
                              userService.get());
    MainWindow window;
    window.meetingWindow()->setVideoPrivacyOptions(cfg.blurRadius,
                                                   cfg.useKalmanTracking,
                                                   cfg.aiProcessedFrameMaxAgeMs);
    window.loginWindow()->setRuntimeMode(cfg.useMockServices ? "Mock" : "Real");
    window.registerWindow()->setRuntimeMode(cfg.useMockServices ? "Mock" : "Real");
    controller.bindView(&window);
    window.show();

    return app.exec();
}
