#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/table.h>
#include <system_error>
#include <xxhash.h>

#include "Bincode.hpp"
#include "Common.hpp"
#include "Crypto.hpp"
#include "Env.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "ShardDB.hpp"
#include "ShardDBData.hpp"
#include "Assert.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "Time.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDBData.hpp"
#include "AssertiveLock.hpp"
#include "crc32c.h"
#include "wyhash.h"

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
// ## RocksDB schema
//
// Overview of the column families we use. Assumptions from the document:
//
// * 1e12 files
// * 10EiB storage
// * 100000 concurrent clients
// * 20000 storage nodes
//
// * Files:
//     - Lots of point queries, we actually never do iteration of any kind here.
//     - InodeId (8 bytes): FileBody (16 bytes)
//     - Total size: 1e12 files / 256 shards * 24 bytes = ~100GiB
//
// * Spans:
//     - Lots of range queries, mostly with SeekForPrev
//     - Both files and transient files point to this. This is part of why it makes
//         sense to have a different column family: otherwise we couldn't easily
//         traverse the transient files with them without stepping over spans.
//     - { id: InodeId, byte_offset: uint64_t } (16 bytes): SpanBody
//         * SpanBody is 7 bytes + 10*(4+20) = 256bytes
//         * SpanBody is 22 bytes of header, plus 0-255 inline body, or ~(10+4)*12 = 168 bytes for blocks
//         * Say an average of 300 bytes for key + body, being a bit conservative
//         * Let's also say that the max span size is 100MiB
//         * Total size: 10EiB / 100MiB / 256 shards * 200 bytes ~ 130GiB
//         * However the above is quite optimistic, assuming no fragmentation -- the average file will probably
//             be something like 10MiB. So the total dataset for spans will probably be around 1TiB in
//             reality.
//
// * TransientFiles:
//     - Should use bloom filter here, lots of point queries
//     - Unordered iteration for garbage collection
//     - InodeId (8 bytes): TransientFileBody (25-281 bytes)
//     - The size for this will be neglegible -- say 1% of Files.
//
// * Directories:
//     - We store the directory body
//     - Unordered traversal for garbage collection
//     - InodeId (8 bytes): DirectoryBody (18-273 bytes)
//     - Unclear how many directories we have, but it's probably going to have a few order of magnitude less
//         than files, e.g. 1e8 directories / 256 shards * 200 bytes = ~80MiB, tiny dataset.
//
// * Edges:
//     - Lots of range queries
//     - Might want to exploit the fact that for current edges we always seek by prefix (without the name)
//     - Two kind of records:
//         * { dir: InodeId, current: false, nameHash: nameHash, name: bytes, creationTime: uint64_t }: SnapshotEdgeBody
//         * { dir: InodeId, current: true,  nameHash: nameHash, name: bytes }: CurrentEdgeBody
//     - The sizes below are for snapshot, for current the creation time moves to the body
//     - They key size is 25-300 bytes, say 80 bytes on average with a 50 bytes filename
//     - The value size is 10 bytes or so
//     - Make it 100bytes total for key and value
//     - The number of files puts a lower bound on the number of edges, and we don't expect the number to be much
//         higher, since we won't have many snaphsot edges, and we have no hardlinks. Also there aren't that many
//         directories.
//     - So total size is something like 1e12 / 256 * 250 bytes = ~400GiB
//     - Similar size as spans -- I think it makes a ton of sense to have them in a different column family
//         from directories, given the different order of magnitude when it comes to size.
//
// * Block services to files:
//     - Goes from block service, file id to count
//     - { blockServiceId: uint64_t, fileId: uint64_t }: int64
//     - Assuming RS(10,4), roughly 1e12/256 * 24 bytes ~ 100GiB
//     - Not small, but not a huge deal either.
//
// We'll also store some small metadata in the default column family:
//
// - SHARD_INFO_KEY ShardInfoBody
// - LAST_APPLIED_LOG_ENTRY_KEY uint64_t
// - NEXT_FILE_ID_KEY InodeId
// - NEXT_SYMLINK_ID_KEY InodeId
// - NEXT_BLOCK_ID_KEY InodeId
// - CURRENT_BLOCK_SERVICES_KEY CurrentBlockServicesBody
// - BLOCK_SERVICE_KEY {blockServiceId: uint64_t} BlockServiceBody
//
// Note that the block services might be out of date, especially those not referred to by
// CURRENT_BLOCK_SERVICES_KEY.
//
// Total data for the shard will be 1-2TiB. The big ones are `Spans` and `Edges`, which are also
// the ones requiring range queries.
//
// Does this sit well with RocksDB? Their own benchmarks use 900M keys, 20 byte key, 400 byte value,
// so 900e6 * (20+400)byte = ~400GiB. They're not very far off, so it seems reasonable.
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

static auto wrappedSnapshot(rocksdb::DB* db) {
    auto deleter = [db](const rocksdb::Snapshot* snapshot) {
        db->ReleaseSnapshot(snapshot);
    };
    return std::unique_ptr<const rocksdb::Snapshot, decltype(deleter)>(db->GetSnapshot(), deleter);
}

