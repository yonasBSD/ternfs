#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <system_error>
#include <xxhash.h>

#include "Bincode.hpp"
#include "Crypto.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"
#include "ShardDBData.hpp"
#include "Assert.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Time.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDBData.hpp"
#include "AssertiveLock.hpp"
#include "crc32c.hpp"
#include "splitmix64.hpp"

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
//         * SpanBody is 22 bytes of header, plus 0-255 inline body, or ~(10+4)*12 = 168 bytes for blocks
//         * Say an average of 200 bytes for key + body, being a bit conservative
//         * Let's also say that the max block size is 100MiB
//         * Total size: 10EiB / 100MiB / 256 shards * 200 bytes ~ 100GiB
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
// * Blocks:
//     - Goes from block service to file ids
//     - No values, only { blockServiceId: uint64_t, fileId: uint64_t }, "set like"
//     - Assuming RS(10,4), roughly 1e12/256 * 8 bytes = 31GiB
//     - Not a big deal.
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
    std::array<uint8_t, 16> secretKey;
    std::array<uint8_t, 4> ip;
    uint16_t port;
    uint8_t storageClass;
};

struct ShardDBImpl {
    Env _env;

    ShardId _shid;
    std::array<uint8_t, 16> _secretKey;
    
    // TODO it would be good to store basically all of the metadata in memory,
    // so that we'd just read from it, but this requires a bit of care when writing
    // since we rollback on error.

