#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "CDCDB.hpp"
#include "AssertiveLock.hpp"
#include "CDCDBData.hpp"
#include "Env.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDB.hpp"

// The CDC needs to remember about multi-step transactions which it executes by
// talking to the shards. So essentially we need to store a bunch of queued
// queued requests, and a bunch of currently executing transactions, which are
// waiting for shard responses.
//
// Generally speaking we need to make sure to not have transactions step on each
// others toes. This is especially pertinent for edge locking logic -- given our
// requirements for idempotency (we don't know when a request has gone through)
// when we lock an edge we're really locking it so that shard-specific operations
// don't mess things up, rather than other CDC operations.
//
// The most obvious way to do this is to lock directory modifications here on
// the CDC servers (e.g. have transactions which need to lock edges in directory
// X to execute serially). Then at every update we can pop stuff from the queue.
//
// For now though, to keep things simple, we just execute _everything_ serially.
//
// So we need to store two things:
//
// * A queue of client requests that we haven't gotten to execute yet;
// * The state for the currently running request.
//
// For now, we just store a queue of requests.
//
// The queue is just indexed by a uint64_t transaction id, which monotonically
// increases. We store this txn id in the database so that we can keep generating
// unique ones even when the queue is empty. We also need to remember it so that
// we can just read/insert without seeking (see
// <https://github.com/facebook/rocksdb/wiki/Implement-Queue-Service-Using-RocksDB>).
//
// So we have one CF which is just a queue with this schema:
//
// * txnid -> cdc req
//
// Then we have another CF with some metadata, specifically:
//
// * LAST_APPLIED_LOG_ENTRY_KEY: to ensure the log entries are applied sequentially
// * LAST_TXN_KEY: last txn enqueued, to generate txn ids (note that we need this on top of the two below to generate globally unique ids)
// * FIRST_TXN_IN_QUEUE_KEY: next in line in the reqs queue, 0 if queue is empty
// * LAST_TXN_IN_QUEUE_KEY: last txn id in the queue, 0 if queue is empty. We need this to
// * EXECUTING_TXN_KEY: txn currently executing. if zero, none.
// * EXECUTING_TXN_STATE_KEY: state of the currently executing txn.
//
// The request stays on the queue until it finishes executing.
//
// The txn state would look something like this:
//
// * uint8_t step
// * ... additional state depending on the request type and step ...
//
// Finally, we store another CF with directory child -> directory parent
// cache, which is updated in directory create and directory moves operations.
// We never delete data from here for now, since even if we stored everything in
// it it wouldn't be very big anyway (~100GiB, assuming an average of 100 files
// per directory).
//
// With regards to log entries, we have three sort of updates:
//
// * A new cdc request arrives;
// * A shard response arrives which we need to advance the current transaction;
// * We want to begin the next transaction.
//
// We separate the action that begins the next transaction to simplify the
// implementation (we don't need "draining" logic in the CDC DB itself),
// and also to offer freedom regarding how to process transactions to what
// uses the CDC DB.

std::ostream& operator<<(std::ostream& out, const CDCShardReq& x) {
    out << "CDCShardReq(shid=" << x.shid << ", req=" << x.req << ")";
    return out;
}

std::ostream& operator<<(std::ostream& out, const CDCStep& x) {
    ALWAYS_ASSERT(!(x.txnFinished != 0 && x.txnNeedsShard != 0));
    out << "CDCStep(";
    if (x.txnFinished != 0) {
        out << "finishedTxn=" << x.txnFinished;
        if (x.err != NO_ERROR) {
            out << ", err=" << x.err;
        } else {
            out << ", resp=" << x.resp;
        }
    }
    if (x.txnNeedsShard != 0) {
        out << "txnNeedsShard=" << x.txnNeedsShard << ", shardReq=" << x.shardReq;
    }
    if (x.nextTxn != 0) {
        if (x.txnFinished != 0 && x.txnNeedsShard != 0) {
            out << ", ";
        }
        out << "nextTxn=" << x.nextTxn;
    }
    out << ")";
    return out;
}

enum class MakeDirectoryStep : uint8_t {
    START = 0,
    AFTER_CREATE_DIR = 1,
    AFTER_CREATE_LOCKED_EDGE = 2,
    AFTER_UNLOCK_EDGE = 3,
    AFTER_ROLLBACK = 4,
};

enum class HardUnlinkDirectoryStep : uint8_t {
    START = 0,
    AFTER_REMOVE_INODE = 1,
};

enum class RenameFileStep : uint8_t {
    START = 0,
    AFTER_LOCK_OLD_EDGE = 1,
    AFTER_CREATE_NEW_LOCKED_EDGE = 2,
    AFTER_UNLOCK_NEW_EDGE = 3,
    AFTER_UNLOCK_OLD_EDGE = 4,
    AFTER_ROLLBACK = 5,
};

enum class SoftUnlinkDirectoryStep : uint8_t {
    START = 0,
    AFTER_LOCK_EDGE = 1,
    AFTER_STAT = 2,
    AFTER_REMOVE_OWNER = 3,
    AFTER_UNLOCK_EDGE = 4,
    AFTER_ROLLBACK = 5,
};

