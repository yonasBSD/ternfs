#include "ShardDBTools.hpp"

#include <optional>
#include <memory>
#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <vector>

#include "Common.hpp"
#include "Env.hpp"
#include "LogsDB.hpp"
#include "LogsDBTools.hpp"
#include "Msgs.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDB.hpp"
#include "SharedRocksDB.hpp"
#include "ShardDBData.hpp"

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

    size_t totalKeysCompared{0};
    size_t totalKeySize{0};
    size_t totalValueSize{0};

    for(const auto& cf : ShardDB::getColumnFamilyDescriptors()) {
        LOG_INFO(env, "Starting comparison on CF %s", cf.name);
        auto cf1 = sharedDb1.getCF(cf.name);
        auto cf2 = sharedDb2.getCF(cf.name);

        size_t keysCompared{0};
        size_t keySize{0};
        size_t valueSize{0};

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
            ++keysCompared;
            keySize += it1->key().size();
            valueSize += it1->value().size();
            if (keysCompared % 100000000ull == 0) {
                LOG_INFO(env, "Compared %s key/value pairs so far and they are identical", keysCompared);
            }

            it1->Next();
            it2->Next();
        }
        if (it1->Valid()) {
            LOG_ERROR(env, "Database %s has extra keys in cf %s", db1Path, cf.name);
            return;
        }
        if (it2->Valid()) {
            LOG_ERROR(env, "Database %s has extra keys in cf %s", db2Path, cf.name);
            return;
        }
        delete it1;
        delete it2;

        LOG_INFO(env, "CF %s identical. Compared %s key/value pairs. KeySize: %s, ValueSize: %s", cf.name, keysCompared, keySize, valueSize);
        totalKeysCompared += keysCompared;
        totalKeySize += keySize;
        totalValueSize += valueSize;
    }
    LOG_INFO(env, "Databases identical! Compared %s cfs, %s key/value pairs. Total key size: %s, Total value size: %s", ShardDB::getColumnFamilyDescriptors().size(), totalKeysCompared, totalKeySize, totalValueSize);
}

std::ostream& operator<<(std::ostream& out, const std::vector<LogIdx>& indices) {
    out << "[";
    for (auto idx : indices) {
        out << idx << ", ";
    }
    out << "]";
    return out;
}

void ShardDBTools::outputUnreleasedState(const std::string& dbPath) {
    Logger logger(LogLevel::LOG_INFO, STDERR_FILENO, false, false);
    std::shared_ptr<XmonAgent> xmon;
    Env env(logger, xmon, "ShardDBTools");
    SharedRocksDB sharedDb(logger, xmon);
    sharedDb.registerCFDescriptors(ShardDB::getColumnFamilyDescriptors());
    sharedDb.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    rocksdb::Options rocksDBOptions;
    rocksDBOptions.compression = rocksdb::kLZ4Compression;
    rocksDBOptions.bottommost_compression = rocksdb::kZSTD;
    sharedDb.openForReadOnly(rocksDBOptions, dbPath);
    LogIdx lastReleased;
    std::vector<LogIdx> unreleasedLogEntries;

    LogsDBTools::getUnreleasedLogEntries(env, sharedDb, lastReleased, unreleasedLogEntries);

    LOG_INFO(env, "Last released: %s", lastReleased);
    LOG_INFO(env, "Unreleased entries: %s", unreleasedLogEntries);
}

