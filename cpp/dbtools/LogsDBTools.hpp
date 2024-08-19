#pragma once

#include <vector>

#include "LogsDB.hpp"
#include "Msgs.hpp"
#include "SharedRocksDB.hpp"


class LogsDBTools {
public:
    static void getUnreleasedLogEntries(Env& env, SharedRocksDB& sharedDB, LogIdx& lastReleasedOut, std::vector<LogIdx>& unreleasedLogEntriesOut);
    static void getLogEntries(Env& env, SharedRocksDB& sharedDB, LogIdx start, size_t count, std::vector<LogsDBLogEntry>& logEntriesOut);
};
