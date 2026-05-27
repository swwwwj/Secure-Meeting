#include "services/CameraPermissionService.h"

#include <QMetaObject>

#import <AVFoundation/AVFoundation.h>

CameraPermissionService::CameraPermissionService(QObject *parent)
    : QObject(parent)
{
}

CameraPermissionService::AccessState CameraPermissionService::ensureCameraAccess(QString *message)
{
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    switch (status) {
    case AVAuthorizationStatusAuthorized:
        if (message) {
            *message = QStringLiteral("macOS 已授予摄像头权限。");
        }
        return AccessState::Granted;
    case AVAuthorizationStatusNotDetermined:
        if (message) {
            *message = QStringLiteral("macOS 正在请求摄像头权限，请在系统弹窗中选择允许。");
        }
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
            const QString resolvedMessage = granted
                ? QStringLiteral("macOS 已授予摄像头权限。")
                : QStringLiteral("macOS 拒绝了摄像头权限，请在系统设置中允许该应用访问摄像头。");
            QMetaObject::invokeMethod(
                this,
                [this, granted, resolvedMessage]() {
                    emit cameraAccessResolved(granted, resolvedMessage);
                },
                Qt::QueuedConnection);
        }];
        return AccessState::Pending;
    case AVAuthorizationStatusDenied:
        if (message) {
            *message = QStringLiteral(
                "macOS 未授予摄像头权限，请在“系统设置 -> 隐私与安全性 -> 摄像头”中允许 SecureMeetingClient 访问摄像头。");
        }
        return AccessState::Denied;
    case AVAuthorizationStatusRestricted:
        if (message) {
            *message = QStringLiteral("macOS 限制了摄像头访问，当前系统策略不允许该应用使用摄像头。");
        }
        return AccessState::Denied;
    }

    if (message) {
        *message = QStringLiteral("无法确定 macOS 摄像头权限状态。");
    }
    return AccessState::Denied;
}

void CameraPermissionService::onPermissionRequestFinished()
{
    // The native AVFoundation callback emits cameraAccessResolved directly.
}
