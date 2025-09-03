#pragma once

#include <cstdint>
#include <optional>

#include "Env.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "ShardDB.hpp"
#include "Metrics.hpp"

struct ShardOptions {
    ShardReplicaId shrid;
    uint8_t location = 0;
    std::string dbDir;

    uint16_t port;
    LogLevel logLevel = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    // The second will be used if the port is non-null
    AddrsInfo shardAddrs;
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulateOutgoingPacketDrop = 0.0;
    bool syslog = false;
    std::string xmonAddr;
    std::optional<InfluxDB> influxDB;
    Duration transientDeadlineInterval = DEFAULT_DEADLINE_INTERVAL;

    // LogsDB settings
    bool avoidBeingLeader = true;
    bool noReplication = false;

    // implicit options
    bool isLeader() const { return !avoidBeingLeader; }
    bool isProxyLocation() const { return location != 0; }
};

void runShard(ShardOptions& options);
