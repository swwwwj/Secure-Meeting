#include "services/MockNetworkService.h"

bool MockNetworkService::sendFrame(const QByteArray &payload)
{
    return !payload.isEmpty();
}

QByteArray MockNetworkService::receiveFrame()
{
    return {};
}
