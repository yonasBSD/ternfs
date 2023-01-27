#pragma once

#include "Env.hpp"

struct CDCOptions {
    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    uint16_t port = 0; // chosen randomly and recorded in shuckle
    std::string shuckleHost = "";
    uint16_t shucklePort = 0;
    std::array<uint8_t, 4> ownIp = {0, 0, 0, 0};
};

void runCDC(const std::string& dbDir, const CDCOptions& options);