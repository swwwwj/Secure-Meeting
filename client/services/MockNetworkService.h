#pragma once

#include "services/NetworkService.h"

class MockNetworkService : public NetworkService
{
public:
    bool sendFrame(const QByteArray &payload) override;
    QByteArray receiveFrame() override;
};
