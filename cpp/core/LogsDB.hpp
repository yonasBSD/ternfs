#pragma once

#include <vector>
#include <rocksdb/db.h>

class LogsDB {
public:
    LogsDB() = delete;

    static std::vector<rocksdb::ColumnFamilyDescriptor> getColumnFamilyDescriptors();
};
