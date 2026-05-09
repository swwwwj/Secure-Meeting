#pragma once

#include <QByteArray>

// Network abstraction placeholder for future RTC or transport layer.
class NetworkService
{
public:
    virtual ~NetworkService() = default;
    virtual bool sendFrame(const QByteArray &payload) = 0;
    virtual QByteArray receiveFrame() = 0;
};
