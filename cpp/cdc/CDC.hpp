#pragma once

#include "Env.hpp"
#include "Shard.hpp"
#include "Time.hpp"

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
    // currently the maximum CDC throughput is roughly 200req/s, and the max timeout is 2sec,
    // so storing a maximum of 5sec worth of requests seems like a decent compromise.
    uint64_t maximumEnqueuedRequests = 1000;
    bool metrics = false;
};

void runCDC(const std::string& dbDir, const CDCOptions& options);