    // These two block services things change infrequently, and are used to add spans
    // -- keep them in memory.
    std::vector<uint64_t> _currentBlockServices; // the block services we currently want to use to add new spans
    std::unordered_map<uint64_t, BlockServiceCache> _blockServicesCache;

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
        std::vector<rocksdb::ColumnFamilyDescriptor> familiesDescriptors{
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"files", {}},
            {"spans", {}},
            {"transientFiles", {}},
            {"directories", {}},
            {"edges", {}},
            {"blockServicesToFiles", {}},
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
                auto info = defaultDirectoryInfo();
                StaticValue<DirectoryBody> dirBody;
                dirBody().setOwnerId(NULL_INODE_ID);
                dirBody().setMtime({});
                dirBody().setHashMode(HashMode::XXH3_63);
                dirBody().setInfoInherited(false);
                dirBody().setInfo(info.ref());
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
                WrappedIterator it(_db->NewIterator(options, _defaultCf));
                StaticValue<BlockServiceKey> beginKey;
                beginKey().setKey(BLOCK_SERVICE_KEY);
                beginKey().setBlockServiceId(0);
                for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                    auto k = ExternalValue<BlockServiceKey>::FromSlice(it->key());
                    ALWAYS_ASSERT(k().key() == BLOCK_SERVICE_KEY);
                    auto v = ExternalValue<BlockServiceBody>::FromSlice(it->value());
                    auto& cache = _blockServicesCache[k().blockServiceId()];
                    cache.ip = v().ip();
                    cache.port = v().port();
                    cache.secretKey = v().secretKey();
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
        resp.info = dir().info();
        return NO_ERROR;
    }

    EggsError _readDir(const ReadDirReq& req, ReadDirResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        WrappedSnapshot snapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.snapshot;

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
            auto it = WrappedIterator(_db->NewIterator(options, _edgesCf));
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
        WrappedSnapshot snapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.snapshot;

        {
            auto it = WrappedIterator(_db->NewIterator(options, _edgesCf));
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
        WrappedSnapshot snapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.snapshot;

        uint64_t nameHash;
        {
            std::string value;
            ExternalValue<DirectoryBody> dir;
            EggsError err = _getDirectory(options, req.dirId, false, value, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir().hashMode(), req.name.ref());
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
            WrappedIterator it(_db->NewIterator({}, _transientCf));
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
            WrappedIterator it(_db->NewIterator({}, cf));
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
        WrappedSnapshot snapshot(_db);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.snapshot;

        int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - FileSpansResp::STATIC_SIZE;
        // if -1, we ran out of budget.
        const auto addBlockService = [this, &resp, &budget](uint64_t blockServiceId) -> int {
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
            const auto& cache = _blockServicesCache.at(blockServiceId);
            blockService.id = blockServiceId;
            blockService.ip = cache.ip;
            blockService.port = cache.port;
            // TODO propagade block service flags here
            return resp.blockServices.els.size()-1;
        };

        StaticValue<SpanKey> beginKey;
        beginKey().setFileId(req.fileId);
        beginKey().setOffset(req.byteOffset);
        {
            WrappedIterator it(_db->NewIterator(options, _spansCf));
            for (it->SeekForPrev(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<SpanKey>::FromSlice(it->key());
                if (key().fileId() != req.fileId) {
                    break;
                }
                auto value = ExternalValue<SpanBody>::FromSlice(it->value());
                auto& respSpan = resp.spans.els.emplace_back();
                respSpan.byteOffset = key().offset();
                respSpan.parity = value().parity();
                respSpan.storageClass = value().storageClass();
                respSpan.crc32 = value().crc32();
                respSpan.size = value().spanSize();
                respSpan.blockSize = value().blockSize();
                if (value().storageClass() == INLINE_STORAGE) {
                    respSpan.bodyBytes = value().inlineBody();
                } else {
                    respSpan.bodyBytes.clear();
                    for (int i = 0; i < value().parity().blocks(); i++) {
                        auto block = value().block(i);
                        int blockServiceIx = addBlockService(block.blockServiceId);
                        if (blockServiceIx < 0) {
                            break; // no need to break in outer loop -- we will break out anyway because budget < 0
                        }
                        ALWAYS_ASSERT(blockServiceIx < 256);
                        auto& respBlock = respSpan.bodyBlocks.els.emplace_back();
                        respBlock.blockId = block.blockId;
                        respBlock.blockServiceIx = blockServiceIx;
                        respBlock.crc32 = block.crc32;
                    }
                }
                budget -= respSpan.packedSize();
                if (budget < 0) {
                    resp.nextOffset = respSpan.byteOffset;
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
        beginKey().setBlockServiceId(req.blockServiceId);
        beginKey().setFileId(req.startFrom);
        StaticValue<BlockServiceToFileKey> endKey;
        beginKey().setBlockServiceId(req.blockServiceId+1);
        beginKey().setFileId(NULL_INODE_ID);
        auto endKeySlice = endKey.toSlice();

        rocksdb::ReadOptions options;
        options.iterate_upper_bound = &endKeySlice;
        WrappedIterator it(_db->NewIterator(options));
        int i;
        for (
            it->Seek(beginKey.toSlice()), i = 0;
            it->Valid() && i < maxFiles;
            it->Next(), i++
        ) {
            auto key = ExternalValue<BlockServiceToFileKey>::FromSlice(it->key());
            resp.fileIds.els.emplace_back(key().fileId());
        }
        ROCKS_DB_CHECKED(it->status());
        return NO_ERROR;
    }

    EggsError _visitFiles(const VisitFilesReq& req, VisitFilesResp& resp) {
        return _visitInodes(_filesCf, req, resp);
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
        return NO_ERROR;
    }

    EggsError _prepareCreateDirectoryInode(EggsTime time, const CreateDirectoryInodeReq& req, CreateDirectoryInodeEntry& entry) {
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.id.type() != InodeType::DIRECTORY || req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.info.inherited != (req.info.body.size() == 0)) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        entry.id = req.id;
        entry.ownerId = req.ownerId;
        entry.info.inherited = req.info.inherited;
        entry.info.body = req.info.body;
        return NO_ERROR;
    }

    EggsError _prepareCreateLockedCurrentEdge(EggsTime time, const CreateLockedCurrentEdgeReq& req, CreateLockedCurrentEdgeEntry& entry) {
        if (entry.creationTime >= time) {
            return EggsError::CREATION_TIME_TOO_RECENT;
        }
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
        entry.creationTime = req.creationTime;
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
        return NO_ERROR;
    }

    EggsError _prepareRemoveDirectoryOwner(EggsTime time, const RemoveDirectoryOwnerReq& req, RemoveDirectoryOwnerEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.info.size() == 0) {
            return EggsError::BAD_DIRECTORY_INFO;
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
        if (req.info.inherited && req.id == ROOT_DIR_INODE_ID) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        if (req.info.inherited != (req.info.body.size() == 0)) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        entry.dirId = req.id;
        entry.info.inherited = req.info.inherited;
        entry.info.body = req.info.body;
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

    EggsError _prepareIntraShardHardFileUnlink(EggsTime time, const IntraShardHardFileUnlinkReq& req, IntraShardHardFileUnlinkEntry& entry) {
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

        // The two special cases
        if (req.storageClass == EMPTY_STORAGE) {
            if (req.bodyBytes.size() != 0 || req.bodyBlocks.els.size() != 0 || req.parity != Parity(0) || req.size != 0 || req.blockSize != 0) {
                return false;
            }
            if (req.crc32.data != crc32c("", 0)) {
                return false;
            }
            return true;
        }
        if (req.storageClass == INLINE_STORAGE) {
            if (req.bodyBytes.size() == 0 || req.bodyBlocks.els.size() != 0 || req.parity != Parity(0) || req.size != req.bodyBytes.size() || req.blockSize != 0) {
                return false;
            }
            auto expectedCrc32 = crc32c(req.bodyBytes.data(), req.bodyBytes.size());
            if (req.crc32.data != expectedCrc32) {
                return false;
            }
            return true;
        }

        if (req.parity.dataBlocks() == 1) {
            // mirroring blocks should all be the same
            for (const auto& block: req.bodyBlocks.els) {
                if (block.crc32 != req.crc32) {
                    return false;
                }
            }
        } else {
            // consistency check for the general case
            //
            // For parity mode 1+M (mirroring), for any M, the server can validate
            // that the CRC of the span equals the CRC of every block.
            // For N+M in the general case, it can probably validate that the CRC of
            // the span equals the concatenation of the CRCs of the N, and that the
            // CRC of the 1st of the M equals the XOR of the CRCs of the N.
            // It cannot validate anything about the rest of the M though (scrubbing
            // can validate the rest, if it wants to, but it would be too expensive
            // for the shard to validate).
            std::array<uint8_t, 4> blocksCrc32;
            memset(&blocksCrc32[0], 0, 4);
            for (const auto& block: req.bodyBlocks.els) {
                blocksCrc32 = crc32cCombine(blocksCrc32, block.crc32.data, req.blockSize);
            }
            if (req.crc32.data != blocksCrc32) {
                return false;
            }
            // TODO check parity block CRC32
        }

        return true;
    }

    bool _blockServiceMatchesBlacklist(const std::vector<BlockServiceBlacklist>& blacklists, uint64_t blockServiceId, const BlockServiceCache& cache) {
        for (const auto& blacklist: blacklists) {
            if (blacklist.id == blockServiceId) {
                return true;
            }
            if (blacklist.ip == cache.ip && blacklist.port == cache.port) {
                return true;
            }
        }
        return false;
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
        if (!_checkSpanBody(req)) {
            return EggsError::BAD_SPAN_BODY;
        }
    
        // start filling in entry
        entry.fileId = req.fileId;
        entry.byteOffset = req.byteOffset;
        entry.storageClass = req.storageClass;
        entry.parity = req.parity;
        entry.crc32 = req.crc32;
        entry.size = req.size;
        entry.blockSize = req.blockSize;
        entry.bodyBytes = req.bodyBytes;

        // Now fill in the block services. Generally we want to try to keep them the same
        // throughout the file, if possible, so that the likelihood of data loss is minimized.
        //
        // TODO factor failure domains in the decision
        if (entry.storageClass != INLINE_STORAGE && entry.storageClass != EMPTY_STORAGE) {
            std::vector<uint64_t> candidateBlockServices;
            candidateBlockServices.reserve(_currentBlockServices.size());
            LOG_DEBUG(_env, "Starting out with %s current block services", _currentBlockServices.size());
            for (uint64_t id: _currentBlockServices) {
                const auto& cache = _blockServicesCache.at(id);
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
            LOG_DEBUG(_env, "Starting out with %s block service candidates", candidateBlockServices.size());
            std::vector<uint64_t> pickedBlockServices;
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
                    for (
                        int i = 0;
                        i < span().parity().blocks() && pickedBlockServices.size() < req.parity.blocks() && candidateBlockServices.size() > 0;
                        i++
                    ) {
                        BlockBody spanBlock = span().block(i);
                        auto isCandidate = std::find(candidateBlockServices.begin(), candidateBlockServices.end(), spanBlock.blockServiceId);
                        if (isCandidate == candidateBlockServices.end()) {
                            continue;
                        }
                        LOG_DEBUG(_env, "(1) Picking block service candidate %s", spanBlock.blockServiceId);
                        pickedBlockServices.emplace_back(spanBlock.blockServiceId);
                        std::iter_swap(isCandidate, candidateBlockServices.end()-1);
                        candidateBlockServices.pop_back();
                    }
                }
            }
            // Fill in whatever remains. We don't need to be deterministic here (we would have to
            // if we were in log application), but we might as well.
            {
                uint64_t rand = time.ns;
                while (pickedBlockServices.size() < req.parity.blocks() && candidateBlockServices.size() > 0) {
                    uint64_t ix = splitmix64(rand) % candidateBlockServices.size();
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
                const auto& reqBlock = req.bodyBlocks.els[i];
                auto& block = entry.bodyBlocks.els[i];
                block.blockServiceId = pickedBlockServices[i];
                block.crc32 = reqBlock.crc32;
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
        case ShardMessageKind::INTRA_SHARD_HARD_FILE_UNLINK:
            err = _prepareIntraShardHardFileUnlink(time, req.getIntraShardHardFileUnlink(), logEntryBody.setIntraShardHardFileUnlink());
            break;
        case ShardMessageKind::REMOVE_SPAN_INITIATE:
            err = _prepareRemoveSpanInitiate(time, req.getRemoveSpanInitiate(), logEntryBody.setRemoveSpanInitiate());
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
            throw EGGS_EXCEPTION("UNIMPLEMENTED");
        default:
            throw EGGS_EXCEPTION("bad write shard message kind %s", req.kind());
        }

        if (err == NO_ERROR) {
            LOG_DEBUG(_env, "prepared log entry of kind %s, for request of kind %s", logEntryBody.kind(), req.kind());
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
        transientFile().setFileSize(0);
        transientFile().setMtime(time);
        transientFile().setDeadline(entry.deadlineTime);
        transientFile().setLastSpanState(SpanState::CLEAN);
        transientFile().setNote(entry.note.ref());
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
                ExternalValue<FileBody> file;
                EggsError err = _getFile({}, entry.fileId, fileValue, file); // don't overwrite old error
                if (err == NO_ERROR) {
                    return NO_ERROR; // the non-transient file exists already, we're done
                }
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
        file().setMtime(time);
        file().setFileSize(transientFile().fileSize());
        ROCKS_DB_CHECKED(batch.Put(_filesCf, fileKey.toSlice(), file.toSlice()));

        // create edge in owner.
        {
            EggsError err = ShardDBImpl::_createCurrentEdge(time, time, batch, entry.ownerId, entry.name, entry.fileId, false);
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
        // that we do not have snapshot edges to be the same. This should be very uncommon.
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

    // Note that we cannot expose an API which allows us to create non-locked current edges,
    // see comment for CreateLockedCurrentEdgeReq.
    //
    // `logEntryTime` and `creationTime` are separate since we need a different `creationTime`
    // for CreateDirectoryInodeReq.
    //
    // It must be the case that `creationTime <= logEntryTime`.
    EggsError _createCurrentEdge(EggsTime logEntryTime, EggsTime creationTime, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytes& name, InodeId targetId, bool locked) {
        ALWAYS_ASSERT(creationTime <= logEntryTime);

        // fetch the directory
        ExternalValue<DirectoryBody> dir;
        std::string dirValue;
        {
            // allowSnaphsot=false since we cannot create current edges in snapshot directories.
            EggsError err = _initiateDirectoryModification(logEntryTime, false, batch, dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        uint64_t nameHash = computeHash(dir().hashMode(), name.ref());

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
            WrappedIterator it(_db->NewIterator({}, _edgesCf));
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
                    (targetId != existingEdge().targetIdWithLocked().id() || creationTime != existingEdge().creationTime()) // we're trying to create a different locked edge
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
                    // this was current, so it's now owned.
                    v().setTargetIdWithOwned(InodeIdExtra(existingEdge().targetIdWithLocked().id(), true));
                    ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
                }
            }
        }

        // OK, we're now ready to insert the current edge
        StaticValue<CurrentEdgeBody> edgeBody;
        edgeBody().setTargetIdWithLocked(InodeIdExtra(targetId, locked));
        edgeBody().setCreationTime(creationTime);
        ROCKS_DB_CHECKED(batch.Put(_edgesCf, edgeKey.toSlice(), edgeBody.toSlice()));

        return NO_ERROR;

    }

    EggsError _applySameDirectoryRename(EggsTime time, rocksdb::WriteBatch& batch, const SameDirectoryRenameEntry& entry, SameDirectoryRenameResp& resp) {
        // First, remove the old edge -- which won't be owned anymore, since we're renaming it.
        {
            EggsError err = _softUnlinkCurrentEdge(time, batch, entry.dirId, entry.oldName, entry.targetId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // Now, create the new one
        {
            EggsError err = _createCurrentEdge(time, time, batch, entry.dirId, entry.newName, entry.targetId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }
        return NO_ERROR;
    }

    EggsError _softUnlinkCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, InodeId dirId, const BincodeBytes& name, InodeId targetId, bool owned) {
        // fetch the directory
        ExternalValue<DirectoryBody> dir;
        std::string dirValue;
        {
            // allowSnaphsot=false since we can't have current edges in snapshot dirs
            EggsError err = _initiateDirectoryModification(time, false, batch, dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        uint64_t nameHash = computeHash(dir().hashMode(), name.ref());

        // get the edge
        StaticValue<EdgeKey> edgeKey;
        edgeKey().setDirIdWithCurrent(dirId, true); // current=true
        edgeKey().setNameHash(nameHash);
        edgeKey().setName(name.ref());
        std::string edgeValue;
        auto status = _db->Get({}, _edgesCf, edgeKey.toSlice(), &edgeValue);
        if (status.IsNotFound()) {
            return EggsError::NAME_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        ExternalValue<CurrentEdgeBody> edgeBody(edgeValue);
        if (edgeBody().targetIdWithLocked().id() != targetId) {
            return EggsError::MISMATCHING_TARGET;
        }
        if (edgeBody().targetIdWithLocked().extra()) { // locked
            return EggsError::NAME_IS_LOCKED;
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
            v().setTargetIdWithOwned(InodeIdExtra(targetId, owned));
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
            k().setCreationTime(time);
            v().setTargetIdWithOwned(InodeIdExtra(NULL_INODE_ID, false));
            ROCKS_DB_CHECKED(batch.Put(_edgesCf, k.toSlice(), v.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applySoftUnlinkFile(EggsTime time, rocksdb::WriteBatch& batch, const SoftUnlinkFileEntry& entry, SoftUnlinkFileResp& resp) {
        return _softUnlinkCurrentEdge(time, batch, entry.ownerId, entry.name, entry.fileId, true);
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
            StaticValue<DirectoryBody> dir;
            dir().setOwnerId(entry.ownerId);
            dir().setMtime(time);
            dir().setHashMode(HashMode::XXH3_63);
            dir().setInfoInherited(entry.info.inherited);
            LOG_DEBUG(_env, "inherited: %s, hasInfo: %s, body: %s", entry.info.inherited, dir().mustHaveInfo(), entry.info.body.ref());
            dir().setInfo(entry.info.body.ref());
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, dirKey.toSlice(), dir.toSlice()));
        }

        resp.mtime = time;

        return NO_ERROR;
    }

    EggsError _applyCreateLockedCurrentEdge(EggsTime time, rocksdb::WriteBatch& batch, const CreateLockedCurrentEdgeEntry& entry, CreateLockedCurrentEdgeResp& resp) {
        LOG_INFO(_env, "Creating edge %s -> %s", entry.dirId, entry.targetId);
        return _createCurrentEdge(time, entry.creationTime, batch, entry.dirId, entry.name, entry.targetId, true); // locked=true
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
            snapshotBody().setTargetIdWithOwned(InodeIdExtra(entry.targetId, false)); // not owned (this was moved somewhere else)
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
        if (!edge().locked()) {
            edge().setTargetIdWithLocked(InodeIdExtra(entry.targetId, true)); // locked=true
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
            WrappedIterator it(_db->NewIterator({}, _edgesCf));
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

        // we need to create a new DirectoryBody, the materialized info might have changed size
        {
            StaticValue<DirectoryBody> newDir;
            newDir().setOwnerId(NULL_INODE_ID);
            newDir().setMtime(time);
            newDir().setHashMode(dir().hashMode());
            newDir().setInfoInherited(dir().infoInherited());
            newDir().setInfo(entry.info.ref());
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
            WrappedIterator it(_db->NewIterator({}, _edgesCf));
            it->Seek(edgeKey.toSlice());
            if (it->Valid()) {
                auto otherEdge = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (otherEdge().dirId() == entry.id) {
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
        ALWAYS_ASSERT(entry.id.type() == InodeType::FILE || entry.id.type() == InodeType::DIRECTORY);

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
                    return EggsError::FILE_NOT_FOUND;
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
                WrappedIterator it(_db->NewIterator({}, _spansCf));
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
        // set the owner, and restore the directory entry if needed.
        StaticValue<DirectoryBody> newDir;
        newDir().setOwnerId(entry.ownerId);
        newDir().setMtime(dir().mtime());
        newDir().setHashMode(dir().hashMode());
        newDir().setInfoInherited(dir().infoInherited());
        // remove directory info if it was inherited, now that we have an
        // owner again.
        if (dir().infoInherited()) {
            newDir().setInfo({});
        }
        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), newDir.toSlice()));
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

        StaticValue<DirectoryBody> newDir;
        newDir().setOwnerId(dir().ownerId());
        newDir().setMtime(dir().mtime());
        newDir().setHashMode(dir().hashMode());
        newDir().setInfoInherited(entry.info.inherited);
        // For snapshot directories, we need to preserve the last known info if inherited.
        // It'll be reset when it's made non-snapshot again.
        if (!entry.info.inherited || (dir().ownerId() != NULL_INODE_ID)) {
            newDir().setInfo(entry.info.body.ref());
        }

        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directoriesCf, k.toSlice(), newDir.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveNonOwnedEdge(EggsTime time, rocksdb::WriteBatch& batch, const RemoveNonOwnedEdgeEntry& entry, RemoveNonOwnedEdgeResp& resp) {
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnapshot=true since GC needs to be able to remove non-owned edges from snapshot dir
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.dirId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir().hashMode(), entry.name.ref());
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

    EggsError _applyIntraShardHardFileUnlink(EggsTime time, rocksdb::WriteBatch& batch, const IntraShardHardFileUnlinkEntry& entry, IntraShardHardFileUnlinkResp& resp) {
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
            v().setFileSize(file().fileSize());
            v().setMtime(time);
            v().setDeadline(0);
            v().setLastSpanState(SpanState::CLEAN);
            v().setNote(entry.name.ref());
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
            return EggsError::FILE_EMPTY;
        }
        
        LOG_DEBUG(_env, "deleting span from file %s of size %s", entry.fileId, file().fileSize());

        // Fetch the last span
        WrappedIterator spanIt(_db->NewIterator({}, _spansCf));
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
        
        // Otherwise, we need to condemn it first, and then certify the deletion.
        // Note that we allow to remove dirty spans -- this is important to deal well with
        // the case where a writer dies in the middle of adding a span.
        file().setLastSpanState(SpanState::CONDEMNED);
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // Fill in the response blocks
        resp.blocks.els.reserve(span().parity().blocks());
        for (int i = 0; i < span().parity().blocks(); i++) {
            const auto& block = span().block(i);
            const auto& cache = _blockServicesCache.at(block.blockServiceId);
            auto& respBlock = resp.blocks.els.emplace_back();
            respBlock.blockServiceIp = cache.ip;
            respBlock.blockServicePort = cache.port;
            respBlock.blockServiceId = block.blockServiceId;
            respBlock.blockId = block.blockId;
            respBlock.certificate = _blockDeleteCertificate(span().blockSize(), block, cache.secretKey);
        }

        return NO_ERROR;
    }

    // Important for this to never error: we rely on the write to go through since 
    // we write _currentBlockServices/_blockServicesCache, which we rely to be in
    // sync with RocksDB.
    void _applyUpdateBlockServices(EggsTime time, rocksdb::WriteBatch& batch, const UpdateBlockServicesEntry& entry) {
        // The idea here is that we ask for some block services to use to shuckle once a minute,
        // and then just pick at random within them. We shouldn't have many to pick from at any
        // given point.
        ALWAYS_ASSERT(entry.blockServices.els.size() < 256); // TODO proper error
        OwnedValue<CurrentBlockServicesBody> currentBody(entry.blockServices.els.size());
        _currentBlockServices.resize(entry.blockServices.els.size());
        StaticValue<BlockServiceKey> blockKey;
        blockKey().setKey(BLOCK_SERVICE_KEY);
        StaticValue<BlockServiceBody> blockBody;
        for (int i = 0; i < entry.blockServices.els.size(); i++) {
            const auto& entryBlock = entry.blockServices.els[i];
            currentBody().set(i, entryBlock.id);
            _currentBlockServices[i] = entryBlock.id;
            blockKey().setBlockServiceId(entryBlock.id);
            blockBody().setId(entryBlock.id);
            blockBody().setIp(entryBlock.ip.data);
            blockBody().setPort(entryBlock.port);
            blockBody().setStorageClass(entryBlock.storageClass);
            blockBody().setFailureDomain(entryBlock.failureDomain.data);
            blockBody().setSecretKey(entryBlock.secretKey.data);
            ROCKS_DB_CHECKED(batch.Put(_defaultCf, blockKey.toSlice(), blockBody.toSlice()));
            auto& cache = _blockServicesCache[entryBlock.id];
            cache.secretKey = entryBlock.secretKey.data;
            cache.ip = entryBlock.ip.data;
            cache.port = entryBlock.port;
            cache.storageClass = entryBlock.storageClass;
        }
        ROCKS_DB_CHECKED(batch.Put(_defaultCf, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY), currentBody.toSlice()));
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

    EggsError _applyAddSpanInitiate(EggsTime time, rocksdb::WriteBatch& batch, const AddSpanInitiateEntry& entry, AddSpanInitiateResp& resp) {
        std::string fileValue;
        ExternalValue<TransientFileBody> file;
        {
            EggsError err = _initiateTransientFileModification(time, false, batch, entry.fileId, fileValue, file);
            if (err != NO_ERROR) {
                return err;
            }
        }

        // Special case -- for empty spans we have nothing to do
        if (entry.size == 0) {
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
                    existingSpan().blockSize() != entry.blockSize ||
                    existingSpan().parity() != entry.parity ||
                    existingSpan().crc32() != entry.crc32
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

        // Update the file with the new file size and set the last span state to dirty, 
        // if we're not blockless
        file().setFileSize(entry.byteOffset+entry.size);
        if (entry.storageClass != EMPTY_STORAGE && entry.storageClass != INLINE_STORAGE) {
            file().setLastSpanState(SpanState::DIRTY);
        }
        {
            auto k = InodeIdKey::Static(entry.fileId);
            ROCKS_DB_CHECKED(batch.Put(_transientCf, k.toSlice(), file.toSlice()));
        }

        // Now manufacture and add the span, also recording the blocks
        // in the block service -> files index.
        OwnedValue<SpanBody> spanBody(entry.storageClass, entry.parity, entry.bodyBytes.ref());
        {
            spanBody().setSpanSize(entry.size);
            spanBody().setBlockSize(entry.blockSize);
            spanBody().setCrc32(entry.crc32.data);
            uint64_t nextBlockId = _getNextBlockId();
            BlockBody block;
            for (int i = 0; i < entry.parity.blocks(); i++) {
                auto& entryBlock = entry.bodyBlocks.els[i];
                block.blockId = _updateNextBlockId(time, nextBlockId);
                block.blockServiceId = entryBlock.blockServiceId;
                block.crc32 = entryBlock.crc32.data;
                spanBody().setBlock(i, block);
                {
                    StaticValue<BlockServiceToFileKey> k;
                    k().setBlockServiceId(entryBlock.blockServiceId);
                    k().setFileId(entry.fileId);
                    ROCKS_DB_CHECKED(batch.Put(_blockServicesToFilesCf, k.toSlice(), {}));
                }
            }
            _writeNextBlockId(batch, nextBlockId);
            ROCKS_DB_CHECKED(batch.Put(_spansCf, spanKey.toSlice(), spanBody.toSlice()));
        }

        // Fill in the response
        resp.blocks.els.reserve(spanBody().parity().blocks());
        BlockBody block;
        for (int i = 0; i < spanBody().parity().blocks(); i++) {
            BlockBody block = spanBody().block(i);
            auto& respBlock = resp.blocks.els.emplace_back();
            respBlock.blockServiceId = block.blockServiceId;
            respBlock.blockId = block.blockId;
            const auto& cache = _blockServicesCache.at(block.blockServiceId);
            respBlock.blockServiceIp = cache.ip;
            respBlock.blockServicePort = cache.port;
            respBlock.certificate.data = _blockAddCertificate(spanBody().blockSize(), block, cache.secretKey);
        }

        return NO_ERROR;
    }

    std::array<uint8_t, 8> _blockAddCertificate(uint32_t blockSize, const BlockBody& block, const std::array<uint8_t, 16>& secretKey) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ4sI', b, 0, block['block_service_id'], b'w', block['block_id'], crc32_from_int(block['crc32']), block_size)
        bbuf.packScalar<uint64_t>(block.blockServiceId);
        bbuf.packScalar<char>('w');
        bbuf.packScalar<uint64_t>(block.blockId);
        bbuf.packFixedBytes<4>({block.crc32});
        bbuf.packScalar<uint32_t>(blockSize);

        return cbcmac(secretKey, (uint8_t*)buf, sizeof(buf));
    }

    bool _checkBlockAddProof(uint64_t blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
        bbuf.packScalar<uint64_t>(blockServiceId);
        bbuf.packScalar<char>('W');
        bbuf.packScalar<uint64_t>(proof.blockId);
    
        const auto& cache = _blockServicesCache.at(blockServiceId);
        auto expectedProof = cbcmac(cache.secretKey, (uint8_t*)buf, sizeof(buf));

        return proof.proof == expectedProof;
    }

    std::array<uint8_t, 8> _blockDeleteCertificate(uint32_t blockSize, const BlockBody& block, const std::array<uint8_t, 16>& secretKey) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'e', block['block_id'])
        bbuf.packScalar<uint64_t>(block.blockServiceId);
        bbuf.packScalar<char>('e');
        bbuf.packScalar<uint64_t>(block.blockId);

        return cbcmac(secretKey, (uint8_t*)buf, sizeof(buf));
    }

    bool _checkBlockDeleteProof(uint64_t blockServiceId, const BlockProof& proof) {
        char buf[32];
        memset(buf, 0, sizeof(buf));
        BincodeBuf bbuf(buf, sizeof(buf));
        // struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'E', block['block_id'])
        bbuf.packScalar<uint64_t>(blockServiceId);
        bbuf.packScalar<char>('E');
        bbuf.packScalar<uint64_t>(proof.blockId);
    
        const auto& cache = _blockServicesCache.at(blockServiceId);
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
            if (span().parity().blocks() != entry.proofs.els.size()) {
                return EggsError::BAD_NUMBER_OF_BLOCKS_PROOFS;
            }
            BlockBody block;
            for (int i = 0; i < span().parity().blocks(); i++) {
                BlockBody block = span().block(i);
                if (!_checkBlockAddProof(block.blockServiceId, entry.proofs.els[i])) {
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
                if (err != NO_ERROR) {
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
        transientFile().setFileSize(file().fileSize());
        transientFile().setMtime(time);
        transientFile().setDeadline(0);
        transientFile().setLastSpanState(SpanState::CLEAN);
        transientFile().setNote(entry.note.ref());
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

        // Make sure we're condemned
        if (file().lastSpanState() != SpanState::CONDEMNED) {
            return EggsError::SPAN_NOT_FOUND; // TODO maybe better error?
        }

        // Verify proofs
        if (entry.proofs.els.size() != span().parity().blocks()) {
            return EggsError::BAD_NUMBER_OF_BLOCKS_PROOFS;
        }
        for (int i = 0; i < span().parity().blocks(); i++) {
            const auto& block = span().block(i);
            const auto& proof = entry.proofs.els[i];
            if (block.blockId != proof.blockId) {
                RAISE_ALERT(_env, "bad block proof id, expected %s, got %s", block.blockId, proof.blockId);
                return EggsError::BAD_BLOCK_PROOF;
            }
            if (!_checkBlockDeleteProof(block.blockServiceId, proof)) {
                return EggsError::BAD_BLOCK_PROOF;
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
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // the GC needs to work on deleted dirs who might still have owned files, so allowSnapshot=true
            EggsError err = _initiateDirectoryModification(time, true, batch, entry.ownerId, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir().hashMode(), entry.name.ref());
        }

        {
            StaticValue<EdgeKey> edgeKey;
            edgeKey().setDirIdWithCurrent(entry.ownerId, false); // snapshot (current=false)
            edgeKey().setNameHash(nameHash);
            edgeKey().setName(entry.name.ref());
            edgeKey().setCreationTime(time);
            ROCKS_DB_CHECKED(batch.Delete(_edgesCf, edgeKey.toSlice()));
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

        LOG_DEBUG(_env, "about to apply log entry %s", logEntryBody);

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
        case ShardLogEntryKind::INTRA_SHARD_HARD_FILE_UNLINK:
            err = _applyIntraShardHardFileUnlink(time, batch, logEntryBody.getIntraShardHardFileUnlink(), resp.setIntraShardHardFileUnlink());
            break;
        case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
            err = _applyRemoveSpanInitiate(time, batch, logEntryBody.getRemoveSpanInitiate(), resp.setRemoveSpanInitiate());
            break;
        case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
            // no resp here -- the callers are internal
            _applyUpdateBlockServices(time, batch, logEntryBody.getUpdateBlockServices());
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
        default:
            throw EGGS_EXCEPTION("bad log entry kind %s", logEntryBody.kind());
        }

        if (err != NO_ERROR) {
            LOG_INFO(_env, "could not apply log entry %s, index %s, because of err %s", logEntryBody.kind(), logIndex, err);
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
        return cbcmac(_secretKey, (const uint8_t*)&id, sizeof(id));
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

BincodeBytes defaultDirectoryInfo() {
    char buf[255];
    DirectoryInfoBody info;
    info.version = 0;
    // delete after 30 days
    info.deleteAfterTime = (30ull /*days*/ * 24 /*hours*/ * 60 /*minutes*/ * 60 /*seconds*/ * 1'000'000'000 /*ns*/) | (1ull<<63);
    // do not delete after N versions
    info.deleteAfterVersions = 0;
    // up to 1MiB flash, up to 10MiB HDD (should be 100MiB really, it's nicer to test with smaller
    // spans now).
    auto& flashPolicy = info.spanPolicies.els.emplace_back();
    flashPolicy.maxSize = 1ull << 20; // 1MiB
    flashPolicy.storageClass = storageClassByName("FLASH");
    flashPolicy.parity = Parity(1, 1); // RAID1
    auto& hddPolicy = info.spanPolicies.els.emplace_back();
    hddPolicy.maxSize = 10ull << 20; // 10MiB
    hddPolicy.storageClass = storageClassByName("HDD");
    hddPolicy.parity = Parity(1, 1); // RAID1

    BincodeBuf bbuf(buf, 255);
    info.pack(bbuf);

    return BincodeBytes(buf, bbuf.len());
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
    case ShardMessageKind::ADD_SPAN_CERTIFY:
    case ShardMessageKind::LINK_FILE:
    case ShardMessageKind::SOFT_UNLINK_FILE:
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
    case ShardMessageKind::SET_DIRECTORY_INFO:
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
    case ShardMessageKind::INTRA_SHARD_HARD_FILE_UNLINK:
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
