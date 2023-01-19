#pragma once

#include "Msgs.hpp"
#include "Env.hpp"

struct ShardOptions {
    bool waitForBlockServices = false;
    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    uint16_t port = 0; // automatically assigned, stored in shuckle
    std::string shuckleHost = "localhost";
    uint16_t shucklePort = 39999;
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulateIncomingPacketDrop = 0.0;
    double simulateOutgoingPacketDrop = 0.0;
};

void runShard(ShardId shid, const std::string& dbDir, const ShardOptions& options);
