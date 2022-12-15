#pragma once

#include "Env.hpp"

struct CDCOptions {
    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
};

void runCDC(const std::string& dbDir, const CDCOptions& options);