void ShardDBTools::fsck(const std::string& dbPath) {
    Logger logger(LogLevel::LOG_INFO, STDERR_FILENO, false, false);
    std::shared_ptr<XmonAgent> xmon;
    Env env(logger, xmon, "ShardDBTools");
    SharedRocksDB sharedDb(logger, xmon);
    sharedDb.registerCFDescriptors(ShardDB::getColumnFamilyDescriptors());
    sharedDb.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    rocksdb::Options rocksDBOptions;
    rocksDBOptions.compression = rocksdb::kLZ4Compression;
    rocksDBOptions.bottommost_compression = rocksdb::kZSTD;
    sharedDb.openForReadOnly(rocksDBOptions, dbPath);
    auto db = sharedDb.db();

    const rocksdb::Snapshot* snapshotPtr = db->GetSnapshot();
    ALWAYS_ASSERT(snapshotPtr != nullptr);
    const auto snapshotDeleter = [db](const rocksdb::Snapshot* ptr) { db->ReleaseSnapshot(ptr); };
    std::unique_ptr<const rocksdb::Snapshot, decltype(snapshotDeleter)> snapshot(snapshotPtr, snapshotDeleter);

    auto directoriesCf = sharedDb.getCF("directories");
    auto edgesCf = sharedDb.getCF("edges");
    auto filesCf = sharedDb.getCF("files");
    auto spansCf = sharedDb.getCF("spans");
    auto transientFilesCf = sharedDb.getCF("transientFiles");
    auto blockServicesToFilesCf = sharedDb.getCF("blockServicesToFiles");

    rocksdb::ReadOptions options;
    options.snapshot = snapshot.get();

    LOG_INFO(env, "This will take a while and will take 10s of gigabytes of RAM.");

    {
        LOG_INFO(env, "Directories pass");
        uint64_t analyzedDirs = 0;
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, directoriesCf));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            analyzedDirs++;
            auto dirK = ExternalValue<InodeIdKey>::FromSlice(it->key());
            if (dirK().id().type() != InodeType::DIRECTORY) {
                LOG_ERROR(env, "Found key with bad directory inode %s", dirK().id());
                continue;
            }
            auto _ = ExternalValue<DirectoryBody>::FromSlice(it->value()); // just to check that this works
        }
        ROCKS_DB_CHECKED(it->status());
        LOG_INFO(env, "Analyzed %s directories", analyzedDirs);
    }

    // no duplicate ownership of any kind in edges -- currently broken in shard 0
    // because of the outage which lead to the fork
    bool checkEdgeOrdering = dbPath.find("000") == std::string::npos;

    if (!checkEdgeOrdering) {
        LOG_INFO(env, "Will not check edge ordering since we think this is shard 0");
    }

    {
        LOG_INFO(env, "Edges pass");
        const rocksdb::ReadOptions options;
        std::unordered_set<InodeId> ownedInodes;
        auto dummyCurrentDirectory = InodeId::FromU64Unchecked(1ull << 63);
        auto thisDir = dummyCurrentDirectory;
        bool thisDirHasCurrentEdges = false;
        EggsTime thisDirMaxTime = 0;
        std::string thisDirMaxTimeEdge;
        // the last edge for a given name, in a given directory
        std::unordered_map<std::string, std::pair<StaticValue<EdgeKey>, StaticValue<SnapshotEdgeBody>>> thisDirLastEdges;
        uint64_t analyzedEdges = 0;
        uint64_t analyzedDirectories = 0;
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, edgesCf));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            analyzedEdges++;
            auto edgeK = ExternalValue<EdgeKey>::FromSlice(it->key());
            if (edgeK().dirId().type() != InodeType::DIRECTORY) {
                LOG_ERROR(env, "Found edge with bad inode id %s", edgeK().dirId());
                continue;
            }
            if (edgeK().dirId() != thisDir) {
                analyzedDirectories++;
                if (thisDir != dummyCurrentDirectory) {
                    auto dirIdK = InodeIdKey::Static(thisDir);
                    std::string dirValue;
                    auto status = db->Get(options, directoriesCf, dirIdK.toSlice(), &dirValue);
                    if (status.IsNotFound()) {
                        // the directory must exist
                        LOG_ERROR(env, "Directory %s is referenced in edges, but it does not exist.", thisDir);
                    } else {
                        ROCKS_DB_CHECKED(status);
                        auto dir = ExternalValue<DirectoryBody>(dirValue);
                        // the mtime must be greater or equal than all edges
                        if (dir().mtime() < thisDirMaxTime) {
                            LOG_ERROR(env, "Directory %s has time %s, but max edge with name %s has greater time %s", thisDir, dir().mtime(), thisDirMaxTimeEdge, thisDirMaxTime);
                        }
                        // if we have current edges, the directory can't be snapshot
                        if (thisDirHasCurrentEdges && thisDir != ROOT_DIR_INODE_ID && dir().ownerId() == NULL_INODE_ID) {
                            LOG_ERROR(env, "Directory %s has current edges, but is snaphsot (no owner)", thisDir);
                        }
                    }
                }
                thisDir = edgeK().dirId();
                thisDirLastEdges.clear();
                thisDirHasCurrentEdges = false;
                thisDirMaxTime = 0;
            }
            std::string name(edgeK().name().data(), edgeK().name().size());
            InodeId ownedTargetId = NULL_INODE_ID;
            EggsTime creationTime;
            std::optional<ExternalValue<CurrentEdgeBody>> currentEdge;
            std::optional<ExternalValue<SnapshotEdgeBody>> snapshotEdge;
            if (edgeK().current()) {
                currentEdge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                ownedTargetId = (*currentEdge)().targetId();
                creationTime = (*currentEdge)().creationTime();
                thisDirHasCurrentEdges = true;
            } else {
                snapshotEdge = ExternalValue<SnapshotEdgeBody>::FromSlice(it->value());
                creationTime = edgeK().creationTime();
                if ((*snapshotEdge)().targetIdWithOwned().extra()) {
                    ownedTargetId = (*snapshotEdge)().targetIdWithOwned().id();
                }
            }
            if (ownedTargetId != NULL_INODE_ID) {
                if (ownedInodes.contains(ownedTargetId)) {
                    LOG_ERROR(env, "Inode %s is owned twice!", ownedTargetId);
                }
                ownedInodes.emplace(ownedTargetId);
            }
            if (creationTime > thisDirMaxTime) {
                thisDirMaxTime = creationTime;
                thisDirMaxTimeEdge = name;
            }
            // No intra directory dangling pointers
            if (ownedTargetId != NULL_INODE_ID && ownedTargetId.shard() == thisDir.shard()) {
                auto idK = InodeIdKey::Static(ownedTargetId);
                if (ownedTargetId.type() == InodeType::DIRECTORY) {
                    std::string tmp;
                    auto status = db->Get(options, directoriesCf, idK.toSlice(), &tmp);
                    if (status.IsNotFound()) {
                        LOG_ERROR(env, "Directory %s is referenced in directory %s but I could not find it.", ownedTargetId, edgeK().dirId());
                    } else {
                        ROCKS_DB_CHECKED(status);
                    }
                } else {
                    std::string tmp;
                    auto status = db->Get(options, filesCf, idK.toSlice(), &tmp);
                    if (status.IsNotFound()) {
                        LOG_ERROR(env, "File %s is referenced in directory %s but I could not find it.", ownedTargetId, edgeK().dirId());
                    } else {
                        ROCKS_DB_CHECKED(status);
                    }
                }
            }
            {
                auto prevEdge = thisDirLastEdges.find(name);
                if (prevEdge != thisDirLastEdges.end()) {
                    EggsTime prevCreationTime = prevEdge->second.first().creationTime();
                    // The edge must be newer than every non-current edge before it, with the exception of deletion edges
                    // (when we override the deletion edge)
                    if (
                        (prevCreationTime > creationTime) ||
                        (prevCreationTime == creationTime && !(snapshotEdge && (*snapshotEdge)().targetIdWithOwned().id() == NULL_INODE_ID))
                    ) {
                        if (checkEdgeOrdering) {
                            LOG_ERROR(env, "Name %s in directory %s has overlapping edges (%s >= %s).", name, edgeK().dirId(), prevCreationTime, creationTime);
                        }
                    }
                    // Deletion edges are not preceded by another deletion edge
                    if (snapshotEdge && (*snapshotEdge)().targetIdWithOwned().id() == NULL_INODE_ID && prevEdge->second.second().targetIdWithOwned().id() == NULL_INODE_ID) {
                        if (checkEdgeOrdering) {
                            LOG_ERROR(env, "Deletion edge with name %s creation time %s is preceded by another deletion edge in directory %s", name, edgeK().creationTime(), thisDir);
                        }
                    }
                } else {
                    // Deletion edges are preceded by something not -- this is actually not enforced
                    // by the schema, but by GC. So it's not a huge deal if it's wrong.
                    if (snapshotEdge && (*snapshotEdge)().targetIdWithOwned().id() == NULL_INODE_ID) {
                        if (checkEdgeOrdering) {
                            LOG_ERROR(env, "Deletion edge with name %s creation time %s is not preceded by anything in directory %s", name, edgeK().creationTime(), thisDir);
                        }
                    }
                }
            }
            // Add last edge (only snapshot edges can have predecessors)
            if (snapshotEdge) {
                StaticValue<EdgeKey> staticEdgeK(edgeK());
                StaticValue<SnapshotEdgeBody> staticEdgeV((*snapshotEdge)());
                thisDirLastEdges[name] = {staticEdgeK, staticEdgeV};
            }
        }
        ROCKS_DB_CHECKED(it->status());
        LOG_INFO(env, "Analyzed %s edges in %s directories", analyzedEdges, analyzedDirectories);
    }

    struct HashBlockServiceInodeId {
        std::size_t operator () (const std::pair<BlockServiceId, InodeId>& k) const {
            return std::hash<uint64_t>{}(k.first.u64) ^ std::hash<uint64_t>{}(k.second.u64);
        }
    };

    std::unordered_map<std::pair<BlockServiceId, InodeId>, int64_t, HashBlockServiceInodeId> blockServicesToFiles;
    std::unordered_map<InodeId, uint64_t> filesWithSpans; // file to expected size
    {
        LOG_INFO(env, "Spans pass");
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, spansCf));
        InodeId thisFile = NULL_INODE_ID;
        uint64_t expectedNextOffset = 0;
        uint64_t analyzedSpans = 0;
        uint64_t analyzedFiles = 0;
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            analyzedSpans++;
            auto spanK = ExternalValue<SpanKey>::FromSlice(it->key());
            if (spanK().fileId().type() != InodeType::FILE && spanK().fileId().type() != InodeType::SYMLINK) {
                LOG_ERROR(env, "Found span with bad file id %s", spanK().fileId());
                continue;
            }
            if (spanK().fileId() != thisFile) {
                analyzedFiles++;
                expectedNextOffset = 0;
                thisFile = spanK().fileId();
            }
            // check that file exists (either transient or non)
            {
                auto idK = InodeIdKey::Static(thisFile);
                std::string tmp;
                auto status = db->Get(options, filesCf, idK.toSlice(), &tmp);
                if (status.IsNotFound()) {
                    auto status = db->Get(options, transientFilesCf, idK.toSlice(), &tmp);
                    if (status.IsNotFound()) {
                        LOG_ERROR(env, "We have a span for non-esistant file %s", thisFile);
                    } else {
                        ROCKS_DB_CHECKED(status);
                    }
                } else {
                    ROCKS_DB_CHECKED(status);
                }
            }
            // Record blocks
            auto spanV = ExternalValue<SpanBody>::FromSlice(it->value());
            filesWithSpans[spanK().fileId()] = spanK().offset() + spanV().spanSize();
            if (spanK().offset() != expectedNextOffset) {
                LOG_ERROR(env, "Expected offset %s, got %s in span for file %s", expectedNextOffset, spanK().offset(), thisFile);
            }
            expectedNextOffset = spanK().offset() + spanV().spanSize();
            if (spanV().storageClass() == EMPTY_STORAGE) {
                LOG_ERROR(env, "File %s has empty storage span at offset %s", thisFile, spanK().offset());
            } else if (spanV().storageClass() != INLINE_STORAGE) {
                const auto& spanBlock = spanV().blocksBody();
                for (int i = 0; i < spanBlock.parity().blocks(); i++) {
                    auto block = spanBlock.block(i);
                    blockServicesToFiles[std::pair<BlockServiceId, InodeId>(block.blockService(), thisFile)] += 1;
                }
            }
        }
        ROCKS_DB_CHECKED(it->status());
        LOG_INFO(env, "Analyzed %s spans in %s files", analyzedSpans, analyzedFiles);
    }

    const auto filesPass = [&env, db, &options, &filesWithSpans]<typename T>(rocksdb::ColumnFamilyHandle* cf, const std::string& what) {
        uint64_t analyzedFiles = 0;
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, cf));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            analyzedFiles++;
            auto fileK = ExternalValue<InodeIdKey>::FromSlice(it->key());
            InodeId fileId = fileK().id();
            if (fileId.type() != InodeType::FILE && fileId.type() != InodeType::SYMLINK) {
                LOG_ERROR(env, "Found %s with bad file id %s", what, fileId);
                continue;
            }
            auto fileV = ExternalValue<T>::FromSlice(it->value());
            if (fileV().fileSize() == 0) { continue; } // nothing in this
            auto spansSize = filesWithSpans.find(fileId);
            if (spansSize == filesWithSpans.end()) {
                LOG_ERROR(env, "Could not find spans for non-zero-sized file %s", fileId);
            } else if (spansSize->second != fileV().fileSize()) {
                LOG_ERROR(env, "Spans tell us file %s should be of size %s, but it actually has size %s", fileId, spansSize->second, fileV().fileSize());
            }
        }
        ROCKS_DB_CHECKED(it->status());
        LOG_INFO(env, "Analyzed %s %s", analyzedFiles, what);
    };

    LOG_INFO(env, "Files pass");
    filesPass.template operator()<FileBody>(filesCf, "files");

    LOG_INFO(env, "Transient files pass");
    filesPass.template operator()<TransientFileBody>(transientFilesCf, "transient files");

    LOG_INFO(env, "Block services to files pass");
    {
        uint64_t foundItems = 0;
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, blockServicesToFilesCf));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            auto k = ExternalValue<BlockServiceToFileKey>::FromSlice(it->key());
            if (k().fileId().type() != InodeType::FILE && k().fileId().type() != InodeType::SYMLINK) {
                LOG_ERROR(env, "Found block service to files with bad file id %s", k().fileId());
                continue;
            }
            auto v = ExternalValue<I64Value>::FromSlice(it->value());
            if (v().i64() != 0) { foundItems++; }
            auto expected = blockServicesToFiles.find(std::pair<BlockServiceId, InodeId>(k().blockServiceId(), k().fileId()));
            if (expected == blockServicesToFiles.end() && v().i64() != 0) {
                LOG_ERROR(env, "Found spurious block services to file entry for block service %s, file %s, with count %s", k().blockServiceId(), k().fileId(), v().i64());
            }
            if (expected != blockServicesToFiles.end() && v().i64() != expected->second) {
                LOG_ERROR(env, "Found wrong block services to file entry for block service %s, file %s, expected count %s, got %s", k().blockServiceId(), k().fileId(), expected->second, v().i64());
            }
        }
        ROCKS_DB_CHECKED(it->status());
        if (foundItems != blockServicesToFiles.size()) {
            LOG_ERROR(env, "Expected %s block services to files, got %s", blockServicesToFiles.size(), foundItems);
        }
        LOG_INFO(env, "Analyzed %s block service to files entries", foundItems);
    }
}


