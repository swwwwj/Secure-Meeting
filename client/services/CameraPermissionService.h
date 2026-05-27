#pragma once

#include <QObject>
#include <QString>

class CameraPermissionService : public QObject
{
    Q_OBJECT
public:
    enum class AccessState {
        Granted,
        Pending,
        Denied
    };
    Q_ENUM(AccessState)

    explicit CameraPermissionService(QObject *parent = nullptr);

    AccessState ensureCameraAccess(QString *message = nullptr);

signals:
    void cameraAccessResolved(bool granted, const QString &message);

private:
    void onPermissionRequestFinished();
};
