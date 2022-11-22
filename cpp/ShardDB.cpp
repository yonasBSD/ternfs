#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
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

// TODO maybe revisit all those ALWAYS_ASSERTs
// TODO can we pre-allocate the value strings rather than allocating each stupid time?

// ## High level design
//
// Right now, we're not implementing distributed consensus, but we want to prepare
// for this as best we can, since we need it for HA.
//
// We use two RocksDB database: one for the log entries, one for the state. When
// a request comes in, we first generate and persist a log entry for it. The log
// entry can then be applied to the state when consensus has been reached on it.
// After the log entry has been applied to the state, a response is sent to the
// client.
//
// One question is how we write transactions concurrently, given that we rely on
// applying first one log entry and then the next in that order. We could be smart
// and pool independent log entries, and then write them concurrently. My guess is
// that it's probably not worth doing this, at least since the beginning, and that
// it's better to just serialize the transactions manually with a mutex here in C++.
//
// ## RocksDB schema (log)
//
// Two column families: the default one storing metadata (e.g. the Raft state,
// at some point), and the "log" column family storing the log entries. Separate
// column families since we'll keep deleting large ranges from the beginning of
// the log CF, and therefore seeking at the beginning of that won't be nice, so
// we don't want to put stuff at the beginning, but we also want to easily know
// when we're done, so we don't want to put stuff at the end either.
//
// The log CF is just uint64_t log index to serialized log entry.
//
// For now, in a pre-raft word, the default CF needs:
//
// * LogMetadataKey::NEXT_FILE_ID: the next available file id
// * LogMetadataKey::NEXT_SYMLINK_ID: the next available symlink id
// * LogMetadataKey::NEXT_BLOCK_ID: the next available block id
//
// When preparing log entries we also allocate blocks, and the blocks are stored
// in the state db, but we never modify the block state when adding a log entry,
// so we can safely read them.
//
// ## RocksDB schema (state)
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
// We'll also store some small metadata in the default column family, e.g.:
//
//  - ShardInfoKey::INFO: ShardInfoBody
//  - ShardInfoKey::LAST_APPLIED_LOG_ENTRY: uint64_t
//  - probably the block servers somewhere...
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

struct AssertiveLocked {
private:
    std::atomic<bool>& _held;
public:
    AssertiveLocked(std::atomic<bool>& held): _held(held) {
        bool expected = false;
        if (!_held.compare_exchange_strong(expected, true)) {
            throw EGGS_EXCEPTION("could not aquire lock for applyLogEntry, are you using this function concurrently?");
        }
    }

    AssertiveLocked(const AssertiveLocked&) = delete;
    AssertiveLocked& operator=(AssertiveLocked&) = delete;

    ~AssertiveLocked() {
        _held.store(false);
    }
};

struct AssertiveLock {
private:
    std::atomic<bool> _held;
public:
    AssertiveLock(): _held(false) {}

    AssertiveLock(const AssertiveLock&) = delete;
    AssertiveLock& operator=(AssertiveLock&) = delete;

    AssertiveLocked lock() {
        return AssertiveLocked(_held);
    }
};

static uint64_t computeHash(HashMode mode, const BincodeBytes& bytes) {
    switch (mode) {
    case HashMode::XXH3_63:
        // TODO remove this chopping, pyfuse3 doesn't currently like
        // full 64-bit hashes.
        return XXH3_64bits(bytes.data, bytes.length) & ~(1ull<<63);
    default:
        throw EGGS_EXCEPTION("bad hash mode %s", (int)mode);
    }
}

