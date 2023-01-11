#pragma once

#include "Env.hpp"

struct CDCOptions {
    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    // If non-zero, packets will be dropped with this probability. Useful to test
    // resilience of the system.
    double simulatePacketDrop = 0.0;
};

void runCDC(const std::string& dbDir, const CDCOptions& options);