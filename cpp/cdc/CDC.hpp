#pragma once

#include "Env.hpp"
#include "Shard.hpp"
#include "Time.hpp"
#include <cstdint>

struct CDCOptions {
    LogLevel logLevel = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    uint16_t port = 0; // chosen randomly and recorded in shuckle
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    // The second will be used if the ip is non-null
    std::array<IpPort, 2> ipPorts = {IpPort(0, 0), IpPort(0, 0)};
    bool syslog = false;
    Duration shardTimeout = 100_ms;
    bool xmon = false;
    bool xmonProd = false;
    bool metrics = false;
    ReplicaId replicaId = 0;
    ReplicaId leaderReplicaId = 0;
};

void runCDC(const std::string& dbDir, const CDCOptions& options);
