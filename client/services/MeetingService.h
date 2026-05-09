#pragma once

#include <QObject>
#include <QString>

// Abstract meeting control interface.
// Later this can be backed by WebRTC signaling and room management.
class MeetingService : public QObject
{
    Q_OBJECT
public:
    explicit MeetingService(QObject *parent = nullptr) : QObject(parent) {}
    ~MeetingService() override = default;

    virtual bool joinMeeting(const QString &meetingId, const QString &userId) = 0;
    virtual void leaveMeeting() = 0;
    virtual QString lastError() const = 0;

signals:
    void meetingStateChanged(bool joined, const QString &message);
};
