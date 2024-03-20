#pragma once

#include <vector>

#include "Msgs.hpp"
#include "SharedRocksDB.hpp"


class LogsDBTools {
public:
    static void getUnreleasedLogEntries(Env& env, SharedRocksDB& sharedDB, LogIdx& lastReleasedOut, std::vector<LogIdx>& unreleasedLogEntriesOut);
};
