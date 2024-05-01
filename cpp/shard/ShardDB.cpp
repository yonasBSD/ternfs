#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/snapshot.h>
#include <rocksdb/statistics.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/table.h>
#include <system_error>
#include <xxhash.h>
#include <type_traits>
#include <fstream>
#include <memory>

#include "Bincode.hpp"
#include "Common.hpp"
#include "Crypto.hpp"
#include "Env.hpp"
#include "Msgs.hpp"
#include "ShardDB.hpp"
#include "ShardDBData.hpp"
#include "Assert.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "SharedRocksDB.hpp"
#include "Time.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDBData.hpp"
#include "AssertiveLock.hpp"
#include "XmonAgent.hpp"
#include "crc32c.h"
#include "wyhash.h"
#include "BlockServicesCacheDB.hpp"

// TODO maybe revisit all those ALWAYS_ASSERTs

// ## High level design
//
// Right now, we're not implementing distributed consensus, but we want to prepare
// for this as best we can, since we need it for HA.
//
// This class implements the state storage, assuming that log entries will be persisted
// separatedly (if at all). The state storage is also used to prepare the log entries --
// e.g. there's a tiny bit of state that we read/write to without regards for the
// distributed consensus.
//
// Specifically, we allocate fresh ids, and we read info about block services, when
// preparing log entries.
//
// One question is how we write transactions concurrently, given that we rely on
// applying first one log entry and then the next in that order. We could be smart
// and pool independent log entries, and then write them concurrently. My guess is
// that it's probably not worth doing this, at least since the beginning, and that
// it's better to just serialize the transactions manually with a mutex here in C++.
//
// ## RocksDB size
//
// See docs/eggsfs-rocksdb-size.xlsx for some computation. Please update that document
// if you update the schema. The per-shard size should be ~3.5TB at capacity. This computation
// tracks decently with what we currently witness. Most of the space is taken by the edges
// (expected) and by the block service to files mapping (not so great, but I think required).
// I think 3.5TB dataset, with maximum single column family dataset being 1.5TB or so, is
// acceptable.
//
// ## RocksDB test (state)
//
// We want to test the two heavy cases: spans and edges.
//
// For spans, we have 9 bytes keys, and 200 bytes values. 10EiB / 10MiB / 256 = ~5 billion keys. Total data,
// around 1TiB.
//
// I test with
//
//     $ WAL_DIR=benchmarkdb DB_DIR=benchmarkdb KEY_SIZE=9 VALUE_SIZE=200 NUM_KEYS=5000000000 CACHE_SIZE=6442450944 ./benchmark.sh bulkload
//
// This runs like so:
//
//     /usr/bin/time -f '%e %U %S' -o /tmp/benchmark_bulkload_fillrandom.log.time ./db_bench --benchmarks=fillrandom --use_existing_db=0 --disable_auto_compactions=1 --sync=0 --max_background_jobs=16 --max_write_buffer_number=8 --allow_concurrent_memtable_write=false --level0_file_num_compaction_trigger=10485760 --level0_slowdown_writes_trigger=10485760 --level0_stop_writes_trigger=10485760 --db=benchmarkdb --wal_dir=benchmarkdb --num=5000000000 --key_size=9 --value_size=200 --block_size=8192 --cache_size=6442450944 --cache_numshardbits=6 --compression_max_dict_bytes=0 --compression_ratio=0.5 --compression_type=zstd --bytes_per_sync=8388608 --benchmark_write_rate_limit=0 --write_buffer_size=134217728 --target_file_size_base=134217728 --max_bytes_for_level_base=1073741824 --verify_checksum=1 --delete_obsolete_files_period_micros=62914560 --max_bytes_for_level_multiplier=8 --statistics=0 --stats_per_interval=1 --stats_interval_seconds=60 --report_interval_seconds=5 --histogram=1 --memtablerep=skip_list --bloom_bits=10 --open_files=-1 --subcompactions=1 --compaction_style=0 --num_levels=8 --min_level_to_compress=-1 --level_compaction_dynamic_level_bytes=true --pin_l0_filter_and_index_blocks_in_cache=1 --threads=1 --memtablerep=vector --allow_concurrent_memtable_write=false --disable_wal=1 --seed=1669566020 --report_file=/tmp/benchmark_bulkload_fillrandom.log.r.csv 2>&1 | tee -a /tmp/benchmark_bulkload_fillrandom.log
//
// This runs in 3:51:09.16, which is 4 times the time it takes to load ~500GiB. The
// vast majority of the time is taken by compaction.
//
// Next I did
//
//     WAL_DIR=benchmarkdb DB_DIR=benchmarkdb KEY_SIZE=9 VALUE_SIZE=200 NUM_KEYS=5000000000 CACHE_SIZE=6442450944 DURATION=5400 ./benchmark.sh readrandom
//
// TODO fill in results

static constexpr uint64_t EGGSFS_PAGE_SIZE = 4096;

static uint64_t computeHash(HashMode mode, const BincodeBytesRef& bytes) {
    switch (mode) {
    case HashMode::XXH3_63:
        return XXH3_64bits(bytes.data(), bytes.size()) & ~(1ull<<63);
    default:
        throw EGGS_EXCEPTION("bad hash mode %s", (int)mode);
    }
}

static bool validName(const BincodeBytesRef& name) {
    if (name.size() == 0) {
        return false;
    }
    if (name == BincodeBytesRef(".") || name == BincodeBytesRef("..")) {
        return false;
    }
    for (int i = 0; i < name.size(); i++) {
        if (name.data()[i] == (uint8_t)'/' || name.data()[i] == 0) {
            return false;
        }
    }
    return true;
}

static int pickMtu(uint16_t mtu) {
    if (mtu == 0) { mtu = DEFAULT_UDP_MTU; }
    mtu = std::min<uint16_t>(MAX_UDP_MTU, mtu);
    return mtu;
}

void ShardLogEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(SHARD_LOG_PROTOCOL_VERSION);
    idx.pack(buf);
    time.pack(buf);
    buf.packScalar<uint16_t>((uint16_t)body.kind());
    body.pack(buf);
}

void ShardLogEntry::unpack(BincodeBuf& buf) {
    uint32_t protocol = buf.unpackScalar<uint32_t>();
    ALWAYS_ASSERT(protocol == SHARD_LOG_PROTOCOL_VERSION);
    idx.unpack(buf);
    time.unpack(buf);
    ShardLogEntryKind kind = (ShardLogEntryKind)buf.unpackScalar<uint16_t>();
    body.unpack(buf, kind);
}

static char maxNameChars[255];
__attribute__((constructor))
static void fillMaxNameChars() {
    memset(maxNameChars, CHAR_MAX, 255);
}
static BincodeBytes maxName(maxNameChars, 255);

std::vector<rocksdb::ColumnFamilyDescriptor> ShardDB::getColumnFamilyDescriptors() {
    // TODO actually figure out the best strategy for each family, including the default
    // one.
    rocksdb::ColumnFamilyOptions blockServicesToFilesOptions;
    blockServicesToFilesOptions.merge_operator = CreateInt64AddOperator();
    return std::vector<rocksdb::ColumnFamilyDescriptor> {
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"files", {}},
            {"spans", {}},
            {"transientFiles", {}},
            {"directories", {}},
            {"edges", {}},
            {"blockServicesToFiles", blockServicesToFilesOptions},
    };
}

struct ShardDBImpl {
    Env _env;

    ShardId _shid;
    Duration _transientDeadlineInterval;
    std::array<uint8_t, 16> _secretKey;
    AES128Key _expandedSecretKey;

    // TODO it would be good to store basically all of the metadata in memory,
    // so that we'd just read from it, but this requires a bit of care when writing
    // since we rollback on error.

    rocksdb::DB* _db;
    rocksdb::ColumnFamilyHandle* _defaultCf;
    rocksdb::ColumnFamilyHandle* _transientCf;
    rocksdb::ColumnFamilyHandle* _filesCf;
    rocksdb::ColumnFamilyHandle* _spansCf;
    rocksdb::ColumnFamilyHandle* _directoriesCf;
    rocksdb::ColumnFamilyHandle* _edgesCf;
    rocksdb::ColumnFamilyHandle* _blockServicesToFilesCf;

    AssertiveLock _applyLogEntryLock;

    std::shared_ptr<const rocksdb::Snapshot> _currentReadSnapshot;

    const BlockServicesCacheDB& _blockServicesCache;

    // ----------------------------------------------------------------
    // initialization

    ShardDBImpl() = delete;

    ShardDBImpl(
        Logger& logger,
        std::shared_ptr<XmonAgent>& xmon,
        ShardId shid,
        Duration deadlineInterval,
        const SharedRocksDB& sharedDB,
        const BlockServicesCacheDB& blockServicesCache
    ) :
        _env(logger, xmon, "shard_db"),
        _shid(shid),
        _transientDeadlineInterval(deadlineInterval),
        _db(sharedDB.db()),
        _defaultCf(sharedDB.getCF(rocksdb::kDefaultColumnFamilyName)),
        _transientCf(sharedDB.getCF("transientFiles")),
        _filesCf(sharedDB.getCF("files")),
        _spansCf(sharedDB.getCF("spans")),
        _directoriesCf(sharedDB.getCF("directories")),
        _edgesCf(sharedDB.getCF("edges")),
        _blockServicesToFilesCf(sharedDB.getCF("blockServicesToFiles")),
        _blockServicesCache(blockServicesCache)
    {
        LOG_INFO(_env, "initializing shard %s RocksDB", _shid);
        _initDb();
        _updateCurrentReadSnapshot();
    }

    void close() {
        LOG_INFO(_env, "destroying read snapshot");
        _currentReadSnapshot.reset();
    }

