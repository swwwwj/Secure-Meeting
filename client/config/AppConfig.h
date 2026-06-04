#pragma once

#include <QUrl>
#include <QString>

struct AppConfig
{
    QString env = "dev";
    QUrl aiEndpoint = QUrl("http://127.0.0.1:8000/api/v1/process_frame");
    QUrl meetingServerEndpoint = QUrl("http://127.0.0.1:8100");
    bool aiEnabledByDefault = false;
    bool useMockServices = false;
    int aiTimeoutMs = 2500;
    int meetingTimeoutMs = 1500;
    int maxInFlightRequests = 2;
    int aiMinFrameIntervalMs = 250;
    int aiTransportMaxEdge = 480;
    int aiTransportJpegQuality = 75;
    int aiProcessedFrameMaxAgeMs = 150;
    int blurRadius = 15;
    bool useKalmanTracking = true;
    int trackingFallbackThresholdMs = 200;
    QString modelVersion = "demo-model-v1";
    QString policyVersion = "policy-default-v1";

    static AppConfig load();
};