enum class RenameDirectoryStep : uint8_t {
    START = 0,
    AFTER_LOCK_OLD_EDGE = 1,
    AFTER_CREATE_LOCKED_EDGE = 2,
    AFTER_UNLOCK_NEW_EDGE = 3,
    AFTER_UNLOCK_OLD_EDGE = 4,
    AFTER_SET_OWNER = 5,
    AFTER_ROLLBACK = 6,
};

// Wrapper types to pack/unpack with kind
struct PackCDCReq {
    const CDCReqContainer& req;

    PackCDCReq(const CDCReqContainer& req_): req(req_) {}

    void pack(BincodeBuf& buf) const {
        buf.packScalar<CDCMessageKind>(req.kind());
        req.pack(buf);
    }

    uint16_t packedSize() const {
        return 1 + req.packedSize();
    }
};
struct UnpackCDCReq {
    CDCReqContainer& req;

    UnpackCDCReq(CDCReqContainer& req_): req(req_) {}

    void unpack(BincodeBuf& buf) {
        auto kind = buf.unpackScalar<CDCMessageKind>();
        req.unpack(buf, kind);
    }
};

struct CDCDBImpl {
    Env _env;

    // TODO it would be good to store basically all of the metadata in memory,
    // so that we'd just read from it, but this requires a bit of care when writing
    // if we want to be able to not write the batch at the end.

    rocksdb::OptimisticTransactionDB* _db;
    rocksdb::ColumnFamilyHandle* _defaultCf;
    rocksdb::ColumnFamilyHandle* _reqQueueCf;
    rocksdb::ColumnFamilyHandle* _parentCf;

    AssertiveLock _processLock;

    // Used to deserialize into
    CDCReqContainer _cdcReq;

    // ----------------------------------------------------------------
    // initialization

    CDCDBImpl() = delete;
    CDCDBImpl& operator=(const CDCDBImpl&) = delete;

    CDCDBImpl(Logger& logger, const std::string& path) : _env(logger, "cdc_db") {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.create_missing_column_families = true;
        std::vector<rocksdb::ColumnFamilyDescriptor> familiesDescriptors{
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"reqQueue", {}},
            {"parent", {}},
        };
        std::vector<rocksdb::ColumnFamilyHandle*> familiesHandles;
        auto dbPath = path + "/db";
        LOG_INFO(_env, "initializing CDC RocksDB in %s", dbPath);
        ROCKS_DB_CHECKED_MSG(
            rocksdb::OptimisticTransactionDB::Open(options, dbPath, familiesDescriptors, &familiesHandles, &_db),
            "could not open RocksDB %s", dbPath
        );
        ALWAYS_ASSERT(familiesDescriptors.size() == familiesHandles.size());
        _defaultCf = familiesHandles[0];
        _reqQueueCf = familiesHandles[1];
        _parentCf = familiesHandles[2];
        