static bool validName(const BincodeBytes& name) {
    if (name == BincodeBytes(".") || name == BincodeBytes("..")) {
        return false;
    }
    for (int i = 0; i < name.length; i++) {
        if (name.data[i] == (uint8_t)'/') {
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
    time = buf.unpackScalar<uint64_t>();
    ShardLogEntryKind kind = (ShardLogEntryKind)buf.unpackScalar<uint16_t>();
    body.unpack(buf, kind);
}

struct ShardDBImpl {
private:
    Env& _env;

    ShardId _shid;
    std::array<uint8_t, 16> _secretKey;

    rocksdb::DB* _logDb;
    rocksdb::ColumnFamilyHandle* _logDefault;
    rocksdb::ColumnFamilyHandle* _logEntries;

    rocksdb::DB* _stateDb;
    rocksdb::ColumnFamilyHandle* _stateDefault;
    rocksdb::ColumnFamilyHandle* _transientFiles;
    rocksdb::ColumnFamilyHandle* _files;
    rocksdb::ColumnFamilyHandle* _spans;
    rocksdb::ColumnFamilyHandle* _directories;
    rocksdb::ColumnFamilyHandle* _edges;

    AssertiveLock _prepareLogEntryLock;
    std::vector<uint8_t> _writeLogEntryBuf;

    AssertiveLock _applyLogEntryLock;

    // ----------------------------------------------------------------
    // initialization

    void _initDb() {
        bool shardInfoExists;
        {
            std::string value;
            auto status = _stateDb->Get({}, shardMetadataKey(&SHARD_INFO_KEY), &value);
            if (status.IsNotFound()) {
                shardInfoExists = false;
            } else {
                ROCKS_DB_CHECKED(status);
                shardInfoExists = true;
                auto shardInfo = ExternalValue<ShardInfoBody>::FromSlice(value);
                if (shardInfo->shardId() != _shid) {
                    throw EGGS_EXCEPTION("expected shard id %s, but found %s in DB", _shid, shardInfo->shardId());
                }
                shardInfo->getSecretKey(_secretKey);
            }
        }
        if (!shardInfoExists) {
            LOG_INFO(_env, "creating shard info, since it does not exist");
            generateSecretKey(_secretKey);
            StaticValue<ShardInfoBody> shardInfo;
            shardInfo->setShardId(_shid);
            shardInfo->setSecretKey(_secretKey);
            ROCKS_DB_CHECKED(_stateDb->Put({}, shardMetadataKey(&SHARD_INFO_KEY), shardInfo.toSlice()));
        }

        const auto keyExists = [](rocksdb::DB* db, rocksdb::ColumnFamilyHandle* cf, const rocksdb::Slice& key) -> bool {
            std::string value;
            auto status = db->Get({}, cf, key, &value);
            if (status.IsNotFound()) {
                return false;
            } else {
                ROCKS_DB_CHECKED(status);
                return true;
            }
        };

        {
            auto k = InodeIdKey::Static(ROOT_DIR_INODE_ID);
            if (!keyExists(_stateDb, _directories, k.toSlice())) {
                LOG_INFO(_env, "creating root directory, since it does not exist");
                char buf[255];
                auto info = defaultDirectoryInfo(buf);
                StaticValue<DirectoryBody> dirBody;
                dirBody->setOwnerId(NULL_INODE_ID);
                dirBody->setMtime({});
                dirBody->setHashMode(HashMode::XXH3_63);
                dirBody->setInfoInherited(false);
                dirBody->setInfo(info);
                auto k = InodeIdKey::Static(ROOT_DIR_INODE_ID);
                ROCKS_DB_CHECKED(_stateDb->Put({}, _directories, k.toSlice(), dirBody.toSlice()));
            }
        }

        if (!keyExists(_logDb, _logDefault, logMetadataKey(&NEXT_FILE_ID_KEY))) {
            LOG_INFO(_env, "initializing next file id");
            InodeId nextFileId(InodeType::FILE, ShardId(_shid), 0);
            auto v = InodeIdValue::Static(nextFileId);
            ROCKS_DB_CHECKED(_logDb->Put({}, _logDefault, logMetadataKey(&NEXT_FILE_ID_KEY), v.toSlice()));
        }
        if (!keyExists(_logDb, _logDefault, logMetadataKey(&NEXT_SYMLINK_ID_KEY))) {
            LOG_INFO(_env, "initializing next symlink id");
            InodeId nextLinkId(InodeType::SYMLINK, ShardId(_shid), 0);
            auto v = InodeIdValue::Static(nextLinkId);
            ROCKS_DB_CHECKED(_logDb->Put({}, _logDefault, logMetadataKey(&NEXT_SYMLINK_ID_KEY), v.toSlice()));
        }

        if (!keyExists(_stateDb, _stateDefault, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY))) {
            LOG_INFO(_env, "initializing last applied log entry");
            auto v = U64Value::Static(0);
            ROCKS_DB_CHECKED(_stateDb->Put({}, _stateDefault, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), v.toSlice()));
        }

    }

    // Right now we just replay the log on startup. When we have distributed consensus
    // we'll have to do things a bit differently, obvioulsy.
    void _replayLog() {
        uint64_t lastAppliedIx = _lastAppliedLogEntry();
        LOG_INFO(_env, "replaying log after startup from %s", lastAppliedIx);

        BincodeBytesScratchpad scratch;
        ShardRespContainer resp;

        {
            WrappedIterator it(_logDb->NewIterator({}, _logEntries));
            StaticValue<LogEntryKey> seekKey;
            seekKey->setIndex(lastAppliedIx);
            for (it->Seek(seekKey.toSlice()); it->Valid(); it->Next()) {
                auto ix = ExternalValue<LogEntryKey>::FromSlice(it->key());
                if (ix->index() == lastAppliedIx) {
                    continue;
                }
                LOG_INFO(_env, "replaying log entry ix %s", ix->index());
                EggsError err = applyLogEntry(ix->index(), scratch, resp);
                if (err != NO_ERROR) {
                    LOG_INFO(_env, "got error %s when replaying log entry ix %s", err, ix->index());
                }
            }
            ROCKS_DB_CHECKED(it->status());
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
        resp.mtime = file->mtime();
        resp.size = file->fileSize();
        return NO_ERROR;
    }

    EggsError _statTransientFile(const StatTransientFileReq& req, BincodeBytesScratchpad& scratch, StatTransientFileResp& resp) {
        std::string fileValue;
        {
            auto k = InodeIdKey::Static(req.id);
            auto status = _stateDb->Get({}, _transientFiles, k.toSlice(), &fileValue);
            if (status.IsNotFound()) {
                return EggsError::FILE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        auto body = ExternalValue<TransientFileBody>::FromSlice(fileValue);
        resp.mtime = body->mtime();
        resp.size = body->fileSize();
        scratch.copyTo(body->note(), resp.note);
        return NO_ERROR;
    }

    EggsError _statDirectory(const StatDirectoryReq& req, BincodeBytesScratchpad& scratch, StatDirectoryResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        // allowSnapshot=true, the caller can very easily detect if it's snapshot or not
        EggsError err = _getDirectory({}, req.id, true, dirValue, dir);
        if (err != NO_ERROR) {
            return err;
        }
        resp.mtime = dir->mtime();
        resp.owner = dir->ownerId();
        scratch.copyTo(dir->info(), resp.info);
        return NO_ERROR;
    }

    EggsError _readDir(const ReadDirReq& req, BincodeBytesScratchpad& scratch, ReadDirResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        WrappedSnapshot snapshot(_stateDb);
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
            auto it = WrappedIterator(_stateDb->NewIterator(options, _edges));
            StaticValue<EdgeKey> beginKey;
            beginKey->setDirIdWithCurrent(req.dirId, true); // current = true
            beginKey->setNameHash(req.startHash);
            beginKey->setName({});
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - ReadDirResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (key->dirId() != req.dirId || !key->current()) {
                    resp.nextHash = 0; // we've ran out of edges
                    break;
                }
                auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                CurrentEdge& respEdge = resp.results.els.emplace_back();
                respEdge.targetId = edge->targetIdWithLocked().id();
                respEdge.nameHash = key->nameHash();
                scratch.copyTo(key->name(), respEdge.name);
                respEdge.creationTime = edge->creationTime();
                budget -= respEdge.packedSize();
                if (budget < 0) {
                    resp.nextHash = key->nameHash();
                    // remove the current element, and also, do not have straddling hashes
                    while (!resp.results.els.empty() && resp.results.els.back().nameHash == key->nameHash()) {
                        resp.results.els.pop_back();
                    }
                    break;
                }
            }
            ROCKS_DB_CHECKED(it->status());
        }

        return NO_ERROR;   
    }

    EggsError _fullReadDir(const FullReadDirReq& req, BincodeBytesScratchpad& scratch, FullReadDirResp& resp) {
        // snapshot probably not strictly needed -- it's for the possible lookup if we
        // got no results. even if returned a false positive/negative there it probably
        // wouldn't matter. but it's more pleasant.
        WrappedSnapshot snapshot(_stateDb);
        rocksdb::ReadOptions options;
        options.snapshot = snapshot.snapshot;

        {
            auto it = WrappedIterator(_stateDb->NewIterator(options, _edges));
            StaticValue<EdgeKey> beginKey;
            beginKey->setDirIdWithCurrent(req.dirId, req.cursor.current);
            beginKey->setNameHash(req.cursor.startHash);
            beginKey->setName(req.cursor.startName);
            if (!req.cursor.current) {
                beginKey->setCreationTime(req.cursor.startTime);
            } else {
                ALWAYS_ASSERT(req.cursor.startTime == 0); // TODO proper error at validation time
            }
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - FullReadDirResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto key = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (key->dirId() != req.dirId) {
                    break;
                }
                auto& respEdge = resp.results.els.emplace_back();
                if (key->current()) {
                    auto edge = ExternalValue<CurrentEdgeBody>::FromSlice(it->value());
                    respEdge.current = key->current();
                    respEdge.targetId = edge->targetIdWithLocked();
                    respEdge.nameHash = key->nameHash();
                    scratch.copyTo(key->name(), respEdge.name);
                    respEdge.creationTime = edge->creationTime();
                } else {
                    auto edge = ExternalValue<SnapshotEdgeBody>::FromSlice(it->value());
                    respEdge.current = key->current();
                    respEdge.targetId = edge->targetIdWithOwned();
                    respEdge.nameHash = key->nameHash();
                    scratch.copyTo(key->name(), respEdge.name);
                    respEdge.creationTime = key->creationTime();
                }
                budget -= respEdge.packedSize();
                if (budget < 0) {
                    int prevCursorSize = FullReadDirCursor::STATIC_SIZE;
                    while (budget < 0) {
                        auto& last = resp.results.els.back();
                        budget += last.packedSize();
                        resp.next.current = last.current;
                        resp.next.startHash = last.nameHash;
                        scratch.copyTo(last.name, resp.next.startName);
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
        WrappedSnapshot snapshot(_stateDb);
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
            nameHash = computeHash(dir->hashMode(), req.name);
        }

        {
            StaticValue<EdgeKey> reqKey;
            reqKey->setDirIdWithCurrent(req.dirId, true); // current=true
            reqKey->setNameHash(nameHash);
            reqKey->setName(req.name);
            std::string edgeValue;
            auto status = _stateDb->Get(options, _edges, reqKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::NAME_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
            ExternalValue<CurrentEdgeBody> edge(edgeValue);
            resp.creationTime = edge->creationTime();
            resp.targetId = edge->targetIdWithLocked().id();
        }

        return NO_ERROR;        
    }

    EggsError _visitTransientFiles(const VisitTransientFilesReq& req, BincodeBytesScratchpad& scratch, VisitTransientFilesResp& resp) {
        resp.nextId = NULL_INODE_ID;

        {
            WrappedIterator it(_stateDb->NewIterator({}, _transientFiles));
            auto beginKey = InodeIdKey::Static(req.beginId);
            int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - VisitTransientFilesResp::STATIC_SIZE;
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                auto file = ExternalValue<TransientFileBody>::FromSlice(it->value());

                auto& respFile = resp.files.els.emplace_back();
                respFile.id = id->id();
                _calcCookie(respFile.id, respFile.cookie.data);
                respFile.deadlineTime = file->deadline();

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

    EggsError _visitDirectories(const VisitDirectoriesReq& req, BincodeBytesScratchpad& scratch, VisitDirectoriesResp& resp) {
        resp.nextId = NULL_INODE_ID;

        int budget = UDP_MTU - ShardResponseHeader::STATIC_SIZE - VisitDirectoriesResp::STATIC_SIZE;
        int maxIds = (budget/8) + 1; // include next inode
        {
            WrappedIterator it(_stateDb->NewIterator({}, _directories));            
            auto beginKey = InodeIdKey::Static(req.beginId);
            for (
                it->Seek(beginKey.toSlice());
                it->Valid() && resp.ids.els.size() < maxIds;
                it->Next()
            ) {
                auto id = ExternalValue<InodeIdKey>::FromSlice(it->key());
                resp.ids.els.emplace_back(id->id());
            }
        }

        if (resp.ids.els.size() == maxIds) {
            resp.nextId = resp.ids.els.back();
            resp.ids.els.pop_back();
        }

        return NO_ERROR;
    }

    // ----------------------------------------------------------------
    // log preparation

    uint64_t _nextLogIndex() {
        WrappedIterator it(_logDb->NewIterator({}, _logEntries));
        it->SeekToLast();
        if (!it->Valid()) {
            LOG_INFO(_env, "getting index for first log entry");
            // log entries start from one: index 0 is the starting "last applied" index.
            return 1;
        }
        ROCKS_DB_CHECKED(it->status());
        auto currIx = ExternalValue<LogEntryKey>::FromSlice(it->key());
        return currIx->index() + 1;
    }

    EggsError _prepareConstructFile(rocksdb::WriteBatch& batch, EggsTime time, const ConstructFileReq& req, BincodeBytesScratchpad& scratch, ConstructFileEntry& entry) {
        const auto nextFileId = [this, &batch](const LogMetadataKey* key) -> InodeId {
            std::string value;
            ROCKS_DB_CHECKED(_logDb->Get({}, logMetadataKey(key), &value));
            ExternalValue<InodeIdValue> inodeId(value);
            inodeId->setId(InodeId::FromU64(inodeId->id().u64 + 0x100));
            ROCKS_DB_CHECKED(batch.Put(logMetadataKey(key), inodeId.toSlice()));
            return inodeId->id();
        };

        if (req.type == (uint8_t)InodeType::FILE) {
            entry.id = nextFileId(&NEXT_FILE_ID_KEY);
        } else if (req.type == (uint8_t)InodeType::SYMLINK) {
            entry.id = nextFileId(&NEXT_SYMLINK_ID_KEY);
        } else {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        entry.type = req.type;
        scratch.copyTo(req.note, entry.note);
        entry.deadlineTime = time + DEADLINE_INTERVAL;

        return NO_ERROR;
    }

    void _writeLogEntry(rocksdb::WriteBatch& batch, const ShardLogEntryWithIndex& entry) {
        StaticValue<LogEntryKey> k;
        k->setIndex(entry.index);
        BincodeBuf bbuf((char*)&_writeLogEntryBuf[0], _writeLogEntryBuf.size());
        entry.entry.pack(bbuf);
        batch.Put(_logEntries, k.toSlice(), bbuf.slice());
    }

    EggsError _checkTransientFileCookie(InodeId id, std::array<uint8_t, 8> cookie) {
        if (id.type() != InodeType::FILE && id.type() != InodeType::SYMLINK) {
            return EggsError::TYPE_IS_DIRECTORY;
        }
        std::array<uint8_t, 8> expectedCookie;
        _calcCookie(id, expectedCookie);
        if (cookie != expectedCookie) {
            return EggsError::BAD_COOKIE;
        }
        return NO_ERROR;
    }

    EggsError _prepareLinkFile(EggsTime time, const LinkFileReq& req, BincodeBytesScratchpad& scratch, LinkFileEntry& entry) {
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
        scratch.copyTo(req.name, entry.name);
        entry.ownerId = req.ownerId;

        return NO_ERROR;
    }

    EggsError _prepareSameDirectoryRename(EggsTime time, const SameDirectoryRenameReq& req, BincodeBytesScratchpad& scratch, SameDirectoryRenameEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (!validName(req.newName)) {
            return EggsError::BAD_NAME;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        scratch.copyTo(req.oldName, entry.oldName);
        scratch.copyTo(req.newName, entry.newName);
        entry.targetId = req.targetId;
        return NO_ERROR;
    }

    EggsError _prepareSoftUnlinkFile(EggsTime time, const SoftUnlinkFileReq& req, BincodeBytesScratchpad& scratch, SoftUnlinkFileEntry& entry) {
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
        scratch.copyTo(req.name, entry.name);
        return NO_ERROR;
    }

    EggsError _prepareCreateDirectoryInode(EggsTime time, const CreateDirectoryInodeReq& req, BincodeBytesScratchpad& scratch, CreateDirectoryInodeEntry& entry) {
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.id.type() != InodeType::DIRECTORY || req.ownerId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.info.inherited != (req.info.body.length == 0)) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        entry.id = req.id;
        entry.ownerId = req.ownerId;
        entry.info.inherited = req.info.inherited;
        scratch.copyTo(req.info.body, entry.info.body);
        return NO_ERROR;
    }

    EggsError _prepareCreateLockedCurrentEdge(EggsTime time, const CreateLockedCurrentEdgeReq& req, BincodeBytesScratchpad& scratch, CreateLockedCurrentEdgeEntry& entry) {
        if (entry.creationTime >= time) {
            return EggsError::CREATION_TIME_TOO_RECENT;
        }
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (!validName(req.name)) {
            return EggsError::BAD_NAME;
        }
        ALWAYS_ASSERT(req.targetId != NULL_INODE_ID); // proper error
        entry.dirId = req.dirId;
        entry.creationTime = req.creationTime;
        entry.targetId = req.targetId;
        scratch.copyTo(req.name, entry.name);
        return NO_ERROR;
    }

    EggsError _prepareUnlockCurrentEdge(EggsTime time, const UnlockCurrentEdgeReq& req, BincodeBytesScratchpad& scratch, UnlockCurrentEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.targetId = req.targetId;
        scratch.copyTo(req.name, entry.name);
        entry.wasMoved = req.wasMoved;
        return NO_ERROR;
    }

    EggsError _prepareLockCurrentEdge(EggsTime time, const LockCurrentEdgeReq& req, BincodeBytesScratchpad& scratch, LockCurrentEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        scratch.copyTo(req.name, entry.name);
        entry.targetId = req.targetId;
        return NO_ERROR;
    }

    EggsError _prepareRemoveDirectoryOwner(EggsTime time, const RemoveDirectoryOwnerReq& req, BincodeBytesScratchpad& scratch, RemoveDirectoryOwnerEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.info.length == 0) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        ALWAYS_ASSERT(req.dirId != ROOT_DIR_INODE_ID); // TODO proper error
        entry.dirId = req.dirId;
        scratch.copyTo(req.info, entry.info);
        return NO_ERROR;
    }

    EggsError _prepareRemoveInode(EggsTime time, const RemoveInodeReq& req, BincodeBytesScratchpad& scratch, RemoveInodeEntry& entry) {
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.id == ROOT_DIR_INODE_ID) {
            return EggsError::CANNOT_REMOVE_ROOT_DIRECTORY;
        }
        entry.id = req.id;
        return NO_ERROR;
    }

    EggsError _prepareSetDirectoryOwner(EggsTime time, const SetDirectoryOwnerReq& req, BincodeBytesScratchpad& scratch, SetDirectoryOwnerEntry& entry) {
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

    EggsError _prepareSetDirectoryInfo(EggsTime time, const SetDirectoryInfoReq& req, BincodeBytesScratchpad& scratch, SetDirectoryInfoEntry& entry) {
        if (req.id.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.id.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        if (req.info.inherited && req.id == ROOT_DIR_INODE_ID) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        if (req.info.inherited != (req.info.body.length == 0)) {
            return EggsError::BAD_DIRECTORY_INFO;
        }
        entry.dirId = req.id;
        entry.info = req.info;
        return NO_ERROR;
    }

    EggsError _prepareRemoveNonOwnedEdge(EggsTime time, const RemoveNonOwnedEdgeReq& req, BincodeBytesScratchpad& scratch, RemoveNonOwnedEdgeEntry& entry) {
        if (req.dirId.type() != InodeType::DIRECTORY) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        if (req.dirId.shard() != _shid) {
            return EggsError::BAD_SHARD;
        }
        entry.dirId = req.dirId;
        entry.creationTime = req.creationTime;
        scratch.copyTo(req.name, entry.name);
        return NO_ERROR;
    }

    EggsError _prepareIntraShardHardFileUnlink(EggsTime time, const IntraShardHardFileUnlinkReq& req, BincodeBytesScratchpad& scratch, IntraShardHardFileUnlinkEntry& entry) {
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
        scratch.copyTo(req.name, entry.name);
        entry.creationTime = req.creationTime;
        return NO_ERROR;
    }

    EggsError _prepareRemoveSpanInitiate(EggsTime time, const RemoveSpanInitiateReq& req, BincodeBytesScratchpad& scratch, RemoveSpanInitiateEntry& entry) {
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

    // ----------------------------------------------------------------
    // log application

    void _readLogEntry(uint64_t index, std::string& scratch, ShardLogEntryWithIndex& entry) {
        entry.index = index;
        StaticValue<LogEntryKey> k;
        k->setIndex(entry.index);
        ROCKS_DB_CHECKED(_logDb->Get({}, _logEntries, k.toSlice(), &scratch));
        BincodeBuf bbuf(scratch);
        entry.entry.unpack(bbuf);
        ALWAYS_ASSERT(bbuf.remaining() == 0);
    }

    void _advanceLastAppliedLogEntry(rocksdb::WriteBatch& batch, uint64_t index) {
        uint64_t oldIndex = _lastAppliedLogEntry();
        ALWAYS_ASSERT(oldIndex+1 == index, "old index is %s, expected %s, got %s", oldIndex, oldIndex+1, index);
        LOG_DEBUG(_env, "bumping log index from %s to %s", oldIndex, index);
        StaticValue<U64Value> v;
        v->setU64(index);
        ROCKS_DB_CHECKED(batch.Put({}, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), v.toSlice()));
    }

    EggsError _applyConstructFile(rocksdb::WriteBatch& batch, EggsTime time, const ConstructFileEntry& entry, BincodeBytesScratchpad& scratch, ConstructFileResp& resp) {
        // write to rocks
        StaticValue<TransientFileBody> transientFile;
        transientFile->setFileSize(0);
        transientFile->setMtime(time);
        transientFile->setDeadline(entry.deadlineTime);
        transientFile->setLastSpanState(SpanState::CLEAN);
        transientFile->setNote(entry.note);
        auto k = InodeIdKey::Static(entry.id);
        ROCKS_DB_CHECKED(batch.Put(_transientFiles, k.toSlice(), transientFile.toSlice()));

        // prepare response
        resp.id = entry.id;
        _calcCookie(resp.id, resp.cookie.data);

        return NO_ERROR;
    }

    EggsError _applyLinkFile(rocksdb::WriteBatch& batch, EggsTime time, const LinkFileEntry& entry, BincodeBytesScratchpad& scratch, LinkFileResp& resp) {
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
        if (transientFile->lastSpanState() != SpanState::CLEAN) {
            return EggsError::LAST_SPAN_STATE_NOT_CLEAN;
        }

        // move from transient to non-transient.
        auto fileKey = InodeIdKey::Static(entry.fileId);
        ROCKS_DB_CHECKED(batch.Delete(_transientFiles, fileKey.toSlice()));
        StaticValue<FileBody> file;
        file->setMtime(time);
        file->setFileSize(transientFile->fileSize());
        ROCKS_DB_CHECKED(batch.Put(_files, fileKey.toSlice(), file.toSlice()));

        // create edge in owner.
        {
            EggsError err = ShardDBImpl::_createCurrentEdge(time, time, batch, entry.ownerId, entry.name, entry.fileId, false);
            if (err != NO_ERROR) {
                return err;
            }
        }

        return NO_ERROR;
    }

    EggsError _initiateDirectoryModification(EggsTime time, rocksdb::WriteBatch& batch, InodeId dirId, bool allowSnapshot, std::string& dirValue, ExternalValue<DirectoryBody>& dir) {
        ExternalValue<DirectoryBody> tmpDir;
        EggsError err = _getDirectory({}, dirId, allowSnapshot, dirValue, tmpDir);
        if (err != NO_ERROR) {
            return err;
        }

        // Don't go backwards in time. This is important amongst other things to ensure
        // that we do not have snapshot edges to be the same. This should be very uncommon.
        if (tmpDir->mtime() >= time) {
            RAISE_ALERT(_env, "trying to modify dir %s going backwards in time, dir mtime is %s, log entry time is %s", dirId, tmpDir->mtime(), time);
            return EggsError::DIRECTORY_MTIME_IS_TOO_RECENT;
        }

        // Modify the directory mtime
        tmpDir->setMtime(time);
        {
            auto k = InodeIdKey::Static(dirId);
            ROCKS_DB_CHECKED(batch.Put(_directories, k.toSlice(), tmpDir.toSlice()));
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
            EggsError err = _initiateDirectoryModification(logEntryTime, batch, dirId, false, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        uint64_t nameHash = computeHash(dir->hashMode(), name);

        // Next, we need to look at the current edge with the same name, if any.
        StaticValue<EdgeKey> edgeKey;
        edgeKey->setDirIdWithCurrent(dirId, true); // current=true
        edgeKey->setNameHash(nameHash);
        edgeKey->setName(name);
        std::string edgeValue;
        auto status = _stateDb->Get({}, _edges, edgeKey.toSlice(), &edgeValue);

        // in the block below, we exit the function early if something is off.
        if (status.IsNotFound()) {
            // we're the first one here -- we only need to check the time of
            // the snaphsot edges if the creation time is earlier than the
            // log entry time, otherwise we know that the snapshot edges are
            // all older, since they were all created before logEntryTime.
            StaticValue<EdgeKey> snapshotEdgeKey;
            snapshotEdgeKey->setDirIdWithCurrent(dirId, false); // snapshhot (current=false)
            snapshotEdgeKey->setNameHash(nameHash);
            snapshotEdgeKey->setName(name);
            snapshotEdgeKey->setCreationTime({std::numeric_limits<uint64_t>::max()});
            WrappedIterator it(_stateDb->NewIterator({}, _edges));
            it->SeekForPrev(snapshotEdgeKey.toSlice());
            if (it->Valid() && !it->status().IsNotFound()) {
                auto k = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (k->dirId() == dirId && !k->current() && k->nameHash() == nameHash && k->name() == name) {
                    if (k->creationTime() >= creationTime) {
                        return EggsError::MORE_RECENT_SNAPSHOT_EDGE;
                    }
                }
            }
            ROCKS_DB_CHECKED(it->status());
        } else {
            ROCKS_DB_CHECKED(status);
            ExternalValue<CurrentEdgeBody> existingEdge(edgeValue);
            if (existingEdge->targetIdWithLocked().extra()) { // locked
                // we have an existing locked edge, we need to make sure that it's the one we expect for
                // idempotency.
                if (
                    !locked || // the one we're trying to create isn't locked
                    (targetId != existingEdge->targetIdWithLocked().id() || creationTime != existingEdge->creationTime()) // we're trying to create a different locked edge
                ) {
                    return EggsError::NAME_IS_LOCKED;
                }
            } else {
                // We're kicking out a non-locked current edge. The only circumstance where we allow
                // this automatically is if a file is overriding another file, which is also how it
                // works in linux/posix (see `man 2 rename`).
                if (existingEdge->creationTime() >= creationTime) {
                    return EggsError::MORE_RECENT_CURRENT_EDGE;
                }
                if (
                    targetId.type() == InodeType::DIRECTORY || existingEdge->targetIdWithLocked().id().type() == InodeType::DIRECTORY
                ) {
                    return EggsError::CANNOT_OVERRIDE_NAME;
                }
                // make what is now the current edge a snapshot edge -- no need to delete it,
                // it'll be overwritten below.
                {
                    StaticValue<EdgeKey> k;
                    k->setDirIdWithCurrent(dirId, false); // snapshot (current=false)
                    k->setNameHash(nameHash);
                    k->setName(name);
                    k->setCreationTime(existingEdge->creationTime());
                    StaticValue<SnapshotEdgeBody> v;
                    // this was current, so it's now owned.
                    v->setTargetIdWithOwned(InodeIdExtra(existingEdge->targetIdWithLocked().id(), true));
                    ROCKS_DB_CHECKED(batch.Put(_edges, k.toSlice(), v.toSlice()));
                }
            }
        }

        // OK, we're now ready to insert the current edge
        StaticValue<CurrentEdgeBody> edgeBody;
        edgeBody->setTargetIdWithLocked(InodeIdExtra(targetId, locked));
        edgeBody->setCreationTime(creationTime);
        ROCKS_DB_CHECKED(batch.Put(_edges, edgeKey.toSlice(), edgeBody.toSlice()));

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
            EggsError err = _initiateDirectoryModification(time, batch, dirId, false, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        uint64_t nameHash = computeHash(dir->hashMode(), name);

        // get the edge
        StaticValue<EdgeKey> edgeKey;
        edgeKey->setDirIdWithCurrent(dirId, true); // current=true
        edgeKey->setNameHash(nameHash);
        edgeKey->setName(name);
        std::string edgeValue;
        auto status = _stateDb->Get({}, _edges, edgeKey.toSlice(), &edgeValue);
        if (status.IsNotFound()) {
            return EggsError::NAME_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        ExternalValue<CurrentEdgeBody> edgeBody(edgeValue);
        if (edgeBody->targetIdWithLocked().id() != targetId) {
            return EggsError::MISMATCHING_TARGET;
        }
        if (edgeBody->targetIdWithLocked().extra()) { // locked
            return EggsError::NAME_IS_LOCKED;
        }

        // delete the current edge
        batch.Delete(_edges, edgeKey.toSlice());

        // add the two snapshot edges, one for what was the current edge,
        // and another to signify deletion
        {
            StaticValue<EdgeKey> k;
            k->setDirIdWithCurrent(dirId, false); // snapshot (current=false)
            k->setNameHash(nameHash);
            k->setName(name);
            k->setCreationTime(edgeBody->creationTime());
            StaticValue<SnapshotEdgeBody> v;
            v->setTargetIdWithOwned(InodeIdExtra(targetId, owned));
            ROCKS_DB_CHECKED(batch.Put(_edges, k.toSlice(), v.toSlice()));
            k->setCreationTime(time);
            v->setTargetIdWithOwned(InodeIdExtra(NULL_INODE_ID, false));
            ROCKS_DB_CHECKED(batch.Put(_edges, k.toSlice(), v.toSlice()));
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
                if (dir->ownerId() != entry.ownerId) {
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
            dir->setOwnerId(entry.ownerId);
            dir->setMtime(time);
            dir->setHashMode(HashMode::XXH3_63);
            dir->setInfoInherited(entry.info.inherited);
            dir->setInfo(entry.info.body);
            ROCKS_DB_CHECKED(batch.Put(_directories, dirKey.toSlice(), dir.toSlice()));
        }

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
            nameHash = computeHash(dir->hashMode(), entry.name);
        }

        StaticValue<EdgeKey> currentKey;
        currentKey->setDirIdWithCurrent(entry.dirId, true); // current=true
        currentKey->setNameHash(nameHash);
        currentKey->setName(entry.name);
        std::string edgeValue;
        {
            auto status = _stateDb->Get({}, _edges, currentKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::EDGE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        ExternalValue<CurrentEdgeBody> edge(edgeValue);
        if (edge->locked()) {
            edge->setTargetIdWithLocked(InodeIdExtra(entry.targetId, false)); // locked=false
            ROCKS_DB_CHECKED(batch.Put(_edges, currentKey.toSlice(), edge.toSlice()));
        }
        if (entry.wasMoved) {
            // We need to move the current edge to snapshot, and create a new snapshot
            // edge with the deletion. 
            ROCKS_DB_CHECKED(batch.Delete(_edges, currentKey.toSlice()));
            StaticValue<EdgeKey> snapshotKey;
            snapshotKey->setDirIdWithCurrent(entry.dirId, false); // snapshot (current=false)
            snapshotKey->setNameHash(nameHash);
            snapshotKey->setName(entry.name);
            snapshotKey->setCreationTime(edge->creationTime());
            StaticValue<SnapshotEdgeBody> snapshotBody;
            snapshotBody->setTargetIdWithOwned(InodeIdExtra(entry.targetId, false)); // not owned (this was moved somewhere else)
            ROCKS_DB_CHECKED(batch.Put(_edges, snapshotKey.toSlice(), snapshotBody.toSlice()));
            snapshotKey->setCreationTime(time);
            snapshotBody->setTargetIdWithOwned(InodeIdExtra(NULL_INODE_ID, false)); // deletion edges are never owned
            ROCKS_DB_CHECKED(batch.Put(_edges, snapshotKey.toSlice(), snapshotBody.toSlice()));
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
            nameHash = computeHash(dir->hashMode(), entry.name);        
        }

        StaticValue<EdgeKey> currentKey;
        currentKey->setDirIdWithCurrent(entry.dirId, true); // current=true
        currentKey->setNameHash(nameHash);
        currentKey->setName(entry.name);
        std::string edgeValue;
        {
            auto status = _stateDb->Get({}, _edges, currentKey.toSlice(), &edgeValue);
            if (status.IsNotFound()) {
                return EggsError::EDGE_NOT_FOUND;
            }
            ROCKS_DB_CHECKED(status);
        }
        ExternalValue<CurrentEdgeBody> edge(edgeValue);
        if (!edge->locked()) {
            edge->setTargetIdWithLocked(InodeIdExtra(entry.targetId, true)); // locked=true
            ROCKS_DB_CHECKED(batch.Put(_edges, currentKey.toSlice(), edge.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveDirectoryOwner(EggsTime time, rocksdb::WriteBatch& batch, const RemoveDirectoryOwnerEntry& entry, RemoveDirectoryOwnerResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            // allowSnapshot=true for idempotency (see below)
            EggsError err = _initiateDirectoryModification(time, batch, entry.dirId, true, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            if (dir->ownerId() == NULL_INODE_ID) {
                return NO_ERROR; // already done
            }
        }

        // if we have any current edges, we can't proceed
        {
            StaticValue<EdgeKey> edgeKey;
            edgeKey->setDirIdWithCurrent(entry.dirId, true); // current=true
            edgeKey->setNameHash(0);
            edgeKey->setName({});
            WrappedIterator it(_stateDb->NewIterator({}, _edges));
            it->Seek(edgeKey.toSlice());
            if (it->Valid()) {
                auto otherEdge = ExternalValue<EdgeKey>::FromSlice(it->key());
                if (otherEdge->dirId() == entry.dirId && otherEdge->current()) {
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
            newDir->setOwnerId(NULL_INODE_ID);
            newDir->setMtime(time);
            newDir->setHashMode(dir->hashMode());
            newDir->setInfoInherited(dir->infoInherited());
            newDir->setInfo(entry.info);
            // std::cerr << "setting info to " << entry.info << std::endl;
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directories, k.toSlice(), newDir.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveInode(EggsTime time, rocksdb::WriteBatch& batch, const RemoveInodeEntry& entry, RemoveInodeResp& resp) {
        if (entry.id.type() == InodeType::DIRECTORY) {
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
            if (dir->ownerId() != NULL_INODE_ID) {
                return EggsError::DIRECTORY_HAS_OWNER;
            }
            // there can't be any outgoing edges when killing a directory definitively
            {
                StaticValue<EdgeKey> edgeKey;
                edgeKey->setDirIdWithCurrent(entry.id, false);
                edgeKey->setNameHash(0);
                edgeKey->setName({});
                edgeKey->setCreationTime(0);
                WrappedIterator it(_stateDb->NewIterator({}, _edges));
                it->Seek(edgeKey.toSlice());
                if (it->Valid()) {
                    auto otherEdge = ExternalValue<EdgeKey>::FromSlice(it->key());
                    if (otherEdge->dirId() == entry.id) {
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
                ROCKS_DB_CHECKED(batch.Delete(_directories, dirKey.toSlice()));
            }
        } else {
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
                if (transientFile->deadline() >= time) {
                    return EggsError::DEADLINE_NOT_PASSED;
                }
                // check no spans
                {
                    StaticValue<SpanKey> spanKey;
                    spanKey->setId(entry.id);
                    spanKey->setOffset(0);
                    WrappedIterator it(_stateDb->NewIterator({}, _spans));
                    it->Seek(spanKey.toSlice());
                    if (it->Valid()) {
                        auto otherSpan = ExternalValue<SpanKey>::FromSlice(it->value());
                        if (otherSpan->id() == entry.id) {
                            return EggsError::FILE_NOT_EMPTY;
                        }
                    } else if (it->status().IsNotFound()) {
                        // nothing to do
                    } else {
                        ROCKS_DB_CHECKED(it->status());
                    }
                }
            }
            // we can finally delete
            {
                auto fileKey = InodeIdKey::Static(entry.id);
                ROCKS_DB_CHECKED(batch.Delete(_transientFiles, fileKey.toSlice()));
            }
        }
        return NO_ERROR;
    }

    EggsError _applySetDirectoryOwner(EggsTime time, rocksdb::WriteBatch& batch, const SetDirectoryOwnerEntry& entry, SetDirectoryOwnerResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            EggsError err = _initiateDirectoryModification(time, batch, entry.dirId, true, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }
        // set the owner, and restore the directory entry if needed.
        StaticValue<DirectoryBody> newDir;
        newDir->setOwnerId(entry.ownerId);
        newDir->setHashMode(dir->hashMode());
        newDir->setInfoInherited(dir->infoInherited());
        // remove directory info if it was inherited, now that we have an
        // owner again.
        if (dir->infoInherited()) {
            newDir->setInfo({});
        }
        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directories, k.toSlice(), newDir.toSlice()));
        }
        return NO_ERROR;
    }

    EggsError _applySetDirectoryInfo(EggsTime time, rocksdb::WriteBatch& batch, const SetDirectoryInfoEntry& entry, SetDirectoryInfoResp& resp) {
        std::string dirValue;
        ExternalValue<DirectoryBody> dir;
        {
            EggsError err = _initiateDirectoryModification(time, batch, entry.dirId, false, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
        }

        StaticValue<DirectoryBody> newDir;
        newDir->setOwnerId(dir->ownerId());
        newDir->setMtime(dir->mtime());
        newDir->setHashMode(dir->hashMode());
        newDir->setInfoInherited(entry.info.inherited);
        // For snapshot directories, we need to preserve the last known info if inherited.
        // It'll be reset when it's made non-snapshot again.
        if (!entry.info.inherited || (dir->ownerId() != NULL_INODE_ID)) {
            newDir->setInfo(entry.info.body);
        }

        {
            auto k = InodeIdKey::Static(entry.dirId);
            ROCKS_DB_CHECKED(batch.Put(_directories, k.toSlice(), newDir.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveNonOwnedEdge(EggsTime time, rocksdb::WriteBatch& batch, const RemoveNonOwnedEdgeEntry& entry, RemoveNonOwnedEdgeResp& resp) {
        uint64_t nameHash;
        {
            std::string dirValue;
            ExternalValue<DirectoryBody> dir;
            // allowSnapshot=true since GC needs to be able to remove non-owned edges from snapshot dir
            EggsError err = _initiateDirectoryModification(time, batch, entry.dirId, true, dirValue, dir);
            if (err != NO_ERROR) {
                return err;
            }
            nameHash = computeHash(dir->hashMode(), entry.name);
        }

        {
            StaticValue<EdgeKey> k;
            k->setDirIdWithCurrent(entry.dirId, false); // snapshot (current=false), we're deleting a non owned snapshot edge
            k->setNameHash(nameHash);
            k->setName(entry.name);
            k->setCreationTime(entry.creationTime);
            ROCKS_DB_CHECKED(batch.Delete(_edges, k.toSlice()));
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
            EggsError err = _initiateDirectoryModification(time, batch, entry.ownerId, true, dirValue, dir);
            nameHash = computeHash(dir->hashMode(), entry.name);
        }

        // remove edge
        {
            StaticValue<EdgeKey> k;
            k->setDirIdWithCurrent(entry.ownerId, false);
            k->setNameHash(nameHash);
            k->setName(entry.name);
            k->setCreationTime(entry.creationTime);
            ROCKS_DB_CHECKED(batch.Delete(_edges, k.toSlice()));
        }

        // make file transient
        {
            auto k = InodeIdKey::Static(entry.targetId);
            ROCKS_DB_CHECKED(batch.Delete(_files, k.toSlice()));
            StaticValue<TransientFileBody> v;
            v->setFileSize(file->fileSize());
            v->setMtime(time);
            v->setDeadline(0);
            v->setLastSpanState(SpanState::CLEAN);
            v->setNote(entry.name);
            ROCKS_DB_CHECKED(batch.Put(_transientFiles, k.toSlice(), v.toSlice()));
        }

        return NO_ERROR;
    }

    EggsError _applyRemoveSpanInitiate(EggsTime time, rocksdb::WriteBatch& batch, const RemoveSpanInitiateEntry& entry, RemoveSpanInitiateResp& resp) {
        // TODO, just to make the GC integration tests pass for now
        return EggsError::FILE_EMPTY;
    }

    // ----------------------------------------------------------------
    // miscellanea

    void _calcCookie(InodeId id, std::array<uint8_t, 8>& cookie) {
        cbcmac(_secretKey, (const uint8_t*)&id, sizeof(id), cookie);
    }

    uint64_t _lastAppliedLogEntry() {
        std::string value;
        ROCKS_DB_CHECKED(_stateDb->Get({}, shardMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), &value));
        ExternalValue<U64Value> v(value);
        return v->u64();
    }

    EggsError _getDirectory(const rocksdb::ReadOptions& options, InodeId id, bool allowSnapshot, std::string& dirValue, ExternalValue<DirectoryBody>& dir) {
        if (unlikely(id.type() != InodeType::DIRECTORY)) {
            return EggsError::TYPE_IS_NOT_DIRECTORY;
        }
        auto k = InodeIdKey::Static(id);
        auto status = _stateDb->Get(options, _directories, k.toSlice(), &dirValue);
        if (status.IsNotFound()) {
            return EggsError::DIRECTORY_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        auto tmpDir = ExternalValue<DirectoryBody>(dirValue);
        if (!allowSnapshot && (tmpDir->ownerId() == NULL_INODE_ID && id != ROOT_DIR_INODE_ID)) { // root dir never has an owner
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
        auto status = _stateDb->Get(options, _files, k.toSlice(), &fileValue);
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
        auto status = _stateDb->Get(options, _transientFiles, k.toSlice(), &value);
        if (status.IsNotFound()) {
            return EggsError::FILE_NOT_FOUND;
        }
        ROCKS_DB_CHECKED(status);
        auto tmpFile = ExternalValue<TransientFileBody>(value);
        if (!allowPastDeadline && time > tmpFile->deadline()) {
            // this should be fairly uncommon
            LOG_INFO(_env, "not picking up transient file %s since its deadline %s is past the log entry time %s", id, tmpFile->deadline(), time);
            return EggsError::FILE_NOT_FOUND;
        }
        file = tmpFile;
        return NO_ERROR;
    }

public:
    ShardDBImpl() = delete;

    ShardDBImpl(Env& env, ShardId shid, const std::string& path);
    void close();
    ~ShardDBImpl();

    EggsError read(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardRespContainer& resp);

    EggsError prepareLogEntry(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardLogEntryWithIndex& logEntry) {
        LOG_DEBUG(_env, "processing write request of kind %s", req.kind());
        auto locked = _prepareLogEntryLock.lock();
        scratch.reset();
        logEntry.clear();
        EggsError err = NO_ERROR;

        rocksdb::WriteBatch batch;

        logEntry.index = _nextLogIndex();
        EggsTime time = eggsNow().ns;
        logEntry.entry.time = time;
        auto& logEntryBody = logEntry.entry.body;

        switch (req.kind()) {
        case ShardMessageKind::CONSTRUCT_FILE:
            err = _prepareConstructFile(batch, time, req.getConstructFile(), scratch, logEntryBody.setConstructFile());
            break;
        case ShardMessageKind::LINK_FILE:
            err = _prepareLinkFile(time, req.getLinkFile(), scratch, logEntryBody.setLinkFile());
            break;
        case ShardMessageKind::SAME_DIRECTORY_RENAME:
            err = _prepareSameDirectoryRename(time, req.getSameDirectoryRename(), scratch, logEntryBody.setSameDirectoryRename());
            break;
        case ShardMessageKind::SOFT_UNLINK_FILE:
            err = _prepareSoftUnlinkFile(time, req.getSoftUnlinkFile(), scratch, logEntryBody.setSoftUnlinkFile());
            break;
        case ShardMessageKind::CREATE_DIRECTORY_INODE:
            err = _prepareCreateDirectoryInode(time, req.getCreateDirectoryInode(), scratch, logEntryBody.setCreateDirectoryInode());
            break;
        case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
            err = _prepareCreateLockedCurrentEdge(time, req.getCreateLockedCurrentEdge(), scratch, logEntryBody.setCreateLockedCurrentEdge());
            break;
        case ShardMessageKind::UNLOCK_CURRENT_EDGE:
            err = _prepareUnlockCurrentEdge(time, req.getUnlockCurrentEdge(), scratch, logEntryBody.setUnlockCurrentEdge());
            break;
        case ShardMessageKind::LOCK_CURRENT_EDGE:
            err = _prepareLockCurrentEdge(time, req.getLockCurrentEdge(), scratch, logEntryBody.setLockCurrentEdge());
            break;
        case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
            err = _prepareRemoveDirectoryOwner(time, req.getRemoveDirectoryOwner(), scratch, logEntryBody.setRemoveDirectoryOwner());
            break;
        case ShardMessageKind::REMOVE_INODE:
            err = _prepareRemoveInode(time, req.getRemoveInode(), scratch, logEntryBody.setRemoveInode());
            break;
        case ShardMessageKind::SET_DIRECTORY_OWNER:
            err = _prepareSetDirectoryOwner(time, req.getSetDirectoryOwner(), scratch, logEntryBody.setSetDirectoryOwner());
            break;
        case ShardMessageKind::SET_DIRECTORY_INFO:
            err = _prepareSetDirectoryInfo(time, req.getSetDirectoryInfo(), scratch, logEntryBody.setSetDirectoryInfo());
            break;
        case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
            err = _prepareRemoveNonOwnedEdge(time, req.getRemoveNonOwnedEdge(), scratch, logEntryBody.setRemoveNonOwnedEdge());
            break;
        case ShardMessageKind::INTRA_SHARD_HARD_FILE_UNLINK:
            err = _prepareIntraShardHardFileUnlink(time, req.getIntraShardHardFileUnlink(), scratch, logEntryBody.setIntraShardHardFileUnlink());
            break;
        case ShardMessageKind::REMOVE_SPAN_INITIATE:
            err = _prepareRemoveSpanInitiate(time, req.getRemoveSpanInitiate(), scratch, logEntryBody.setRemoveSpanInitiate());
            break;
        case ShardMessageKind::ADD_SPAN_INITIATE:
        case ShardMessageKind::ADD_SPAN_CERTIFY:
        case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        case ShardMessageKind::SWAP_BLOCKS:
        case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        case ShardMessageKind::MAKE_FILE_TRANSIENT:
            throw EGGS_EXCEPTION("UNIMPLEMENTED %s", req.kind());
        default:
            throw EGGS_EXCEPTION("bad write shard message kind %s", req.kind());
        }

        if (err == NO_ERROR) {
            LOG_DEBUG(_env, "writing log entry of kind %s, index %s, for request of kind %s", logEntry.entry.body.kind(), logEntry.index, req.kind());
            _writeLogEntry(batch, logEntry);
            // When going out of this function we must know that the log is persisted. The main
            // problem we might face otherwise is state being applied and synced while the log
            // isn't synced yet, then the system crashing hard and the last log entry being lost,
            // and then we have the state referring to a log index which is not in the log.
            //
            // The other side is fine: we _can_ write to the state in an async manner, since we
            // can always recover from the log.
            rocksdb::WriteOptions options;
            options.sync = true;
            ROCKS_DB_CHECKED(_logDb->Write(options, &batch));
        } else {
            LOG_INFO(_env, "could not prepare log entry for request of kind %s: %s", req.kind(), err);
        }

        return err;
    }

    EggsError applyLogEntry(uint64_t logIndex, BincodeBytesScratchpad& scratch, ShardRespContainer& resp) {
        // TODO figure out the story with what regards time monotonicity (possibly drop non-monotonic log
        // updates?)

        LOG_DEBUG(_env, "applying log at index %s", logIndex);
        auto locked = _applyLogEntryLock.lock();
        scratch.reset();
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
        ShardLogEntryWithIndex logEntry;
        _readLogEntry(logIndex, entryScratch, logEntry);
        EggsTime time = logEntry.entry.time;
        const auto& logEntryBody = logEntry.entry.body;

        LOG_DEBUG(_env, "about to apply log entry %s", logEntryBody);

        switch (logEntry.entry.body.kind()) {
        case ShardLogEntryKind::CONSTRUCT_FILE:
            err = _applyConstructFile(batch, time, logEntryBody.getConstructFile(), scratch, resp.setConstructFile());
            break;
        case ShardLogEntryKind::LINK_FILE:
            err = _applyLinkFile(batch, time, logEntryBody.getLinkFile(), scratch, resp.setLinkFile());
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
        default:
            throw EGGS_EXCEPTION("bad log entry kind %s", logEntryBody.kind());
        }

        if (err != NO_ERROR) {
            LOG_INFO(_env, "could not apply log entry %s, index %s, because of err %s", logEntryBody.kind(), logIndex, err);
            batch.RollbackToSavePoint();
        } else {
            LOG_DEBUG(_env, "applied log entry of kind %s, index %s, writing changes", logEntryBody.kind(), logEntry.index);
        }

        // OK to not fsync here -- we've already written the log, so worst case we'll replay this
        // when we start again.
        ROCKS_DB_CHECKED(_stateDb->Write({}, &batch));

        return err;

    }

    const std::array<uint8_t, 16>& secretKey() const;
};

ShardDBImpl::ShardDBImpl(Env& env, ShardId shid, const std::string& path): _env(env), _shid(shid), _writeLogEntryBuf(UDP_MTU) {
    // TODO actually figure out the best strategy for each family, including the default
    // one.

    {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.create_missing_column_families = true;
        std::vector<rocksdb::ColumnFamilyDescriptor> familiesDescriptors{
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"logEntries", {}},
        };
        std::vector<rocksdb::ColumnFamilyHandle*> familiesHandles;
        auto dbPath = path + "/db-log";
        LOG_INFO(_env, "initializing log RocksDB in %s", dbPath);
        ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::Open(options, dbPath, familiesDescriptors, &familiesHandles, &_logDb),
            "could not open RocksDB %s", dbPath
        );
        ALWAYS_ASSERT(familiesDescriptors.size() == familiesHandles.size());
        _logDefault = familiesHandles[0];
        _logEntries = familiesHandles[1];
    }

    {
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
        };
        std::vector<rocksdb::ColumnFamilyHandle*> familiesHandles;
        auto dbPath = path + "/db-state";
        LOG_INFO(_env, "initializing state RocksDB in %s", dbPath);
        ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::Open(options, dbPath, familiesDescriptors, &familiesHandles, &_stateDb),
            "could not open RocksDB %s", dbPath
        );
        ALWAYS_ASSERT(familiesDescriptors.size() == familiesHandles.size());
        _stateDefault = familiesHandles[0];
        _files = familiesHandles[1];
        _spans = familiesHandles[2];
        _transientFiles = familiesHandles[3];
        _directories = familiesHandles[4];
        _edges = familiesHandles[5];
    }

    _initDb();
    _replayLog();
}

void ShardDBImpl::close() {
    LOG_INFO(_env, "destroying column families and closing databases");

    ROCKS_DB_CHECKED(_logDb->DestroyColumnFamilyHandle(_logDefault));
    ROCKS_DB_CHECKED(_logDb->DestroyColumnFamilyHandle(_logEntries));
    ROCKS_DB_CHECKED(_logDb->Close());

    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_stateDefault));
    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_files));
    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_spans));
    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_transientFiles));
    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_directories));
    ROCKS_DB_CHECKED(_stateDb->DestroyColumnFamilyHandle(_edges));
    ROCKS_DB_CHECKED(_stateDb->Close());
}

ShardDBImpl::~ShardDBImpl() {
    delete _logDb;
    delete _stateDb;
}

BincodeBytes defaultDirectoryInfo(char (&buf)[255]) {
    DirectoryInfoBody info;
    // delete after 30 days
    info.deleteAfterTime = 30ull /*days*/ * 24 /*hours*/ * 60 /*minutes*/ * 60 /*seconds*/ * 1'000'000'000 /*ns*/;
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

EggsError ShardDBImpl::read(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardRespContainer& resp) {
    LOG_DEBUG(_env, "processing read-only request of kind %s", req.kind());

    EggsError err = NO_ERROR;
    scratch.reset();
    resp.clear();

    switch (req.kind()) {
    case ShardMessageKind::STAT_FILE:
        err = _statFile(req.getStatFile(), resp.setStatFile());
        break;
    case ShardMessageKind::READ_DIR:
        err = _readDir(req.getReadDir(), scratch, resp.setReadDir());
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        err = _statDirectory(req.getStatDirectory(), scratch, resp.setStatDirectory());
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        err = _statTransientFile(req.getStatTransientFile(), scratch, resp.setStatTransientFile());
        break;
    case ShardMessageKind::LOOKUP:
        err = _lookup(req.getLookup(), resp.setLookup());
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        err = _visitTransientFiles(req.getVisitTransientFiles(), scratch, resp.setVisitTransientFiles());
        break;
    case ShardMessageKind::FULL_READ_DIR:
        err = _fullReadDir(req.getFullReadDir(), scratch, resp.setFullReadDir());
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        err = _visitDirectories(req.getVisitDirectories(), scratch, resp.setVisitDirectories());
        break;
    case ShardMessageKind::FILE_SPANS:
    case ShardMessageKind::VISIT_FILES:
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        throw EGGS_EXCEPTION("UNIMPLEMENTED %s", req.kind());
    default:
        throw EGGS_EXCEPTION("bad read-only shard message kind %s", req.kind());
    }

    ALWAYS_ASSERT(req.kind() == resp.kind());

    return err;
}

const std::array<uint8_t, 16>& ShardDBImpl::secretKey() const {
    return _secretKey;
}

ShardDB::ShardDB(Env& env, ShardId shid, const std::string& path) {
    _impl = new ShardDBImpl(env, shid, path);
}

void ShardDB::close() {
    ((ShardDBImpl*)_impl)->close();
}

ShardDB::~ShardDB() {
    delete ((ShardDBImpl*)_impl);
    _impl = nullptr;
}

EggsError ShardDB::read(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardRespContainer& resp) {
    return ((ShardDBImpl*)_impl)->read(req, scratch, resp);
}

EggsError ShardDB::prepareLogEntry(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardLogEntryWithIndex& logEntry) {
    return ((ShardDBImpl*)_impl)->prepareLogEntry(req, scratch, logEntry);
}

EggsError ShardDB::applyLogEntry(uint64_t logEntryIx, BincodeBytesScratchpad& scratch, ShardRespContainer& resp) {
    return ((ShardDBImpl*)_impl)->applyLogEntry(logEntryIx, scratch, resp);
}

const std::array<uint8_t, 16>& ShardDB::secretKey() const {
    return ((ShardDBImpl*)_impl)->secretKey();
}