static uint64_t computeHash(HashMode mode, const BincodeBytesRef& bytes) {
    switch (mode) {
    case HashMode::XXH3_63:
        // TODO remove this chopping, pyfuse3 doesn't currently like
        // full 64-bit hashes.
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

constexpr uint64_t DEADLINE_INTERVAL = (60ull /*mins*/ * 60 /*secs*/ * 1'000'000'000 /*ns*/); // 1 hr

void ShardLogEntry::pack(BincodeBuf& buf) const {
    time.pack(buf);
    buf.packScalar<uint16_t>((uint16_t)body.kind());
    body.pack(buf);
}

void ShardLogEntry::unpack(BincodeBuf& buf) {
    time.unpack(buf);
    ShardLogEntryKind kind = (ShardLogEntryKind)buf.unpackScalar<uint16_t>();
    body.unpack(buf, kind);
}

struct BlockServiceCache {
    AES128Key secretKey;
    std::array<uint8_t, 16> failureDomain;
    std::array<uint8_t, 4> ip1;
    std::array<uint8_t, 4> ip2;
    uint16_t port1;
    uint16_t port2;
    uint8_t storageClass;
};

struct ShardDBImpl {
    Env _env;

    ShardId _shid;
    std::array<uint8_t, 16> _secretKey;
    AES128Key _expandedSecretKey;
    
    // TODO it would be good to store basically all of the metadata in memory,
    // so that we'd just read from it, but this requires a bit of care when writing
    // since we rollback on error.

    // These two block services things change infrequently, and are used to add spans
    // -- keep them in memory.
    //
    // Note: we generally need to store all the block services at any time to conjure
    // their full information when getting file spans.
    std::unordered_map<uint64_t, BlockServiceCache> _blockServicesCache;
    // The block services we currently want to use.
    std::vector<uint64_t> _currentBlockServices;

    rocksdb::DB* _db;
    rocksdb::ColumnFamilyHandle* _defaultCf;
    rocksdb::ColumnFamilyHandle* _transientCf;
    rocksdb::ColumnFamilyHandle* _filesCf;
    rocksdb::ColumnFamilyHandle* _spansCf;
    rocksdb::ColumnFamilyHandle* _directoriesCf;
    rocksdb::ColumnFamilyHandle* _edgesCf;
    rocksdb::ColumnFamilyHandle* _blockServicesToFilesCf;

    AssertiveLock _applyLogEntryLock;

    // ----------------------------------------------------------------
    // initialization

    ShardDBImpl() = delete;

    ShardDBImpl(Logger& logger, ShardId shid, const std::string& path) : _env(logger, "shard_db"), _shid(shid) {
        LOG_INFO(_env, "will run shard %s in db dir %s", shid, path);

        // TODO actually figure out the best strategy for each family, including the default
        // one.

        rocksdb::Options options;
        options.create_if_missing = true;
        options.create_missing_column_families = true;
        options.compression = rocksdb::kLZ4Compression;
        options.bottommost_compression = rocksdb::kZSTD;

        rocksdb::ColumnFamilyOptions blockServicesToFilesOptions;
        blockServicesToFilesOptions.merge_operator = CreateInt64AddOperator();
        std::vector<rocksdb::ColumnFamilyDescriptor> familiesDescriptors{
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"files", {}},
            {"spans", {}},
            {"transientFiles", {}},
            {"directories", {}},
            {"edges", {}},
            {"blockServicesToFiles", blockServicesToFilesOptions},
        };
        std::vector<rocksdb::ColumnFamilyHandle*> familiesHandles;
        auto dbPath = path + "/db";
        LOG_INFO(_env, "initializing shard %s RocksDB in %s", _shid, dbPath);
        ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::Open(options, dbPath, familiesDescriptors, &familiesHandles, &_db),
            "could not open RocksDB %s", dbPath
        );
        ALWAYS_ASSERT(familiesDescriptors.size() == familiesHandles.size());
        _defaultCf = familiesHandles[0];
        _filesCf = familiesHandles[1];
        _spansCf = familiesHandles[2];
        _transientCf = familiesHandles[3];
        _directoriesCf = familiesHandles[4];
        _edgesCf = familiesHandles[5];
        _blockServicesToFilesCf = familiesHandles[6];

        _initDb();
    }

    void close() {
        LOG_INFO(_env, "destroying column families and closing database");

        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_defaultCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_filesCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_spansCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_transientCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_directoriesCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_edgesCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_blockServicesToFilesCf));
        ROCKS_DB_CHECKED(_db->Close());
    }

    ~ShardDBImpl() {
        delete _db;
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

        if (!keyExists(_defaultCf, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY))) {
            LOG_INFO(_env, "initializing current block services (as empty)");
            OwnedValue<CurrentBlockServicesBody> v(0);
            v().setLength(0);
            ROCKS_DB_CHECKED(_db->Put({}, _defaultCf, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY), v.toSlice()));
        } else {
            LOG_INFO(_env, "initializing block services cache (from db)");
            std::string buf;
            ROCKS_DB_CHECKED(_db->Get({}, _defaultCf, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY), &buf));
            ExternalValue<CurrentBlockServicesBody> v(buf);
            _currentBlockServices.resize(v().length());
            for (int i = 0; i < v().length(); i++) {
                _currentBlockServices[i] = v().at(i);
            }
            {
                rocksdb::ReadOptions options;
                static_assert(sizeof(ShardMetadataKey) == sizeof(uint8_t));
                auto upperBound = (ShardMetadataKey)((uint8_t)BLOCK_SERVICE_KEY + 1);
                auto upperBoundSlice = shardMetadataKey(&upperBound);
                options.iterate_upper_bound = &upperBoundSlice;
                std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _defaultCf));
                StaticValue<BlockServiceKey> beginKey;
                beginKey().setKey(BLOCK_SERVICE_KEY);
                beginKey().setBlockServiceId(0);
                for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                    auto k = ExternalValue<BlockServiceKey>::FromSlice(it->key());
                    ALWAYS_ASSERT(k().key() == BLOCK_SERVICE_KEY);
                    auto v = ExternalValue<BlockServiceBody>::FromSlice(it->value());
                    auto& cache = _blockServicesCache[k().blockServiceId()];
                    cache.ip1 = v().ip1();
                    cache.port1 = v().port1();
                    cache.ip2 = v().ip2();
                    cache.port2 = v().port2();
                    expandKey(v().secretKey(), cache.secretKey);
                    cache.storageClass = v().storageClass();
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // read-only path

    EggsError _statFile(const StatFileReq& req, StatFileResp& resp) {
        std::string fileValue;
        ExternalValue<FileBody> file;
        EggsError err = _getFile({}, req.id, fileValue, file);
        if (err != NO_ERROR) {
            return err;
        }
        resp.mtime = file().mtime();
        resp.size = file().fileSize();
        return NO_ERROR;
    }

    EggsError _statTransientFile(const StatTransientFileReq& req, StatTransientFileResp& resp) {
        std::string fileValue;
        {
            auto k = InodeIdKey::Static(req.id);
            auto status = _db->Get({}, _transientCf, k.toSlice(), &fileValue);
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

    EggsError _statDirectory(const StatDirectoryReq& req, StatDirectoryResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        // allowSnapshot=true, the caller can very easily detect if it's snapshot or not
        EggsError err = _getDirectory({}, req.id, true, dirValue, dir);
        if (err != NO_ERROR) {
            return err;
        }
        resp.mtime = dir().mtime();
        resp.owner = dir().ownerId();
        dir().info(resp.info);
        return NO_ERROR;
    }

    EggsError _readDir(const ReadDirReq& req, ReadDirResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        auto snapshot = wrappedSnapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

        // we don't want snapshot directories, so check for that early
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            EggsError err = _getDirectory(options, req.dirId, false, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }

        {
            auto it = std::unique_ptr<rocksdb::Iterator>(_db->NewIterator(options, _edgesCf));
            StaticValue<EdgeKey> beginKey;
            beginKey().setDirIdWithCurrent(req.dirId, true); // current = true
            beginKey().setNameHash(req.startHash);
            beginKey().setName({});
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - ReadDirResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (key().dirId() != req.dirId || !key().current()) {
                    resp.nextHash = 0; // we've ran out of edges
                    break;
                }
                auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                CurrentEdge& respEdge = resp.results.els.emplace_back();
                respEdge.targetId = edge().targetIdWithLocked().id();
                respEdge.nameHash = key().nameHash();
                respEdge.name = key().name();
                respEdge.creationTime = edge().creationTime();
                budget -= respEdge.packedSize();
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

    EggsError _fullReadDir(const FullReadDirReq& req, FullReadDirResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        auto snapshot = wrappedSnapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

        {
            auto it = std::unique_ptr<rocksdb::Iterator>(_db->NewIterator(options, _edgesCf));
            StaticValue<EdgeKey> beginKey;
            beginKey().setDirIdWithCurrent(req.dirId, req.cursor.current);
            beginKey().setNameHash(req.cursor.startHash);
            beginKey().setName(req.cursor.startName.ref());
            if (!req.cursor.current) {
                beginKey().setCreationTime(req.cursor.startTime);
            } else {
                ALWAYS_ASSERT(req.cursor.startTime == 0); // TODO proper error at validation time
            }
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - FullReadDirResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (key().dirId() != req.dirId) {
                    break;
                }
                auto& respEdge = resp.results.els.emplace_back();
                if (key().current()) {
                    auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                    respEdge.current = key().current();
                    respEdge.targetId = edge().targetIdWithLocked();
                    respEdge.nameHash = key().nameHash();
                    respEdge.name = key().name();
                    respEdge.creationTime = edge().creationTime();
                } else {
                    auto edge = ExternalValue<SnapshotEdgeBody>::FromSlice(it->value());
                    respEdge.current = key().current();
                    respEdge.targetId = edge().targetIdWithOwned();
                    respEdge.nameHash = key().nameHash();
                    respEdge.name = key().name();
                    respEdge.creationTime = key().creationTime();
                }
                budget -= respEdge.packedSize();
                if (budget < 0) {
                    int prevCursorSize = FullReadDirCursor::STATIC_SIZE;
                    while (budget < 0) {
                        auto& last = resp.results.els.back();
                        budget += last.packedSize();
                        resp.next.current = last.current;
                        resp.next.startHash = last.nameHash;
                        resp.next.startName = last.name;
                        resp.next.startTime = last.current ? 0 : last.creationTime;
                        resp.results.els.pop_back();
                        budget += prevCursorSize;
                        budget -= resp.next.packedSize();
                        prevCursorSize = resp.next.packedSize();
                    }
                    break;
                }
            }
            ROCKS_DB_CHECKED(it->status());
        }

        if (resp.results.els.empty()) {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // here we want snapshots, which is also why we can do this check later
            EggsError err = _getDirectory(options, req.dirId, true, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }

        return NO_ERROR;
    }

    EggsError _lookup(const LookupReq& req, LookupResp& resp) {
        auto snapshot = wrappedSnapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

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

    EggsError _visitTransientFiles(const VisitTransientFilesReq& req, VisitTransientFilesResp& resp) {
        resp.nextId = NULL_INODE_ID;

        {
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _transientCf));
            auto beginKey = InodeIdKey::Static(req.beginId);
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - VisitTransientFilesResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                auto file = ExternalValue<TransientFileBody>::FromSlice(it->value());

                auto& respFile = resp.files.els.emplace_back();
                respFile.id = id().id();
                respFile.cookie.data = _calcCookie(respFile.id);
                respFile.deadlineTime = file().deadline();

                budget -= respFile.packedSize();
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
    EggsError _visitInodes(rocksdb::ColumnFamilyHandle* cf, const Req& req, Resp& resp) {
        resp.nextId = NULL_INODE_ID;

        int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - Resp::STATIC_SIZE;
        int maxIds = (budget/8) + 1; // include next inode
        {
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, cf));
            auto beginKey = InodeIdKey::Static(req.beginId);
            for (
                it->Seek(beginKey.toSlice());
                it->Valid() && resp.ids.els.size() < maxIds;
                it->Next()
            ) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                resp.ids.els.emplace_back(id().id());
            }
        }

        if (resp.ids.els.size() == maxIds) {
            resp.nextId = resp.ids.els.back();
            resp.ids.els.pop_back();
        }

        return NO_ERROR;
    }

    EggsError _visitDirectories(const VisitDirectoriesReq& req, VisitDirectoriesResp& resp) {
        return _visitInodes(_directoriesCf, req, resp);
    }
    
    EggsError _fileSpans(const FileSpansReq& req, FileSpansResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        auto snapshot = wrappedSnapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.get();

        int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - FileSpansResp::STATIC_SIZE;
        // if -1, we ran out of budget.
        const auto addBlockService = [this, &resp, &budget](BlockServiceId blockServiceId) -> int {
            // See if we've placed it already
            for (int i = 0; i < resp.blockServices.els.size(); i++) {
                if (resp.blockServices.els.at(i).id == blockServiceId) {
                    return i;
                }
            }
            // If not, we need to make space for it
            budget -= BlockService::STATIC_SIZE;
            if (budget < 0) {
                return -1;
            }
            auto& blockService = resp.blockServices.els.emplace_back();
            const auto& cache = _blockServicesCache.at(blockServiceId.u64);
            blockService.id = blockServiceId;
            blockService.ip1 = cache.ip1;
            blockService.port1 = cache.port1;
            blockService.ip2 = cache.ip2;
            blockService.port2 = cache.port2;
            // TODO propagade block service flags here
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
                budget -= respSpan.packedSize();
                if (budget < 0) {
                    resp.nextOffset = respSpan.header.byteOffset;
                    resp.spans.els.pop_back();
                    break;
                }
            }
        }

        // Check if file does not exist when we have no spans
        if (resp.spans.els.size() == 0) {
            std::string fileValue;
            ExternalValue<FileBody> file;
            EggsError err = _getFile(options, req.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        return NO_ERROR;
    }

    EggsError _blockServiceFiles(const BlockServiceFilesReq& req, BlockServiceFilesResp& resp) {
        int maxFiles = (UDP_MTU - ShardResponseHeader::STATIC_SIZE - BlockServiceFilesResp::STATIC_SIZE) / 8;
        resp.fileIds.els.reserve(maxFiles);

        StaticValue<BlockServiceToFileKey> beginKey;
        beginKey().setBlockServiceId(req.blockServiceId.u64);
        beginKey().setFileId(req.startFrom);
        StaticValue<BlockServiceToFileKey> endKey;
        endKey().setBlockServiceId(req.blockServiceId.u64+1);
        endKey().setFileId(NULL_INODE_ID);
        auto endKeySlice = endKey.toSlice();

        rocksdb::ReadOptions options;
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

    EggsError _visitFiles(const VisitFilesReq& req, VisitFilesResp& resp) {
        return _visitInodes(_filesCf, req, resp);
    }

    EggsError _snapshotLookup(const SnapshotLookupReq& req, SnapshotLookupResp& resp) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }

        uint64_t nameHash;
        {
            // allowSnapshot=true since we want this to also work for snaphsot dirs
            EggsError err = _getDirectoryAndHash({}, req.dirId, true, req.name.ref(), nameHash);
            if (err != NO_ERROR) {
                return err;
            }
        }

        int maxEdges = 1 + (UDP_MTU - ShardResponseHeader::STATIC_SIZE - SnapshotLookupResp::STATIC_SIZE) / SnapshotLookupEdge::STATIC_SIZE;
        std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator({}, _edgesCf));
        StaticValue<EdgeKey> firstK;
        firstK().setDirIdWithCurrent(req.dirId, false);
        firstK().setNameHash(nameHash);
        firstK().setName(req.name.ref());
        firstK().setCreationTime(req.startFrom);
        int i;
        for (
            i = 0,          it->Seek(firstK.toSlice());
            i < maxEdges && it->Valid();
            i++,            it->Next()
        ) {
            auto k = ExternalValue<EdgeKey>::FromSlice(it->key());
            if (
                k().nameHash() != nameHash ||
                k().current() || // only snapshot edges
                k().dirId() != req.dirId ||
                k().name() != req.name.ref()
            ) {
                break;
            }
            auto v = ExternalValue<SnapshotEdgeBody>::FromSlice(it->value());
            auto& edge = resp.edges.els.emplace_back();
            edge.targetId = v().targetIdWithOwned();
            edge.creationTime = k().creationTime();
        }
        ROCKS_DB_CHECKED(it->status());
        if (resp.edges.els.size() == maxEdges) {
            resp.nextTime = resp.edges.els.back().creationTime;
            resp.edges.els.pop_back();
        }
        return NO_ERROR;
    }

    EggsError read(const ShardReqContainer& req, ShardRespContainer& resp) {
        LOG_DEBUG(_env, "processing read-only request of kind %s", req.kind());

        EggsError err = NO_ERROR;
        resp.clear();

        switch (req.kind()) {
        case ShardMessageKind::STAT_FILE:
            err = _statFile(req.getStatFile(), resp.setStatFile());
            break;
        case ShardMessageKind::READ_DIR:
            err = _readDir(req.getReadDir(), resp.setReadDir());
            break;
        case ShardMessageKind::STAT_DIRECTORY:
            err = _statDirectory(req.getStatDirectory(), resp.setStatDirectory());
            break;
        case ShardMessageKind::STAT_TRANSIENT_FILE:
            err = _statTransientFile(req.getStatTransientFile(), resp.setStatTransientFile());
            break;
        case ShardMessageKind::LOOKUP:
            err = _lookup(req.getLookup(), resp.setLookup());
            break;
        case ShardMessageKind::VISIT_TRANSIENT_FILES:
            err = _visitTransientFiles(req.getVisitTransientFiles(), resp.setVisitTransientFiles());
            break;
        case ShardMessageKind::FULL_READ_DIR:
            err = _fullReadDir(req.getFullReadDir(), resp.setFullReadDir());
            break;
        case ShardMessageKind::VISIT_DIRECTORIES:
            err = _visitDirectories(req.getVisitDirectories(), resp.setVisitDirectories());
            break;
        case ShardMessageKind::FILE_SPANS:
            err = _fileSpans(req.getFileSpans(), resp.setFileSpans());
            break;
        case ShardMessageKind::BLOCK_SERVICE_FILES:
            err = _blockServiceFiles(req.getBlockServiceFiles(), resp.setBlockServiceFiles());
            break;
        case ShardMessageKind::VISIT_FILES:
            err = _visitFiles(req.getVisitFiles(), resp.setVisitFiles());
            break;
        case ShardMessageKind::SNAPSHOT_LOOKUP:
            err = _snapshotLookup(req.getSnapshotLookup(), resp.setSnapshotLookup());
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
        entry.deadlineTime = time + DEADLINE_INTERVAL;

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

    bool _blockServiceMatchesBlacklist(const std::vector<BlockServiceId>& blacklists, BlockServiceId blockServiceId, const BlockServiceCache& cache) {
        for (const auto& blacklist: blacklists) {
            if (blacklist == blockServiceId) {
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
            LOG_DEBUG(_env, "req.byteOffset=%s is not a multiple of PAGE_SIZE=%s", req.byteOffset, EGGSFS_PAGE_SIZE);
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

    EggsError _prepareAddSpanInitiate(EggsTime time, const AddSpanInitiateReq& req, AddSpanInitiateEntry& entry) {
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
        if (req.storageClass == INLINE_STORAGE || req.storageClass == EMPTY_STORAGE) {
            LOG_DEBUG(_env, "bad storage class %s for blocks span", (int)req.storageClass);
            return EggsError::BAD_SPAN_BODY;
        }
        if (req.byteOffset%EGGSFS_PAGE_SIZE != 0 || req.cellSize%EGGSFS_PAGE_SIZE != 0) {
            LOG_DEBUG(_env, "req.byteOffset=%s or cellSize=%s is not a multiple of PAGE_SIZE=%s", req.byteOffset, req.cellSize, EGGSFS_PAGE_SIZE);
            return EggsError::BAD_SPAN_BODY;
        }
        if (!_checkSpanBody(req)) {
            return EggsError::BAD_SPAN_BODY;
        }

        // start filling in entry
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
            std::vector<BlockServiceId> candidateBlockServices;
            candidateBlockServices.reserve(_currentBlockServices.size());
            LOG_DEBUG(_env, "Starting out with %s current block services", _currentBlockServices.size());
            {
                for (BlockServiceId id: _currentBlockServices) {
                    const auto& cache = _blockServicesCache.at(id.u64);
                    if (cache.storageClass != entry.storageClass) {
                        LOG_DEBUG(_env, "Skipping %s because of different storage class (%s != %s)", id, (int)cache.storageClass, (int)entry.storageClass);
                        continue;
                    }
                    if (_blockServiceMatchesBlacklist(req.blacklist.els, id, cache)) {
                        LOG_DEBUG(_env, "Skipping %s because it matches blacklist", id);
                        continue;
                    }
                    candidateBlockServices.emplace_back(id);
                }
            }
            LOG_DEBUG(_env, "Starting out with %s block service candidates, parity %s", candidateBlockServices.size(), entry.parity);
            std::vector<BlockServiceId> pickedBlockServices;
            pickedBlockServices.reserve(req.parity.blocks());
            // Try to get the first span to copy its block services -- this should be the
            // very common case past the first span.
            {
                StaticValue<SpanKey> k;
                k().setFileId(req.fileId);
                k().setOffset(0);
                std::string v;
                auto status = _db->Get({}, _spansCf, k.toSlice(), &v);
                if (status.IsNotFound()) {
                    // no-op -- we'll just generate them all at random
                } else {
                    ROCKS_DB_CHECKED(status);
                    ExternalValue<SpanBody> span(v);
                    // TODO this means that if the first span is inline or smaller, all the other
                    // spans will get block services generated at random, which is not ideal. We
                    // should probably look further.
                    if (span().storageClass() != INLINE_STORAGE) {
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
                            LOG_DEBUG(_env, "(1) Picking block service candidate %s", spanBlock.blockService());
                            BlockServiceId blockServiceId = spanBlock.blockService();
                            pickedBlockServices.emplace_back(blockServiceId);
                            std::iter_swap(isCandidate, candidateBlockServices.end()-1);
                            candidateBlockServices.pop_back();
                        }
                    }
                }
            }
            // Fill in whatever remains. We don't need to be deterministic here (we would have to
            // if we were in log application), but we might as well.
            {
                uint64_t rand = time.ns;
                while (pickedBlockServices.size() < req.parity.blocks() && candidateBlockServices.size() > 0) {
                    uint64_t ix = wyhash64(&rand) % candidateBlockServices.size();
                    LOG_DEBUG(_env, "(2) Picking block service candidate %s", candidateBlockServices[ix]);
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

    EggsError _prepareExpireTransientFile(EggsTime time, const ExpireTransientFileReq& req, ExpireTransientFileEntry& entry) {
        if (req.id.type() == InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.id = req.id;
        return NO_ERROR;
    }

    EggsError prepareLogEntry(const ShardReqContainer& req, ShardLogEntry& logEntry) {
        LOG_DEBUG(_env, "processing write request of kind %s", req.kind());
        logEntry.clear();
        EggsError err = NO_ERROR;

        EggsTime time = eggsNow();
        logEntry.time = time;
        auto& logEntryBody = logEntry.body;

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
        case ShardMessageKind::ADD_SPAN_INITIATE:
            err = _prepareAddSpanInitiate(time, req.getAddSpanInitiate(), logEntryBody.setAddSpanInitiate());
            break;
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
        case ShardMessageKind::EXPIRE_TRANSIENT_FILE:
            err = _prepareExpireTransientFile(time, req.getExpireTransientFile(), logEntryBody.setExpireTransientFile());
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
            ALWAYS_ASSERT(false, "Bad type %s", (int)entry.type);
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
        file().setFileSize(transientFile().fileSize());
        ROCKS_DB_CHECKED(batch.Put(_filesCf, fileKey.toSlice(), file.toSlice()));

        // create edge in owner.
        {
            EggsError err = ShardDBImpl::_createCurrentEdge(time, batch, entry.ownerId, entry.name, entry.fileId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }

        resp.creationTime = time;

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
            RAISE_ALERT(_env, "trying to modify dir %s going backwards in time, dir mtime is %s, log entry time is %s", dirId, tmpDir().mtime(), time);
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
    // `logEntryTime` and `creationTime` are separate since we need a different `creationTime`
    // for CreateDirectoryInodeReq.
    EggsError _createCurrentEdge(EggsTime logEntryTime, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytes& name, InodeId targetId, bool locked) {
        EggsTime creationTime = logEntryTime;

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
                if (
                    !locked || // the one we're trying to create isn't locked
                    (targetId != existingEdge().targetIdWithLocked().id()) // we're trying to create a different locked edge
                ) {
                    return EggsError::NAME_IS_LOCKED;
                }
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
            EggsError err = _createCurrentEdge(time, batch, entry.dirId, entry.newName, entry.targetId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }
        resp.newCreationTime = time;
        return NO_ERROR;
    }

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
        return _softUnlinkCurrentEdge(time, batch, entry.ownerId, entry.name, entry.creationTime, entry.fileId, true);
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
        auto err = _createCurrentEdge(time, batch, entry.dirId, entry.name, entry.targetId, true); // locked=true
        if (err != NO_ERROR) {
            return err;
        }
        resp.creationTime = time;
        return NO_ERROR;
    }

    EggsError _applyUnlockCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, const UnlockCurrentEdgeEntry& entry, UnlockCurrentEdgeResp& resp) {
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnaphsot=false since no current edges in snapshot dirs
            EggsError err = _getDirectory({}, entry.dirId, false, dirValue, dir);
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
            EggsError err = _getDirectory({}, entry.dirId, false, dirValue, dir);
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
            EggsError err = _getDirectory({}, entry.id, true, dirValue, dir);
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
            v().setDeadline(0);
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
        resp.blocks.els.reserve(blocks.parity().blocks());
        for (int i = 0; i < blocks.parity().blocks(); i++) {
            const auto block = blocks.block(i);
            const auto& cache = _blockServicesCache.at(block.blockService().u64);
            auto& respBlock = resp.blocks.els.emplace_back();
            respBlock.blockServiceIp1 = cache.ip1;
            respBlock.blockServicePort1 = cache.port1;
            respBlock.blockServiceIp2 = cache.ip2;
            respBlock.blockServicePort2 = cache.port2;
            respBlock.blockServiceId = block.blockService();
            respBlock.blockId = block.blockId();
            respBlock.certificate = _blockEraseCertificate(blocks.cellSize()*blocks.stripes(), block, cache.secretKey);
        }

        return NO_ERROR;
    }

    void _updateCurrentBlockServices(EggsTime time, rocksdb::WriteBatch& batch) {
        _currentBlockServices.clear();
        // The scheme below is a very cheap way to always pick different failure domains
        // for our block services: we just set the current block services to be all of
        // different failure domains, sharded by storage type.
        //
        // It does require having at least 14 failure domains (to do RS(10,4)), and gives
        // very little slack with the current situation of 17 failure domains.
        std::unordered_map<uint8_t, std::unordered_map<__int128, std::vector<uint64_t>>> blockServicesByFailureDomain;
        for (const auto& [blockServiceId, blockService]: _blockServicesCache) {
            __int128 failureDomain;
            static_assert(sizeof(failureDomain) == sizeof(blockService.failureDomain));
            memcpy(&failureDomain, &blockService.failureDomain[0], sizeof(failureDomain));
            blockServicesByFailureDomain[blockService.storageClass][failureDomain].emplace_back(blockServiceId);
        }
        uint64_t rand = time.ns;
        for (const auto& [storageClass, byFailureDomain]: blockServicesByFailureDomain) {
            for (const auto& [failureDomain, blockServices]: byFailureDomain) {
                _currentBlockServices.emplace_back(blockServices[wyhash64(&rand)%blockServices.size()]);
            }
        }
        ALWAYS_ASSERT(_currentBlockServices.size() < 256); // TODO handle this properly
        OwnedValue<CurrentBlockServicesBody> currentBody(_currentBlockServices.size());
        for (int i = 0; i < _currentBlockServices.size(); i++) {
            currentBody().set(i, _currentBlockServices[i]);
        }
        ROCKS_DB_CHECKED(batch.Put(_defaultCf, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY), currentBody.toSlice()));
    }

    // Important for this to never error: we rely on the write to go through since 
    // we write _currentBlockServices/_blockServicesCache, which we rely to be in
    // sync with RocksDB.
    void _applyUpdateBlockServices(EggsTime time, rocksdb::WriteBatch& batch, const UpdateBlockServicesEntry& entry) {
        StaticValue<BlockServiceKey> blockKey;
        blockKey().setKey(BLOCK_SERVICE_KEY);
        StaticValue<BlockServiceBody> blockBody;
        for (int i = 0; i < entry.blockServices.els.size(); i++) {
            const auto& entryBlock = entry.blockServices.els[i];
            blockKey().setBlockServiceId(entryBlock.id.u64);
            blockBody().setVersion(0);
            blockBody().setId(entryBlock.id.u64);
            blockBody().setIp1(entryBlock.ip1.data);
            blockBody().setPort1(entryBlock.port1);
            blockBody().setIp2(entryBlock.ip2.data);
            blockBody().setPort2(entryBlock.port2);
            blockBody().setStorageClass(entryBlock.storageClass);
            blockBody().setFailureDomain(entryBlock.failureDomain.data);
            blockBody().setSecretKey(entryBlock.secretKey.data);
            ROCKS_DB_CHECKED(batch.Put(_defaultCf, blockKey.toSlice(), blockBody.toSlice()));
            auto& cache = _blockServicesCache[entryBlock.id.u64];
            expandKey(entryBlock.secretKey.data, cache.secretKey);
            cache.ip1 = entryBlock.ip1.data;
            cache.port1 = entryBlock.port1;
            cache.ip2 = entryBlock.ip2.data;
            cache.port2 = entryBlock.port2;
            cache.storageClass = entryBlock.storageClass;
            cache.failureDomain = entryBlock.failureDomain.data;
        }
        _updateCurrentBlockServices(time, batch);
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
        for (int i = 0; i < blocks.parity().blocks(); i++) {
            const BlockBody block = blocks.block(i);
            auto& respBlock = resp.blocks.els.emplace_back();
            respBlock.blockServiceId = block.blockService();
            respBlock.blockId = block.blockId();
            const auto& cache = _blockServicesCache.at(block.blockService().u64);
            respBlock.blockServiceIp1 = cache.ip1;
            respBlock.blockServicePort1 = cache.port1;
            respBlock.blockServiceIp2 = cache.ip2;
            respBlock.blockServicePort2 = cache.port2;
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

    bool _checkBlockAddProof(BlockServiceId blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
        bbuf.packScalar<uint64_t>(blockServiceId.u64);
        bbuf.packScalar<char>('W');
        bbuf.packScalar<uint64_t>(proof.blockId);
    
        const auto& cache = _blockServicesCache.at(blockServiceId.u64);
        auto expectedProof = cbcmac(cache.secretKey, (uint8_t*)buf, sizeof(buf));

        bool good = proof.proof == expectedProof;

        if (!good) {
            LOG_DEBUG(_env, "bad block write proof, expected %s, got %s", BincodeFixedBytes<8>(expectedProof), proof);
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

    bool _checkBlockDeleteProof(BlockServiceId blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'E', block['block_id'])
        bbuf.packScalar<uint64_t>(blockServiceId.u64);
        bbuf.packScalar<char>('E');
        bbuf.packScalar<uint64_t>(proof.blockId);
    
        const auto& cache = _blockServicesCache.at(blockServiceId.u64);
        auto expectedProof = cbcmac(cache.secretKey, (uint8_t*)buf, sizeof(buf));

        bool good = proof.proof == expectedProof;
        if (!good) {
            RAISE_ALERT(_env, "Bad block delete proof, expected %s, got %s", BincodeFixedBytes<8>(expectedProof), BincodeFixedBytes<8>(proof.proof));
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
            BlockBody block;
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                auto block = blocks.block(i);
                if (!_checkBlockAddProof(block.blockService(), entry.proofs.els[i])) {
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
        transientFile().setDeadline(0);
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
        for (int i = 0; i < blocks.parity().blocks(); i++) {
            const auto block = blocks.block(i);
            const auto& proof = entry.proofs.els[i];
            if (block.blockId() != proof.blockId) {
                RAISE_ALERT(_env, "bad block proof id, expected %s, got %s", block.blockId(), proof.blockId);
                return EggsError::BAD_BLOCK_PROOF;
            }
            if (!_checkBlockDeleteProof(block.blockService(), proof)) {
                return EggsError::BAD_BLOCK_PROOF;
            }
            // record balance change in block service to files
            _addBlockServicesToFiles(batch, block.blockService(), entry.fileId, -1);
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

    EggsError _applySwapBlocks(EggsTime time, rocksdb::WriteBatch& batch, const SwapBlocksEntry& entry, SwapBlocksResp& resp) {
        // TODO turn assertions into proper errors
        // Fetch spans
        const auto fetchSpan = [this](InodeId fileId, uint64_t byteOffset, StaticValue<SpanKey>& spanKey, std::string& spanValue, ExternalValue<SpanBody>& span) -> bool {
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
        };
        StaticValue<SpanKey> span1Key;
        std::string span1Value;
        ExternalValue<SpanBody> span1;
        if (!fetchSpan(entry.fileId1, entry.byteOffset1, span1Key, span1Value, span1)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        StaticValue<SpanKey> span2Key;
        std::string span2Value;
        ExternalValue<SpanBody> span2;
        if (!fetchSpan(entry.fileId2, entry.byteOffset2, span2Key, span2Value, span2)) {
            return EggsError::SPAN_NOT_FOUND;
        }
        ALWAYS_ASSERT(span1().storageClass() != INLINE_STORAGE); // TODO better errors
        auto blocks1 = span1().blocksBody();
        ALWAYS_ASSERT(span2().storageClass() != INLINE_STORAGE);
        auto blocks2 = span2().blocksBody();
        uint32_t blockSize1 = blocks1.cellSize()*blocks1.stripes();
        uint32_t blockSize2 = blocks2.cellSize()*blocks2.stripes();
        ALWAYS_ASSERT(blockSize1 == blockSize2);
        // Fetch span state
        const auto fetchState = [this, time](InodeId fileId, uint64_t spanEnd) -> SpanState {
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
        };
        auto state1 = fetchState(entry.fileId1, entry.byteOffset1 + span1().size());
        auto state2 = fetchState(entry.fileId2, entry.byteOffset2 + span2().size());
        // We don't want to put not-certified blocks in clean spans, or similar
        ALWAYS_ASSERT(state1 == state2);
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
            throw EGGS_EXCEPTION("blocks not found");
        }
        ALWAYS_ASSERT(block1.crc() == block2.crc());
        // Check that we're not creating a situation where we have two blocks in the same block service
        const auto checkNoDuplicateBlockServices = [](const auto blocks, int blockToBeReplacedIx, const auto newBlock) {
            for (int i = 0; i < blocks.parity().blocks(); i++) {
                if (i == blockToBeReplacedIx) {
                    continue;
                }
                const auto block = blocks.block(i);
                ALWAYS_ASSERT(block.blockService() != newBlock.blockService());
            }
        };
        checkNoDuplicateBlockServices(blocks1, block1Ix, block2);
        checkNoDuplicateBlockServices(blocks2, block2Ix, block1);
        // Record the block counts
        _addBlockServicesToFiles(batch, block1.blockService(), entry.fileId1, -1);
        _addBlockServicesToFiles(batch, block2.blockService(), entry.fileId1, +1);
        _addBlockServicesToFiles(batch, block1.blockService(), entry.fileId2, +1);
        _addBlockServicesToFiles(batch, block2.blockService(), entry.fileId2, -1);
        // Finally, swap the blocks
        char* tmp = (char*)malloc(decltype(block1)::SIZE); ALWAYS_ASSERT(tmp);
        memcpy(tmp, block1._data, decltype(block1)::SIZE);
        memcpy(block1._data, block2._data, decltype(block1)::SIZE);
        memcpy(block2._data, tmp, decltype(block1)::SIZE);
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span1Key.toSlice(), span1.toSlice()));
        ROCKS_DB_CHECKED(batch.Put(_spansCf, span2Key.toSlice(), span2.toSlice()));
        return NO_ERROR;
    }

    EggsError _applyExpireTransientFile(EggsTime time, rocksdb::WriteBatch& batch, const ExpireTransientFileEntry& entry, ExpireTransientFileResp& resp) {
        std::string value;
        ExternalValue<TransientFileBody> transientFile;
        {
            EggsError err = _getTransientFile({}, time, true, entry.id, value, transientFile);
            if (err != NO_ERROR) {
                return err;
            }
        }
        transientFile().setDeadline(0);
        {
            auto k = InodeIdKey::Static(entry.id);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), transientFile.toSlice()));
        }
        return NO_ERROR;
    }

    EggsError applyLogEntry(bool sync, uint64_t logIndex, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
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
        case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
            // no resp here -- the callers are internal
            _applyUpdateBlockServices(time, batch, logEntryBody.getUpdateBlockServices());
            break;
        case ShardLogEntryKind::ADD_INLINE_SPAN:
            err = _applyAddInlineSpan(time, batch, logEntryBody.getAddInlineSpan(), resp.setAddInlineSpan());
            break;
        case ShardLogEntryKind::ADD_SPAN_INITIATE:
            err = _applyAddSpanInitiate(time, batch, logEntryBody.getAddSpanInitiate(), resp.setAddSpanInitiate());
            break;
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
        case ShardLogEntryKind::EXPIRE_TRANSIENT_FILE:
            err = _applyExpireTransientFile(time, batch, logEntryBody.getExpireTransientFile(), resp.setExpireTransientFile());
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

        {
            rocksdb::WriteOptions options;
            options.sync = sync;
            ROCKS_DB_CHECKED(_db->Write(options, &batch));
        }

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
            RAISE_ALERT(_env, "trying to modify transient file %s going backwards in time, file mtime is %s, log entry time is %s", id, tmpTf().mtime(), time);
            return EggsError::MTIME_IS_TOO_RECENT;
        }

        tmpTf().setMtime(time);
        if (!allowPastDeadline) {
            tmpTf().setDeadline(time + DEADLINE_INTERVAL);
        }
        {
            auto k = InodeIdKey::Static(id);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), tmpTf.toSlice()));
        }

        tf = tmpTf;
        return NO_ERROR;
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

    // Block policy: up to 5MiB: FLASH. Up to 100MiB: HDD. This is the maximum block size.
    BlockPolicy blockPolicy;
    auto& flashBlocks = blockPolicy.entries.els.emplace_back();
    flashBlocks.minSize = 0;
    flashBlocks.storageClass = storageClassByName("FLASH");
    auto& hddBlocks = blockPolicy.entries.els.emplace_back();
    hddBlocks.minSize = 5 << 20;
    hddBlocks.storageClass = storageClassByName("HDD");
    addSegment(BLOCK_POLICY_TAG, blockPolicy);

    // Span policy:
    // * up to 64KiB: RS(1,4). This mirroring span simplifies things in the kernel (so that we
    //     we never have cell sizes that are not multiple of span sizes). We still set things
    //     up so that we can lose 4 copies and still be fine.
    // * up to 10MiB: RS(4,4).
    // * up to 100MiB (max span size): RS(10,4).
    SpanPolicy spanPolicy;
    auto& tinySpans = spanPolicy.entries.els.emplace_back();
    tinySpans.maxSize = 1 << 16;
    tinySpans.parity = Parity(1, 4);
    auto& smallSpans = spanPolicy.entries.els.emplace_back();
    smallSpans.maxSize = 10 << 20;
    smallSpans.parity = Parity(4, 4);
    auto& bigSpans = spanPolicy.entries.els.emplace_back();
    bigSpans.maxSize = 100 << 20;
    bigSpans.parity = Parity(10, 4);
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
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        return true;
    case ShardMessageKind::CONSTRUCT_FILE:
    case ShardMessageKind::ADD_SPAN_INITIATE:
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
    case ShardMessageKind::EXPIRE_TRANSIENT_FILE:
        return false;
    case ShardMessageKind::ERROR:
        throw EGGS_EXCEPTION("unexpected ERROR shard message kind");
    }
    throw EGGS_EXCEPTION("bad message kind %s", kind);
}

ShardDB::ShardDB(Logger& logger, ShardId shid, const std::string& path) {
    _impl = new ShardDBImpl(logger, shid, path);
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

EggsError ShardDB::applyLogEntry(bool sync, uint64_t logEntryIx, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
    return ((ShardDBImpl*)_impl)->applyLogEntry(sync, logEntryIx, logEntry, resp);
}

uint64_t ShardDB::lastAppliedLogEntry() {
    return ((ShardDBImpl*)_impl)->_lastAppliedLogEntry();
}

const std::array<uint8_t, 16>& ShardDB::secretKey() const {
    return ((ShardDBImpl*)_impl)->_secretKey;
}