void ShardDBTools::fixupBadInodes(const std::string& dbPath) {
    ALWAYS_ASSERT(dbPath.find("000") != std::string::npos, "You should only run this on shard 000!");

    Logger logger(LogLevel::LOG_INFO, STDERR_FILENO, false, false);
    std::shared_ptr<XmonAgent> xmon;
    Env env(logger, xmon, "ShardDBTools");
    SharedRocksDB sharedDb(logger, xmon);
    sharedDb.registerCFDescriptors(ShardDB::getColumnFamilyDescriptors());
    sharedDb.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    sharedDb.registerCFDescriptors(BlockServicesCacheDB::getColumnFamilyDescriptors());
    rocksdb::Options rocksDBOptions;
    rocksDBOptions.compression = rocksdb::kLZ4Compression;
    rocksDBOptions.bottommost_compression = rocksdb::kZSTD;
    sharedDb.open(rocksDBOptions, dbPath);
    auto db = sharedDb.db();

    auto directoriesCf = sharedDb.getCF("directories");
    auto filesCf = sharedDb.getCF("files");
    auto spansCf = sharedDb.getCF("spans");

    auto zeroKey = InodeIdKey::Static(NULL_INODE_ID);
    ROCKS_DB_CHECKED(db->Delete({}, directoriesCf, zeroKey.toSlice()));
    ROCKS_DB_CHECKED(db->Delete({}, filesCf, zeroKey.toSlice()));
    {
        const rocksdb::ReadOptions options;
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options, spansCf));
        it->SeekToFirst();
        ALWAYS_ASSERT(it->Valid());
        auto spanK = ExternalValue<SpanKey>::FromSlice(it->key());
        if (spanK().fileId() == NULL_INODE_ID) {
            ROCKS_DB_CHECKED(db->Delete({}, spansCf, spanK.toSlice()));
        } else {
            LOG_INFO(env, "could not find bad edge id");
        }
    }
}