#pragma once

#include "services/MeetingService.h"

class MockMeetingService : public MeetingService
{
    Q_OBJECT
public:
    explicit MockMeetingService(QObject *parent = nullptr);

    bool joinMeeting(const QString &meetingId, const QString &userId) override;
    void leaveMeeting() override;

private:
    bool m_joined = false;
    QString m_lastError;

public:
    QString lastError() const override { return m_lastError; }
};
