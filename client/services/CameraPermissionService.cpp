#include "services/CameraPermissionService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

namespace {
// #region debug-point B:permission-service
void postDebugEvent(const char *hypothesisId, const char *location, const QString &message, const QJsonObject &data)
{
    static QNetworkAccessManager *manager = nullptr;
    if (!manager) {
        manager = new QNetworkAccessManager(QCoreApplication::instance());
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:7777/event")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject payload{
        {"sessionId", "macos-camera-permission"},
        {"runId", "pre-fix"},
        {"hypothesisId", hypothesisId},
        {"location", location},
        {"msg", message},
        {"data", data},
        {"ts", QDateTime::currentMSecsSinceEpoch()}
    };
    manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}
// #endregion
}

CameraPermissionService::CameraPermissionService(QObject *parent)
    : QObject(parent)
{
}

CameraPermissionService::AccessState CameraPermissionService::ensureCameraAccess(QString *message)
{
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto *app = QCoreApplication::instance();
    if (!app) {
        // #region debug-point B:no-app-instance
        postDebugEvent("B",
                       "client/services/CameraPermissionService.cpp:no-app",
                       "[DEBUG] no app instance when ensuring camera access",
                       QJsonObject{});
        // #endregion
        if (message) {
            *message = QStringLiteral("应用尚未初始化，无法请求摄像头权限。");
        }
        return AccessState::Denied;
    }

    QCameraPermission permission;
    const Qt::PermissionStatus status = app->checkPermission(permission);
    // #region debug-point B:permission-status
    postDebugEvent("B",
                   "client/services/CameraPermissionService.cpp:check",
                   "[DEBUG] camera permission status checked",
                   QJsonObject{
                       {"status", static_cast<int>(status)},
                       {"app_file_path", QCoreApplication::applicationFilePath()},
                       {"app_dir_path", QCoreApplication::applicationDirPath()}
                   });
    // #endregion
    switch (status) {
    case Qt::PermissionStatus::Granted:
        return AccessState::Granted;
    case Qt::PermissionStatus::Denied:
        if (message) {
            *message = QStringLiteral(
                "macOS 未授予摄像头权限，请在“系统设置 -> 隐私与安全性 -> 摄像头”中允许当前应用或终端访问摄像头。");
        }
        return AccessState::Denied;
    case Qt::PermissionStatus::Undetermined:
        if (message) {
            *message = QStringLiteral("macOS 正在请求摄像头权限，请在系统弹窗中选择允许。");
        }
        // #region debug-point B:permission-requested
        postDebugEvent("B",
                       "client/services/CameraPermissionService.cpp:request",
                       "[DEBUG] camera permission request issued",
                       QJsonObject{});
        // #endregion
        app->requestPermission(permission, this, &CameraPermissionService::onPermissionRequestFinished);
        return AccessState::Pending;
    }
#endif

    Q_UNUSED(message);
    return AccessState::Granted;
}

void CameraPermissionService::onPermissionRequestFinished()
{
    QString message;
    const AccessState state = ensureCameraAccess(&message);
    // #region debug-point B:permission-finished
    postDebugEvent("B",
                   "client/services/CameraPermissionService.cpp:finished",
                   "[DEBUG] camera permission request finished",
                   QJsonObject{
                       {"access_state", static_cast<int>(state)},
                       {"message", message}
                   });
    // #endregion
    if (state == AccessState::Pending) {
        return;
    }
    emit cameraAccessResolved(state == AccessState::Granted, message);
}
