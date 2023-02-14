#pragma once

#include "Msgs.hpp"
#include "Env.hpp"

struct ShardOptions {
    LogLevel logLevel = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    uint16_t port = 0; // automatically assigned, stored in shuckle
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    std::array<uint8_t, 4> ownIp = {0, 0, 0, 0};
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulateIncomingPacketDrop = 0.0;
    double simulateOutgoingPacketDrop = 0.0;
};

void runShard(ShardId shid, const std::string& dbDir, const ShardOptions& options);