    void _initDb() {
        {
            bool shardInfoExists;
            {
                std::string value;
                auto status = _db->Get({}, shardMetadataKey(&SHARD_INFO_KEY), &value);
                if (status.IsNotFound()) {
                    shardInfoExists = false;
                } else {
                    ROCKS_DB_CHECKED(status);
                    shardInfoExists = true;
                    auto shardInfo = ExternalValue<ShardInfoBody>::FromSlice(value);
                    if (shardInfo().shardId() != _shid) {
                        throw EGGS_EXCEPTION("expected shard id %s, but found %s in DB", _shid, shardInfo().shardId());
                    }
                    _secretKey = shardInfo().secretKey();
                }
            }
            if (!shardInfoExists) {
                LOG_INFO(_env, "creating shard info, since it does not exist");
                generateSecretKey(_secretKey);
                StaticValue<ShardInfoBody> shardInfo;
                shardInfo().setShardId(_shid);
                shardInfo().setSecretKey(_secretKey);
                ROCKS_DB_CHECKED(_db->Put({}, shardMetadataKey(&SHARD_INFO_KEY), shardInfo.toSlice()));
            }
            expandKey(_secretKey, _expandedSecretKey);
        }

        const auto keyExists = [this](rocksdb::ColumnFamilyHandle* cf, const rocksdb::Slice& key) -> bool {
            std::string value;
            auto status = _db->Get({}, cf, key, &value);
            if (status.IsNotFound()) {
                return false;
            } else {
                ROCKS_DB_CHECKED(status);
                return true;
            }
        };

        if (_shid == ROOT_DIR_INODE_ID.shard()) {
            auto k = InodeIdKey::Static(ROOT_DIR_INODE_ID);
            if (!keyExists(_directoriesCf, k.toSlice())) {
                LOG_INFO(_env, "creating root directory, since it does not exist");
                DirectoryInfo info = defaultDirectoryInfo();
                OwnedValue<DirectoryBody> dirBody(info);
                dirBody().setVersion(0);
                dirBody().setOwnerId(NULL_INODE_ID);
                dirBody().setMtime({});
                dirBody().setHashMode(HashMode::XXH3_63);
                auto k = InodeIdKey::Static(ROOT_DIR_INODE_ID);
                ROCKS_DB_CHECKED(_db->Put({}, _directoriesCf, k.toSlice(), dirBody.toSlice()));
            }
        }

        if (!keyExists(_defaultCf, shardMetadataKey(&NEXT_FILE_ID_KEY))) {
            LOG_INFO(_env, "initializing next file id");
            InodeId nextFileId(InodeType::FILE, ShardId(_shid), 0);
            auto v = InodeIdValue::Static(nextFileId);
            ROCKS_DB_CHECKED(_db->Put({}, _defaultCf, shardMetadataKey(&NEXT_FILE_ID_KEY), v.toSlice()));
        }
        if (!keyExists(_defaultCf, shardMetadataKey(&NEXT_SYMLINK_ID_KEY))) {
            LOG_INFO(_env, "initializing next symlink id");
            InodeId nextLinkId(InodeType::SYMLINK, ShardId(_shid), 0);
            auto v = InodeIdValue::Static(nextLinkId);
            ROCKS_DB_CHECKED(_db->Put({}, _defaultCf, shardMetadataKey(&NEXT_SYMLINK_ID_KEY), v.toSlice()));
        }
        if (!keyExists(_defaultCf, shardMetadataKey(&NEXT_BLOCK_ID_KEY))) {
            LOG_INFO(_env, "initializing next block id");
            StaticValue<U64Value> v;
            v().setU64(_shid.u8);
            ROCKS_DB_CHECKED(_db->Put({}, _defaultCf, shardMetadataKey(&NEXT_BLOCK_ID_KEY), v.toSlice()));
        }

        if (!keyExists(_defaultCf, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY))) {
            LOG_INFO(_env, "initializing last applied log entry");
            auto v = U64Value::Static(0);
            ROCKS_DB_CHECKED(_db->Put({}, _defaultCf, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), v.toSlice()));
        }
    }

    // ----------------------------------------------------------------
    // read-only path

    EggsError _statFile(const rocksdb::ReadOptions& options, const StatFileReq& req, StatFileResp& resp) {
        std::string fileValue;
        ExternalValue<FileBody> file;
        EggsError err = _getFile(options, req.id, fileValue, file);
        if (err != NO_ERROR) {
            return err;
        }
        resp.mtime = file().mtime();
        resp.atime = file().atime();
        resp.size = file().fileSize();
        return NO_ERROR;
    }

    EggsError _statTransientFile(const rocksdb::ReadOptions& options, const StatTransientFileReq& req, StatTransientFileResp& resp) {
        std::string fileValue;
        {
            auto k = InodeIdKey::Static(req.id);
            auto status = _db->Get(options, _transientCf, k.toSlice(), &fileValue);
            if (status.IsNotFound()) {
                return EggsError::FILE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        auto body = ExternalValue<TransientFileBody>::FromSlice(fileValue);
        resp.mtime = body().mtime();
        resp.size = body().fileSize();
        resp.note = body().note();
        return NO_ERROR;
    }

    EggsError _statDirectory(const rocksdb::ReadOptions& options, const StatDirectoryReq& req, StatDirectoryResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        // allowSnapshot=true, the caller can very easily detect if it's snapshot or not
        EggsError err = _getDirectory(options, req.id, true, dirValue, dir);
        if (err != NO_ERROR) {
            return err;
        }
        resp.mtime = dir().mtime();
        resp.owner = dir().ownerId();
        dir().info(resp.info);
        return NO_ERROR;
    }

    EggsError _readDir(rocksdb::ReadOptions& options, const ReadDirReq& req, ReadDirResp& resp) {
        // we don't want snapshot directories, so check for that early
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            EggsError err = _getDirectory(options, req.dirId, false, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // stop early when we go out of the directory (will avoid tombstones
        // of freshly cleared dirs)
        StaticValue<EdgeKey> upperBound;
        upperBound().setDirIdWithCurrent(InodeId::FromU64(req.dirId.u64 + 1), false);
        upperBound().setNameHash(0);
        upperBound().setName({});
        auto upperBoundSlice = upperBound.toSlice();
        options.iterate_upper_bound = &upperBoundSlice;

        {
            auto it = std::unique_ptr<rocksdb::Iterator>(_db->NewIterator(options, _edgesCf));
            StaticValue<EdgeKey> beginKey;
            beginKey().setDirIdWithCurrent(req.dirId, true); // current = true
            beginKey().setNameHash(req.startHash);
            beginKey().setName({});
            int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - ReadDirResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
                ALWAYS_ASSERT(key().dirId() == req.dirId && key().current());
                auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                CurrentEdge& respEdge = resp.results.els.emplace_back();
                respEdge.targetId = edge().targetIdWithLocked().id();
                respEdge.nameHash = key().nameHash();
                respEdge.name = key().name();
                respEdge.creationTime = edge().creationTime();
                budget -= (int)respEdge.packedSize();
                if (budget < 0) {
                    resp.nextHash = key().nameHash();
                    // remove the current element, and also, do not have straddling hashes
                    while (!resp.results.els.empty() && resp.results.els.back().nameHash == key().nameHash()) {
                        resp.results.els.pop_back();
                    }
                    break;
                }
            }
            ROCKS_DB_CHECKED(it->status());
        }

        return NO_ERROR;
    }

    // returns whether we're done
    template<template<typename> typename V>
    bool _fullReadDirAdd(
        const FullReadDirReq& req,
        FullReadDirResp& resp,
        int& budget,
        const V<EdgeKey>& key,
        const rocksdb::Slice& edgeValue
    ) {
        auto& respEdge = resp.results.els.emplace_back();
        EggsTime time;
        if (key().current()) {
            auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(edgeValue);
            respEdge.current = key().current();
            respEdge.targetId = edge().targetIdWithLocked();
            respEdge.nameHash = key().nameHash();
            respEdge.name = key().name();
            respEdge.creationTime = edge().creationTime();
        } else {
            auto edge = ExternalValue<SnapshotEdgeBody>::FromSlice(edgeValue);
            respEdge.current = key().current();
            respEdge.targetId = edge().targetIdWithOwned();
            respEdge.nameHash = key().nameHash();
            respEdge.name = key().name();
            respEdge.creationTime = key().creationTime();
        }
        // static limit, terminate immediately to avoid additional seeks
        if (req.limit > 0 && resp.results.els.size() >= req.limit) {
            resp.next.clear(); // we're done looking up
            return true;
        }
        // mtu limit
        budget -= (int)respEdge.packedSize();
        if (budget < 0) {
            int prevCursorSize = FullReadDirCursor::STATIC_SIZE;
            while (budget < 0) {
                auto& last = resp.results.els.back();
                LOG_TRACE(_env, "fullReadDir: removing last %s", last);
                budget += (int)last.packedSize();
                resp.next.current = last.current;
                resp.next.startName = last.name;
                resp.next.startTime = last.current ? 0 : last.creationTime;
                resp.results.els.pop_back();
                budget += prevCursorSize;
                budget -= (int)resp.next.packedSize();
                prevCursorSize = resp.next.packedSize();
            }
            LOG_TRACE(_env, "fullReadDir: out of budget/limit");
            return true;
        }
        return false;
    }

    template<bool forwards>
    EggsError _fullReadDirSameName(const FullReadDirReq& req, rocksdb::ReadOptions& options, HashMode hashMode, FullReadDirResp& resp) {
        bool current = !!(req.flags&FULL_READ_DIR_CURRENT);

        uint64_t nameHash = computeHash(hashMode, req.startName.ref());

        int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - FullReadDirResp::STATIC_SIZE;

        // returns whether we're done
        const auto lookupCurrent = [&]() -> bool {
            StaticValue<EdgeKey> key;
            key().setDirIdWithCurrent(req.dirId, true);
            key().setNameHash(nameHash);
            key().setName(req.startName.ref());
            std::string value;
            auto status = _db->Get(options, _edgesCf, key.toSlice(), &value);
            if (status.IsNotFound()) { return false; }
            ROCKS_DB_CHECKED(status);
            ExternalValue<CurrentEdgeBody> edge(value);
            return _fullReadDirAdd(req, resp, budget, key, rocksdb::Slice(value));
        };

        // begin current
        if (current) { if (lookupCurrent()) { return NO_ERROR; } }

        // we looked at the current and we're going forward, nowhere to go from here.
        if (current && forwards) { return NO_ERROR; }

        // we're looking at snapshot edges now -- first pick the bounds (important to
        // minimize tripping over tombstones)
        StaticValue<EdgeKey> snapshotStart;
        snapshotStart().setDirIdWithCurrent(req.dirId, false);
        snapshotStart().setNameHash(nameHash);
        snapshotStart().setName(req.startName.ref());
        snapshotStart().setCreationTime(req.startTime.ns ? req.startTime : (forwards ? 0 : EggsTime(~(uint64_t)0)));
        StaticValue<EdgeKey> snapshotEnd;
        rocksdb::Slice snapshotEndSlice;
        snapshotEnd().setDirIdWithCurrent(req.dirId, false);
        // when going backwards, just have the beginning of this name list as the lower bound
        snapshotEnd().setNameHash(nameHash + forwards);
        snapshotEnd().setName({});
        snapshotEnd().setCreationTime(0);
        snapshotEndSlice = snapshotEnd.toSlice();
        (forwards ? options.iterate_upper_bound : options.iterate_lower_bound) = &snapshotEndSlice;

        // then start iterating
        auto it = std::unique_ptr<rocksdb::Iterator>(_db->NewIterator(options, _edgesCf));
        for (
            forwards ? it->Seek(snapshotStart.toSlice()) : it->SeekForPrev(snapshotStart.toSlice());
            it->Valid();
            forwards ? it->Next() : it->Prev()
        ) {
            auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
            ALWAYS_ASSERT(key().dirId() == req.dirId);
            if (key().name() != req.startName) {
                // note: we still need to check this since we only utilize the hash
                // for the bounds since we're a bit lazy, so there might be collisions
                break;
            }
            if (_fullReadDirAdd(req, resp, budget, key, it->value())) {
                break;
            }
        }
        ROCKS_DB_CHECKED(it->status());

        options.iterate_lower_bound = {};
        options.iterate_upper_bound = {};

        // we were looking at the snapshots and we're going backwards, nowhere to go from here.
        if (!forwards) { return NO_ERROR; }

        // end current
        if (lookupCurrent()) { return NO_ERROR; }

        return NO_ERROR;
    }

    template<bool forwards>
    EggsError _fullReadDirNormal(const FullReadDirReq& req, rocksdb::ReadOptions& options, HashMode hashMode, FullReadDirResp& resp) {
        // this case is simpler, we just traverse all of it forwards or backwards.
        bool current = !!(req.flags&FULL_READ_DIR_CURRENT);

        // setup bounds
        StaticValue<EdgeKey> endKey; // unchecked because it might stop being a directory
        endKey().setDirIdWithCurrent(InodeId::FromU64Unchecked(req.dirId.u64 + (forwards ? 1 : -1)), !forwards);
        endKey().setNameHash(forwards ? 0 : ~(uint64_t)0);
        endKey().setName(forwards ? BincodeBytes().ref() : maxName.ref());
        endKey().setCreationTime(forwards ? 0 : EggsTime(~(uint64_t)0));
        rocksdb::Slice endKeySlice = endKey.toSlice();
        if (forwards) {
            options.iterate_upper_bound = &endKeySlice;
        } else {
            options.iterate_lower_bound = &endKeySlice;
        }

        // iterate
        StaticValue<EdgeKey> startKey;
        startKey().setDirIdWithCurrent(req.dirId, current);
        startKey().setNameHash(
            req.startName.size() == 0 ?
                (forwards ? 0 : ~(uint64_t)0) :
                computeHash(hashMode, req.startName.ref())
        );
        startKey().setName(req.startName.ref());
        if (!current) {
            startKey().setCreationTime(req.startTime);
        }
        auto it = std::unique_ptr<rocksdb::Iterator>(_db->NewIterator(options, _edgesCf));
        int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - FullReadDirResp::STATIC_SIZE;
        for (
            forwards ? it->Seek(startKey.toSlice()) : it->SeekForPrev(startKey.toSlice());
            it->Valid();
            forwards ? it->Next() : it->Prev()
        ) {
            auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
            ALWAYS_ASSERT(key().dirId() == req.dirId);
            if (_fullReadDirAdd(req, resp, budget, key, it->value())) {
                break;
            }
        }
        ROCKS_DB_CHECKED(it->status());

        return NO_ERROR;
    }

    EggsError _fullReadDir(rocksdb::ReadOptions& options, const FullReadDirReq& req, FullReadDirResp& resp) {
        bool sameName = !!(req.flags&FULL_READ_DIR_SAME_NAME);
        bool current = !!(req.flags&FULL_READ_DIR_CURRENT);
        bool forwards = !(req.flags&FULL_READ_DIR_BACKWARDS);

        // TODO proper errors at validation
        ALWAYS_ASSERT(!(sameName && req.startName.packedSize() == 0));
        ALWAYS_ASSERT(!(current && req.startTime != 0));

        HashMode hashMode;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnaphsot=true, we're in fullReadDir
            EggsError err = _getDirectory(options, req.dirId, true, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            hashMode = dir().hashMode();
        }

        if (sameName) {
            if (forwards) {
                return _fullReadDirSameName<true>(req, options, hashMode, resp);
            } else {
                return _fullReadDirSameName<false>(req, options, hashMode, resp);
            }
        } else {
            if (forwards) {
                return _fullReadDirNormal<true>(req, options, hashMode, resp);
            } else {
                return _fullReadDirNormal<false>(req, options, hashMode, resp);
            }
        }
    }

    EggsError _lookup(const rocksdb::ReadOptions& options, const LookupReq& req, LookupResp& resp) {
        uint64_t nameHash;
        {
            EggsError err = _getDirectoryAndHash(options, req.dirId, false, req.name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        {
            StaticValue<EdgeKey> reqKey;
            reqKey().setDirIdWithCurrent(req.dirId, true); // current=true
            reqKey().setNameHash(nameHash);
            reqKey().setName(req.name.ref());
            std::string edgeValue;
            auto status = _db->Get(options, _edgesCf, reqKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::NAME_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
            ExternalValue<CurrentEdgeBody> edge(edgeValue);
            resp.creationTime = edge().creationTime();
            resp.targetId = edge().targetIdWithLocked().id();
        }

        return NO_ERROR;
    }

    EggsError _visitTransientFiles(const rocksdb::ReadOptions& options, const VisitTransientFilesReq& req, VisitTransientFilesResp& resp) {
        resp.nextId = NULL_INODE_ID;

        {
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _transientCf));
            auto beginKey = InodeIdKey::Static(req.beginId);
            int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - VisitTransientFilesResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                auto file = ExternalValue<TransientFileBody>::FromSlice(it->value());

                auto& respFile = resp.files.els.emplace_back();
                respFile.id = id().id();
                respFile.cookie.data = _calcCookie(respFile.id);
                respFile.deadlineTime = file().deadline();

                budget -= (int)respFile.packedSize();
                if (budget <= 0) {
                    resp.nextId = resp.files.els.back().id;
                    resp.files.els.pop_back();
                    break;
                }
            }
            ROCKS_DB_CHECKED(it->status());
        }

        return NO_ERROR;
    }

    template<typename Req, typename Resp>
    EggsError _visitInodes(const rocksdb::ReadOptions& options, rocksdb::ColumnFamilyHandle* cf, const Req& req, Resp& resp) {
        resp.nextId = NULL_INODE_ID;

        int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - Resp::STATIC_SIZE;
        int maxIds = (budget/8) + 1; // include next inode
        {
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, cf));
            auto beginKey = InodeIdKey::Static(req.beginId);
            for (
                it->Seek(beginKey.toSlice());
                it->Valid() && resp.ids.els.size() < maxIds;
                it->Next()
            ) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                resp.ids.els.emplace_back(id().id());
            }
            ROCKS_DB_CHECKED(it->status());
        }

        if (resp.ids.els.size() == maxIds) {
            resp.nextId = resp.ids.els.back();
            resp.ids.els.pop_back();
        }

        return NO_ERROR;
    }

    EggsError _visitDirectories(const rocksdb::ReadOptions& options, const VisitDirectoriesReq& req, VisitDirectoriesResp& resp) {
        return _visitInodes(options, _directoriesCf, req, resp);
    }

    EggsError _fileSpans(rocksdb::ReadOptions& options, const FileSpansReq& req, FileSpansResp& resp) {
        StaticValue<SpanKey> lowerKey;
        lowerKey().setFileId(InodeId::FromU64Unchecked(req.fileId.u64 - 1));
        lowerKey().setOffset(~(uint64_t)0);
        auto lowerKeySlice = lowerKey.toSlice();
        options.iterate_lower_bound = &lowerKeySlice;

        StaticValue<SpanKey> upperKey;
        upperKey().setFileId(InodeId::FromU64(req.fileId.u64 + 1));
        upperKey().setOffset(0);
        auto upperKeySlice = upperKey.toSlice();
        options.iterate_upper_bound = &upperKeySlice;

        auto inMemoryBlockServicesData = _blockServicesCache.getCache();

        int budget = pickMtu(req.mtu) - ShardResponseHeader::STATIC_SIZE - FileSpansResp::STATIC_SIZE;
        // if -1, we ran out of budget.
        const auto addBlockService = [&resp, &budget, &inMemoryBlockServicesData](BlockServiceId blockServiceId) -> int {
            // See if we've placed it already
            for (int i = 0; i < resp.blockServices.els.size(); i++) {
                if (resp.blockServices.els.at(i).id == blockServiceId) {
                    return i;
                }
            }
            // If not, we need to make space for it
            budget -= (int)BlockService::STATIC_SIZE;
            if (budget < 0) {
                return -1;
            }
            auto& blockService = resp.blockServices.els.emplace_back();
            const auto& cache = inMemoryBlockServicesData.blockServices.at(blockServiceId.u64);
            blockService.id = blockServiceId;
            blockService.ip1 = cache.ip1;
            blockService.port1 = cache.port1;
            blockService.ip2 = cache.ip2;
            blockService.port2 = cache.port2;
            blockService.flags = cache.flags;
            return resp.blockServices.els.size()-1;
        };

        StaticValue<SpanKey> beginKey;
        beginKey().setFileId(req.fileId);
        beginKey().setOffset(req.byteOffset);
        {
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _spansCf));
            for (
                it->SeekForPrev(beginKey.toSlice());
                it->Valid() && (req.limit == 0 || resp.spans.els.size() < req.limit);
                it->Next()
            ) {
                auto key = ExternalValue<SpanKey>::FromSlice(it->key());
                if (key().fileId() != req.fileId) {
                    break;
                }
                auto value = ExternalValue<SpanBody>::FromSlice(it->value());
                if (key().offset()+value().spanSize() < req.byteOffset) { // can only happens if the first cursor is out of bounds
                    LOG_DEBUG(_env, "exiting early from spans since current key starts at %s and ends at %s, which is less than offset %s", key().offset(), key().offset()+value().spanSize(), req.byteOffset);
                    break;
                }
                auto& respSpan = resp.spans.els.emplace_back();
                respSpan.header.byteOffset = key().offset();
                respSpan.header.size = value().spanSize();
                respSpan.header.crc = value().crc();
                if (value().storageClass() == INLINE_STORAGE) {
                    auto& respSpanInline = respSpan.setInlineSpan();
                    respSpanInline.body = value().inlineBody();
                } else {
                    const auto& spanBlock = value().blocksBody();
                    auto& respSpanBlock = respSpan.setBlocksSpan(value().storageClass());
                    respSpanBlock.parity = spanBlock.parity();
                    respSpanBlock.stripes = spanBlock.stripes();
                    respSpanBlock.cellSize = spanBlock.cellSize();
                    respSpanBlock.blocks.els.resize(spanBlock.parity().blocks());
                    for (int i = 0; i < spanBlock.parity().blocks(); i++) {
                        auto block = spanBlock.block(i);
                        int blockServiceIx = addBlockService(block.blockService());
                        if (blockServiceIx < 0) {
                            break; // no need to break in outer loop -- we will break out anyway because budget < 0
                        }
                        ALWAYS_ASSERT(blockServiceIx < 256);
                        auto& respBlock = respSpanBlock.blocks.els[i];
                        respBlock.blockId = block.blockId();
                        respBlock.blockServiceIx = blockServiceIx;
                        respBlock.crc = block.crc();
                    }
                    respSpanBlock.stripesCrc.els.resize(spanBlock.stripes());
                    for (int i = 0; i < spanBlock.stripes(); i++) {
                        respSpanBlock.stripesCrc.els[i] = spanBlock.stripeCrc(i);
                    }
                }
                budget -= (int)respSpan.packedSize();
                if (budget < 0) {
                    resp.nextOffset = respSpan.header.byteOffset;
                    resp.spans.els.pop_back();
                    break;
                }
            }
            ROCKS_DB_CHECKED(it->status());
        }

        // Check if file does not exist when we have no spans
        if (resp.spans.els.size() == 0) {
            std::string fileValue;
            ExternalValue<FileBody> file;
            EggsError err = _getFile(options, req.fileId, fileValue, file);
            if (err != NO_ERROR) {
                // might be a transient file, let's check
                bool isTransient = false;
                if (err == EggsError::FILE_NOT_FOUND) {
                    std::string transientFileValue;
                    ExternalValue<TransientFileBody> transientFile;
                    EggsError transError = _getTransientFile(options, 0, true, req.fileId, transientFileValue, transientFile);
                    if (transError == NO_ERROR) {
                        isTransient = true;
                    } else if (transError != EggsError::FILE_NOT_FOUND) {
                        LOG_INFO(_env, "Dropping error gotten when doing fallback transient lookup for id %s: %s", req.fileId, transError);
                    }
                }
                if (!isTransient) {
                    return err;
                }
            }
        }

        return NO_ERROR;
    }

    EggsError _blockServiceFiles(rocksdb::ReadOptions& options, const BlockServiceFilesReq& req, BlockServiceFilesResp& resp) {
        int maxFiles = (DEFAULT_UDP_MTU - ShardResponseHeader::STATIC_SIZE - BlockServiceFilesResp::STATIC_SIZE) / 8;
        resp.fileIds.els.reserve(maxFiles);

        StaticValue<BlockServiceToFileKey> beginKey;
        beginKey().setBlockServiceId(req.blockServiceId.u64);
        beginKey().setFileId(req.startFrom);
        StaticValue<BlockServiceToFileKey> endKey;
        endKey().setBlockServiceId(req.blockServiceId.u64+1);
        endKey().setFileId(NULL_INODE_ID);
        auto endKeySlice = endKey.toSlice();

        options.iterate_upper_bound = &endKeySlice;
        std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _blockServicesToFilesCf));
        for (
            it->Seek(beginKey.toSlice());
            it->Valid() && resp.fileIds.els.size() < maxFiles;
            it->Next()
        ) {
            auto key = ExternalValue<BlockServiceToFileKey>::FromSlice(it->key());
            int64_t blocks = ExternalValue<I64Value>::FromSlice(it->value())().i64();
            LOG_DEBUG(_env, "we have %s blocks in file %s for block service %s", blocks, key().fileId(), req.blockServiceId);
            ALWAYS_ASSERT(blocks >= 0);
            if (blocks == 0) { continue; } // this can happen when we migrate block services/remove spans
            resp.fileIds.els.emplace_back(key().fileId());
            break;
        }
        ROCKS_DB_CHECKED(it->status());
        return NO_ERROR;
    }

    EggsError _visitFiles(const rocksdb::ReadOptions& options, const VisitFilesReq& req, VisitFilesResp& resp) {
        return _visitInodes(options, _filesCf, req, resp);
    }

    EggsError read(const ShardReqContainer& req, ShardRespContainer& resp) {
        LOG_DEBUG(_env, "processing read-only request of kind %s", req.kind());

        EggsError err = NO_ERROR;
        resp.clear();

        auto snapshot = _getCurrentReadSnapshot();
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

        switch (req.kind()) {
        case ShardMessageKind::STAT_FILE:
            err = _statFile(options, req.getStatFile(), resp.setStatFile());
            break;
        case ShardMessageKind::READ_DIR:
            err = _readDir(options, req.getReadDir(), resp.setReadDir());
            break;
        case ShardMessageKind::STAT_DIRECTORY:
            err = _statDirectory(options, req.getStatDirectory(), resp.setStatDirectory());
            break;
        case ShardMessageKind::STAT_TRANSIENT_FILE:
            err = _statTransientFile(options, req.getStatTransientFile(), resp.setStatTransientFile());
            break;
        case ShardMessageKind::LOOKUP:
            err = _lookup(options, req.getLookup(), resp.setLookup());
            break;
        case ShardMessageKind::VISIT_TRANSIENT_FILES:
            err = _visitTransientFiles(options, req.getVisitTransientFiles(), resp.setVisitTransientFiles());
            break;
        case ShardMessageKind::FULL_READ_DIR:
            err = _fullReadDir(options, req.getFullReadDir(), resp.setFullReadDir());
            break;
        case ShardMessageKind::VISIT_DIRECTORIES:
            err = _visitDirectories(options, req.getVisitDirectories(), resp.setVisitDirectories());
            break;
        case ShardMessageKind::FILE_SPANS:
            err = _fileSpans(options, req.getFileSpans(), resp.setFileSpans());
            break;
        case ShardMessageKind::BLOCK_SERVICE_FILES:
            err = _blockServiceFiles(options, req.getBlockServiceFiles(), resp.setBlockServiceFiles());
            break;
        case ShardMessageKind::VISIT_FILES:
            err = _visitFiles(options, req.getVisitFiles(), resp.setVisitFiles());
            break;
        default:
            throw EGGS_EXCEPTION("bad read-only shard message kind %s", req.kind());
        }

        ALWAYS_ASSERT(req.kind() == resp.kind());

        return err;
    }


    // ----------------------------------------------------------------
    // log preparation

    EggsError _prepareConstructFile(EggsTime time, const ConstructFileReq& req, ConstructFileEntry& entry) {
        if (req.type != (uint8_t)InodeType::FILE && req.type != (uint8_t)InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        entry.type = req.type;
        entry.note = req.note;
        entry.deadlineTime = time + _transientDeadlineInterval;

        return NO_ERROR;
    }

    EggsError _checkTransientFileCookie(InodeId id, std::array<uint8_t, 8> cookie) {
        if (id.type() != InodeType::FILE && id.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        std::array<uint8_t, 8> expectedCookie;
        if (cookie != _calcCookie(id)) {
            return EggsError::BAD_COOKIE;
        }
        return NO_ERROR;
    }

    EggsError _prepareLinkFile(EggsTime time, const LinkFileReq& req, LinkFileEntry& entry) {
        // some early, preliminary checks
        if (req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.ownerId.shard() != _shid || req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
        if (err != NO_ERROR) {
            return err;
        }

        entry.fileId = req.fileId;
        entry.name = req.name;
        entry.ownerId = req.ownerId;

        return NO_ERROR;
    }

    EggsError _prepareSameDirectoryRename(EggsTime time, const SameDirectoryRenameReq& req, SameDirectoryRenameEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.oldName == req.newName) {
            return EggsError::SAME_SOURCE_AND_DESTINATION;
        }
        if (!validName(req.newName.ref())) {
            return EggsError::BAD_NAME;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.oldCreationTime = req.oldCreationTime;
        entry.oldName = req.oldName;
        entry.newName = req.newName;
        entry.targetId = req.targetId;
        return NO_ERROR;
    }

    EggsError _prepareSoftUnlinkFile(EggsTime time, const SoftUnlinkFileReq& req, SoftUnlinkFileEntry& entry) {
        if (req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.ownerId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.ownerId = req.ownerId;
        entry.fileId = req.fileId;
        entry.name = req.name;
        entry.creationTime = req.creationTime;
        return NO_ERROR;
    }

    EggsError _prepareCreateDirectoryInode(EggsTime time, const CreateDirectoryInodeReq& req, CreateDirectoryInodeEntry& entry) {
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.id.type() != InodeType::DIRECTORY || req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        entry.id = req.id;
        entry.ownerId = req.ownerId;
        entry.info = req.info;
        return NO_ERROR;
    }

    EggsError _prepareCreateLockedCurrentEdge(EggsTime time, const CreateLockedCurrentEdgeReq& req, CreateLockedCurrentEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (!validName(req.name.ref())) {
            return EggsError::BAD_NAME;
        }
        ALWAYS_ASSERT(req.targetId != NULL_INODE_ID); // proper error
        entry.dirId = req.dirId;
        entry.targetId = req.targetId;
        entry.name = req.name;
        entry.oldCreationTime = req.oldCreationTime;
        return NO_ERROR;
    }

    EggsError _prepareUnlockCurrentEdge(EggsTime time, const UnlockCurrentEdgeReq& req, UnlockCurrentEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.targetId = req.targetId;
        entry.name = req.name;
        entry.wasMoved = req.wasMoved;
        entry.creationTime = req.creationTime;
        return NO_ERROR;
    }

    EggsError _prepareLockCurrentEdge(EggsTime time, const LockCurrentEdgeReq& req, LockCurrentEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.name = req.name;
        entry.targetId = req.targetId;
        entry.creationTime = req.creationTime;
        return NO_ERROR;
    }

    EggsError _prepareRemoveDirectoryOwner(EggsTime time, const RemoveDirectoryOwnerReq& req, RemoveDirectoryOwnerEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        ALWAYS_ASSERT(req.dirId != ROOT_DIR_INODE_ID); // TODO proper error
        entry.dirId = req.dirId;
        entry.info = req.info;
        return NO_ERROR;
    }

    EggsError _prepareRemoveInode(EggsTime time, const RemoveInodeReq& req, RemoveInodeEntry& entry) {
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.id == ROOT_DIR_INODE_ID) {
            return EggsError::CANNOT_REMOVE_ROOT_DIRECTORY;
        }
        entry.id = req.id;
        return NO_ERROR;
    }

    EggsError _prepareSetDirectoryOwner(EggsTime time, const SetDirectoryOwnerReq& req, SetDirectoryOwnerEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        entry.dirId = req.dirId;
        entry.ownerId = req.ownerId;
        return NO_ERROR;
    }

    EggsError _prepareSetDirectoryInfo(EggsTime time, const SetDirectoryInfoReq& req, SetDirectoryInfoEntry& entry) {
        if (req.id.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.id;
        entry.info = req.info;
        return NO_ERROR;
    }

    EggsError _prepareRemoveNonOwnedEdge(EggsTime time, const RemoveNonOwnedEdgeReq& req, RemoveNonOwnedEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.creationTime = req.creationTime;
        entry.name = req.name;
        return NO_ERROR;
    }

    EggsError _prepareSameShardHardFileUnlink(EggsTime time, const SameShardHardFileUnlinkReq& req, SameShardHardFileUnlinkEntry& entry) {
        if (req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.targetId.type() != InodeType::FILE && req.targetId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.ownerId.shard() != _shid || req.targetId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.ownerId = req.ownerId;
        entry.targetId = req.targetId;
        entry.name = req.name;
        entry.creationTime = req.creationTime;
        return NO_ERROR;
    }

    EggsError _prepareRemoveSpanInitiate(EggsTime time, const RemoveSpanInitiateReq& req, RemoveSpanInitiateEntry& entry) {
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        {
            EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
            if (err != NO_ERROR) {
                return err;
            }
        }
        entry.fileId = req.fileId;
        return NO_ERROR;
    }

    bool _checkSpanBody(const AddSpanInitiateReq& req) {
        // Note that the span size might be bigger or smaller than
        // the data -- check comment on top of `AddSpanInitiateReq` in `msgs.go`
        // for details.
        if (req.size > MAXIMUM_SPAN_SIZE) {
            LOG_DEBUG(_env, "req.size=%s > MAXIMUM_SPAN_SIZE=%s", req.size, MAXIMUM_SPAN_SIZE);
            return false;
        }

        if (req.crcs.els.size() != ((int)req.stripes)*req.parity.blocks()) {
            LOG_DEBUG(_env, "len(crcs)=%s != stripes*blocks=%s", req.crcs.els.size(), ((int)req.stripes)*req.parity.blocks());
            return false;
        }

        uint32_t spanCrc = 0;
        if (req.parity.dataBlocks() == 1) {
            // mirroring blocks should all be the same
            for (int s = 0; s < req.stripes; s++) {
                uint32_t stripeCrc = req.crcs.els[s*req.parity.blocks()].u32;
                spanCrc = crc32c_append(spanCrc, stripeCrc, req.cellSize);
                for (int p = 0; p < req.parity.parityBlocks(); p++) {
                    if (req.crcs.els[s*req.parity.blocks() + 1+p].u32 != stripeCrc) {
                        LOG_DEBUG(_env, "mismatched CRC for mirrored block, expected %s, got %s", Crc(stripeCrc), req.crcs.els[s*req.parity.blocks() + 1+p]);
                        return false;
                    }
                }
            }
        } else {
            // Consistency check for the general case. Given what we do in
            // `rs.h`, we know that the span is the concatenation of the
            // data blocks, and that the first parity block is the XOR of the
            // data blocks. We can't check the rest without the data though.
            for (int s = 0; s < req.stripes; s++) {
                uint32_t parity0Crc;
                for (int d = 0; d < req.parity.dataBlocks(); d++) {
                    uint32_t cellCrc = req.crcs.els[s*req.parity.blocks() + d].u32;
                    spanCrc = crc32c_append(spanCrc, cellCrc, req.cellSize);
                    parity0Crc = d == 0 ? cellCrc : crc32c_xor(parity0Crc, cellCrc, req.cellSize);
                }
                if (parity0Crc != req.crcs.els[s*req.parity.blocks() + req.parity.dataBlocks()].u32) {
                    LOG_DEBUG(_env, "bad parity 0 CRC, expected %s, got %s", Crc(parity0Crc), req.crcs.els[s*req.parity.blocks() + req.parity.dataBlocks()]);
                    return false;
                }
            }
        }
        spanCrc = crc32c_zero_extend(spanCrc, (ssize_t)req.size - (ssize_t)(req.cellSize * req.stripes * req.parity.dataBlocks()));
        if (spanCrc != req.crc) {
            LOG_DEBUG(_env, "bad span CRC, expected %s, got %s", Crc(spanCrc), req.crc);
            return false;
        }

        return true;
    }

    bool _blockServiceMatchesBlacklist(
        const std::vector<BlacklistEntry>& blacklists,
        const std::array<uint8_t, 16>& failureDomain,
        BlockServiceId blockServiceId,
        const BlockServiceCache& cache
    ) {
        for (const auto& blacklist: blacklists) {
            if (blacklist.failureDomain.name.data == failureDomain ||  blacklist.blockService == blockServiceId) {
                return true;
            }
        }
        return false;
    }

    EggsError _prepareAddInlineSpan(EggsTime time, const AddInlineSpanReq& req, AddInlineSpanEntry& entry) {
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        {
            EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
            if (err != NO_ERROR) {
                return err;
            }
        }

        if (req.storageClass == EMPTY_STORAGE) {
            if (req.size != 0) {
                LOG_DEBUG(_env, "empty span has size != 0: %s", req.size);
                return EggsError::BAD_SPAN_BODY;
            }
        } else if (req.storageClass == INLINE_STORAGE) {
            if (req.size == 0 || req.size < req.body.size()) {
                LOG_DEBUG(_env, "inline span has req.size=%s == 0 || req.size=%s < req.body.size()=%s", req.size, req.size, (int)req.body.size());
                return EggsError::BAD_SPAN_BODY;
            }
        } else {
            LOG_DEBUG(_env, "inline span has bad storage class %s", req.storageClass);
            return EggsError::BAD_SPAN_BODY;
        }

        if (req.byteOffset%EGGSFS_PAGE_SIZE != 0) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "req.byteOffset=%s is not a multiple of PAGE_SIZE=%s", req.byteOffset, EGGSFS_PAGE_SIZE);
            return EggsError::BAD_SPAN_BODY;
        }

        uint32_t expectedCrc = crc32c(0, req.body.data(), req.body.size());
        expectedCrc = crc32c_zero_extend(expectedCrc, req.size - req.body.size());
        if (expectedCrc != req.crc.u32) {
            LOG_DEBUG(_env, "inline span expected CRC %s, got %s", Crc(expectedCrc), req.crc);
            return EggsError::BAD_SPAN_BODY;
        }

        entry.fileId = req.fileId;
        entry.storageClass = req.storageClass;
        entry.byteOffset = req.byteOffset;
        entry.size = req.size;
        entry.body = req.body;
        entry.crc = req.crc;

        return NO_ERROR;
    }

    EggsError _prepareAddSpanInitiate(const rocksdb::ReadOptions& options, EggsTime time, const AddSpanInitiateReq& req, InodeId reference, AddSpanInitiateEntry& entry, bool withReference) {
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (reference.type() != InodeType::FILE && reference.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        {
            EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
            if (err != NO_ERROR) {
                return err;
            }
        }
        if (req.storageClass == INLINE_STORAGE || req.storageClass == EMPTY_STORAGE) {
            LOG_DEBUG(_env, "bad storage class %s for blocks span", (int)req.storageClass);
            return EggsError::BAD_SPAN_BODY;
        }
        if (req.byteOffset%EGGSFS_PAGE_SIZE != 0 || req.cellSize%EGGSFS_PAGE_SIZE != 0) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "req.byteOffset=%s or cellSize=%s is not a multiple of PAGE_SIZE=%s", req.byteOffset, req.cellSize, EGGSFS_PAGE_SIZE);
            return EggsError::BAD_SPAN_BODY;
        }
        if (!_checkSpanBody(req)) {
            return EggsError::BAD_SPAN_BODY;
        }

        // start filling in entry
        entry.withReference = withReference;
        entry.fileId = req.fileId;
        entry.byteOffset = req.byteOffset;
        entry.storageClass = req.storageClass;
        entry.parity = req.parity;
        entry.size = req.size;
        entry.cellSize = req.cellSize;
        entry.crc = req.crc;
        entry.stripes = req.stripes;

        // fill stripe CRCs
        for (int s = 0; s < req.stripes; s++) {
            uint32_t stripeCrc = 0;
            for (int d = 0; d < req.parity.dataBlocks(); d++) {
                stripeCrc = crc32c_append(stripeCrc, req.crcs.els[s*req.parity.blocks() + d].u32, req.cellSize);
            }
            entry.bodyStripes.els.emplace_back(stripeCrc);
        }

        // Now fill in the block services. Generally we want to try to keep them the same
        // throughout the file, if possible, so that the likelihood of data loss is minimized.
        //
        // Currently things are spread out in faiure domains nicely by just having the current
        // block services to be all on different failure domains.
        {
            auto inMemoryBlockServicesData = _blockServicesCache.getCache();
            std::vector<BlockServiceId> candidateBlockServices;
            candidateBlockServices.reserve(inMemoryBlockServicesData.currentBlockServices.size());
            LOG_DEBUG(_env, "Starting out with %s current block services", candidateBlockServices.size());
            {
                for (BlockServiceId id: inMemoryBlockServicesData.currentBlockServices) {
                    const auto& cache = inMemoryBlockServicesData.blockServices.at(id.u64);
                    if (cache.storageClass != entry.storageClass) {
                        LOG_DEBUG(_env, "Skipping %s because of different storage class (%s != %s)", id, (int)cache.storageClass, (int)entry.storageClass);
                        continue;
                    }
                    if (_blockServiceMatchesBlacklist(req.blacklist.els, cache.failureDomain, id, cache)) {
                        LOG_DEBUG(_env, "Skipping %s because it matches blacklist", id);
                        continue;
                    }
                    candidateBlockServices.emplace_back(id);
                }
            }
            LOG_DEBUG(_env, "Starting out with %s block service candidates, parity %s", candidateBlockServices.size(), entry.parity);
            std::vector<BlockServiceId> pickedBlockServices;
            pickedBlockServices.reserve(req.parity.blocks());
            // We try to copy the block services from the first and the last span. The first
            // span is generally considered the "reference" one, and should work in the common
            // case. The last span is useful only in the case where we start using different
            // block services mid-file, probably because a block service went down. Why not
            // always use the last span? When we migrate or defrag or in generally reorganize
            // the spans we generally work from left-to-right, and in that case if we always
            // looked at the last one we'd pick a random block service every time. The "last
            // span" fallback is free in the common case anyhow.
            const auto fillInBlockServicesFromSpan = [&](bool first) {
                // empty file, bail out early and avoid pointless double lookup
                if (entry.fileId == reference && entry.byteOffset == 0) {
                    return;
                }
                // we're already done (avoid double seek in the common case)
                if (pickedBlockServices.size() >= req.parity.blocks() || candidateBlockServices.size() < 0) {
                    return;
                }
                StaticValue<SpanKey> startK;
                startK().setFileId(reference);
                // We should never have many tombstones here (spans aren't really deleted and
                // re-added apart from rare cases), so the offset upper bound is fine.
                startK().setOffset(first ? 0 : ~(uint64_t)0);
                std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _spansCf));
                it->SeekForPrev(startK.toSlice());
                if (!it->Valid()) { // nothing to do if we can't find a span
                    if (!it->status().IsNotFound()) {
                        ROCKS_DB_CHECKED(it->status());
                    }
                    return;
                }
                auto k = ExternalValue<SpanKey>::FromSlice(it->key());
                auto span = ExternalValue<SpanBody>::FromSlice(it->value());
                if (span().storageClass() == INLINE_STORAGE) { return; }
                const auto blocks = span().blocksBody();
                for (
                    int i = 0;
                    i < blocks.parity().blocks() && pickedBlockServices.size() < req.parity.blocks() && candidateBlockServices.size() > 0;
                    i++
                ) {
                    const BlockBody spanBlock = blocks.block(i);
                    auto isCandidate = std::find(candidateBlockServices.begin(), candidateBlockServices.end(), spanBlock.blockService());
                    if (isCandidate == candidateBlockServices.end()) {
                        continue;
                    }
                    LOG_DEBUG(_env, "(1) Picking block service candidate %s, failure domain %s", spanBlock.blockService(), GoLangQuotedStringFmt((const char*)inMemoryBlockServicesData.blockServices.at(spanBlock.blockService().u64).failureDomain.data(), 16));
                    BlockServiceId blockServiceId = spanBlock.blockService();
                    pickedBlockServices.emplace_back(blockServiceId);
                    std::iter_swap(isCandidate, candidateBlockServices.end()-1);
                    candidateBlockServices.pop_back();
                }
            };
            fillInBlockServicesFromSpan(true);
            fillInBlockServicesFromSpan(false);
            // Fill in whatever remains. We don't need to be deterministic here (we would have to
            // if we were in log application), but we might as well.
            {
                uint64_t rand = time.ns;
                while (pickedBlockServices.size() < req.parity.blocks() && candidateBlockServices.size() > 0) {
                    uint64_t ix = wyhash64(&rand) % candidateBlockServices.size();
                    LOG_DEBUG(_env, "(2) Picking block service candidate %s, failure domain %s", candidateBlockServices[ix], GoLangQuotedStringFmt((const char*)inMemoryBlockServicesData.blockServices.at(candidateBlockServices[ix].u64).failureDomain.data(), 16));
                    pickedBlockServices.emplace_back(candidateBlockServices[ix]);
                    std::iter_swap(candidateBlockServices.begin()+ix, candidateBlockServices.end()-1);
                    candidateBlockServices.pop_back();
                }
            }
            // If we still couldn't find enough block services, we're toast.
            if (pickedBlockServices.size() < req.parity.blocks()) {
                return EggsError::COULD_NOT_PICK_BLOCK_SERVICES;
            }
            // Now generate the blocks
            entry.bodyBlocks.els.resize(req.parity.blocks());
            for (int i = 0; i < req.parity.blocks(); i++) {
                auto& block = entry.bodyBlocks.els[i];
                block.blockServiceId = pickedBlockServices[i];
                uint32_t blockCrc = 0;
                for (int s = 0; s < req.stripes; s++) {
                    blockCrc = crc32c_append(blockCrc, req.crcs.els[s*req.parity.blocks() + i].u32, req.cellSize);
                }
                block.crc = blockCrc;
            }
        }

        return NO_ERROR;
    }

    EggsError _prepareAddSpanCertify(EggsTime time, const AddSpanCertifyReq& req, AddSpanCertifyEntry& entry) {
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        {
            EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
            if (err != NO_ERROR) {
                return err;
            }
        }
        entry.fileId = req.fileId;
        entry.byteOffset = req.byteOffset;
        entry.proofs = req.proofs;
        return NO_ERROR;
    }

    EggsError _prepareMakeFileTransient(EggsTime time, const MakeFileTransientReq& req, MakeFileTransientEntry& entry) {
        if (req.id.type() != InodeType::FILE && req.id.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.id = req.id;
        entry.note = req.note;
        return NO_ERROR;
    }

    EggsError _prepareRemoveSpanCertify(EggsTime time, const RemoveSpanCertifyReq& req, RemoveSpanCertifyEntry& entry) {
        if (req.fileId.type() != InodeType::FILE && req.fileId.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        {
            EggsError err = _checkTransientFileCookie(req.fileId, req.cookie.data);
            if (err != NO_ERROR) {
                return err;
            }
        }
        entry.fileId = req.fileId;
        entry.byteOffset = req.byteOffset;
        entry.proofs = req.proofs;
        return NO_ERROR;
    }

    EggsError _prepareRemoveOwnedSnapshotFileEdge(EggsTime time, const RemoveOwnedSnapshotFileEdgeReq& req, RemoveOwnedSnapshotFileEdgeEntry& entry) {
        if (req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.ownerId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.targetId.type () != InodeType::FILE && req.targetId.type () != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        entry.ownerId = req.ownerId;
        entry.targetId = req.targetId;
        entry.creationTime = req.creationTime;
        entry.name = req.name;
        return NO_ERROR;
    }

    EggsError _prepareSwapBlocks(EggsTime time, const SwapBlocksReq& req, SwapBlocksEntry& entry) {
        if (req.fileId1.type() == InodeType::DIRECTORY || req.fileId2.type() == InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId1.shard() != _shid || req.fileId2.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        ALWAYS_ASSERT(req.fileId1 != req.fileId2);
        entry.fileId1 = req.fileId1;
        entry.byteOffset1 = req.byteOffset1;
        entry.blockId1 = req.blockId1;
        entry.fileId2 = req.fileId2;
        entry.byteOffset2 = req.byteOffset2;
        entry.blockId2 = req.blockId2;
        return NO_ERROR;
    }

    EggsError _prepareSwapSpans(EggsTime time, const SwapSpansReq& req, SwapSpansEntry& entry) {
        if (req.fileId1.type() == InodeType::DIRECTORY || req.fileId2.type() == InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId1.shard() != _shid || req.fileId2.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        ALWAYS_ASSERT(req.fileId1 != req.fileId2);
        entry.fileId1 = req.fileId1;
        entry.byteOffset1 = req.byteOffset1;
        entry.blocks1 = req.blocks1;
        entry.fileId2 = req.fileId2;
        entry.byteOffset2 = req.byteOffset2;
        entry.blocks2 = req.blocks2;
        return NO_ERROR;
    }

    EggsError _prepareMoveSpan(EggsTime time, const MoveSpanReq& req, MoveSpanEntry& entry) {
        if (req.fileId1.type() == InodeType::DIRECTORY || req.fileId2.type() == InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.fileId1.shard() != _shid || req.fileId2.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        EggsError err = _checkTransientFileCookie(req.fileId1, req.cookie1.data);
        if (err != NO_ERROR) {
            return err;
        }
        err = _checkTransientFileCookie(req.fileId2, req.cookie2.data);
        if (err != NO_ERROR) {
            return err;
        }
        entry.fileId1 = req.fileId1;
        entry.cookie1 = req.cookie1;
        entry.byteOffset1 = req.byteOffset1;
        entry.fileId2 = req.fileId2;
        entry.cookie2 = req.cookie2;
        entry.byteOffset2 = req.byteOffset2;
        entry.spanSize = req.spanSize;
        return NO_ERROR;
    }

    EggsError _prepareSetTime(EggsTime time, const SetTimeReq& req, SetTimeEntry& entry) {
        if (req.id.type() == InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.id = req.id;
        entry.atime = req.atime;
        entry.mtime = req.mtime;
        return NO_ERROR;
    }

    EggsError _prepareRemoveZeroBlockServiceFiles(EggsTime time, const RemoveZeroBlockServiceFilesReq& req, RemoveZeroBlockServiceFilesEntry& entry) {
        entry.startBlockService = req.startBlockService;
        entry.startFile = req.startFile;
        return NO_ERROR;
    }

    EggsError prepareLogEntry(const ShardReqContainer& req, ShardLogEntry& logEntry) {
        LOG_DEBUG(_env, "processing write request of kind %s", req.kind());
        logEntry.clear();
        EggsError err = NO_ERROR;

        EggsTime time = eggsNow();
        logEntry.time = time;
        auto& logEntryBody = logEntry.body;

        auto snapshot = _getCurrentReadSnapshot();
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

        switch (req.kind()) {
        case ShardMessageKind::CONSTRUCT_FILE:
            err = _prepareConstructFile(time, req.getConstructFile(), logEntryBody.setConstructFile());
            break;
        case ShardMessageKind::LINK_FILE:
            err = _prepareLinkFile(time, req.getLinkFile(), logEntryBody.setLinkFile());
            break;
        case ShardMessageKind::SAME_DIRECTORY_RENAME:
            err = _prepareSameDirectoryRename(time, req.getSameDirectoryRename(), logEntryBody.setSameDirectoryRename());
            break;
        case ShardMessageKind::SOFT_UNLINK_FILE:
            err = _prepareSoftUnlinkFile(time, req.getSoftUnlinkFile(), logEntryBody.setSoftUnlinkFile());
            break;
        case ShardMessageKind::CREATE_DIRECTORY_INODE:
            err = _prepareCreateDirectoryInode(time, req.getCreateDirectoryInode(), logEntryBody.setCreateDirectoryInode());
            break;
        case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
            err = _prepareCreateLockedCurrentEdge(time, req.getCreateLockedCurrentEdge(), logEntryBody.setCreateLockedCurrentEdge());
            break;
        case ShardMessageKind::UNLOCK_CURRENT_EDGE:
            err = _prepareUnlockCurrentEdge(time, req.getUnlockCurrentEdge(), logEntryBody.setUnlockCurrentEdge());
            break;
        case ShardMessageKind::LOCK_CURRENT_EDGE:
            err = _prepareLockCurrentEdge(time, req.getLockCurrentEdge(), logEntryBody.setLockCurrentEdge());
            break;
        case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
            err = _prepareRemoveDirectoryOwner(time, req.getRemoveDirectoryOwner(), logEntryBody.setRemoveDirectoryOwner());
            break;
        case ShardMessageKind::REMOVE_INODE:
            err = _prepareRemoveInode(time, req.getRemoveInode(), logEntryBody.setRemoveInode());
            break;
        case ShardMessageKind::SET_DIRECTORY_OWNER:
            err = _prepareSetDirectoryOwner(time, req.getSetDirectoryOwner(), logEntryBody.setSetDirectoryOwner());
            break;
        case ShardMessageKind::SET_DIRECTORY_INFO:
            err = _prepareSetDirectoryInfo(time, req.getSetDirectoryInfo(), logEntryBody.setSetDirectoryInfo());
            break;
        case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
            err = _prepareRemoveNonOwnedEdge(time, req.getRemoveNonOwnedEdge(), logEntryBody.setRemoveNonOwnedEdge());
            break;
        case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
            err = _prepareSameShardHardFileUnlink(time, req.getSameShardHardFileUnlink(), logEntryBody.setSameShardHardFileUnlink());
            break;
        case ShardMessageKind::REMOVE_SPAN_INITIATE:
            err = _prepareRemoveSpanInitiate(time, req.getRemoveSpanInitiate(), logEntryBody.setRemoveSpanInitiate());
            break;
        case ShardMessageKind::ADD_INLINE_SPAN:
            err = _prepareAddInlineSpan(time, req.getAddInlineSpan(), logEntryBody.setAddInlineSpan());
            break;
        case ShardMessageKind::ADD_SPAN_INITIATE: {
            const auto& addSpanReq = req.getAddSpanInitiate();
            err = _prepareAddSpanInitiate(options, time, addSpanReq, addSpanReq.fileId, logEntryBody.setAddSpanInitiate(), false);
            break; }
        case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE: {
            const auto& addSpanReq = req.getAddSpanInitiateWithReference();
            err = _prepareAddSpanInitiate(options, time, addSpanReq.req, addSpanReq.reference, logEntryBody.setAddSpanInitiate(), true);
            break; }
        case ShardMessageKind::ADD_SPAN_CERTIFY:
            err = _prepareAddSpanCertify(time, req.getAddSpanCertify(), logEntryBody.setAddSpanCertify());
            break;
        case ShardMessageKind::MAKE_FILE_TRANSIENT:
            err = _prepareMakeFileTransient(time, req.getMakeFileTransient(), logEntryBody.setMakeFileTransient());
            break;
        case ShardMessageKind::REMOVE_SPAN_CERTIFY:
            err = _prepareRemoveSpanCertify(time, req.getRemoveSpanCertify(), logEntryBody.setRemoveSpanCertify());
            break;
        case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
            err = _prepareRemoveOwnedSnapshotFileEdge(time, req.getRemoveOwnedSnapshotFileEdge(), logEntryBody.setRemoveOwnedSnapshotFileEdge());
            break;
        case ShardMessageKind::SWAP_BLOCKS:
            err = _prepareSwapBlocks(time, req.getSwapBlocks(), logEntryBody.setSwapBlocks());
            break;
        case ShardMessageKind::MOVE_SPAN:
            err = _prepareMoveSpan(time, req.getMoveSpan(), logEntryBody.setMoveSpan());
            break;
        case ShardMessageKind::SET_TIME:
            err = _prepareSetTime(time, req.getSetTime(), logEntryBody.setSetTime());
            break;
        case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
            err = _prepareRemoveZeroBlockServiceFiles(time, req.getRemoveZeroBlockServiceFiles(), logEntryBody.setRemoveZeroBlockServiceFiles());
            break;
        case ShardMessageKind::SWAP_SPANS:
            err = _prepareSwapSpans(time, req.getSwapSpans(), logEntryBody.setSwapSpans());
            break;
        default:
            throw EGGS_EXCEPTION("bad write shard message kind %s", req.kind());
        }

        if (err == NO_ERROR) {
            LOG_DEBUG(_env, "prepared log entry of kind %s, for request of kind %s", logEntryBody.kind(), req.kind());
            LOG_TRACE(_env, "log entry body: %s", logEntryBody);
        } else {
            LOG_INFO(_env, "could not prepare log entry for request of kind %s: %s", req.kind(), err);
        }

        return err;
    }

    // ----------------------------------------------------------------
    // log application

    void _advanceLastAppliedLogEntry(rocksdb::WriteBatch& batch, uint64_t index) {
        uint64_t oldIndex = _lastAppliedLogEntry();
        ALWAYS_ASSERT(oldIndex+1 == index, "old index is %s, expected %s, got %s", oldIndex, oldIndex+1, index);
        LOG_DEBUG(_env, "bumping log index from %s to %s", oldIndex, index);
        StaticValue<U64Value> v;
        v().setU64(index);
        ROCKS_DB_CHECKED(batch.Put({}, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), v.toSlice()));
    }

    EggsError _applyConstructFile(rocksdb::WriteBatch& batch, EggsTime time, const ConstructFileEntry& entry, ConstructFileResp& resp) {
        const auto nextFileId = [this, &batch](const ShardMetadataKey* key) -> InodeId {
            std::string value;
            ROCKS_DB_CHECKED(_db->Get({}, shardMetadataKey(key), &value));
            ExternalValue<InodeIdValue> inodeId(value);
            inodeId().setId(InodeId::FromU64(inodeId().id().u64 + 0x100));
            ROCKS_DB_CHECKED(batch.Put(shardMetadataKey(key), inodeId.toSlice()));
            return inodeId().id();
        };
        InodeId id;
        if (entry.type == (uint8_t)InodeType::FILE) {
            id = nextFileId(&NEXT_FILE_ID_KEY);
        } else if (entry.type == (uint8_t)InodeType::SYMLINK) {
            id = nextFileId(&NEXT_SYMLINK_ID_KEY);
        } else {
            throw EGGS_EXCEPTION("Bad type %s", (int)entry.type);
        }

        // write to rocks
        StaticValue<TransientFileBody> transientFile;
        transientFile().setVersion(0);
        transientFile().setFileSize(0);
        transientFile().setMtime(time);
        transientFile().setDeadline(entry.deadlineTime);
        transientFile().setLastSpanState(SpanState::CLEAN);
        transientFile().setNoteDangerous(entry.note.ref());
        auto k = InodeIdKey::Static(id);
        ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), transientFile.toSlice()));

        // prepare response
        resp.id = id;
        resp.cookie.data = _calcCookie(resp.id);

        return NO_ERROR;
    }

    EggsError _applyLinkFile(rocksdb::WriteBatch& batch, EggsTime time, const LinkFileEntry& entry, LinkFileResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> transientFile;
        {
            EggsError err = _getTransientFile({}, time, false /*allowPastDeadline*/, entry.fileId, fileValue, transientFile);
            if (err == EggsError::FILE_NOT_FOUND) {
                // Check if the file has already been linked to simplify the life of retrying
                // clients.
                uint64_t nameHash;
                // Return original error if the dir doens't exist, since this is some recovery mechanism anyway
                if (_getDirectoryAndHash({}, entry.ownerId, false /*allowSnapshot*/, entry.name.ref(), nameHash) != NO_ERROR) {
                    LOG_DEBUG(_env, "could not find directory after FILE_NOT_FOUND for link file");
                    return err;
                }
                StaticValue<EdgeKey> edgeKey;
                edgeKey().setDirIdWithCurrent(entry.ownerId, true);
                edgeKey().setNameHash(nameHash);
                edgeKey().setName(entry.name.ref());
                std::string edgeValue;
                {
                    auto status = _db->Get({}, _edgesCf, edgeKey.toSlice(), &edgeValue);
                    if (status.IsNotFound()) {
                        LOG_DEBUG(_env, "could not find edge after FILE_NOT_FOUND for link file");
                        return err;
                    }
                    ROCKS_DB_CHECKED(status);
                }
                ExternalValue<CurrentEdgeBody> edge(edgeValue);
                if (edge().targetId() != entry.fileId) {
                    LOG_DEBUG(_env, "mismatching file id after FILE_NOT_FOUND for link file");
                    return err;
                }
                resp.creationTime = edge().creationTime();
                return NO_ERROR;
            } else if (err != NO_ERROR) {
                return err;
            }
        }
        if (transientFile().lastSpanState() != SpanState::CLEAN) {
            return EggsError::LAST_SPAN_STATE_NOT_CLEAN;
        }

        // move from transient to non-transient.
        auto fileKey = InodeIdKey::Static(entry.fileId);
        ROCKS_DB_CHECKED(batch.Delete(_transientCf, fileKey.toSlice()));
        StaticValue<FileBody> file;
        file().setVersion(0);
        file().setMtime(time);
        file().setAtime(time);
        file().setFileSize(transientFile().fileSize());
        ROCKS_DB_CHECKED(batch.Put(_filesCf, fileKey.toSlice(), file.toSlice()));

        // create edge in owner.
        {
            EggsError err = ShardDBImpl::_createCurrentEdge(time, batch, entry.ownerId, entry.name, entry.fileId, false, 0, resp.creationTime);
            if (err != NO_ERROR) {
                return err;
            }
        }

        return NO_ERROR;
    }

    EggsError _initiateDirectoryModification(EggsTime time, bool allowSnapshot, rocksdb::WriteBatch& batch, InodeId dirId, std::string& dirValue, ExternalValue<DirectoryBody>& dir) {
        ExternalValue<DirectoryBody> tmpDir;
        EggsError err = _getDirectory({}, dirId, allowSnapshot, dirValue, tmpDir);
        if (err != NO_ERROR) {
            return err;
        }

        // Don't go backwards in time. This is important amongst other things to ensure
        // that we have snapshot edges to be uniquely identified by name, hash, creationTime.
        // This should be very uncommon.
        if (tmpDir().mtime() >= time) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "trying to modify dir %s going backwards in time, dir mtime is %s, log entry time is %s", dirId, tmpDir().mtime(), time);
            return EggsError::MTIME_IS_TOO_RECENT;
        }

        // Modify the directory mtime
        tmpDir().setMtime(time);
        {
            auto k = InodeIdKey::Static(dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), tmpDir.toSlice()));
        }

        dir = tmpDir;
        return NO_ERROR;
    }

    // When we just want to compute the hash of something when modifying the dir
    EggsError _initiateDirectoryModificationAndHash(EggsTime time, bool allowSnapshot, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytesRef& name, uint64_t& nameHash) {
        ExternalValue<DirectoryBody> dir;
        std::string dirValue;
        EggsError err = _initiateDirectoryModification(time, allowSnapshot, batch, dirId, dirValue, dir);
        if (err != NO_ERROR) {
            return err;
        }
        nameHash = computeHash(dir().hashMode(), name);
        return NO_ERROR;
    }

    // Note that we cannot expose an API which allows us to create non-locked current edges,
    // see comment for CreateLockedCurrentEdgeReq.
    //
    // The creation time might be different than the current time because we might find it
    // in an existing edge.
    EggsError _createCurrentEdge(
        EggsTime logEntryTime, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytes& name, InodeId targetId,
        // if locked=true, oldCreationTime will be used to check that we're locking the right edge.
        bool locked, EggsTime oldCreationTime,
        EggsTime& creationTime
    ) {
        ALWAYS_ASSERT(locked || oldCreationTime == 0);

        creationTime = logEntryTime;

        uint64_t nameHash;
        {
            // allowSnaphsot=false since we cannot create current edges in snapshot directories.
            EggsError err = _initiateDirectoryModificationAndHash(logEntryTime, false, batch, dirId, name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // Next, we need to look at the current edge with the same name, if any.
        StaticValue<EdgeKey> edgeKey;
        edgeKey().setDirIdWithCurrent(dirId, true); // current=true
        edgeKey().setNameHash(nameHash);
        edgeKey().setName(name.ref());
        std::string edgeValue;
        auto status = _db->Get({}, _edgesCf, edgeKey.toSlice(), &edgeValue);

        // in the block below, we exit the function early if something is off.
        if (status.IsNotFound()) {
            // we're the first one here -- we only need to check the time of
            // the snaphsot edges if the creation time is earlier than the
            // log entry time, otherwise we know that the snapshot edges are
            // all older, since they were all created before logEntryTime.
            StaticValue<EdgeKey> snapshotEdgeKey;
            snapshotEdgeKey().setDirIdWithCurrent(dirId, false); // snapshhot (current=false)
            snapshotEdgeKey().setNameHash(nameHash);
            snapshotEdgeKey().setName(name.ref());
            snapshotEdgeKey().setCreationTime({std::numeric_limits<uint64_t>::max()});
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _edgesCf));
            // TODO add iteration bounds
            it->SeekForPrev(snapshotEdgeKey.toSlice());
            if (it->Valid() && !it->status().IsNotFound()) {
                auto k = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (k().dirId() == dirId && !k().current() && k().nameHash() == nameHash && k().name() == name.ref()) {
                    if (k().creationTime() >= creationTime) {
                        return EggsError::MORE_RECENT_SNAPSHOT_EDGE;
                    }
                }
            }
            ROCKS_DB_CHECKED(it->status());
        } else {
            ROCKS_DB_CHECKED(status);
            ExternalValue<CurrentEdgeBody> existingEdge(edgeValue);
            if (existingEdge().targetIdWithLocked().extra()) { // locked
                // we have an existing locked edge, we need to make sure that it's the one we expect for
                // idempotency.
                if (!locked) { // the edge we're trying to create is not locked
                    return EggsError::NAME_IS_LOCKED;
                }
                if (existingEdge().targetId() != targetId) {
                    LOG_DEBUG(_env, "expecting target %s, got %s instead", existingEdge().targetId(), targetId);
                    return EggsError::MISMATCHING_TARGET;
                }
                // we're not locking the right thing
                if (existingEdge().creationTime() != oldCreationTime) {
                    LOG_DEBUG(_env, "expecting time %s, got %s instead", existingEdge().creationTime(), oldCreationTime);
                    return EggsError::MISMATCHING_CREATION_TIME;
                }
                // The new creation time doesn't budge!
                creationTime = existingEdge().creationTime();
            } else {
                // We're kicking out a non-locked current edge. The only circumstance where we allow
                // this automatically is if a file is overriding another file, which is also how it
                // works in linux/posix (see `man 2 rename`).
                if (existingEdge().creationTime() >= creationTime) {
                    return EggsError::MORE_RECENT_CURRENT_EDGE;
                }
                if (
                    targetId.type() == InodeType::DIRECTORY || existingEdge().targetIdWithLocked().id().type() == InodeType::DIRECTORY
                ) {
                    return EggsError::CANNOT_OVERRIDE_NAME;
                }
                // make what is now the current edge a snapshot edge -- no need to delete it,
                // it'll be overwritten below.
                {
                    StaticValue<EdgeKey> k;
                    k().setDirIdWithCurrent(dirId, false); // snapshot (current=false)
                    k().setNameHash(nameHash);
                    k().setName(name.ref());
                    k().setCreationTime(existingEdge().creationTime());
                    StaticValue<SnapshotEdgeBody> v;
                    v().setVersion(0);
                    // this was current, so it's now owned.
                    v().setTargetIdWithOwned(InodeIdExtra(existingEdge().targetIdWithLocked().id(), true));
                    ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
                }
            }
        }

        // OK, we're now ready to insert the current edge
        StaticValue<CurrentEdgeBody> edgeBody;
        edgeBody().setVersion(0);
        edgeBody().setTargetIdWithLocked(InodeIdExtra(targetId, locked));
        edgeBody().setCreationTime(creationTime);
        ROCKS_DB_CHECKED(batch.Put(_edgesCf, edgeKey.toSlice(), edgeBody.toSlice()));

        return NO_ERROR;

    }

    EggsError _applySameDirectoryRename(EggsTime time, rocksdb::WriteBatch& batch, const SameDirectoryRenameEntry& entry, SameDirectoryRenameResp& resp) {
        // First, remove the old edge -- which won't be owned anymore, since we're renaming it.
        {
            EggsError err = _softUnlinkCurrentEdge(time, batch, entry.dirId, entry.oldName, entry.oldCreationTime, entry.targetId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // Now, create the new one
        {
            EggsError err = _createCurrentEdge(time, batch, entry.dirId, entry.newName, entry.targetId, false, 0, resp.newCreationTime);
            if (err != NO_ERROR) {
                return err;
            }
        }
        return NO_ERROR;
    }

    // the creation time of the delete edge is always `time`.
    EggsError _softUnlinkCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytes& name, EggsTime creationTime, InodeId targetId, bool owned) {
        // compute hash
        uint64_t nameHash;
        {
            // allowSnaphsot=false since we can't have current edges in snapshot dirs
            EggsError err = _initiateDirectoryModificationAndHash(time, false, batch, dirId, name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // get the edge
        StaticValue<EdgeKey> edgeKey;
        edgeKey().setDirIdWithCurrent(dirId, true); // current=true
        edgeKey().setNameHash(nameHash);
        edgeKey().setName(name.ref());
        std::string edgeValue;
        auto status = _db->Get({}, _edgesCf, edgeKey.toSlice(), &edgeValue);
        if (status.IsNotFound()) {
            return EggsError::EDGE_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        ExternalValue<CurrentEdgeBody> edgeBody(edgeValue);
        if (edgeBody().targetIdWithLocked().id() != targetId) {
            LOG_DEBUG(_env, "expecting target %s, but got %s", targetId, edgeBody().targetIdWithLocked().id());
            return EggsError::MISMATCHING_TARGET;
        }
        if (edgeBody().creationTime() != creationTime) {
            LOG_DEBUG(_env, "expected time %s, got %s", edgeBody().creationTime(), creationTime);
            return EggsError::MISMATCHING_CREATION_TIME;
        }
        if (edgeBody().targetIdWithLocked().extra()) { // locked
            return EggsError::EDGE_IS_LOCKED;
        }

        // delete the current edge
        batch.Delete(_edgesCf, edgeKey.toSlice());

        // add the two snapshot edges, one for what was the current edge,
        // and another to signify deletion
        {
            StaticValue<EdgeKey> k;
            k().setDirIdWithCurrent(dirId, false); // snapshot (current=false)
            k().setNameHash(nameHash);
            k().setName(name.ref());
            k().setCreationTime(edgeBody().creationTime());
            StaticValue<SnapshotEdgeBody> v;
            v().setVersion(0);
            v().setTargetIdWithOwned(InodeIdExtra(targetId, owned));
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
            k().setCreationTime(time);
            v().setTargetIdWithOwned(InodeIdExtra(NULL_INODE_ID, false));
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applySoftUnlinkFile(EggsTime time, rocksdb::WriteBatch& batch, const SoftUnlinkFileEntry& entry, SoftUnlinkFileResp& resp) {
        EggsError err = _softUnlinkCurrentEdge(time, batch, entry.ownerId, entry.name, entry.creationTime, entry.fileId, true);
        if (err != NO_ERROR) { return err; }
        resp.deleteCreationTime = time;
        return NO_ERROR;
    }

    EggsError _applyCreateDirectoryInode(EggsTime time, rocksdb::WriteBatch& batch, const CreateDirectoryInodeEntry& entry, CreateDirectoryInodeResp& resp) {
        // The assumption here is that only the CDC creates directories, and it doles out
        // inode ids per transaction, so that you'll never get competing creates here, but
        // we still check that the parent makes sense.
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // we never create directories as snapshot
            EggsError err = _getDirectory({}, entry.id, false, dirValue, dir);
            if (err == NO_ERROR) {
                if (dir().ownerId() != entry.ownerId) {
                    return EggsError::MISMATCHING_OWNER;
                } else {
                    return NO_ERROR;
                }
            } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
                // we continue
            } else {
                return err;
            }
        }

        {
            auto dirKey = InodeIdKey::Static(entry.id);
            OwnedValue<DirectoryBody> dir(entry.info);
            dir().setVersion(0);
            dir().setOwnerId(entry.ownerId);
            dir().setMtime(time);
            dir().setHashMode(HashMode::XXH3_63);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, dirKey.toSlice(), dir.toSlice()));
        }

        resp.mtime = time;

        return NO_ERROR;
    }

    EggsError _applyCreateLockedCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, const CreateLockedCurrentEdgeEntry& entry, CreateLockedCurrentEdgeResp& resp) {
        auto err = _createCurrentEdge(time, batch, entry.dirId, entry.name, entry.targetId, true, entry.oldCreationTime, resp.creationTime); // locked=true
        if (err != NO_ERROR) {
            return err;
        }
        return NO_ERROR;
    }

    EggsError _applyUnlockCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, const UnlockCurrentEdgeEntry& entry, UnlockCurrentEdgeResp& resp) {
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnaphsot=false since no current edges in snapshot dirs
            EggsError err = _initiateDirectoryModification(time, false, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir().hashMode(), entry.name.ref());
        }

        StaticValue<EdgeKey> currentKey;
        currentKey().setDirIdWithCurrent(entry.dirId, true); // current=true
        currentKey().setNameHash(nameHash);
        currentKey().setName(entry.name.ref());
        std::string edgeValue;
        {
            auto status = _db->Get({}, _edgesCf, currentKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::EDGE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        ExternalValue<CurrentEdgeBody> edge(edgeValue);
        if (edge().creationTime() != entry.creationTime) {
            LOG_DEBUG(_env, "expected time %s, got %s", edge().creationTime(), entry.creationTime);
            return EggsError::MISMATCHING_CREATION_TIME;
        }
        if (edge().locked()) {
            edge().setTargetIdWithLocked(InodeIdExtra(entry.targetId, false)); // locked=false
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, currentKey.toSlice(), edge.toSlice()));
        }
        if (entry.wasMoved) {
            // We need to move the current edge to snapshot, and create a new snapshot
            // edge with the deletion.
            ROCKS_DB_CHECKED(batch.Delete(_edgesCf, currentKey.toSlice()));
            StaticValue<EdgeKey> snapshotKey;
            snapshotKey().setDirIdWithCurrent(entry.dirId, false); // snapshot (current=false)
            snapshotKey().setNameHash(nameHash);
            snapshotKey().setName(entry.name.ref());
            snapshotKey().setCreationTime(edge().creationTime());
            StaticValue<SnapshotEdgeBody> snapshotBody;
            snapshotBody().setVersion(0);
            snapshotBody().setTargetIdWithOwned(InodeIdExtra(entry.targetId, false));
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, snapshotKey.toSlice(), snapshotBody.toSlice()));
            snapshotKey().setCreationTime(time);
            snapshotBody().setTargetIdWithOwned(InodeIdExtra(NULL_INODE_ID, false)); // deletion edges are never owned
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, snapshotKey.toSlice(), snapshotBody.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyLockCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, const LockCurrentEdgeEntry& entry, LockCurrentEdgeResp& resp) {
        // TODO lots of duplication with _applyUnlockCurrentEdge
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnaphsot=false since no current edges in snapshot dirs
            EggsError err = _initiateDirectoryModification(time, false, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir().hashMode(), entry.name.ref());
        }

        StaticValue<EdgeKey> currentKey;
        currentKey().setDirIdWithCurrent(entry.dirId, true); // current=true
        currentKey().setNameHash(nameHash);
        currentKey().setName(entry.name.ref());
        std::string edgeValue;
        {
            auto status = _db->Get({}, _edgesCf, currentKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::EDGE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        ExternalValue<CurrentEdgeBody> edge(edgeValue);
        if (edge().creationTime() != entry.creationTime) {
            LOG_DEBUG(_env, "expected time %s, got %s", edge().creationTime(), entry.creationTime);
            return EggsError::MISMATCHING_CREATION_TIME;
        }
        if (!edge().locked()) {
            edge().setTargetIdWithLocked({entry.targetId, true}); // locked=true
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, currentKey.toSlice(), edge.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveDirectoryOwner(EggsTime time, rocksdb::WriteBatch& batch, const RemoveDirectoryOwnerEntry& entry, RemoveDirectoryOwnerResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            // allowSnapshot=true for idempotency (see below)
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            if (dir().ownerId() == NULL_INODE_ID) {
                return NO_ERROR; // already done
            }
        }

        // if we have any current edges, we can't proceed
        {
            StaticValue<EdgeKey> edgeKey;
            edgeKey().setDirIdWithCurrent(entry.dirId, true); // current=true
            edgeKey().setNameHash(0);
            edgeKey().setName({});
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _edgesCf));
            // TODO apply iteration bound
            it->Seek(edgeKey.toSlice());
            if (it->Valid()) {
                auto otherEdge = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (otherEdge().dirId() == entry.dirId && otherEdge().current()) {
                    return EggsError::DIRECTORY_NOT_EMPTY;
                }
            } else if (it->status().IsNotFound()) {
                // nothing to do
            } else {
                ROCKS_DB_CHECKED(it->status());
            }
        }

        // we need to create a new DirectoryBody rather than modify the old one, the info might have changed size
        {
            OwnedValue<DirectoryBody> newDir(entry.info);
            newDir().setVersion(0);
            newDir().setOwnerId(NULL_INODE_ID);
            newDir().setMtime(time);
            newDir().setHashMode(dir().hashMode());
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), newDir.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveDirectoryInode(EggsTime time, rocksdb::WriteBatch& batch, const RemoveInodeEntry& entry, RemoveInodeResp& resp) {
        ALWAYS_ASSERT(entry.id.type() == InodeType::DIRECTORY);

        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.id, dirValue, dir);
            if (err == EggsError::DIRECTORY_NOT_FOUND) {
                return NO_ERROR; // we're already done
            }
            if (err != NO_ERROR) {
                return err;
            }
        }
        if (dir().ownerId() != NULL_INODE_ID) {
            return EggsError::DIRECTORY_HAS_OWNER;
        }
        // there can't be any outgoing edges when killing a directory definitively
        {
            StaticValue<EdgeKey> edgeKey;
            edgeKey().setDirIdWithCurrent(entry.id, false);
            edgeKey().setNameHash(0);
            edgeKey().setName({});
            edgeKey().setCreationTime(0);
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _edgesCf));
            // TODO apply iteration bound
            it->Seek(edgeKey.toSlice());
            if (it->Valid()) {
                auto otherEdge = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (otherEdge().dirId() == entry.id) {
                    LOG_DEBUG(_env, "found edge %s when trying to remove directory %s", otherEdge(), entry.id);
                    return EggsError::DIRECTORY_NOT_EMPTY;
                }
            } else if (it->status().IsNotFound()) {
                // nothing to do
            } else {
                ROCKS_DB_CHECKED(it->status());
            }
        }
        // we can finally delete
        {
            auto dirKey = InodeIdKey::Static(entry.id);
            ROCKS_DB_CHECKED(batch.Delete(_directoriesCf, dirKey.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveFileInode(EggsTime time, rocksdb::WriteBatch& batch, const RemoveInodeEntry& entry, RemoveInodeResp& resp) {
        ALWAYS_ASSERT(entry.id.type() == InodeType::FILE || entry.id.type() == InodeType::SYMLINK);

        // we demand for the file to be transient, for the deadline to have passed, and for it to have
        // no spans
        {
            std::string transientFileValue;
            ExternalValue<TransientFileBody> transientFile;
            EggsError err = _getTransientFile({}, time, true /*allowPastDeadline*/, entry.id, transientFileValue, transientFile);
            if (err == EggsError::FILE_NOT_FOUND) {
                std::string fileValue;
                ExternalValue<FileBody> file;
                EggsError err = _getFile({}, entry.id, fileValue, file);
                if (err == NO_ERROR) {
                    return EggsError::FILE_IS_NOT_TRANSIENT;
                } else if (err == EggsError::FILE_NOT_FOUND) {
                    // In this case the inode is just gone. The best thing to do is
                    // to just be OK with it, since we need to handle repeated calls
                    // nicely.
                    return NO_ERROR;
                } else {
                    return err;
                }
            } else if (err == NO_ERROR) {
                // keep going
            } else {
                return err;
            }
            // check deadline
            if (transientFile().deadline() >= time) {
                return EggsError::DEADLINE_NOT_PASSED;
            }
            // check no spans
            {
                StaticValue<SpanKey> spanKey;
                spanKey().setFileId(entry.id);
                spanKey().setOffset(0);
                std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _spansCf));
                // TODO apply iteration bound
                it->Seek(spanKey.toSlice());
                if (it->Valid()) {
                    auto otherSpan = ExternalValue<SpanKey>::FromSlice(it->key());
                    if (otherSpan().fileId() == entry.id) {
                        return EggsError::FILE_NOT_EMPTY;
                    }
                } else {
                    ROCKS_DB_CHECKED(it->status());
                }
            }
        }
        // we can finally delete
        {
            auto fileKey = InodeIdKey::Static(entry.id);
            ROCKS_DB_CHECKED(batch.Delete(_transientCf, fileKey.toSlice()));
        }
        return NO_ERROR;
    }

    EggsError _applyRemoveInode(EggsTime time, rocksdb::WriteBatch& batch, const RemoveInodeEntry& entry, RemoveInodeResp& resp) {
        if (entry.id.type() == InodeType::DIRECTORY) {
            return _applyRemoveDirectoryInode(time, batch, entry, resp);
        } else {
            return _applyRemoveFileInode(time, batch, entry, resp);
        }
    }

    EggsError _applySetDirectoryOwner(EggsTime time, rocksdb::WriteBatch& batch, const SetDirectoryOwnerEntry& entry, SetDirectoryOwnerResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // Set the owner. Note that we don't know whether the directory info was set because we deleted
        // the owner first -- i.e. we might mistakenly end with a non-inherited directory info. But this
        // ought to be uncommon enough to not be a problem.
        dir().setOwnerId(entry.ownerId);
        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), dir.toSlice()));
        }
        return NO_ERROR;
    }

    EggsError _applySetDirectoryInfo(EggsTime time, rocksdb::WriteBatch& batch, const SetDirectoryInfoEntry& entry, SetDirectoryInfoResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            // allowSnapshot=true since we might want to influence deletion policies for already deleted
            // directories.
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }

        OwnedValue<DirectoryBody> newDir(entry.info);
        newDir().setVersion(0);
        newDir().setOwnerId(dir().ownerId());
        newDir().setMtime(dir().mtime());
        newDir().setHashMode(dir().hashMode());
        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), newDir.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveNonOwnedEdge(EggsTime time, rocksdb::WriteBatch& batch, const RemoveNonOwnedEdgeEntry& entry, RemoveNonOwnedEdgeResp& resp) {
        uint64_t nameHash;
        {
            // allowSnapshot=true since GC needs to be able to remove non-owned edges from snapshot dir
            EggsError err = _initiateDirectoryModificationAndHash(time, true, batch, entry.dirId, entry.name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // We check that edge is still not owned -- otherwise we might orphan a file.
        {
            StaticValue<EdgeKey> k;
            k().setDirIdWithCurrent(entry.dirId, false); // snapshot (current=false), we're deleting a non owned snapshot edge
            k().setNameHash(nameHash);
            k().setName(entry.name.ref());
            k().setCreationTime(entry.creationTime);
            std::string edgeValue;
            auto status = _db->Get({}, _edgesCf, k.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return NO_ERROR; // make the client's life easier
            }
            ROCKS_DB_CHECKED(status);
            ExternalValue<SnapshotEdgeBody> edge(edgeValue);
            if (edge().targetIdWithOwned().extra()) {
                // TODO better error here?
                return EggsError::EDGE_NOT_FOUND; // unexpectedly owned
            }
            // we can go ahead and safely delete
            ROCKS_DB_CHECKED(batch.Delete(_edgesCf, k.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applySameShardHardFileUnlink(EggsTime time, rocksdb::WriteBatch& batch, const SameShardHardFileUnlinkEntry& entry, SameShardHardFileUnlinkResp& resp) {
        // fetch the file
        std::string fileValue;
        ExternalValue<FileBody> file;
        {
            EggsError err = _getFile({}, entry.targetId, fileValue, file);
            if (err == EggsError::FILE_NOT_FOUND) {
                // if the file is already transient, we're done
                std::string transientFileValue;
                ExternalValue<TransientFileBody> transientFile;
                EggsError err = _getTransientFile({}, time, true, entry.targetId, fileValue, transientFile);
                if (err == NO_ERROR) {
                    return NO_ERROR;
                } else if (err == EggsError::FILE_NOT_FOUND) {
                    return EggsError::FILE_NOT_FOUND;
                } else {
                    return err;
                }
            } else if (err != NO_ERROR) {
                return err;
            }
        }

        // fetch dir, compute hash
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnapshot=true since GC needs to be able to do this in snapshot dirs
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.ownerId, dirValue, dir);
            nameHash = computeHash(dir().hashMode(), entry.name.ref());
        }

        // We need to check that the edge is still there, and that it still owns the
        // file. Maybe the file was re-owned by someone else in the meantime, in which case
        // we can't proceed making the file transient.
        {
            StaticValue<EdgeKey> k;
            // current=false since we can only delete
            k().setDirIdWithCurrent(entry.ownerId, false);
            k().setNameHash(nameHash);
            k().setName(entry.name.ref());
            k().setCreationTime(entry.creationTime);
            std::string edgeValue;
            auto status = _db->Get({}, _edgesCf, k.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::EDGE_NOT_FOUND; // can't return NO_ERROR, since the transient file still exists
            }
            ROCKS_DB_CHECKED(status);
            ExternalValue<SnapshotEdgeBody> edge(edgeValue);
            if (!edge().targetIdWithOwned().extra()) { // not owned
                return EggsError::EDGE_NOT_FOUND;
            }
            // we can proceed
            ROCKS_DB_CHECKED(batch.Delete(_edgesCf, k.toSlice()));
        }

        // make file transient
        {
            auto k = InodeIdKey::Static(entry.targetId);
            ROCKS_DB_CHECKED(batch.Delete(_filesCf, k.toSlice()));
            StaticValue<TransientFileBody> v;
            v().setVersion(0);
            v().setFileSize(file().fileSize());
            v().setMtime(time);
            v().setDeadline(time + _transientDeadlineInterval);
            v().setLastSpanState(SpanState::CLEAN);
            v().setNoteDangerous(entry.name.ref());
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), v.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveSpanInitiate(EggsTime time, rocksdb::WriteBatch& batch, const RemoveSpanInitiateEntry& entry, RemoveSpanInitiateResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, true, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // Exit early if file is empty. Crucial to do this with the size,
        // otherwise we might spend a lot of time poring through the SSTs
        // making sure there are no spans.
        if (file().fileSize() == 0) {
            LOG_DEBUG(_env, "exiting early from remove span since file is empty");
            return EggsError::FILE_EMPTY;
        }

        LOG_DEBUG(_env, "deleting span from file %s of size %s", entry.fileId, file().fileSize());

        // Fetch the last span
        std::unique_ptr<rocksdb::Iterator> spanIt(_db->NewIterator({}, _spansCf));
        ExternalValue<SpanKey> spanKey;
        ExternalValue<SpanBody> span;
        {
            StaticValue<SpanKey> endKey;
            endKey().setFileId(entry.fileId);
            endKey().setOffset(file().fileSize());
            // TODO apply iteration bound
            spanIt->SeekForPrev(endKey.toSlice());
            ROCKS_DB_CHECKED(spanIt->status());
            ALWAYS_ASSERT(spanIt->Valid()); // we know the file isn't empty, we must have a span
            spanKey = ExternalValue<SpanKey>::FromSlice(spanIt->key());
            ALWAYS_ASSERT(spanKey().fileId() == entry.fileId); // again, we know the file isn't empty
            span = ExternalValue<SpanBody>::FromSlice(spanIt->value());
        }
        ALWAYS_ASSERT(span().storageClass() != EMPTY_STORAGE);
        resp.byteOffset = spanKey().offset();

        // If the span is blockless, the only thing we need to to do is remove it
        if (span().storageClass() == INLINE_STORAGE) {
            ROCKS_DB_CHECKED(batch.Delete(_spansCf, spanKey.toSlice()));
            file().setFileSize(spanKey().offset());
            {
                auto k = InodeIdKey::Static(entry.fileId);
                ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
            }
            return NO_ERROR;
        }
        const auto blocks = span().blocksBody();

        // Otherwise, we need to condemn it first, and then certify the deletion.
        // Note that we allow to remove dirty spans -- this is important to deal well with
        // the case where a writer dies in the middle of adding a span.
        file().setLastSpanState(SpanState::CONDEMNED);
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // Fill in the response blocks
        {
            resp.blocks.els.reserve(blocks.parity().blocks());
            BlockServicesCache inMemoryBlockServicesData = _blockServicesCache.getCache();
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                const auto block = blocks.block(i);
                const auto& cache = inMemoryBlockServicesData.blockServices.at(block.blockService().u64);
                auto& respBlock = resp.blocks.els.emplace_back();
                respBlock.blockServiceIp1 = cache.ip1;
                respBlock.blockServicePort1 = cache.port1;
                respBlock.blockServiceIp2 = cache.ip2;
                respBlock.blockServicePort2 = cache.port2;
                respBlock.blockServiceId = block.blockService();
                respBlock.blockId = block.blockId();
                respBlock.blockServiceFlags = cache.flags;
                respBlock.certificate = _blockEraseCertificate(blocks.cellSize()*blocks.stripes(), block, cache.secretKey);
            }
        }

        return NO_ERROR;
    }

    uint64_t _getNextBlockId() {
        std::string v;
        ROCKS_DB_CHECKED(_db->Get({}, _defaultCf, shardMetadataKey(&NEXT_BLOCK_ID_KEY), &v));
        return ExternalValue<U64Value>(v)().u64();
    }

    uint64_t _updateNextBlockId(EggsTime time, uint64_t& nextBlockId) {
        // time is embedded into the id, other than LSB which is shard
        nextBlockId = std::max<uint64_t>(nextBlockId + 0x100, _shid.u8 | (time.ns & ~0xFFull));
        return nextBlockId;
    }

    void _writeNextBlockId(rocksdb::WriteBatch& batch, uint64_t nextBlockId) {
        StaticValue<U64Value> v;
        v().setU64(nextBlockId);
        ROCKS_DB_CHECKED(batch.Put(_defaultCf, shardMetadataKey(&NEXT_BLOCK_ID_KEY), v.toSlice()));
    }

    void _fillInAddSpanInitiate(const SpanBlocksBody blocks, AddSpanInitiateResp& resp) {
        resp.blocks.els.reserve(blocks.parity().blocks());
        BlockBody block;
        auto inMemoryBlockServiceData = _blockServicesCache.getCache();
        for (int i = 0; i < blocks.parity().blocks(); i++) {
            const BlockBody block = blocks.block(i);
            auto& respBlock = resp.blocks.els.emplace_back();
            respBlock.blockServiceId = block.blockService();
            respBlock.blockId = block.blockId();
            const auto& cache = inMemoryBlockServiceData.blockServices.at(block.blockService().u64);
            respBlock.blockServiceIp1 = cache.ip1;
            respBlock.blockServicePort1 = cache.port1;
            respBlock.blockServiceIp2 = cache.ip2;
            respBlock.blockServicePort2 = cache.port2;
            respBlock.blockServiceFailureDomain.name.data = cache.failureDomain;
            respBlock.certificate.data = _blockWriteCertificate(blocks.cellSize()*blocks.stripes(), block, cache.secretKey);
        }
    }

    void _addBlockServicesToFiles(rocksdb::WriteBatch& batch, BlockServiceId blockServiceId, InodeId fileId, int64_t delta) {
        StaticValue<BlockServiceToFileKey> k;
        k().setBlockServiceId(blockServiceId);
        k().setFileId(fileId);
        LOG_DEBUG(_env, "Adding %s to block services to file entry with block service %s, file %s", delta, blockServiceId, fileId);
        StaticValue<I64Value> v;
        v().setI64(delta);
        ROCKS_DB_CHECKED(batch.Merge(_blockServicesToFilesCf, k.toSlice(), v.toSlice()));
    }

    EggsError _applyAddInlineSpan(EggsTime time, rocksdb::WriteBatch& batch, const AddInlineSpanEntry& entry, AddInlineSpanResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, false, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // Special case -- for empty spans we have nothing to do
        if (entry.body.size() == 0) {
            return NO_ERROR;
        }

        StaticValue<SpanKey> spanKey;
        spanKey().setFileId(entry.fileId);
        spanKey().setOffset(entry.byteOffset);

        // Check that the file is where where we expect it
        if (file().fileSize() != entry.byteOffset) {
            // Special case: we're trying to add the same span again. This is acceptable
            // in the name of idempotency, but we return the blocks we've previously
            // computed.
            //
            // We _must_ return the blocks we've first returned, and ignored whatever
            // comes next, even if a new blacklist might have influenced the decision.
            //
            // This is since we need to be certain that we don't leak blocks. So either
            // the client needs to migrate the blocks, or just throw away the file.
            if (file().fileSize() == entry.byteOffset+entry.size) {
                std::string spanValue;
                auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
                if (status.IsNotFound()) {
                    LOG_DEBUG(_env, "file size does not match, but could not find existing span");
                    return EggsError::SPAN_NOT_FOUND;
                }
                ROCKS_DB_CHECKED(status);
                ExternalValue<SpanBody> existingSpan(spanValue);
                if (
                    existingSpan().spanSize() != entry.size ||
                    existingSpan().storageClass() != INLINE_STORAGE ||
                    existingSpan().crc() != entry.crc ||
                    existingSpan().inlineBody() != entry.body
                ) {
                    LOG_DEBUG(_env, "file size does not match, and existing span does not match");
                    return EggsError::SPAN_NOT_FOUND;
                }
                return NO_ERROR;
            }
            LOG_DEBUG(_env, "expecting file size %s, but got %s, returning span not found", entry.byteOffset, file().fileSize());
            return EggsError::SPAN_NOT_FOUND;
        }

        // We're actually adding a new span -- the span state must be clean.
        if (file().lastSpanState() != SpanState::CLEAN) {
            return EggsError::LAST_SPAN_STATE_NOT_CLEAN;
        }

        // Update the file with the new file size, no need to set the thing to dirty since it's inline
        file().setFileSize(entry.byteOffset+entry.size);
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // Now manufacture and add the span
        OwnedValue<SpanBody> spanBody(entry.body.ref());
        {
            spanBody().setVersion(0);
            spanBody().setSpanSize(entry.size);
            spanBody().setCrc(entry.crc.u32);
            spanBody().setStorageClass(entry.storageClass);
            ROCKS_DB_CHECKED(batch.Put(_spansCf, spanKey.toSlice(), spanBody.toSlice()));
        }

        return NO_ERROR;

    }

    EggsError _applyAddSpanInitiate(EggsTime time, rocksdb::WriteBatch& batch, const AddSpanInitiateEntry& entry, AddSpanInitiateResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, false, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        StaticValue<SpanKey> spanKey;
        spanKey().setFileId(entry.fileId);
        spanKey().setOffset(entry.byteOffset);

        // Check that the file is where where we expect it
        if (file().fileSize() != entry.byteOffset) {
            // Special case: we're trying to add the same span again. This is acceptable
            // in the name of idempotency, but we return the blocks we've previously
            // computed.
            //
            // We _must_ return the blocks we've first returned, and ignored whatever
            // comes next, even if a new blacklist might have influenced the decision.
            //
            // This is since we need to be certain that we don't leak blocks. So either
            // the client needs to migrate the blocks, or just throw away the file.
            if (file().fileSize() == entry.byteOffset+entry.size) {
                std::string spanValue;
                auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
                if (status.IsNotFound()) {
                    LOG_DEBUG(_env, "file size does not match, but could not find existing span");
                    return EggsError::SPAN_NOT_FOUND;
                }
                ROCKS_DB_CHECKED(status);
                ExternalValue<SpanBody> existingSpan(spanValue);
                if (
                    existingSpan().spanSize() != entry.size ||
                    existingSpan().storageClass() == INLINE_STORAGE ||
                    existingSpan().crc() != entry.crc ||
                    existingSpan().blocksBody().cellSize() != entry.cellSize ||
                    existingSpan().blocksBody().stripes() != entry.stripes ||
                    existingSpan().blocksBody().parity() != entry.parity
                ) {
                    LOG_DEBUG(_env, "file size does not match, and existing span does not match");
                    return EggsError::SPAN_NOT_FOUND;
                }
                _fillInAddSpanInitiate(existingSpan().blocksBody(), resp);
                return NO_ERROR;
            }
            LOG_DEBUG(_env, "expecting file size %s, but got %s, returning span not found", entry.byteOffset, file().fileSize());
            return EggsError::SPAN_NOT_FOUND;
        }

        // We're actually adding a new span -- the span state must be clean.
        if (file().lastSpanState() != SpanState::CLEAN) {
            return EggsError::LAST_SPAN_STATE_NOT_CLEAN;
        }

        // Update the file with the new file size and set the last span state to dirty
        file().setFileSize(entry.byteOffset+entry.size);
        file().setLastSpanState(SpanState::DIRTY);
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // Now manufacture and add the span, also recording the blocks
        // in the block service -> files index.
        OwnedValue<SpanBody> spanBody(entry.storageClass, entry.parity, entry.stripes);
        {
            spanBody().setVersion(0);
            spanBody().setSpanSize(entry.size);
            spanBody().setCrc(entry.crc.u32);
            spanBody().setStorageClass(entry.storageClass);
            auto blocks = spanBody().blocksBody();
            blocks.setParity(entry.parity);
            blocks.setStripes(entry.stripes);
            blocks.setCellSize(entry.cellSize);
            uint64_t nextBlockId = _getNextBlockId();
            for (int i = 0; i < entry.parity.blocks(); i++) {
                const auto& entryBlock = entry.bodyBlocks.els[i];
                auto block = blocks.block(i);
                block.setBlockId(_updateNextBlockId(time, nextBlockId));
                block.setBlockService(entryBlock.blockServiceId.u64);
                block.setCrc(entryBlock.crc.u32);
                _addBlockServicesToFiles(batch, entryBlock.blockServiceId.u64, entry.fileId, 1);
            }
            _writeNextBlockId(batch, nextBlockId);
            for (int i = 0; i < entry.stripes; i++) {
                blocks.setStripeCrc(i, entry.bodyStripes.els[i].u32);
            }
            ROCKS_DB_CHECKED(batch.Put(_spansCf, spanKey.toSlice(), spanBody.toSlice()));
        }

        // Fill in the response
        _fillInAddSpanInitiate(spanBody().blocksBody(), resp);

        return NO_ERROR;
    }

    std::array<uint8_t, 8> _blockWriteCertificate(uint32_t blockSize, const BlockBody block, const AES128Key& secretKey) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ4sI', b, 0, block['block_service_id'], b'w', block['block_id'], crc32_from_int(block['crc32']), block_size)
        bbuf.packScalar<uint64_t>(block.blockService().u64);
        bbuf.packScalar<char>('w');
        bbuf.packScalar<uint64_t>(block.blockId());
        bbuf.packScalar<uint32_t>(block.crc());
        bbuf.packScalar<uint32_t>(blockSize);

        return cbcmac(secretKey, (uint8_t*)buf, sizeof(buf));
    }

    bool _checkBlockAddProof(const BlockServicesCache& inMemoryBlockServiceData, BlockServiceId blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
        bbuf.packScalar<uint64_t>(blockServiceId.u64);
        bbuf.packScalar<char>('W');
        bbuf.packScalar<uint64_t>(proof.blockId);

        const auto& cache = inMemoryBlockServiceData.blockServices.at(blockServiceId.u64);
        auto expectedProof = cbcmac(cache.secretKey, (uint8_t*)buf, sizeof(buf));

        bool good = proof.proof == expectedProof;

        if (!good) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "bad block write proof for block service id %s, expected %s, got %s", blockServiceId, BincodeFixedBytes<8>(expectedProof), proof);
        }

        return good;
    }

    std::array<uint8_t, 8> _blockEraseCertificate(uint32_t blockSize, const BlockBody block, const AES128Key& secretKey) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'e', block['block_id'])
        bbuf.packScalar<uint64_t>(block.blockService().u64);
        bbuf.packScalar<char>('e');
        bbuf.packScalar<uint64_t>(block.blockId());

        return cbcmac(secretKey, (uint8_t*)buf, sizeof(buf));
    }

    bool _checkBlockDeleteProof(const BlockServicesCache& inMemoryBlockServiceData, InodeId fileId, BlockServiceId blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'E', block['block_id'])
        bbuf.packScalar<uint64_t>(blockServiceId.u64);
        bbuf.packScalar<char>('E');
        bbuf.packScalar<uint64_t>(proof.blockId);

        const auto& cache = inMemoryBlockServiceData.blockServices.at(blockServiceId.u64);
        auto expectedProof = cbcmac(cache.secretKey, (uint8_t*)buf, sizeof(buf));

        bool good = proof.proof == expectedProof;
        if (!good) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "Bad block delete proof for file %s, block service id %s, expected %s, got %s", fileId, blockServiceId, BincodeFixedBytes<8>(expectedProof), BincodeFixedBytes<8>(proof.proof));
        }
        return good;
    }

    EggsError _applyAddSpanCertify(EggsTime time, rocksdb::WriteBatch& batch, const AddSpanCertifyEntry& entry, AddSpanCertifyResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, false, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        StaticValue<SpanKey> spanKey;
        spanKey().setFileId(entry.fileId);
        spanKey().setOffset(entry.byteOffset);

        // Make sure the existing span exists, exit early if we're already done with
        // it, verify the proofs.
        {
            std::string spanValue;
            auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
            if (status.IsNotFound()) {
                return EggsError::SPAN_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
            ExternalValue<SpanBody> span(spanValue);
            // "Is the span still there"
            if (file().fileSize() > entry.byteOffset+span().spanSize()) {
                return NO_ERROR; // already certified (we're past it)
            }
            if (file().lastSpanState() == SpanState::CLEAN) {
                return NO_ERROR; // already certified
            }
            if (file().lastSpanState() == SpanState::CONDEMNED) {
                return EggsError::SPAN_NOT_FOUND; // we could probably have a better error here
            }
            ALWAYS_ASSERT(file().lastSpanState() == SpanState::DIRTY);
            // Now verify the proofs
            if (span().storageClass() == INLINE_STORAGE) {
                return EggsError::CANNOT_CERTIFY_BLOCKLESS_SPAN;
            }
            auto blocks = span().blocksBody();
            if (blocks.parity().blocks() != entry.proofs.els.size()) {
                return EggsError::BAD_NUMBER_OF_BLOCKS_PROOFS;
            }
            auto inMemoryBlockServiceData = _blockServicesCache.getCache();
            BlockBody block;
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                auto block = blocks.block(i);
                if (!_checkBlockAddProof(inMemoryBlockServiceData, block.blockService(), entry.proofs.els[i])) {
                    return EggsError::BAD_BLOCK_PROOF;
                }
            }
        }

        // Okay, now we can update the file to mark the last span as clean
        file().setLastSpanState(SpanState::CLEAN);
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // We're done.
        return NO_ERROR;
    }

    EggsError _applyMakeFileTransient(EggsTime time, rocksdb::WriteBatch& batch, const MakeFileTransientEntry& entry, MakeFileTransientResp& resp) {
        std::string fileValue;
        ExternalValue<FileBody> file;
        {
            EggsError err = _getFile({}, entry.id, fileValue, file);
            if (err == EggsError::FILE_NOT_FOUND) {
                // if it's already transient, we're done
                std::string transientFileValue;
                ExternalValue<TransientFileBody> transientFile;
                EggsError err = _getTransientFile({}, time, true, entry.id, transientFileValue, transientFile);
                if (err == NO_ERROR) {
                    return NO_ERROR;
                }
            }
            if (err != NO_ERROR) {
                return err;
            }
        }

        // delete the file
        auto k = InodeIdKey::Static(entry.id);
        ROCKS_DB_CHECKED(batch.Delete(_filesCf, k.toSlice()));

        // make a transient one
        StaticValue<TransientFileBody> transientFile;
        transientFile().setVersion(0);
        transientFile().setFileSize(file().fileSize());
        transientFile().setMtime(time);
        transientFile().setDeadline(time + _transientDeadlineInterval);
        transientFile().setLastSpanState(SpanState::CLEAN);
        transientFile().setNoteDangerous(entry.note.ref());
        ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), transientFile.toSlice()));

        return NO_ERROR;
    }

    EggsError _applyRemoveSpanCertify(EggsTime time, rocksdb::WriteBatch& batch, const RemoveSpanCertifyEntry& entry, RemoveSpanCertifyResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, true, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // Fetch span
        StaticValue<SpanKey> spanKey;
        spanKey().setFileId(entry.fileId);
        spanKey().setOffset(entry.byteOffset);
        std::string spanValue;
        ExternalValue<SpanBody> span;
        {
            auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
            if (status.IsNotFound()) {
                LOG_DEBUG(_env, "skipping removal of span for file %s, offset %s, since we're already done", entry.fileId, entry.byteOffset);
                return NO_ERROR; // already done
            }
            ROCKS_DB_CHECKED(status);
            span = ExternalValue<SpanBody>(spanValue);
        }

        ALWAYS_ASSERT(span().storageClass() != EMPTY_STORAGE);
        if (span().storageClass() == INLINE_STORAGE) {
            return EggsError::CANNOT_CERTIFY_BLOCKLESS_SPAN;
        }
        auto blocks = span().blocksBody();

        // Make sure we're condemned
        if (file().lastSpanState() != SpanState::CONDEMNED) {
            return EggsError::SPAN_NOT_FOUND; // TODO maybe better error?
        }

        // Verify proofs
        if (entry.proofs.els.size() != blocks.parity().blocks()) {
            return EggsError::BAD_NUMBER_OF_BLOCKS_PROOFS;
        }
        {
            auto inMemoryBlockServiceData = _blockServicesCache.getCache();
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                const auto block = blocks.block(i);
                const auto& proof = entry.proofs.els[i];
                if (block.blockId() != proof.blockId) {
                    RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "bad block proof id for file %s, expected %s, got %s", entry.fileId, block.blockId(), proof.blockId);
                    return EggsError::BAD_BLOCK_PROOF;
                }
                if (!_checkBlockDeleteProof(inMemoryBlockServiceData, entry.fileId, block.blockService(), proof)) {
                    return EggsError::BAD_BLOCK_PROOF;
                }
                // record balance change in block service to files
                _addBlockServicesToFiles(batch, block.blockService(), entry.fileId, -1);
            }
        }

        // Delete span, set new size, and go back to clean state
        LOG_DEBUG(_env, "deleting span for file %s, at offset %s", entry.fileId, entry.byteOffset);
        ROCKS_DB_CHECKED(batch.Delete(_spansCf, spanKey.toSlice()));
        {
            auto k = InodeIdKey::Static(entry.fileId);
            file().setLastSpanState(SpanState::CLEAN);
            file().setFileSize(spanKey().offset());
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveOwnedSnapshotFileEdge(EggsTime time, rocksdb::WriteBatch& batch, const RemoveOwnedSnapshotFileEdgeEntry& entry, RemoveOwnedSnapshotFileEdgeResp& resp) {
        uint64_t nameHash;
        {
            // the GC needs to work on deleted dirs who might still have owned files, so allowSnapshot=true
            EggsError err = _initiateDirectoryModificationAndHash(time, true, batch, entry.ownerId, entry.name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        {
            StaticValue<EdgeKey> edgeKey;
            edgeKey().setDirIdWithCurrent(entry.ownerId, false); // snapshot (current=false)
            edgeKey().setNameHash(nameHash);
            edgeKey().setName(entry.name.ref());
            edgeKey().setCreationTime(entry.creationTime);
            ROCKS_DB_CHECKED(batch.Delete(_edgesCf, edgeKey.toSlice()));
        }

        return NO_ERROR;
    }

    bool _fetchSpan(InodeId fileId, uint64_t byteOffset, StaticValue<SpanKey>& spanKey, std::string& spanValue, ExternalValue<SpanBody>& span) {
        spanKey().setFileId(fileId);
        spanKey().setOffset(byteOffset);
        auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
        if (status.IsNotFound()) {
            LOG_DEBUG(_env, "could not find span at offset %s in file %s", byteOffset, fileId);
            return false;
        }
        ROCKS_DB_CHECKED(status);
        span = ExternalValue<SpanBody>(spanValue);
        return true;
    }

    SpanState _fetchSpanState(EggsTime time, InodeId fileId, uint64_t spanEnd) {
        // See if it's a normal file first
        std::string fileValue;
        ExternalValue<FileBody> file;
        auto err = _getFile({}, fileId, fileValue, file);
        ALWAYS_ASSERT(err == NO_ERROR || err == EggsError::FILE_NOT_FOUND);
        if (err == NO_ERROR) {
            return SpanState::CLEAN;
        }
        // couldn't find normal file, must be transient
        ExternalValue<TransientFileBody> transientFile;
        err = _getTransientFile({}, time, true, fileId, fileValue, transientFile);
        ALWAYS_ASSERT(err == NO_ERROR);
        if (spanEnd == transientFile().fileSize()) {
            return transientFile().lastSpanState();
        } else {
            return SpanState::CLEAN;
        }
    }

    EggsError _applySwapBlocks(EggsTime time, rocksdb::WriteBatch& batch, const SwapBlocksEntry& entry, SwapBlocksResp& resp) {
        // Fetch spans
        StaticValue<SpanKey> span1Key;
        std::string span1Value;
        ExternalValue<SpanBody> span1;
        if (!_fetchSpan(entry.fileId1, entry.byteOffset1, span1Key, span1Value, span1)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        StaticValue<SpanKey> span2Key;
        std::string span2Value;
        ExternalValue<SpanBody> span2;
        if (!_fetchSpan(entry.fileId2, entry.byteOffset2, span2Key, span2Value, span2)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        if (span1().storageClass() == INLINE_STORAGE || span2().storageClass() == INLINE_STORAGE) {
            return EggsError::SWAP_BLOCKS_INLINE_STORAGE;
        }
        auto blocks1 = span1().blocksBody();
        auto blocks2 = span2().blocksBody();
        uint32_t blockSize1 = blocks1.cellSize()*blocks1.stripes();
        uint32_t blockSize2 = blocks2.cellSize()*blocks2.stripes();
        if (blockSize1 != blockSize2) {
            return EggsError::SWAP_BLOCKS_MISMATCHING_SIZE;
        }
        // Fetch span state
        auto state1 = _fetchSpanState(time, entry.fileId1, entry.byteOffset1 + span1().size());
        auto state2 = _fetchSpanState(time, entry.fileId2, entry.byteOffset2 + span2().size());
        // We don't want to put not-certified blocks in clean spans, or similar
        if (state1 != state2) {
            return EggsError::SWAP_BLOCKS_MISMATCHING_STATE;
        }
        // Find blocks
        const auto findBlock = [](const SpanBlocksBody blocks, uint64_t blockId, BlockBody& block) -> int {
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                block = blocks.block(i);
                if (block.blockId() == blockId) {
                    return i;
                }
            }
            return -1;
        };
        BlockBody block1;
        int block1Ix = findBlock(blocks1, entry.blockId1, block1);
        BlockBody block2;
        int block2Ix = findBlock(blocks2, entry.blockId2, block2);
        if (block1Ix < 0 || block2Ix < 0) {
            // if neither are found, check if we haven't swapped already, for idempotency
            if (block1Ix < 0 && block2Ix < 0) {
                if (findBlock(blocks1, entry.blockId2, block1) >= 0 && findBlock(blocks2, entry.blockId1, block2) >= 0) {
                    return NO_ERROR;
                }
            }
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "blocks not found when swapping blocks, are you running two migrations at once? fileId1=%s offset1=%s block1=%s fileId2=%s offset2=%s block2=%s", entry.fileId1, entry.byteOffset1, entry.blockId1, entry.fileId2, entry.byteOffset2, entry.blockId2);
            return EggsError::BLOCK_NOT_FOUND;
        }
        if (block1.crc() != block2.crc()) {
            return EggsError::SWAP_BLOCKS_MISMATCHING_CRC;
        }
        // Check that we're not creating a situation where we have two blocks in the same block service
        const auto checkNoDuplicateBlockServices = [](const auto blocks, int blockToBeReplacedIx, const auto newBlock) {
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                if (i == blockToBeReplacedIx) {
                    continue;
                }
                const auto block = blocks.block(i);
                if (block.blockService() == newBlock.blockService()) {
                    return EggsError::SWAP_BLOCKS_DUPLICATE_BLOCK_SERVICE;
                }
            }
            return NO_ERROR;
        };
        {
            EggsError err = checkNoDuplicateBlockServices(blocks1, block1Ix, block2);
            if (err != NO_ERROR) {
                return err;
            }
            err = checkNoDuplicateBlockServices(blocks2, block2Ix, block1);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // Record the block counts
        _addBlockServicesToFiles(batch, block1.blockService(), entry.fileId1, -1);
        _addBlockServicesToFiles(batch, block2.blockService(), entry.fileId1, +1);
        _addBlockServicesToFiles(batch, block1.blockService(), entry.fileId2, +1);
        _addBlockServicesToFiles(batch, block2.blockService(), entry.fileId2, -1);
        // Finally, swap the blocks
        std::vector<char> tmp(decltype(block1)::SIZE);
        memcpy(tmp.data(), block1._data, decltype(block1)::SIZE);
        memcpy(block1._data, block2._data, decltype(block1)::SIZE);
        memcpy(block2._data, tmp.data(), decltype(block1)::SIZE);
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span1Key.toSlice(), span1.toSlice()));
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span2Key.toSlice(), span2.toSlice()));
        return NO_ERROR;
    }

    EggsError _applyMoveSpan(EggsTime time, rocksdb::WriteBatch& batch, const MoveSpanEntry& entry, MoveSpanResp& resp) {
        // fetch files
        std::string transientValue1;
        ExternalValue<TransientFileBody> transientFile1;
        {
            EggsError err = _initiateTransientFileModification(time, true, batch, entry.fileId1, transientValue1, transientFile1);
            if (err != NO_ERROR) {
                return err;
            }
        }
        std::string transientValue2;
        ExternalValue<TransientFileBody> transientFile2;
        {
            EggsError err = _initiateTransientFileModification(time, true, batch, entry.fileId2, transientValue2, transientFile2);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // We might have already moved this, need to be idempotent.
        LOG_DEBUG(_env, "movespan: spanSize=%s offset1=%s offset2=%s", entry.spanSize, entry.byteOffset1, entry.byteOffset2);
        LOG_DEBUG(_env, "movespan: size1=%s state1=%s size2=%s state2=%s", transientFile1().fileSize(), transientFile1().lastSpanState(), transientFile2().fileSize(), transientFile2().lastSpanState());
        if (
            transientFile1().fileSize() == entry.byteOffset1 && transientFile1().lastSpanState() == SpanState::CLEAN &&
            transientFile2().fileSize() == entry.byteOffset2 + entry.spanSize && transientFile2().lastSpanState() == SpanState::DIRTY
        ) {
            return NO_ERROR;
        }
        if (
            transientFile1().lastSpanState() != SpanState::DIRTY ||
            transientFile1().fileSize() != entry.byteOffset1 + entry.spanSize ||
            transientFile2().lastSpanState() != SpanState::CLEAN ||
            transientFile2().fileSize() != entry.byteOffset2
        ) {
            LOG_DEBUG(_env, "span not found because of offset checks");
            return EggsError::SPAN_NOT_FOUND; // TODO better error?
        }
        // fetch span to move
        StaticValue<SpanKey> spanKey;
        std::string spanValue;
        spanKey().setFileId(entry.fileId1);
        spanKey().setOffset(entry.byteOffset1);
        auto status = _db->Get({}, _spansCf, spanKey.toSlice(), &spanValue);
        if (status.IsNotFound()) {
            LOG_DEBUG(_env, "span not found in db (this should probably never happen)");
            return EggsError::SPAN_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        ExternalValue<SpanBody> span(spanValue);
        ExternalValue<SpanBody> spanBody(spanValue);
        if (spanBody().spanSize() != entry.spanSize) {
            LOG_DEBUG(_env, "span not found because of differing sizes");
            return EggsError::SPAN_NOT_FOUND; // TODO better error
        }
        // move span
        ROCKS_DB_CHECKED(batch.Delete(_spansCf, spanKey.toSlice()));
        spanKey().setFileId(entry.fileId2);
        spanKey().setOffset(entry.byteOffset2);
        ROCKS_DB_CHECKED(batch.Put(_spansCf, spanKey.toSlice(), span.toSlice()));
        // change size and dirtiness
        transientFile1().setFileSize(transientFile1().fileSize() - span().spanSize());
        transientFile1().setLastSpanState(SpanState::CLEAN);
        {
            auto k = InodeIdKey::Static(entry.fileId1);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), transientFile1.toSlice()));
        }
        transientFile2().setFileSize(transientFile2().fileSize() + span().spanSize());
        transientFile2().setLastSpanState(SpanState::DIRTY);
        {
            auto k = InodeIdKey::Static(entry.fileId2);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), transientFile2.toSlice()));
        }
        // record block count changes
        auto blocksBody = span().blocksBody();
        for (int i = 0; i < blocksBody.parity().blocks(); i++) {
            auto block = blocksBody.block(i);
            _addBlockServicesToFiles(batch, block.blockService(), entry.fileId1, -1);
            _addBlockServicesToFiles(batch, block.blockService(), entry.fileId2, +1);
        }
        // we're done
        return NO_ERROR;
    }

    EggsError _applySwapSpans(EggsTime time, rocksdb::WriteBatch& batch, const SwapSpansEntry& entry, SwapSpansResp& resp) {
        StaticValue<SpanKey> span1Key;
        std::string span1Value;
        ExternalValue<SpanBody> span1;
        if (!_fetchSpan(entry.fileId1, entry.byteOffset1, span1Key, span1Value, span1)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        StaticValue<SpanKey> span2Key;
        std::string span2Value;
        ExternalValue<SpanBody> span2;
        if (!_fetchSpan(entry.fileId2, entry.byteOffset2, span2Key, span2Value, span2)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        if (span1().storageClass() == INLINE_STORAGE || span2().storageClass() == INLINE_STORAGE) {
            return EggsError::SWAP_SPANS_INLINE_STORAGE;
        }
        auto blocks1 = span1().blocksBody();
        auto blocks2 = span2().blocksBody();
        // check that size and crc is the same
        if (span1().spanSize() != span2().spanSize()) {
            return EggsError::SWAP_SPANS_MISMATCHING_SIZE;
        }
        if (span1().crc() != span2().crc()) {
            return EggsError::SWAP_SPANS_MISMATCHING_CRC;
        }
        // Fetch span state
        auto state1 = _fetchSpanState(time, entry.fileId1, entry.byteOffset1 + span1().size());
        auto state2 = _fetchSpanState(time, entry.fileId2, entry.byteOffset2 + span2().size());
        if (state1 != SpanState::CLEAN || state2 != SpanState::CLEAN) {
            return EggsError::SWAP_SPANS_NOT_CLEAN;
        }
        // check if we've already swapped
        const auto blocksMatch = [](const SpanBlocksBody span, const BincodeList<uint64_t>& blocks) {
            if (span.parity().blocks() != blocks.els.size()) { return false; }
            for (int i = 0; i < blocks.els.size(); i++) {
                if (span.block(i).blockId() != blocks.els[i]) { return false; }
            }
            return true;
        };
        if (blocksMatch(blocks1, entry.blocks2) && blocksMatch(blocks2, entry.blocks1)) {
            return NO_ERROR; // we're already done
        }
        if (!(blocksMatch(blocks1, entry.blocks1) && blocksMatch(blocks2, entry.blocks2))) {
            return EggsError::SWAP_SPANS_MISMATCHING_BLOCKS;
        }
        // we're ready to swap, first do the blocks bookkeeping
        const auto adjustBlockServices = [this, &batch](const SpanBlocksBody blocks, InodeId addTo, InodeId subtractFrom) {
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                const auto block = blocks.block(i);
                _addBlockServicesToFiles(batch, block.blockService(), addTo, +1);
                _addBlockServicesToFiles(batch, block.blockService(), subtractFrom, -1);
            }
        };
        adjustBlockServices(blocks1, entry.fileId2, entry.fileId1);
        adjustBlockServices(blocks2, entry.fileId1, entry.fileId2);
        // now do the swap
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span1Key.toSlice(), span2.toSlice()));
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span2Key.toSlice(), span1.toSlice()));
        return NO_ERROR;
    }

    EggsError _applySetTime(EggsTime time, rocksdb::WriteBatch& batch, const SetTimeEntry& entry, SetTimeResp& resp) {
        std::string fileValue;
        ExternalValue<FileBody> file;
        EggsError err = _getFile({}, entry.id, fileValue, file);
        if (err != NO_ERROR) {
            return err;
        }
        const auto set = [&file](uint64_t entryT, void (FileBody::*setTime)(EggsTime t)) {
            if (entryT & (1ull<<63)) {
                EggsTime t = entryT & ~(1ull<<63);
                (file().*setTime)(t);
            }
        };
        set(entry.atime, &FileBody::setAtime);
        set(entry.mtime, &FileBody::setMtime);
        {
            auto fileKey = InodeIdKey::Static(entry.id);
            ROCKS_DB_CHECKED(batch.Put(_filesCf, fileKey.toSlice(), file.toSlice()));
        }
        return NO_ERROR;
    }

    EggsError _applyRemoveZeroBlockServiceFiles(EggsTime time, rocksdb::WriteBatch& batch, const RemoveZeroBlockServiceFilesEntry& entry, RemoveZeroBlockServiceFilesResp& resp) {
        // Max number of entries we'll look at, otherwise each req will spend tons of time
        // iterating.
        int maxEntries = 1'000;

        StaticValue<BlockServiceToFileKey> beginKey;
        beginKey().setBlockServiceId(entry.startBlockService.u64);
        beginKey().setFileId(entry.startFile);

        rocksdb::ReadOptions options;
        std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _blockServicesToFilesCf));
        int i;
        resp.removed = 0;
        for (
            i = 0, it->Seek(beginKey.toSlice());
            it->Valid() && i < maxEntries;
            it->Next(), i++
        ) {
            auto key = ExternalValue<BlockServiceToFileKey>::FromSlice(it->key());
            int64_t blocks = ExternalValue<I64Value>::FromSlice(it->value())().i64();
            if (blocks == 0) {
                LOG_DEBUG(_env, "Removing block service id %s file %s", key().blockServiceId(), key().fileId());
                ROCKS_DB_CHECKED(batch.Delete(_blockServicesToFilesCf, it->key()));
                resp.removed++;
            }
        }
        if (it->Valid()) {
            LOG_DEBUG(_env, "Not done removing zero block service files");
            auto key = ExternalValue<BlockServiceToFileKey>::FromSlice(it->key());
            resp.nextBlockService = key().blockServiceId();
            resp.nextFile = key().fileId();
        } else {
            ROCKS_DB_CHECKED(it->status());
            LOG_DEBUG(_env, "Done removing zero block service files");
            resp.nextBlockService = 0;
            resp.nextFile = NULL_INODE_ID;
        }
        return NO_ERROR;
    }

    EggsError applyLogEntry(uint64_t logIndex, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        // TODO figure out the story with what regards time monotonicity (possibly drop non-monotonic log
        // updates?)

        LOG_DEBUG(_env, "applying log at index %s", logIndex);
        auto locked = _applyLogEntryLock.lock();
        resp.clear();
        EggsError err = NO_ERROR;

        rocksdb::WriteBatch batch;
        _advanceLastAppliedLogEntry(batch, logIndex);
        // We set this savepoint since we still want to record the log index advancement
        // even if the application does _not_ go through.
        //
        // This gives us the freedom to write freely in the specific _apply* functions below,
        // and fail midway, without worrying that we could write an inconsistent state, since
        // it's all rolled back.
        batch.SetSavePoint();

        std::string entryScratch;
        EggsTime time = logEntry.time;
        const auto& logEntryBody = logEntry.body;

        LOG_TRACE(_env, "about to apply log entry %s", logEntryBody);

        switch (logEntryBody.kind()) {
        case ShardLogEntryKind::CONSTRUCT_FILE:
            err = _applyConstructFile(batch, time, logEntryBody.getConstructFile(), resp.setConstructFile());
            break;
        case ShardLogEntryKind::LINK_FILE:
            err = _applyLinkFile(batch, time, logEntryBody.getLinkFile(), resp.setLinkFile());
            break;
        case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
            err = _applySameDirectoryRename(time, batch, logEntryBody.getSameDirectoryRename(), resp.setSameDirectoryRename());
            break;
        case ShardLogEntryKind::SOFT_UNLINK_FILE:
            err = _applySoftUnlinkFile(time, batch, logEntryBody.getSoftUnlinkFile(), resp.setSoftUnlinkFile());
            break;
        case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
            err = _applyCreateDirectoryInode(time, batch, logEntryBody.getCreateDirectoryInode(), resp.setCreateDirectoryInode());
            break;
        case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
            err = _applyCreateLockedCurrentEdge(time, batch, logEntryBody.getCreateLockedCurrentEdge(), resp.setCreateLockedCurrentEdge());
            break;
        case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
            err = _applyUnlockCurrentEdge(time, batch, logEntryBody.getUnlockCurrentEdge(), resp.setUnlockCurrentEdge());
            break;
        case ShardLogEntryKind::LOCK_CURRENT_EDGE:
            err = _applyLockCurrentEdge(time, batch, logEntryBody.getLockCurrentEdge(), resp.setLockCurrentEdge());
            break;
        case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
            err = _applyRemoveDirectoryOwner(time, batch, logEntryBody.getRemoveDirectoryOwner(), resp.setRemoveDirectoryOwner());
            break;
        case ShardLogEntryKind::REMOVE_INODE:
            err = _applyRemoveInode(time, batch, logEntryBody.getRemoveInode(), resp.setRemoveInode());
            break;
        case ShardLogEntryKind::SET_DIRECTORY_OWNER:
            err = _applySetDirectoryOwner(time, batch, logEntryBody.getSetDirectoryOwner(), resp.setSetDirectoryOwner());
            break;
        case ShardLogEntryKind::SET_DIRECTORY_INFO:
            err = _applySetDirectoryInfo(time, batch, logEntryBody.getSetDirectoryInfo(), resp.setSetDirectoryInfo());
            break;
        case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
            err = _applyRemoveNonOwnedEdge(time, batch, logEntryBody.getRemoveNonOwnedEdge(), resp.setRemoveNonOwnedEdge());
            break;
        case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
            err = _applySameShardHardFileUnlink(time, batch, logEntryBody.getSameShardHardFileUnlink(), resp.setSameShardHardFileUnlink());
            break;
        case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
            err = _applyRemoveSpanInitiate(time, batch, logEntryBody.getRemoveSpanInitiate(), resp.setRemoveSpanInitiate());
            break;
        case ShardLogEntryKind::ADD_INLINE_SPAN:
            err = _applyAddInlineSpan(time, batch, logEntryBody.getAddInlineSpan(), resp.setAddInlineSpan());
            break;
        case ShardLogEntryKind::ADD_SPAN_INITIATE: {
            if (logEntryBody.getAddSpanInitiate().withReference) {
                auto& refResp = resp.setAddSpanInitiateWithReference();
                err = _applyAddSpanInitiate(time, batch, logEntryBody.getAddSpanInitiate(), refResp.resp);
            } else {
                err = _applyAddSpanInitiate(time, batch, logEntryBody.getAddSpanInitiate(), resp.setAddSpanInitiate());
            }
            break; }
        case ShardLogEntryKind::ADD_SPAN_CERTIFY:
            err = _applyAddSpanCertify(time, batch, logEntryBody.getAddSpanCertify(), resp.setAddSpanCertify());
            break;
        case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
            err = _applyMakeFileTransient(time, batch, logEntryBody.getMakeFileTransient(), resp.setMakeFileTransient());
            break;
        case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
            err = _applyRemoveSpanCertify(time, batch, logEntryBody.getRemoveSpanCertify(), resp.setRemoveSpanCertify());
            break;
        case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
            err = _applyRemoveOwnedSnapshotFileEdge(time, batch, logEntryBody.getRemoveOwnedSnapshotFileEdge(), resp.setRemoveOwnedSnapshotFileEdge());
            break;
        case ShardLogEntryKind::SWAP_BLOCKS:
            err = _applySwapBlocks(time, batch, logEntryBody.getSwapBlocks(), resp.setSwapBlocks());
            break;
        case ShardLogEntryKind::SWAP_SPANS:
            err = _applySwapSpans(time, batch, logEntryBody.getSwapSpans(), resp.setSwapSpans());
            break;
        case ShardLogEntryKind::MOVE_SPAN:
            err = _applyMoveSpan(time, batch, logEntryBody.getMoveSpan(), resp.setMoveSpan());
            break;
        case ShardLogEntryKind::SET_TIME:
            err = _applySetTime(time, batch, logEntryBody.getSetTime(), resp.setSetTime());
            break;
        case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
            err = _applyRemoveZeroBlockServiceFiles(time, batch, logEntryBody.getRemoveZeroBlockServiceFiles(), resp.setRemoveZeroBlockServiceFiles());
            break;
        default:
            throw EGGS_EXCEPTION("bad log entry kind %s", logEntryBody.kind());
        }

        if (err != NO_ERROR) {
            LOG_DEBUG(_env, "could not apply log entry %s, index %s, because of err %s", logEntryBody.kind(), logIndex, err);
            batch.RollbackToSavePoint();
        } else {
            LOG_DEBUG(_env, "applied log entry of kind %s, index %s, writing changes", logEntryBody.kind(), logIndex);
        }

        ROCKS_DB_CHECKED(_db->Write({}, &batch));

        return err;
    }

    // ----------------------------------------------------------------
    // miscellanea

    std::array<uint8_t, 8> _calcCookie(InodeId id) {
        return cbcmac(_expandedSecretKey, (const uint8_t*)&id, sizeof(id));
    }

    uint64_t _lastAppliedLogEntry() {
        std::string value;
        ROCKS_DB_CHECKED(_db->Get({}, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), &value));
        ExternalValue<U64Value> v(value);
        return v().u64();
    }

    EggsError _getDirectory(const rocksdb::ReadOptions& options, InodeId id, bool allowSnapshot, std::string& dirValue, ExternalValue<DirectoryBody>& dir) {
        if (unlikely(id.type() != InodeType::DIRECTORY)) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        auto k = InodeIdKey::Static(id);
        auto status = _db->Get(options, _directoriesCf, k.toSlice(), &dirValue);
        if (status.IsNotFound()) {
            return EggsError::DIRECTORY_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        auto tmpDir = ExternalValue<DirectoryBody>(dirValue);
        if (!allowSnapshot && (tmpDir().ownerId() == NULL_INODE_ID && id != ROOT_DIR_INODE_ID)) { // root dir never has an owner
            return EggsError::DIRECTORY_NOT_FOUND;
        }
        dir = tmpDir;
        return NO_ERROR;
    }

    EggsError _getDirectoryAndHash(const rocksdb::ReadOptions& options, InodeId id, bool allowSnapshot, const BincodeBytesRef& name, uint64_t& nameHash) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        EggsError err = _getDirectory(options, id, allowSnapshot, dirValue, dir);
        if (err != NO_ERROR) {
            return err;
        }
        nameHash = computeHash(dir().hashMode(), name);
        return NO_ERROR;
    }

    EggsError _getFile(const rocksdb::ReadOptions& options, InodeId id, std::string& fileValue, ExternalValue<FileBody>& file) {
        if (unlikely(id.type() != InodeType::FILE && id.type() != InodeType::SYMLINK)) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        auto k = InodeIdKey::Static(id);
        auto status = _db->Get(options, _filesCf, k.toSlice(), &fileValue);
        if (status.IsNotFound()) {
            return EggsError::FILE_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        file = ExternalValue<FileBody>(fileValue);
        return NO_ERROR;
    }

    EggsError _getTransientFile(const rocksdb::ReadOptions& options, EggsTime time, bool allowPastDeadline, InodeId id, std::string& value, ExternalValue<TransientFileBody>& file) {
        if (id.type() != InodeType::FILE && id.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        auto k = InodeIdKey::Static(id);
        auto status = _db->Get(options, _transientCf, k.toSlice(), &value);
        if (status.IsNotFound()) {
            return EggsError::FILE_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        auto tmpFile = ExternalValue<TransientFileBody>(value);
        if (!allowPastDeadline && time > tmpFile().deadline()) {
            // this should be fairly uncommon
            LOG_INFO(_env, "not picking up transient file %s since its deadline %s is past the log entry time %s", id, tmpFile().deadline(), time);
            return EggsError::FILE_NOT_FOUND;
        }
        file = tmpFile;
        return NO_ERROR;
    }

    EggsError _initiateTransientFileModification(
        EggsTime time, bool allowPastDeadline, rocksdb::WriteBatch& batch, InodeId id, std::string& tfValue, ExternalValue<TransientFileBody>& tf
    ) {
        ExternalValue<TransientFileBody> tmpTf;
        EggsError err = _getTransientFile({}, time, allowPastDeadline, id, tfValue, tmpTf);
        if (err != NO_ERROR) {
            return err;
        }

        // Like with dirs, don't go backwards in time. This is possibly less needed than
        // with directories, but still seems good hygiene.
        if (tmpTf().mtime() >= time) {
            RAISE_ALERT_APP_TYPE(_env, XmonAppType::DAYTIME, "trying to modify transient file %s going backwards in time, file mtime is %s, log entry time is %s", id, tmpTf().mtime(), time);
            return EggsError::MTIME_IS_TOO_RECENT;
        }

        tmpTf().setMtime(time);
        if (!allowPastDeadline) {
            tmpTf().setDeadline(time + _transientDeadlineInterval);
        }
        {
            auto k = InodeIdKey::Static(id);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), tmpTf.toSlice()));
        }

        tf = tmpTf;
        return NO_ERROR;
    }

    std::shared_ptr<const rocksdb::Snapshot> _getCurrentReadSnapshot() {
        return std::atomic_load(&_currentReadSnapshot);
    }

    void _updateCurrentReadSnapshot() {
        const rocksdb::Snapshot* snapshotPtr = _db->GetSnapshot();
        ALWAYS_ASSERT(snapshotPtr != nullptr);
        std::shared_ptr<const rocksdb::Snapshot> snapshot(snapshotPtr, [this](const rocksdb::Snapshot* ptr) { _db->ReleaseSnapshot(ptr); });
        std::atomic_exchange(&_currentReadSnapshot, snapshot);
    }

    void flush(bool sync) {
        ROCKS_DB_CHECKED(_db->FlushWAL(sync));
        _updateCurrentReadSnapshot();
    }
};

