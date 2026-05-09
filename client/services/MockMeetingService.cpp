#include "services/MockMeetingService.h"

#include <QDebug>

MockMeetingService::MockMeetingService(QObject *parent)
    : MeetingService(parent)
{
}

bool MockMeetingService::joinMeeting(const QString &meetingId, const QString &userId)
{
    m_lastError.clear();
    if (m_joined) {
        emit meetingStateChanged(true, "Already in meeting");
        return true;
    }

    m_joined = true;
    qInfo() << "[MockMeetingService] joinMeeting:" << meetingId << userId;
    emit meetingStateChanged(true, QString("Joined room %1 as %2").arg(meetingId, userId));
    return true;
}

void MockMeetingService::leaveMeeting()
{
    m_lastError.clear();
    if (!m_joined) {
        emit meetingStateChanged(false, "Not in a meeting");
        return;
    }

    m_joined = false;
    qInfo() << "[MockMeetingService] leaveMeeting";
    emit meetingStateChanged(false, "Left meeting");
}
