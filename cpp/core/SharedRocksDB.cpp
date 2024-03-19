#include "SharedRocksDB.hpp"

#include <fstream>
#include <mutex>
#include <rocksdb/db.h>
#include <rocksdb/statistics.h>
#include <shared_mutex>
#include <utility>

#include "RocksDBUtils.hpp"

static void closeDB(rocksdb::DB* db) {
    ROCKS_DB_CHECKED(db->Close());
    delete db;
}

SharedRocksDB::SharedRocksDB(Logger& logger, std::shared_ptr<XmonAgent>& xmon)
    : _env(logger, xmon, "shared_rocksdb"), _db(nullptr, closeDB) {}

SharedRocksDB::~SharedRocksDB() {
    close();
}

void SharedRocksDB::open(rocksdb::Options options, const std::string& path) {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() == nullptr);
    ALWAYS_ASSERT(options.statistics.get() == nullptr);
    _dbStatistics = rocksdb::CreateDBStatistics();
    _dbStatisticsFile = path + "/db-statistics.txt";
    options.statistics = _dbStatistics;


    std::vector<rocksdb::ColumnFamilyHandle*> cfHandles;
    cfHandles.reserve(_cfDescriptors.size());
    rocksdb::DB* db;
    auto dbPath = path + "/db";
    LOG_INFO(_env, "Opening RocksDB in %s", dbPath);
    ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::Open(options, dbPath, _cfDescriptors, &cfHandles, &db),
            "could not open RocksDB %s", path
    );

    _db = std::unique_ptr<rocksdb::DB, void (*)(rocksdb::DB*)>(db,closeDB);
    ALWAYS_ASSERT(_cfDescriptors.size() == cfHandles.size());

    for (auto i = 0; i < _cfDescriptors.size(); ++i) {
        _cfs.insert(std::make_pair(_cfDescriptors[i].name, cfHandles[i]));
    }

    _cfDescriptors.clear();
}

void SharedRocksDB::openForReadOnly(rocksdb::Options options, const std::string& path) {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() == nullptr);
    options.create_if_missing = false;
    options.create_missing_column_families = false;

    std::vector<rocksdb::ColumnFamilyHandle*> cfHandles;
    cfHandles.reserve(_cfDescriptors.size());
    rocksdb::DB* db;
    LOG_INFO(_env, "Opening RocksDB as readonly in %s", path);
    ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::OpenForReadOnly(options, path, _cfDescriptors, &cfHandles, &db),
            "could not open RocksDB %s", path
    );

    _db = std::unique_ptr<rocksdb::DB, void (*)(rocksdb::DB*)>(db,closeDB);
    ALWAYS_ASSERT(_cfDescriptors.size() == cfHandles.size());

    for (auto i = 0; i < _cfDescriptors.size(); ++i) {
        _cfs.insert(std::make_pair(_cfDescriptors[i].name, cfHandles[i]));
    }

    _cfDescriptors.clear();
}

void SharedRocksDB::close() {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    if (_db.get() == nullptr) {
        return;
    }
    LOG_INFO(_env, "Destroying column families and closing database");
    for( auto& cf : _cfs ) {
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(cf.second));
    }
    _cfs.clear();
    _db.reset(nullptr);
    LOG_INFO(_env, "database closed");
}

void SharedRocksDB::registerCFDescriptors(const std::vector<rocksdb::ColumnFamilyDescriptor>& cfDescriptors) {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() == nullptr);
    _cfDescriptors.insert(_cfDescriptors.end(), cfDescriptors.begin(), cfDescriptors.end());
}

rocksdb::ColumnFamilyHandle* SharedRocksDB::getCF(const std::string& name) const {
    std::shared_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() != nullptr);
    auto it = _cfs.find(name);
    if (it == _cfs.end()) {
        return nullptr;
    }
    return it->second;
}

void SharedRocksDB::deleteCF(const std::string& name) {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() != nullptr);
    auto it = _cfs.find(name);
    if (it == _cfs.end()) {
        return;
    }
    ROCKS_DB_CHECKED(_db->DropColumnFamily(it->second));
    ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(it->second));
    _cfs.erase(it);
}

rocksdb::ColumnFamilyHandle* SharedRocksDB::createCF(const rocksdb::ColumnFamilyDescriptor& descriptor) {
    std::unique_lock<std::shared_mutex> _(_stateMutex);
    ASSERT(_db.get() != nullptr);
    auto it = _cfs.find(descriptor.name);
    if (it != _cfs.end()) {
        return it->second;
    }
    rocksdb::ColumnFamilyHandle* handle;
    ROCKS_DB_CHECKED(_db->CreateColumnFamily(descriptor.options, descriptor.name, &handle));
    _cfs.emplace(descriptor.name, handle);
    return handle;
}

rocksdb::DB* SharedRocksDB::db() const {
    std::shared_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() != nullptr);
    return _db.get();
}

void SharedRocksDB::rocksDBMetrics(std::unordered_map<std::string, uint64_t>& stats) {
    std::shared_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() != nullptr);
    ::rocksDBMetrics(_env, _db.get(), *_dbStatistics, stats);
}

void SharedRocksDB::dumpRocksDBStatistics() {
    std::shared_lock<std::shared_mutex> _(_stateMutex);
    ALWAYS_ASSERT(_db.get() != nullptr);
    LOG_INFO(_env, "Dumping statistics to %s", _dbStatisticsFile);
    std::ofstream file(_dbStatisticsFile);
    file << _dbStatistics->ToString();
}