DirectoryInfo defaultDirectoryInfo() {
    DirectoryInfo info;
    const auto addSegment = [&info](uint8_t tag, const auto& segment) {
        char buf[255];
        BincodeBuf bbuf(buf, 255);
        segment.pack(bbuf);
        auto& entry = info.entries.els.emplace_back();
        entry.tag = tag;
        entry.body.copy(buf, bbuf.len());
    };

    // Snapshot
    SnapshotPolicy snapshot;
    // delete after 30 days
    snapshot.deleteAfterTime = (30ull /*days*/ * 24 /*hours*/ * 60 /*minutes*/ * 60 /*seconds*/ * 1'000'000'000 /*ns*/) | (1ull<<63);
    // do not delete after N versions
    snapshot.deleteAfterVersions = 0;
    addSegment(SNAPSHOT_POLICY_TAG, snapshot);

    // For reasonining of the values here, see docs/parity.md

    // Block policy: up to 2.5MB: FLASH. Up to 100MiB: HDD. This is the maximum block size.
    BlockPolicy blockPolicy;
    auto& flashBlocks = blockPolicy.entries.els.emplace_back();
    flashBlocks.minSize = 0;
    flashBlocks.storageClass = storageClassByName("FLASH");
    auto& hddBlocks = blockPolicy.entries.els.emplace_back();
    hddBlocks.minSize = 610 << 12; // roughly 2.5MB, page aligned
    hddBlocks.storageClass = storageClassByName("HDD");
    addSegment(BLOCK_POLICY_TAG, blockPolicy);

    // Span policy -- see docs/parity.md
    SpanPolicy spanPolicy;
    {
        auto& flashSpans = spanPolicy.entries.els.emplace_back();
        flashSpans.maxSize = (2*610) << 12; // roughly 5MB, page aligned
        flashSpans.parity = Parity(10, 4);
    }
    for (int i = 1; i < 10; i++) {
        auto& spans = spanPolicy.entries.els.emplace_back();
        spans.maxSize = spanPolicy.entries.els.back().maxSize + (610 << 12);
        spans.parity = Parity(i+1, 4);
    }
    addSegment(SPAN_POLICY_TAG, spanPolicy);

    // Stripe policy: try to have 1MiB stripes (they'll be larger the vast majority
    // of the times).
    StripePolicy stripePolicy;
    stripePolicy.targetStripeSize = 1 << 20;
    addSegment(STRIPE_POLICY_TAG, stripePolicy);

    return info;
}

bool readOnlyShardReq(const ShardMessageKind kind) {
    switch (kind) {
    case ShardMessageKind::LOOKUP:
    case ShardMessageKind::STAT_FILE:
    case ShardMessageKind::STAT_TRANSIENT_FILE:
    case ShardMessageKind::STAT_DIRECTORY:
    case ShardMessageKind::READ_DIR:
    case ShardMessageKind::FULL_READ_DIR:
    case ShardMessageKind::FILE_SPANS:
    case ShardMessageKind::VISIT_DIRECTORIES:
    case ShardMessageKind::VISIT_FILES:
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return true;
    case ShardMessageKind::CONSTRUCT_FILE:
    case ShardMessageKind::ADD_SPAN_INITIATE:
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
    case ShardMessageKind::ADD_SPAN_CERTIFY:
    case ShardMessageKind::ADD_INLINE_SPAN:
    case ShardMessageKind::LINK_FILE:
    case ShardMessageKind::SOFT_UNLINK_FILE:
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
    case ShardMessageKind::SET_DIRECTORY_INFO:
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
    case ShardMessageKind::SWAP_BLOCKS:
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
    case ShardMessageKind::SET_DIRECTORY_OWNER:
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
    case ShardMessageKind::LOCK_CURRENT_EDGE:
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
    case ShardMessageKind::REMOVE_INODE:
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
    case ShardMessageKind::MOVE_SPAN:
    case ShardMessageKind::SET_TIME:
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
    case ShardMessageKind::SWAP_SPANS:
        return false;
    case ShardMessageKind::ERROR:
        throw EGGS_EXCEPTION("unexpected ERROR shard message kind");
    }
    throw EGGS_EXCEPTION("bad message kind %s", kind);
}

ShardDB::ShardDB(Logger& logger, std::shared_ptr<XmonAgent>& agent, ShardId shid, Duration deadlineInterval, const SharedRocksDB& sharedDB, const BlockServicesCacheDB& blockServicesCache) {
    _impl = new ShardDBImpl(logger, agent, shid, deadlineInterval, sharedDB, blockServicesCache);
}

void ShardDB::close() {
    ((ShardDBImpl*)_impl)->close();
}

ShardDB::~ShardDB() {
    delete (ShardDBImpl*)_impl;
    _impl = nullptr;
}

EggsError ShardDB::read(const ShardReqContainer& req, ShardRespContainer& resp) {
    return ((ShardDBImpl*)_impl)->read(req, resp);
}

EggsError ShardDB::prepareLogEntry(const ShardReqContainer& req, ShardLogEntry& logEntry) {
    return ((ShardDBImpl*)_impl)->prepareLogEntry(req, logEntry);
}

EggsError ShardDB::applyLogEntry(uint64_t logEntryIx, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
    return ((ShardDBImpl*)_impl)->applyLogEntry(logEntryIx, logEntry, resp);
}

uint64_t ShardDB::lastAppliedLogEntry() {
    return ((ShardDBImpl*)_impl)->_lastAppliedLogEntry();
}

const std::array<uint8_t, 16>& ShardDB::secretKey() const {
    return ((ShardDBImpl*)_impl)->_secretKey;
}

void ShardDB::flush(bool sync) {
    return ((ShardDBImpl*)_impl)->flush(sync);
}
