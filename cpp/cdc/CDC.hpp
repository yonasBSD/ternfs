#pragma once

#include "Env.hpp"
#include "Msgs.hpp"
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
    AddrsInfo cdcAddrs = {};
    AddrsInfo cdcToShardAddress = {};
    bool syslog = false;
    Duration shardTimeout = 100_ms;
    bool xmon = false;
    bool xmonProd = false;
    bool metrics = false;
    ReplicaId replicaId;
    uint8_t location;

    std::string dbDir;


    // LogsDB settings
    bool avoidBeingLeader = true;
    bool noReplication = false;
};

void runCDC(CDCOptions& options);
