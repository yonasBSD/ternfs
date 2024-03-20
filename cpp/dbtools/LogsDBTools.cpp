#include "LogsDBTools.hpp"

#include <rocksdb/slice.h>

#include "Env.hpp"
#include "LogsDB.hpp"
#include "SharedRocksDB.hpp"

void LogsDBTools::getUnreleasedLogEntries(Env& env, SharedRocksDB& sharedDB, LogIdx& lastReleasedOut, std::vector<LogIdx>& unreleasedLogEntriesOut) {
    LogsDB::_getUnreleasedLogEntries(env, sharedDB, lastReleasedOut, unreleasedLogEntriesOut);
}
