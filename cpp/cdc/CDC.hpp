#pragma once

#include "CommonOptions.hpp"

struct CDCOptions {
    LogOptions logOptions;
    XmonOptions xmonOptions;
    MetricsOptions metricsOptions;
    RegistryClientOptions registryClientOptions;
    LogsDBOptions logsDBOptions;
    ServerOptions serverOptions;

    Duration shardTimeout = 100_ms;
    AddrsInfo cdcToShardAddress = {};
};

void runCDC(CDCOptions& options);
