#include "LogsDB.hpp"

std::vector<rocksdb::ColumnFamilyDescriptor> LogsDB::getColumnFamilyDescriptors() {
    return {
        {"logMetadata",{}},
        {"logTimePartition0",{}},
        {"logTimePartition1",{}},
    };
}
