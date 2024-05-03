#pragma once

#include "Msgs.hpp"
#include "Env.hpp"
#include "MsgsGen.hpp"
#include "ShardDB.hpp"

struct ShardOptions {
    LogLevel logLevel = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    // The second will be used if the port is non-null
    AddrsInfo shardAddrs;
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulateIncomingPacketDrop = 0.0;
    double simulateOutgoingPacketDrop = 0.0;
    bool syslog = false;
    bool xmon = false;
    bool xmonProd = false;
    bool metrics = false;
    bool shuckleStats = false;
    Duration transientDeadlineInterval = DEFAULT_DEADLINE_INTERVAL;

    // LogsDB settings
    bool dontDoReplication = false;
    bool forceLeader = false;
    LogIdx forcedLastReleased = 0;
};

void runShard(ShardReplicaId shrid, const std::string& dbDir, ShardOptions& options);
