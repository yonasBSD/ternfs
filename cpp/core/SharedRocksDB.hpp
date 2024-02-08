#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <rocksdb/db.h>
#include <rocksdb/options.h>

#include "Env.hpp"

class SharedRocksDB {
public:
    SharedRocksDB(Logger& logger, std::shared_ptr<XmonAgent>& xmon);
    ~SharedRocksDB();
    void open(rocksdb::Options options, const std::string& path);
    void close();

    void registerCFDescriptors(const std::vector<rocksdb::ColumnFamilyDescriptor>& _cfDescriptors);
    rocksdb::ColumnFamilyHandle* getCF(const std::string& name) const;

    rocksdb::DB* db() const;
    void rocksDBMetrics(std::unordered_map<std::string, uint64_t>& stats);
    void dumpRocksDBStatistics();

private:
    Env _env;
    std::unique_ptr<rocksdb::DB, void(*)(rocksdb::DB*)> _db;
    std::shared_ptr<rocksdb::Statistics> _dbStatistics;
    std::string _dbStatisticsFile;
    std::vector<rocksdb::ColumnFamilyDescriptor> _cfDescriptors;
    std::unordered_map<std::string, rocksdb::ColumnFamilyHandle*> _cfs;
};
