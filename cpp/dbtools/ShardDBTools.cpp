#include "ShardDBTools.hpp"

#include <memory>
#include <rocksdb/slice.h>

#include "Env.hpp"
#include "LogsDB.hpp"
#include "ShardDB.hpp"
#include "SharedRocksDB.hpp"

namespace rocksdb {
std::ostream& operator<<(std::ostream& out, const rocksdb::Slice& slice) {
    return goLangBytesFmt(out, (const char*)slice.data(), slice.size());
}
}


void ShardDBTools::verifyEqual(const std::string &db1Path, const std::string &db2Path) {
    Logger logger(LogLevel::LOG_INFO, STDERR_FILENO, false, false);
    std::shared_ptr<XmonAgent> xmon;
    Env env(logger, xmon, "ShardDBTools");
    SharedRocksDB sharedDb1(logger, xmon);
    sharedDb1.registerCFDescriptors(ShardDB::getColumnFamilyDescriptors());
    sharedDb1.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    rocksdb::Options rocksDBOptions;
    rocksDBOptions.compression = rocksdb::kLZ4Compression;
    rocksDBOptions.bottommost_compression = rocksdb::kZSTD;
    sharedDb1.openForReadOnly(rocksDBOptions, db1Path);
    SharedRocksDB sharedDb2(logger, xmon);
    sharedDb2.registerCFDescriptors(ShardDB::getColumnFamilyDescriptors());
    sharedDb2.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    sharedDb2.openForReadOnly(rocksDBOptions, db2Path);
    auto db1 = sharedDb1.db();
    auto db2 = sharedDb2.db();

    for(const auto& cf : ShardDB::getColumnFamilyDescriptors()) {
        LOG_INFO(env, "Starting comparison on CF %s", cf.name);
        auto cf1 = sharedDb1.getCF(cf.name);
        auto cf2 = sharedDb2.getCF(cf.name);
        size_t keysCompared = 0;

        auto it1 = db1->NewIterator({}, cf1);
        auto it2 = db2->NewIterator({}, cf2);
        it1->SeekToFirst();
        it2->SeekToFirst();
        while (it1->Valid() && it2->Valid()) {
            if (it1->key() != it2->key()) {
                LOG_ERROR(env, "Found mismatch in key cf %s, key1: %s, key2: %s", cf.name, it1->key(), it2->key());
                return;
            }
            if (it1->value() != it2->value()) {
                LOG_ERROR(env, "Found mismatch in value cf %s", cf.name);
                return;
            }
            it1->Next();
            it2->Next();
            ++keysCompared;
            if (keysCompared % 1000000 == 0) {
                LOG_INFO(env, "Compared %s key/value pairs so far and they are identical", keysCompared);
            }
        }
        if (it1->Valid()) {
            LOG_ERROR(env, "Database %s has extra keys in cf %s", db1Path, cf.name);
            return;
        }
        if (it2->Valid()) {
            LOG_ERROR(env, "Database %s has extra keys in cf %s", db2Path, cf.name);
            return;
        }
        LOG_INFO(env, "CF %s identical. Compared %s key/value pairs", cf.name, keysCompared);
        delete it1;
        delete it2;
    }
    LOG_INFO(env, "Databases identical!");
}
