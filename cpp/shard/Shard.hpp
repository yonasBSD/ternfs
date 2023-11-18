#pragma once

#include "Msgs.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"

struct IpPort {
    uint32_t ip;
    uint16_t port;

    IpPort(uint32_t ip_, uint16_t port_) :
        ip(ip_), port(port_) {}
};

struct ShardOptions {
    LogLevel logLevel = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    // The second will be used if the ip is non-null
    std::array<IpPort, 2> ipPorts = {IpPort(0, 0), IpPort(0, 0)};
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulateIncomingPacketDrop = 0.0;
    double simulateOutgoingPacketDrop = 0.0;
    bool syslog = false;
    bool xmon = false;
    bool xmonProd = false;
    bool metrics = false;
    Duration transientDeadlineInterval = DEFAULT_DEADLINE_INTERVAL;
};

[[noreturn]] void runShard(ShardId shid, const std::string& dbDir, const ShardOptions& options);