        _initDb();
    }

    void close() {
        LOG_INFO(_env, "destroying column families and closing database");

        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_defaultCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_reqQueueCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_parentCf));

        ROCKS_DB_CHECKED(_db->Close());
    }

    ~CDCDBImpl() {
        delete _db;
    }

    // Getting/setting txn ids from our txn ids keys

    #define TXN_ID_SETTER_GETTER(key, getterName, setterName) \
        uint64_t getterName(rocksdb::Transaction& dbTxn) { \
            std::string v; \
            ROCKS_DB_CHECKED(dbTxn.Get({}, _defaultCf, cdcMetadataKey(&key), &v)); \
            ExternalValue<U64Value> txnIdV(v); \
            return txnIdV().u64(); \
        } \
        void setterName(rocksdb::Transaction& dbTxn, uint64_t x) { \
            auto v = U64Value::Static(x); \
            ROCKS_DB_CHECKED(dbTxn.Put(_defaultCf, cdcMetadataKey(&key), v.toSlice())); \
        }

    TXN_ID_SETTER_GETTER(LAST_TXN_KEY, _lastTxn, _setLastTxn)
    TXN_ID_SETTER_GETTER(FIRST_TXN_IN_QUEUE_KEY, _firstTxnInQueue, _setFirstTxnInQueue)
    TXN_ID_SETTER_GETTER(LAST_TXN_IN_QUEUE_KEY, _lastTxnInQueue, _setLastTxnInQueue)
    TXN_ID_SETTER_GETTER(EXECUTING_TXN_KEY, _executingTxn, _setExecutingTxn)

    #undef TXN_ID_SETTER_GETTER

    void _initDb() {
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

        if (!keyExists(_defaultCf, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY))) {
            LOG_INFO(_env, "initializing next directory id");
            auto id = InodeIdValue::Static(InodeId::FromU64(ROOT_DIR_INODE_ID.u64 + 1));
            ROCKS_DB_CHECKED(_db->Put({}, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY), id.toSlice()));
        }

        const auto initZeroValue = [this, &keyExists](const std::string& what, const CDCMetadataKey& key) {
            if (!keyExists(_defaultCf, cdcMetadataKey(&key))) {
                LOG_INFO(_env, "initializing %s", what);
                StaticValue<U64Value> v;
                v().setU64(0);
                ROCKS_DB_CHECKED(_db->Put({}, cdcMetadataKey(&key), v.toSlice()));
            } 
        };

        initZeroValue("last txn", LAST_TXN_KEY);
        initZeroValue("first txn in queue", FIRST_TXN_IN_QUEUE_KEY);
        initZeroValue("last txn in queue", LAST_TXN_IN_QUEUE_KEY);
        initZeroValue("executing txn", EXECUTING_TXN_KEY);
        initZeroValue("last applied log index", LAST_APPLIED_LOG_ENTRY_KEY);
    }

    // Processing
    // ----------------------------------------------------------------

    uint64_t _lastAppliedLogEntry() {
        std::string value;
        ROCKS_DB_CHECKED(_db->Get({}, cdcMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), &value));
        ExternalValue<U64Value> v(value);
        return v().u64();
    }

    void _advanceLastAppliedLogEntry(rocksdb::Transaction& dbTxn, uint64_t index) {
        uint64_t oldIndex = _lastAppliedLogEntry();
        ALWAYS_ASSERT(oldIndex+1 == index, "old index is %s, expected %s, got %s", oldIndex, oldIndex+1, index);
        LOG_DEBUG(_env, "bumping log index from %s to %s", oldIndex, index);
        StaticValue<U64Value> v;
        v().setU64(index);
        ROCKS_DB_CHECKED(dbTxn.Put({}, cdcMetadataKey(&LAST_APPLIED_LOG_ENTRY_KEY), v.toSlice()));
    }

    // Pushes a new request onto the queue.
    void _reqQueuePush(rocksdb::Transaction& dbTxn, uint64_t txnId, const CDCReqContainer& req) {
        // Check metadata
        uint64_t last = _lastTxnInQueue(dbTxn);
        ALWAYS_ASSERT(last == 0 || txnId == last + 1);
        // Write to queue
        {
            auto k = U64Key::Static(txnId);
            std::string v = bincodeToRocksValue(PackCDCReq(req));
            ROCKS_DB_CHECKED(dbTxn.Put(_reqQueueCf, k.toSlice(), v));
        }
        // Update metadata
        if (last == 0) {
            _setFirstTxnInQueue(dbTxn, txnId);
        }
        _setLastTxnInQueue(dbTxn, txnId);
    }

    // Returns 0 if the queue was empty, otherwise what was popped
    uint64_t _reqQueuePeek(rocksdb::Transaction& dbTxn, CDCReqContainer& req) {
        uint64_t first = _firstTxnInQueue(dbTxn);
        if (first == 0) {
            return 0;
        }
        // Read
        {
            auto k = U64Key::Static(first);
            std::string v;
            ROCKS_DB_CHECKED(dbTxn.Get({}, _reqQueueCf, k.toSlice(), &v));
            UnpackCDCReq ureq(req);
            bincodeFromRocksValue(v, ureq);
        }
        return first;
    }

    // Returns 0 if the queue was empty, otherwise what was popped
    uint64_t _reqQueuePop(rocksdb::Transaction& dbTxn) {
        uint64_t first = _firstTxnInQueue(dbTxn);
        if (first == 0) {
            return 0;
        }
        // Update metadata
        uint64_t last = _lastTxnInQueue(dbTxn);
        if (first == last) { // empty queue
            _setFirstTxnInQueue(dbTxn, 0);
            _setLastTxnInQueue(dbTxn, 0);
        } else {
            _setFirstTxnInQueue(dbTxn, first+1);
        }
        return first;
    }

    InodeId _nextDirectoryId(rocksdb::Transaction& dbTxn) {
        std::string v;
        ROCKS_DB_CHECKED(_db->Get({}, _defaultCf, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY), &v));
        ExternalValue<InodeIdValue> nextId(v);
        InodeId id = nextId().id();
        nextId().setId(InodeId::FromU64(id.u64 + 1));
        ROCKS_DB_CHECKED(dbTxn.Put(_defaultCf, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY), nextId.toSlice()));
        return id;
    }

    ShardReqContainer& _needsShard(CDCStep& step, uint64_t txnId, ShardId shid) {
        step.txnFinished = 0;
        step.txnNeedsShard = txnId;
        step.shardReq.shid = shid;
        return step.shardReq.req;
    }

    void _finishWithError(CDCStep& step, uint64_t txnId, EggsError err) {
        ALWAYS_ASSERT(err != NO_ERROR);
        step.txnFinished = txnId;
        step.err = err;
        step.txnNeedsShard = 0;
    }

    CDCRespContainer& _finish(CDCStep& step, uint64_t txnId) {
        step.txnFinished = txnId;
        step.err = NO_ERROR;
        return step.resp;
    }

    // Steps:
    //
    // 1. Allocate inode id here in the CDC
    // 2. Create directory in shard we get from the inode   
    // 3. Create locked edge from owner to newly created directory
    // 4. Unlock the edge created in 3
    //
    // If 3 fails, 2 must be rolled back. 4 does not fail.
    MakeDirectoryStep _advanceMakeDirectory(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& reqContainer,
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        MakeDirectoryStep mkDirStep,
        MakeDirectoryState state,
        CDCStep& step
    ) {
        ALWAYS_ASSERT((shardRespError == NO_ERROR && shardResp == nullptr) == (mkDirStep == MakeDirectoryStep::START));
        const auto& req = reqContainer.getMakeDirectory();

        switch (mkDirStep) {
        case MakeDirectoryStep::START: {
            mkDirStep = MakeDirectoryStep::AFTER_CREATE_DIR;
            auto dirId = _nextDirectoryId(dbTxn);
            state.setDirId(dirId);
            auto& shardReq = _needsShard(step, txnId, state.dirId().shard()).setCreateDirectoryInode();
            shardReq.id = dirId;
            shardReq.info.inherited = req.info.inherited;
            shardReq.info.body = req.info.body;
            shardReq.ownerId = req.ownerId;
        } break;
        case MakeDirectoryStep::AFTER_CREATE_DIR: {
            if (shardRespError != NO_ERROR) {
                // We never expect to fail here. That said, there's nothing to roll back,
                // so we just finish.
                RAISE_ALERT(_env, "Unexpected error %s when creating diretory inode with id %s", shardRespError, state.dirId());
                _finishWithError(step, txnId, EggsError::INTERNAL_ERROR);
            } else {
                mkDirStep = MakeDirectoryStep::AFTER_CREATE_LOCKED_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.ownerId.shard()).setCreateLockedCurrentEdge();
                shardReq.creationTime = time;
                shardReq.dirId = req.ownerId;
                shardReq.targetId = state.dirId();
                shardReq.name = req.name;
            }
        } break;
        case MakeDirectoryStep::AFTER_CREATE_LOCKED_EDGE: {
            if (shardRespError != NO_ERROR) {
                // We've failed to link: we need to rollback the directory creation
                LOG_INFO(_env, "Failed to create locked edge, rolling back");
                mkDirStep = MakeDirectoryStep::AFTER_ROLLBACK;
                state.setExitError(shardRespError);
                auto& shardReq = _needsShard(step, txnId, state.dirId().shard()).setRemoveDirectoryOwner();
                shardReq.dirId = state.dirId();
                // we've just created this directory, it is empty, therefore the policy
                // is irrelevant.
                shardReq.info = defaultDirectoryInfo();
            } else {
                mkDirStep = MakeDirectoryStep::AFTER_UNLOCK_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.ownerId.shard()).setUnlockCurrentEdge();
                shardReq.dirId = req.ownerId;
                shardReq.name = req.name;
                shardReq.targetId = state.dirId();
                shardReq.wasMoved = false;
            }
        } break;
        case MakeDirectoryStep::AFTER_UNLOCK_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts and similar, and possibly other stuff
            // We're done, record the parent relationship and finish
            {
                auto k = InodeIdKey::Static(state.dirId());
                auto v = InodeIdKey::Static(req.ownerId);
                ROCKS_DB_CHECKED(dbTxn.Put(_parentCf, k.toSlice(), v.toSlice()));
            }
            auto& resp = _finish(step, txnId).setMakeDirectory();
            resp.id = state.dirId();
        } break;
        case MakeDirectoryStep::AFTER_ROLLBACK: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO retry, handle timeouts, etc.
            _finishWithError(step, txnId, state.exitError());
        } break;
        default:
            throw EGGS_EXCEPTION("bad directory step %s", (int)mkDirStep);
        }

        return mkDirStep;
    }

    // The only reason we have this here is for possible conflicts with RemoveDirectoryOwner,
    // which might temporarily set the owner of a directory to NULL. Since in the current
    // implementation we only ever have one transaction in flight in the CDC, we can just
    // execute this.
    HardUnlinkDirectoryStep _advanceHardUnlinkDirectory(
        uint64_t txnId,
        const CDCReqContainer& reqContainer,
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        HardUnlinkDirectoryStep reqStep,
        HardUnlinkDirectoryState state,
        CDCStep& step
    ) {
        ALWAYS_ASSERT((shardRespError == NO_ERROR && shardResp == nullptr) == (reqStep == HardUnlinkDirectoryStep::START));
        const auto& req = reqContainer.getHardUnlinkDirectory();

        switch (reqStep) {
        case HardUnlinkDirectoryStep::START: {
            reqStep = HardUnlinkDirectoryStep::AFTER_REMOVE_INODE;
            auto& shardReq = _needsShard(step, txnId, req.dirId.shard()).setRemoveInode();
            shardReq.id = req.dirId;
        } break;
        case HardUnlinkDirectoryStep::AFTER_REMOVE_INODE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle
            _finish(step, txnId);
        } break;
        default:
            throw EGGS_EXCEPTION("bad hard unlink directory step %s", (int)reqStep);
        }

        return reqStep;
    }

    // Steps:
    //
    // 1. lock source current edge
    // 2. create destination locked current target edge
    // 3. unlock edge in step 2
    // 4. unlock source target current edge, and soft unlink it
    //
    // If we fail at step 2, we need to roll back step 1. Steps 3 and 4 should never fail.
    RenameFileStep _advanceRenameFile(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& reqContainer,
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        RenameFileStep reqStep,
        RenameFileState state,
        CDCStep& step
    ) {
        ALWAYS_ASSERT((shardRespError == NO_ERROR && shardResp == nullptr) == (reqStep == RenameFileStep::START));
        const auto& req = reqContainer.getRenameFile();

        switch (reqStep) {
        case RenameFileStep::START: {
            // We need this explicit check here because moving directories is more complicated,
            // and therefore we do it in another transaction type entirely.
            if (req.targetId.type() == InodeType::DIRECTORY) {
                _finishWithError(step, txnId, EggsError::TYPE_IS_NOT_DIRECTORY);
            } else if (req.oldOwnerId == req.newOwnerId) {
                _finishWithError(step, txnId, EggsError::SAME_DIRECTORIES);
            } else {
                reqStep = RenameFileStep::AFTER_LOCK_OLD_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setLockCurrentEdge();
                shardReq.dirId = req.oldOwnerId;
                shardReq.name = req.oldName;
                shardReq.targetId = req.targetId;
            }
        } break;
        case RenameFileStep::AFTER_LOCK_OLD_EDGE: {
            if (shardRespError != NO_ERROR) {
                // We couldn't acquire the lock at the source -- we can terminate immediately, there's nothing to roll back
                if (shardRespError == EggsError::DIRECTORY_NOT_FOUND) {
                    shardRespError = EggsError::OLD_DIRECTORY_NOT_FOUND;
                }
                if (shardRespError == EggsError::NAME_IS_LOCKED) {
                    shardRespError = EggsError::OLD_NAME_IS_LOCKED;
                }
                _finishWithError(step, txnId, shardRespError);
            } else {
                reqStep = RenameFileStep::AFTER_CREATE_NEW_LOCKED_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.newOwnerId.shard()).setCreateLockedCurrentEdge();
                shardReq.creationTime = time,
                shardReq.dirId = req.newOwnerId;
                shardReq.name = req.newName;
                shardReq.targetId = req.targetId;
            }
        } break;
        case RenameFileStep::AFTER_CREATE_NEW_LOCKED_EDGE: {
            if (shardRespError != NO_ERROR) {
                // We couldn't create the new edge, we need to unlock the old edge.
                if (shardRespError == EggsError::DIRECTORY_NOT_FOUND) {
                    shardRespError = EggsError::NEW_DIRECTORY_NOT_FOUND;
                }
                if (shardRespError == EggsError::NAME_IS_LOCKED) {
                    shardRespError = EggsError::NEW_NAME_IS_LOCKED;
                }
                state.setExitError(shardRespError);
                reqStep = RenameFileStep::AFTER_ROLLBACK;
                auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setUnlockCurrentEdge();
                shardReq.dirId = req.oldOwnerId;
                shardReq.name = req.oldName;
                shardReq.targetId = req.targetId;
                shardReq.wasMoved = false;
            } else {
                reqStep = RenameFileStep::AFTER_UNLOCK_NEW_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.newOwnerId.shard()).setUnlockCurrentEdge();
                shardReq.dirId = req.newOwnerId;
                shardReq.targetId = req.targetId;
                shardReq.name = req.newName;
                shardReq.wasMoved = false;
            }
        } break;
        case RenameFileStep::AFTER_UNLOCK_NEW_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO Handle timeouts etc.
            // We're done creating the destination edge, now unlock the source, marking it as moved
            reqStep = RenameFileStep::AFTER_UNLOCK_OLD_EDGE;
            auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setUnlockCurrentEdge();
            shardReq.dirId = req.oldOwnerId;
            shardReq.targetId = req.targetId;
            shardReq.name = req.oldName;
            shardReq.wasMoved = true;
        } break;
        case RenameFileStep::AFTER_UNLOCK_OLD_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            // We're finally done
            auto& resp = _finish(step, txnId);
            resp.setRenameFile();
        } break;
        case RenameFileStep::AFTER_ROLLBACK: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            _finishWithError(step, txnId, state.exitError());
        } break;
        default:
            throw EGGS_EXCEPTION("bad rename file step %s", (int)reqStep);
        }

        return reqStep;
    }

    // Steps:
    //
    // 1. Lock edge going into the directory to remove. This prevents things making
    //     making it snapshot or similar in the meantime.
    // 2. Resolve the directory info, since we'll need to store it when we remove the directory owner.
    // 3. Remove directory owner from directory that we want to remove. This will fail if there still
    //     are current edges there.
    // 4. Unlock edge going into the directory, making it snapshot.
    //
    // If 2 or 3 fail, we need to roll back the locking, without making the edge snapshot.
    SoftUnlinkDirectoryStep _advanceSoftUnlinkDirectory(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& reqContainer,
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        SoftUnlinkDirectoryStep reqStep,
        SoftUnlinkDirectoryState state,
        CDCStep& step
    ) {
        ALWAYS_ASSERT((shardRespError == NO_ERROR && shardResp == nullptr) == (reqStep == SoftUnlinkDirectoryStep::START));
        const auto& req = reqContainer.getSoftUnlinkDirectory();

        auto initStat = [&](InodeId dir) {
            reqStep = SoftUnlinkDirectoryStep::AFTER_STAT;
            auto& shardReq = _needsShard(step, txnId, dir.shard()).setStatDirectory();
            shardReq.id = dir;
        };

        auto initRollback = [&](EggsError err) {
            state.setExitError(err);
            reqStep = SoftUnlinkDirectoryStep::AFTER_ROLLBACK;
            auto& shardReq = _needsShard(step, txnId, req.ownerId.shard()).setUnlockCurrentEdge();
            shardReq.dirId = req.ownerId;
            shardReq.name = req.name;
            shardReq.targetId = req.targetId;
            shardReq.wasMoved = false;
        };

        switch (reqStep) {
        case SoftUnlinkDirectoryStep::START: {
            if (req.targetId.type() != InodeType::DIRECTORY) {
                _finishWithError(step, txnId, EggsError::TYPE_IS_NOT_DIRECTORY);
            } else {
                reqStep = SoftUnlinkDirectoryStep::AFTER_LOCK_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.ownerId.shard()).setLockCurrentEdge();
                shardReq.dirId = req.ownerId;
                shardReq.name = req.name;
                shardReq.targetId = req.targetId;
            }
        } break;
        case SoftUnlinkDirectoryStep::AFTER_LOCK_EDGE: {
            if (shardRespError != NO_ERROR) { // TODO handle timeouts etc.
                // Nothing to roll back
                _finishWithError(step, txnId, shardRespError); 
            } else {
                // Start resolving dir info
                initStat(req.targetId);
            }
        } break;
        case SoftUnlinkDirectoryStep::AFTER_STAT: {
            if (shardRespError != NO_ERROR) { // TODO handle timeouts etc.
                initRollback(shardRespError);
            } else {
                const auto& statResp = shardResp->getStatDirectory();
                if (statResp.info.size() > 0) {
                    // We're done for the info, we can remove the owner
                    reqStep = SoftUnlinkDirectoryStep::AFTER_REMOVE_OWNER;
                    auto& shardReq = _needsShard(step, txnId, req.targetId.shard()).setRemoveDirectoryOwner();
                    shardReq.dirId = req.targetId;
                    shardReq.info = statResp.info;
                } else {
                    ALWAYS_ASSERT(statResp.owner != NULL_INODE_ID); // can't be root with no info
                    // keep walking upwards
                    initStat(statResp.owner);
                }
            }
        } break;
        case SoftUnlinkDirectoryStep::AFTER_REMOVE_OWNER: {
            if (shardRespError != NO_ERROR) { // TODO handle timeouts etc.
                initRollback(shardRespError);
            } else {
                reqStep = SoftUnlinkDirectoryStep::AFTER_UNLOCK_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.ownerId.shard()).setUnlockCurrentEdge();
                shardReq.dirId = req.ownerId;
                shardReq.name = req.name;
                shardReq.targetId = req.targetId;
                shardReq.wasMoved = true; // make it snapshot
            }
        } break;
        case SoftUnlinkDirectoryStep::AFTER_UNLOCK_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc
            auto& resp = _finish(step, txnId).setSoftUnlinkDirectory();
            // Update parent map
            {
                auto k = InodeIdKey::Static(req.targetId);
                ROCKS_DB_CHECKED(dbTxn.Delete(_parentCf, k.toSlice()));
            }
        } break;
        case SoftUnlinkDirectoryStep::AFTER_ROLLBACK: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            _finishWithError(step, txnId, state.exitError());
        } break;
        default:
            throw EGGS_EXCEPTION("bad step %s", (int)reqStep);
        }

        return reqStep;
    }

    bool _loopCheck(rocksdb::Transaction& dbTxn, const RenameDirectoryReq& req) {
        return true;
        // throw EGGS_EXCEPTION("UNIMPLEMENTED");
    }

    // Steps:
    //
    // 1. Make sure there's no loop by traversing the parents
    // 2. Lock old edge
    // 3. Create and lock the new edge
    // 4. Unlock the new edge
    // 5. Unlock and unlink the old edge
    // 6. Update the owner of the moved directory to the new directory
    //
    // If we fail at step 2 or 3, we need to unlock the edge we locked at step 1. Step 4 and 5
    // should never fail.
    RenameDirectoryStep _advanceRenameDirectory(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& reqContainer,
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        RenameDirectoryStep reqStep,
        RenameDirectoryState state,
        CDCStep& step
    ) {
        ALWAYS_ASSERT((shardRespError == NO_ERROR && shardResp == nullptr) == (reqStep == RenameDirectoryStep::START));
        const auto& req = reqContainer.getRenameDirectory();

        auto initRollback = [&](EggsError err) {
            state.setExitError(err);
            reqStep = RenameDirectoryStep::AFTER_ROLLBACK;
            auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setUnlockCurrentEdge();
            shardReq.dirId = req.oldOwnerId;
            shardReq.name = req.oldName;
            shardReq.targetId = req.targetId;
            shardReq.wasMoved = false;
        };

        switch (reqStep) {
        case RenameDirectoryStep::START: {
            if (req.targetId.type() != InodeType::DIRECTORY) {
                _finishWithError(step, txnId, EggsError::TYPE_IS_NOT_DIRECTORY);
            } else if (req.oldOwnerId == req.newOwnerId) {
                _finishWithError(step, txnId, EggsError::SAME_DIRECTORIES);
            } else if (!_loopCheck(dbTxn, req)) {
                // First, check if we'd create a loop
                _finishWithError(step, txnId, EggsError::LOOP_IN_DIRECTORY_RENAME);
            } else {
                // Now, actually start by locking the old edge
                reqStep = RenameDirectoryStep::AFTER_LOCK_OLD_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setLockCurrentEdge();
                shardReq.dirId = req.oldOwnerId;
                shardReq.name = req.oldName;
                shardReq.targetId = req.targetId;
            }
        } break;
        case RenameDirectoryStep::AFTER_LOCK_OLD_EDGE: {
            if (shardRespError != NO_ERROR) { // TODO handle timeouts etc
                if (shardRespError == EggsError::DIRECTORY_NOT_FOUND) {
                    shardRespError = EggsError::OLD_DIRECTORY_NOT_FOUND;
                }
                if (shardRespError == EggsError::NAME_IS_LOCKED) {
                    shardRespError = EggsError::OLD_NAME_IS_LOCKED;
                }
                _finishWithError(step, txnId, shardRespError);
            } else {
                reqStep = RenameDirectoryStep::AFTER_CREATE_LOCKED_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.newOwnerId.shard()).setCreateLockedCurrentEdge();
                shardReq.dirId = req.newOwnerId;
                shardReq.name = req.newName;
                shardReq.creationTime = time;
                shardReq.targetId = req.targetId;
            }
        } break;
        case RenameDirectoryStep::AFTER_CREATE_LOCKED_EDGE: {
            if (shardRespError != NO_ERROR) { // TODO handle timeouts etc
                if (shardRespError == EggsError::DIRECTORY_NOT_FOUND) {
                    shardRespError = EggsError::NEW_DIRECTORY_NOT_FOUND;
                }
                if (shardRespError == EggsError::NAME_IS_LOCKED) {
                    shardRespError = EggsError::NEW_NAME_IS_LOCKED;
                }
                initRollback(shardRespError);
            } else {
                reqStep = RenameDirectoryStep::AFTER_UNLOCK_NEW_EDGE;
                auto& shardReq = _needsShard(step, txnId, req.newOwnerId.shard()).setUnlockCurrentEdge();
                shardReq.dirId = req.newOwnerId;
                shardReq.name = req.newName;
                shardReq.targetId = req.targetId;
                shardReq.wasMoved = false;
            }
        } break;
        case RenameDirectoryStep::AFTER_UNLOCK_NEW_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            reqStep = RenameDirectoryStep::AFTER_UNLOCK_OLD_EDGE;
            auto& shardReq = _needsShard(step, txnId, req.oldOwnerId.shard()).setUnlockCurrentEdge();
            shardReq.dirId = req.oldOwnerId;
            shardReq.name = req.oldName;
            shardReq.targetId = req.targetId;
            shardReq.wasMoved = true;
        } break;
        case RenameDirectoryStep::AFTER_UNLOCK_OLD_EDGE: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            reqStep = RenameDirectoryStep::AFTER_SET_OWNER;
            auto& shardReq = _needsShard(step, txnId, req.targetId.shard()).setSetDirectoryOwner();
            shardReq.ownerId = req.newOwnerId;
            shardReq.dirId = req.targetId;
        } break;
        case RenameDirectoryStep::AFTER_SET_OWNER: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            _finish(step, txnId).setRenameDirectory();
            // update cache
            {
                auto k = InodeIdKey::Static(req.targetId);
                auto v = InodeIdKey::Static(req.newOwnerId);
                ROCKS_DB_CHECKED(dbTxn.Put(_parentCf, k.toSlice(), v.toSlice()));
            }
        }
        case RenameDirectoryStep::AFTER_ROLLBACK: {
            ALWAYS_ASSERT(shardRespError == NO_ERROR); // TODO handle timeouts etc.
            _finishWithError(step, txnId, state.exitError());
        } break;
        default:
            throw EGGS_EXCEPTION("bad step %s", (int)reqStep);
        }

        return reqStep;
    }

    // Moves the state forward, filling in `step` appropriatedly, and writing
    // out the updated state.
    //
    // This will _not_ start a new transaction automatically if the old one
    // finishes.
    template<template<typename> typename V>
    void _advance(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& req,
        // If `shardRespError` and `shardResp` are null, we're starting to execute.
        // Otherwise, (err == NO_ERROR) == (req != nullptr).
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        V<TxnState> state,
        CDCStep& step
    ) {
        uint8_t stateStep = state().step();
        switch (req.kind()) {
        case CDCMessageKind::MAKE_DIRECTORY:
            stateStep = (uint8_t)_advanceMakeDirectory(time, dbTxn, txnId, req, shardRespError, shardResp, (MakeDirectoryStep)stateStep, state().getMakeDirectory(), step);
            break;
        case CDCMessageKind::HARD_UNLINK_DIRECTORY:
            stateStep = (uint8_t)_advanceHardUnlinkDirectory(txnId, req, shardRespError, shardResp, (HardUnlinkDirectoryStep)stateStep, state().getHardUnlinkDirectory(), step);
            break;
        case CDCMessageKind::RENAME_FILE:
            stateStep = (uint8_t)_advanceRenameFile(time, dbTxn, txnId, req, shardRespError, shardResp, (RenameFileStep)stateStep, state().getRenameFile(), step);
            break;
        case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
            stateStep = (uint8_t)_advanceSoftUnlinkDirectory(time, dbTxn, txnId, req, shardRespError, shardResp, (SoftUnlinkDirectoryStep)stateStep, state().getSoftUnlinkDirectory(), step);
            break;
        case CDCMessageKind::RENAME_DIRECTORY:
            stateStep = (uint8_t)_advanceRenameDirectory(time, dbTxn, txnId, req, shardRespError, shardResp, (RenameDirectoryStep)stateStep, state().getRenameDirectory(), step);
            break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", req.kind());
        }
        state().setStep(stateStep);

        ALWAYS_ASSERT(step.txnFinished == 0 || step.txnFinished == txnId);
        ALWAYS_ASSERT(step.txnNeedsShard == 0 || step.txnNeedsShard == txnId);
        ALWAYS_ASSERT(step.txnFinished == txnId || step.txnNeedsShard == txnId);
        if (step.txnFinished == txnId) {
            // we're done, clear the transaction from the system
            ALWAYS_ASSERT(_reqQueuePop(dbTxn) == txnId);
            _setExecutingTxn(dbTxn, 0);
            // not strictly necessary, it'll just be overwritten next time, but let's be tidy
            ROCKS_DB_CHECKED(dbTxn.Delete(_defaultCf, cdcMetadataKey(&EXECUTING_TXN_STATE_KEY)));
            // also, fill in whether we've got something next
            step.nextTxn = _firstTxnInQueue(dbTxn);
        } else {
            // we're _not_ done, write out the state
            ROCKS_DB_CHECKED(dbTxn.Put(_defaultCf, cdcMetadataKey(&EXECUTING_TXN_STATE_KEY), state.toSlice()));
        }
    }

    // Starts executing the next transaction in line, if possible. If it managed
    // to start something, it immediately advances it as well (no point delaying
    // that).
    void _startExecuting(EggsTime time, rocksdb::Transaction& dbTxn, CDCStep& step) {
        uint64_t executingTxn = _executingTxn(dbTxn);
        if (executingTxn != 0) {
            LOG_DEBUG(_env, "another transaction %s is already executing, can't start", executingTxn);
            return;
        }

        uint64_t txnToExecute = _reqQueuePeek(dbTxn, _cdcReq);
        if (txnToExecute == 0) {
            LOG_DEBUG(_env, "no transactions in queue found, can't start");
            return;
        }

        LOG_DEBUG(_env, "starting to execute txn %s", txnToExecute);
        _setExecutingTxn(dbTxn, txnToExecute);
        StaticValue<TxnState> txnState;
        txnState().start(_cdcReq.kind());
        _advance(time, dbTxn, txnToExecute, _cdcReq, NO_ERROR, nullptr, txnState, step);
    }

    uint64_t processCDCReq(
        bool sync,
        EggsTime time,
        uint64_t logIndex,
        const CDCReqContainer& req,
        CDCStep& step
    ) {
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        WrappedTransaction dbTxn(_db->BeginTransaction(options));

        step.clear();

        _advanceLastAppliedLogEntry(*dbTxn, logIndex);

        // Enqueue req
        uint64_t txnId;
        {
            LOG_DEBUG(_env, "enqueueing cdc req %s", req.kind());

            // Generate new txn id
            txnId = _lastTxn(*dbTxn) + 1;
            _setLastTxn(*dbTxn, txnId);

            // Push to queue
            _reqQueuePush(*dbTxn, txnId, req);
        }

        // Start executing, if we can
        _startExecuting(time, *dbTxn, step);

        dbTxn->Commit();

        return txnId;
    }

    void processShardResp(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        EggsError respError,
        const ShardRespContainer* resp,
        CDCStep& step
    ) {
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        WrappedTransaction dbTxn(_db->BeginTransaction(options));

        step.clear();

        _advanceLastAppliedLogEntry(*dbTxn, logIndex);

        // Find the txn
        uint64_t txnId = _executingTxn(*dbTxn);
        ALWAYS_ASSERT(txnId != 0);

        // Get the req
        {
            auto k = U64Key::Static(txnId);
            std::string reqV;
            ROCKS_DB_CHECKED(dbTxn->Get({}, _reqQueueCf, k.toSlice(), &reqV));
            UnpackCDCReq ureq(_cdcReq);
            bincodeFromRocksValue(reqV, ureq);
        }

        // Get the state
        std::string txnStateV;
        ROCKS_DB_CHECKED(dbTxn->Get({}, _defaultCf, cdcMetadataKey(&EXECUTING_TXN_STATE_KEY), &txnStateV));
        ExternalValue<TxnState> txnState(txnStateV);

        // Advance
        _advance(time, *dbTxn, txnId, _cdcReq, respError, resp, txnState, step);

        dbTxn->Commit();
    }

    void startNextTransaction(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        CDCStep& step
    ) {
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        WrappedTransaction dbTxn(_db->BeginTransaction(options));

        step.clear();

        _advanceLastAppliedLogEntry(*dbTxn, logIndex);
        _startExecuting(time, *dbTxn, step);
         
        dbTxn->Commit();
    }
};

CDCDB::CDCDB(Logger& logger, const std::string& path) {
    _impl = new CDCDBImpl(logger, path);
}

void CDCDB::close() {
    ((CDCDBImpl*)_impl)->close();
}

CDCDB::~CDCDB() {
    delete ((CDCDBImpl*)_impl);
}

uint64_t CDCDB::processCDCReq(bool sync, EggsTime time, uint64_t logIndex, const CDCReqContainer& req, CDCStep& step) {
    return ((CDCDBImpl*)_impl)->processCDCReq(sync, time, logIndex, req, step);
}

void CDCDB::processShardResp(bool sync, EggsTime time, uint64_t logIndex, EggsError respError, const ShardRespContainer* resp, CDCStep& step) {
    return ((CDCDBImpl*)_impl)->processShardResp(sync, time, logIndex, respError, resp, step);
}

void CDCDB::startNextTransaction(bool sync, EggsTime time, uint64_t logIndex, CDCStep& step) {
    return ((CDCDBImpl*)_impl)->startNextTransaction(sync, time, logIndex, step);
}

uint64_t CDCDB::lastAppliedLogEntry() {
    return ((CDCDBImpl*)_impl)->_lastAppliedLogEntry();
}
