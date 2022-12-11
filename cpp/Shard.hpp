#pragma once

#include <atomic>

#include "Msgs.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"
#include "Undertaker.hpp"

struct ShardOptions {
    bool waitForShuckle = false;
    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile = ""; // if empty, stdout
    std::string shuckleHost = "http://localhost:39999";
};

void runShard(ShardId shid, const std::string& dbDir, const ShardOptions& options);
