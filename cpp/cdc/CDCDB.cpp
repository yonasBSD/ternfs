#include <memory>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/status.h>
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
#include "RocksDBUtils.hpp"
#include "ShardDB.hpp"
#include "Time.hpp"
#include "XmonAgent.hpp"

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
        if (x.txnFinished != 0 || x.txnNeedsShard != 0) {
            out << ", ";
        }
        out << "nextTxn=" << x.nextTxn;
    }
    out << ")";
    return out;
}

inline bool createCurrentLockedEdgeRetry(EggsError err) {
    return
        err == EggsError::TIMEOUT || err == EggsError::MTIME_IS_TOO_RECENT ||
        err == EggsError::MORE_RECENT_SNAPSHOT_EDGE || err == EggsError::MORE_RECENT_CURRENT_EDGE;
}

struct StateMachineEnv {
    Env& env;
    rocksdb::OptimisticTransactionDB* db;
    rocksdb::ColumnFamilyHandle* defaultCf;
    rocksdb::ColumnFamilyHandle* parentCf;
    rocksdb::Transaction& dbTxn;
    EggsTime time;
    uint64_t txnId;
    uint8_t txnStep;
    CDCStep& cdcStep;

    StateMachineEnv(
        Env& env_, rocksdb::OptimisticTransactionDB* db_, rocksdb::ColumnFamilyHandle* defaultCf_, rocksdb::ColumnFamilyHandle* parentCf_, rocksdb::Transaction& dbTxn_, EggsTime time_, uint64_t txnId_, uint8_t step_, CDCStep& cdcStep_
    ):
        env(env_), db(db_), defaultCf(defaultCf_), parentCf(parentCf_), dbTxn(dbTxn_), time(time_), txnId(txnId_), txnStep(step_), cdcStep(cdcStep_)
    {}

    InodeId nextDirectoryId() {
        std::string v;
        ROCKS_DB_CHECKED(db->Get({}, defaultCf, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY), &v));
        ExternalValue<InodeIdValue> nextId(v);
        InodeId id = nextId().id();
        nextId().setId(InodeId::FromU64(id.u64 + 1));
        ROCKS_DB_CHECKED(dbTxn.Put(defaultCf, cdcMetadataKey(&NEXT_DIRECTORY_ID_KEY), nextId.toSlice()));
        return id;
    }

    ShardReqContainer& needsShard(uint8_t step, ShardId shid, bool repeated) {
        txnStep = step;
        cdcStep.txnFinished = 0;
        cdcStep.txnNeedsShard = txnId;
        cdcStep.shardReq.shid = shid;
        cdcStep.shardReq.repeated = repeated;
        return cdcStep.shardReq.req;
    }

    CDCRespContainer& finish() {
        cdcStep.txnFinished = txnId;
        cdcStep.err = NO_ERROR;
        return cdcStep.resp;
    }

    void finishWithError(EggsError err) {
        ALWAYS_ASSERT(err != NO_ERROR);
        cdcStep.txnFinished = txnId;
        cdcStep.err = err;
        cdcStep.txnNeedsShard = 0;
    }
};

constexpr uint8_t TXN_START = 0;

enum MakeDirectoryStep : uint8_t {
    MAKE_DIRECTORY_LOOKUP = 1,
    MAKE_DIRECTORY_CREATE_DIR = 2,
    MAKE_DIRECTORY_LOOKUP_OLD_CREATION_TIME = 3,
    MAKE_DIRECTORY_CREATE_LOCKED_EDGE = 4,
    MAKE_DIRECTORY_UNLOCK_EDGE = 5,
    MAKE_DIRECTORY_ROLLBACK = 6,
};

// Steps:
//
// 1. Lookup if an existing directory exists. If it does, immediately succeed.
// 2. Allocate inode id here in the CDC
// 3. Create directory in shard we get from the inode   
// 4. Lookup old creation time for the edge we're about to create
// 5. Create locked edge from owner to newly created directory. If this fail because of bad creation time, go back to 4
// 6. Unlock the edge created in 3
//
// If 4 or 5 fails, 3 must be rolled back. 6 does not fail.
//
// 1 is necessary rather than failing on attempted override because otherwise failures
// due to repeated calls are indistinguishable from genuine failures.
struct MakeDirectoryStateMachine {
    StateMachineEnv& env;
    const MakeDirectoryReq& req;
    MakeDirectoryState state;

    MakeDirectoryStateMachine(StateMachineEnv& env_, const MakeDirectoryReq& req_, MakeDirectoryState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            start();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case MAKE_DIRECTORY_LOOKUP: lookup(); break;
                case MAKE_DIRECTORY_CREATE_DIR: createDirectoryInode(); break;
                case MAKE_DIRECTORY_LOOKUP_OLD_CREATION_TIME: lookupOldCreationTime(); break;
                case MAKE_DIRECTORY_CREATE_LOCKED_EDGE: createLockedEdge(); break;
                case MAKE_DIRECTORY_UNLOCK_EDGE: unlockEdge(); break;
                case MAKE_DIRECTORY_ROLLBACK: rollback(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case MAKE_DIRECTORY_LOOKUP: afterLookup(err, resp); break;
                case MAKE_DIRECTORY_CREATE_DIR: afterCreateDirectoryInode(err, resp); break;
                case MAKE_DIRECTORY_LOOKUP_OLD_CREATION_TIME: afterLookupOldCreationTime(err, resp); break;
                case MAKE_DIRECTORY_CREATE_LOCKED_EDGE: afterCreateLockedEdge(err, resp); break;
                case MAKE_DIRECTORY_UNLOCK_EDGE: afterUnlockEdge(err, resp); break;
                case MAKE_DIRECTORY_ROLLBACK: afterRollback(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }

    void start() {
        state.setDirId(env.nextDirectoryId());
        lookup();
    }

    void lookup(bool repeated = false) {
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_LOOKUP, req.ownerId.shard(), repeated).setLookup();
        shardReq.dirId = req.ownerId;
        shardReq.name = req.name;
    }

    void afterLookup(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lookup(true); // retry
        } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
            env.finishWithError(err);
        } else if (err == EggsError::NAME_NOT_FOUND) {
            // normal case, let's proceed
            createDirectoryInode();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            const auto& lookupResp = resp->getLookup();
            if (lookupResp.targetId.type() == InodeType::DIRECTORY) {
                // we're good already
                auto& cdcResp = env.finish().setMakeDirectory();
                cdcResp.creationTime = lookupResp.creationTime;
                cdcResp.id = lookupResp.targetId;
            } else {
                env.finishWithError(EggsError::CANNOT_OVERRIDE_NAME);
            }
        }
    }

    void createDirectoryInode(bool repeated = false) {
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_CREATE_DIR, state.dirId().shard(), repeated).setCreateDirectoryInode();
        shardReq.id = state.dirId();
        shardReq.ownerId = req.ownerId;
    }

    void afterCreateDirectoryInode(EggsError shardRespError, const ShardRespContainer* shardResp) {
        if (shardRespError == EggsError::TIMEOUT) {
            // Try again -- note that the call to create directory inode is idempotent.
            createDirectoryInode(true);
        } else {
            ALWAYS_ASSERT(shardRespError == NO_ERROR);
            lookupOldCreationTime();
        }
    }

    void lookupOldCreationTime(bool repeated = false) {
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_LOOKUP_OLD_CREATION_TIME, req.ownerId.shard(), repeated).setFullReadDir();
        shardReq.dirId = req.ownerId;
        shardReq.flags = FULL_READ_DIR_BACKWARDS | FULL_READ_DIR_SAME_NAME | FULL_READ_DIR_CURRENT;
        shardReq.limit = 1;
        shardReq.startName = req.name;
        shardReq.startTime = 0; // we have current set
    }

    void afterLookupOldCreationTime(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lookupOldCreationTime(true); // retry
        } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
            // the directory we looked into doesn't even exist anymore --
            // we've failed hard and we need to remove the inode.
            state.setExitError(err);
            rollback();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            // there might be no existing edge
            const auto& fullReadDir = resp->getFullReadDir();
            ALWAYS_ASSERT(fullReadDir.results.els.size() < 2); // we have limit=1
            if (fullReadDir.results.els.size() == 0) {
                state.setOldCreationTime(0); // there was nothing present for this name
            } else {
                state.setOldCreationTime(fullReadDir.results.els[0].creationTime);
            }
            // keep going
            createLockedEdge();
        }
    }

    void createLockedEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_CREATE_LOCKED_EDGE, req.ownerId.shard(), repeated).setCreateLockedCurrentEdge();
        shardReq.dirId = req.ownerId;
        shardReq.targetId = state.dirId();
        shardReq.name = req.name;
        shardReq.oldCreationTime = state.oldCreationTime();
    }

    void afterCreateLockedEdge(EggsError err, const ShardRespContainer* resp) {
        if (createCurrentLockedEdgeRetry(err)) {
            createLockedEdge(true); // try again
        } else if (err == EggsError::CANNOT_OVERRIDE_NAME) {
            // this happens when a file gets created between when we looked
            // up whether there was something else and now.
            state.setExitError(err);
            rollback();
        } else if (err == EggsError::MISMATCHING_CREATION_TIME) {
            // lookup the old creation time again
            lookupOldCreationTime();
        } else {
            // We know we cannot get directory not found because we managed to lookup
            // old creation time.
            //
            // We also cannot get MISMATCHING_TARGET since we are the only one
            // creating locked edges, and transactions execute serially.
            ALWAYS_ASSERT(err == NO_ERROR);
            state.setCreationTime(resp->getCreateLockedCurrentEdge().creationTime);
            unlockEdge();
        }
    }

    void unlockEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_UNLOCK_EDGE, req.ownerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.ownerId;
        shardReq.name = req.name;
        shardReq.targetId = state.dirId();
        shardReq.wasMoved = false;
        shardReq.creationTime = state.creationTime();
    }

    void afterUnlockEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            // retry
            unlockEdge(true);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            // We're done, record the parent relationship and finish
            {
                auto k = InodeIdKey::Static(state.dirId());
                auto v = InodeIdValue::Static(req.ownerId);
                ROCKS_DB_CHECKED(env.dbTxn.Put(env.parentCf, k.toSlice(), v.toSlice()));
            }
            auto& resp = env.finish().setMakeDirectory();
            resp.id = state.dirId();
            resp.creationTime = state.creationTime();
        }
    }

    void rollback(bool repeated = false) {
        // disown the child, it'll be collected by GC.
        auto& shardReq = env.needsShard(MAKE_DIRECTORY_ROLLBACK, state.dirId().shard(), repeated).setRemoveDirectoryOwner();
        shardReq.dirId = state.dirId();
        // we've just created this directory, it is empty, therefore the policy
        // is irrelevant.
        shardReq.info = defaultDirectoryInfo();
    }

    void afterRollback(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            rollback(true); // retry
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            env.finishWithError(state.exitError());
        }
    }
};

enum HardUnlinkDirectoryStep : uint8_t {
    HARD_UNLINK_DIRECTORY_REMOVE_INODE = 1,
};

// The only reason we have this here is for possible conflicts with RemoveDirectoryOwner,
// which might temporarily set the owner of a directory to NULL. Since in the current
// implementation we only ever have one transaction in flight in the CDC, we can just
// execute this.
struct HardUnlinkDirectoryStateMachine {
    StateMachineEnv& env;
    const HardUnlinkDirectoryReq& req;
    HardUnlinkDirectoryState state;

    HardUnlinkDirectoryStateMachine(StateMachineEnv& env_, const HardUnlinkDirectoryReq& req_, HardUnlinkDirectoryState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            removeInode();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case HARD_UNLINK_DIRECTORY_REMOVE_INODE: removeInode(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case HARD_UNLINK_DIRECTORY_REMOVE_INODE: afterRemoveInode(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }

    void removeInode(bool repeated = false) {
        auto& shardReq = env.needsShard(HARD_UNLINK_DIRECTORY_REMOVE_INODE, req.dirId.shard(), repeated).setRemoveInode();
        shardReq.id = req.dirId;
    }

    void afterRemoveInode(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            removeInode(true); // try again
        } else if (
            err == EggsError::DIRECTORY_NOT_FOUND || err == EggsError::DIRECTORY_HAS_OWNER || err == EggsError::DIRECTORY_NOT_EMPTY
        ) {
            env.finishWithError(err);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            env.finish().setHardUnlinkDirectory();
        }
    }
};

enum RenameFileStep : uint8_t {
    RENAME_FILE_LOCK_OLD_EDGE = 1,
    RENAME_FILE_LOOKUP_OLD_CREATION_TIME = 2,
    RENAME_FILE_CREATE_NEW_LOCKED_EDGE = 3,
    RENAME_FILE_UNLOCK_NEW_EDGE = 4,
    RENAME_FILE_UNLOCK_OLD_EDGE = 5,
    RENAME_FILE_ROLLBACK = 6,
};

// Steps:
//
// 1. lock source current edge
// 2. lookup prev creation time for current target edge
// 3. create destination locked current target edge. if it fails because of bad creation time, go back to 2
// 4. unlock edge in step 3
// 5. unlock source target current edge, and soft unlink it
//
// If we fail at step 2 or 3, we need to roll back step 1. Steps 3 and 4 should never fail.    
struct RenameFileStateMachine {
    StateMachineEnv& env;
    const RenameFileReq& req;
    RenameFileState state;

    RenameFileStateMachine(StateMachineEnv& env_, const RenameFileReq& req_, RenameFileState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            start();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case RENAME_FILE_LOCK_OLD_EDGE: lockOldEdge(); break;
                case RENAME_FILE_LOOKUP_OLD_CREATION_TIME: lookupOldCreationTime(); break;
                case RENAME_FILE_CREATE_NEW_LOCKED_EDGE: createNewLockedEdge(); break;
                case RENAME_FILE_UNLOCK_NEW_EDGE: unlockNewEdge(); break;
                case RENAME_FILE_UNLOCK_OLD_EDGE: unlockOldEdge(); break;
                case RENAME_FILE_ROLLBACK: rollback(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case RENAME_FILE_LOCK_OLD_EDGE: afterLockOldEdge(err, resp); break;
                case RENAME_FILE_LOOKUP_OLD_CREATION_TIME: afterLookupOldCreationTime(err, resp); break;
                case RENAME_FILE_CREATE_NEW_LOCKED_EDGE: afterCreateNewLockedEdge(err, resp); break;
                case RENAME_FILE_UNLOCK_NEW_EDGE: afterUnlockNewEdge(err, resp); break;
                case RENAME_FILE_UNLOCK_OLD_EDGE: afterUnlockOldEdge(err, resp); break;
                case RENAME_FILE_ROLLBACK: afterRollback(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }

    void start() {
        // We need this explicit check here because moving directories is more complicated,
        // and therefore we do it in another transaction type entirely.
        if (req.targetId.type() == InodeType::DIRECTORY) {
            env.finishWithError(EggsError::TYPE_IS_NOT_DIRECTORY);
        } else if (req.oldOwnerId == req.newOwnerId) {
            env.finishWithError(EggsError::SAME_DIRECTORIES);
        } else {
            lockOldEdge();
        }
    }

    void lockOldEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_LOCK_OLD_EDGE, req.oldOwnerId.shard(), repeated).setLockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.name = req.oldName;
        shardReq.targetId = req.targetId;
        shardReq.creationTime = req.oldCreationTime;
    }

    void afterLockOldEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lockOldEdge(true); // retry
        } else if (
            err == EggsError::EDGE_NOT_FOUND || err == EggsError::MISMATCHING_CREATION_TIME || err == EggsError::DIRECTORY_NOT_FOUND
        ) {
            // We failed hard and we have nothing to roll back
            if (err == EggsError::DIRECTORY_NOT_FOUND) {
                err = EggsError::OLD_DIRECTORY_NOT_FOUND;
            }
            env.finishWithError(err);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            lookupOldCreationTime();
        }
    }

    void lookupOldCreationTime(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_LOOKUP_OLD_CREATION_TIME, req.newOwnerId.shard(), repeated).setFullReadDir(); 
        shardReq.dirId = req.newOwnerId;
        shardReq.flags = FULL_READ_DIR_BACKWARDS | FULL_READ_DIR_SAME_NAME | FULL_READ_DIR_CURRENT;
        shardReq.limit = 1;
        shardReq.startName = req.newName;
        shardReq.startTime = 0; // we have current set
    }

    void afterLookupOldCreationTime(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lookupOldCreationTime(true); // retry
        } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
            // we've failed hard and we need to unlock the old edge.
            err = EggsError::NEW_DIRECTORY_NOT_FOUND;
            state.setExitError(err);
            rollback();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            // there might be no existing edge
            const auto& fullReadDir = resp->getFullReadDir();
            ALWAYS_ASSERT(fullReadDir.results.els.size() < 2); // we have limit=1
            if (fullReadDir.results.els.size() == 0) {
                state.setNewOldCreationTime(0); // there was nothing present for this name
            } else {
                state.setNewOldCreationTime(fullReadDir.results.els[0].creationTime);
            }
            // keep going
            createNewLockedEdge();
        }
    }

    void createNewLockedEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_CREATE_NEW_LOCKED_EDGE, req.newOwnerId.shard(), repeated).setCreateLockedCurrentEdge();
        shardReq.dirId = req.newOwnerId;
        shardReq.name = req.newName;
        shardReq.targetId = req.targetId;
        shardReq.oldCreationTime = state.newOldCreationTime();
    }

    void afterCreateNewLockedEdge(EggsError err, const ShardRespContainer* resp) {
        if (createCurrentLockedEdgeRetry(err)) {
            createNewLockedEdge(true); // retry
        } else if (err == EggsError::MISMATCHING_CREATION_TIME) {
            // we need to lookup the creation time again.
            lookupOldCreationTime();
        } else if (err == EggsError::CANNOT_OVERRIDE_NAME) {
            // we failed hard and we need to rollback
            state.setExitError(err);
            rollback();
        } else {
            state.setNewCreationTime(resp->getCreateLockedCurrentEdge().creationTime);
            unlockNewEdge();
        }
    }

    void unlockNewEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_UNLOCK_NEW_EDGE, req.newOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.newOwnerId;
        shardReq.targetId = req.targetId;
        shardReq.name = req.newName;
        shardReq.wasMoved = false;
        shardReq.creationTime = state.newCreationTime();
    }

    void afterUnlockNewEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            unlockNewEdge(true); // retry
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            unlockOldEdge();
        }
    }

    void unlockOldEdge(bool repeated = false) {
        // We're done creating the destination edge, now unlock the source, marking it as moved
        auto& shardReq = env.needsShard(RENAME_FILE_UNLOCK_OLD_EDGE, req.oldOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.targetId = req.targetId;
        shardReq.name = req.oldName;
        shardReq.wasMoved = true;
        shardReq.creationTime = req.oldCreationTime;
    }
    
    void afterUnlockOldEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            unlockOldEdge(true); // retry
        } else {
            // This can only be because of repeated calls from here: we have the edge locked,
            // and only the CDC does changes.
            // TODO it would be cleaner to verify this with a lookup
            ALWAYS_ASSERT(err == NO_ERROR || err == EggsError::EDGE_NOT_FOUND);
            // we're finally done
            auto& resp = env.finish().setRenameFile();
            resp.creationTime = state.newCreationTime();
        }
    }

    void rollback(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_ROLLBACK, req.oldOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.name = req.oldName;
        shardReq.targetId = req.targetId;
        shardReq.wasMoved = false;
        shardReq.creationTime = state.newCreationTime();
    }

    void afterRollback(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            rollback(true); // retry
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            env.finishWithError(state.exitError());
        }
    }
};

enum SoftUnlinkDirectoryStep : uint8_t {
    SOFT_UNLINK_DIRECTORY_LOCK_EDGE = 1,
    SOFT_UNLINK_DIRECTORY_STAT = 2,
    SOFT_UNLINK_DIRECTORY_REMOVE_OWNER = 3,
    SOFT_UNLINK_DIRECTORY_UNLOCK_EDGE = 4,
    SOFT_UNLINK_DIRECTORY_ROLLBACK = 5,
};

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
struct SoftUnlinkDirectoryStateMachine {
    StateMachineEnv& env;
    const SoftUnlinkDirectoryReq& req;
    SoftUnlinkDirectoryState state;
    DirectoryInfo info;

    SoftUnlinkDirectoryStateMachine(StateMachineEnv& env_, const SoftUnlinkDirectoryReq& req_, SoftUnlinkDirectoryState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            start();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case SOFT_UNLINK_DIRECTORY_LOCK_EDGE: lockEdge(); break;
                case SOFT_UNLINK_DIRECTORY_STAT: stat(); break;
                // We don't persist the directory info, so we need to restart the stating when resuming
                case SOFT_UNLINK_DIRECTORY_REMOVE_OWNER: stat(); break;
                case SOFT_UNLINK_DIRECTORY_UNLOCK_EDGE: unlockEdge(); break;
                case SOFT_UNLINK_DIRECTORY_ROLLBACK: rollback(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case SOFT_UNLINK_DIRECTORY_LOCK_EDGE: afterLockEdge(err, resp); break;
                case SOFT_UNLINK_DIRECTORY_STAT: afterStat(err, resp); break;
                case SOFT_UNLINK_DIRECTORY_REMOVE_OWNER: afterRemoveOwner(err, resp); break;
                case SOFT_UNLINK_DIRECTORY_UNLOCK_EDGE: afterUnlockEdge(err, resp); break;
                case SOFT_UNLINK_DIRECTORY_ROLLBACK: afterRollback(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }

    void start() {
        if (req.targetId.type() != InodeType::DIRECTORY) {
            env.finishWithError(EggsError::TYPE_IS_NOT_DIRECTORY);
        } else {
            lockEdge();
        }
    }

    void lockEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(SOFT_UNLINK_DIRECTORY_LOCK_EDGE, req.ownerId.shard(), repeated).setLockCurrentEdge();
        shardReq.dirId = req.ownerId;
        shardReq.name = req.name;
        shardReq.targetId = req.targetId;
        shardReq.creationTime = req.creationTime;
    }

    void afterLockEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lockEdge(true);
        } else if (err == EggsError::MISMATCHING_CREATION_TIME || err == EggsError::EDGE_NOT_FOUND) {
            env.finishWithError(err); // no rollback to be done
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            state.setStatDirId(req.targetId);
            stat();
        }
    }

    void stat(bool repeated = false) {
        auto& shardReq = env.needsShard(SOFT_UNLINK_DIRECTORY_STAT, state.statDirId().shard(), repeated).setStatDirectory();
        shardReq.id = state.statDirId();
    }

    void afterStat(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            stat(true); // retry
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            const auto& statResp = resp->getStatDirectory();
            // insert tags
            for (const auto& newEntry : statResp.info.entries.els) {
                bool found = false;
                for (const auto& entry: info.entries.els) {
                    if (entry.tag == newEntry.tag) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    info.entries.els.emplace_back(newEntry);
                }
            }
            if (info.entries.els.size() == REQUIRED_DIR_INFO_TAGS.size()) { // we've found everything
                auto& shardReq = env.needsShard(SOFT_UNLINK_DIRECTORY_REMOVE_OWNER, req.targetId.shard(), false).setRemoveDirectoryOwner();
                shardReq.dirId = req.targetId;
                shardReq.info = info;
            } else {
                ALWAYS_ASSERT(statResp.owner != NULL_INODE_ID);
                // keep walking upwards
                state.setStatDirId(statResp.owner);
                stat();
            }
        }
    }

    void afterRemoveOwner(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            // we don't want to keep the dir info around start again from the last stat
            stat();
        } else if (err == EggsError::DIRECTORY_NOT_EMPTY) {
            state.setExitError(err);
            rollback();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            unlockEdge();
        }
    }

    void unlockEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(SOFT_UNLINK_DIRECTORY_UNLOCK_EDGE, req.ownerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.ownerId;
        shardReq.name = req.name;
        shardReq.targetId = req.targetId;
        // Note that here we used wasMoved even if the subsequent
        // snapshot edge will be non-owned, since we're dealing with
        // a directory, rather than a file.
        shardReq.wasMoved = true;
        shardReq.creationTime = req.creationTime;
    }

    void afterUnlockEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            unlockEdge(true);
        } else {
            // This can only be because of repeated calls from here: we have the edge locked,
            // and only the CDC does changes.
            // TODO it would be cleaner to verify this with a lookup 
            ALWAYS_ASSERT(err == NO_ERROR || err == EggsError::EDGE_NOT_FOUND);
            auto& cdcResp = env.finish().setSoftUnlinkDirectory();
            // Update parent map
            {
                auto k = InodeIdKey::Static(req.targetId);
                ROCKS_DB_CHECKED(env.dbTxn.Delete(env.parentCf, k.toSlice()));
            }
        }
    }

    void rollback(bool repeated = false) {
        auto& shardReq = env.needsShard(SOFT_UNLINK_DIRECTORY_ROLLBACK, req.ownerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.ownerId;
        shardReq.name = req.name;
        shardReq.targetId = req.targetId;
        shardReq.wasMoved = false;
        shardReq.creationTime = req.creationTime;
    }

    void afterRollback(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            rollback(true);
        } else {
            // This can only be because of repeated calls from here: we have the edge locked,
            // and only the CDC does changes.
            // TODO it would be cleaner to verify this with a lookup 
            ALWAYS_ASSERT(err == NO_ERROR || err == EggsError::EDGE_NOT_FOUND);
            env.finishWithError(state.exitError());
        }
    }
};

enum RenameDirectoryStep : uint8_t {
    RENAME_DIRECTORY_LOCK_OLD_EDGE = 1,
    RENAME_DIRECTORY_LOOKUP_OLD_CREATION_TIME = 2,
    RENAME_DIRECTORY_CREATE_LOCKED_NEW_EDGE = 3,
    RENAME_DIRECTORY_UNLOCK_NEW_EDGE = 4,
    RENAME_DIRECTORY_UNLOCK_OLD_EDGE = 5,
    RENAME_DIRECTORY_SET_OWNER = 6,
    RENAME_DIRECTORY_ROLLBACK = 7,
};

// Steps:
//
// 1. Make sure there's no loop by traversing the parents
// 2. Lock old edge
// 3. Lookup old creation time for the edge
// 4. Create and lock the new edge
// 5. Unlock the new edge
// 6. Unlock and unlink the old edge
// 7. Update the owner of the moved directory to the new directory
//
// If we fail at step 3 or 4, we need to unlock the edge we locked at step 2. Step 5 and 6
// should never fail.
struct RenameDirectoryStateMachine {
    StateMachineEnv& env;
    const RenameDirectoryReq& req;
    RenameDirectoryState state;

    RenameDirectoryStateMachine(StateMachineEnv& env_, const RenameDirectoryReq& req_, RenameDirectoryState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            start();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case RENAME_DIRECTORY_LOCK_OLD_EDGE: lockOldEdge(); break;
                case RENAME_DIRECTORY_LOOKUP_OLD_CREATION_TIME: lookupOldCreationTime(); break;
                case RENAME_DIRECTORY_CREATE_LOCKED_NEW_EDGE: createLockedNewEdge(); break;
                case RENAME_DIRECTORY_UNLOCK_NEW_EDGE: unlockNewEdge(); break;
                case RENAME_DIRECTORY_UNLOCK_OLD_EDGE: unlockOldEdge(); break;
                case RENAME_DIRECTORY_SET_OWNER: setOwner(); break;
                case RENAME_DIRECTORY_ROLLBACK: rollback(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case RENAME_DIRECTORY_LOCK_OLD_EDGE: afterLockOldEdge(err, resp); break;
                case RENAME_DIRECTORY_LOOKUP_OLD_CREATION_TIME: afterLookupOldCreationTime(err, resp); break;
                case RENAME_DIRECTORY_CREATE_LOCKED_NEW_EDGE: afterCreateLockedEdge(err, resp); break;
                case RENAME_DIRECTORY_UNLOCK_NEW_EDGE: afterUnlockNewEdge(err, resp); break;
                case RENAME_DIRECTORY_UNLOCK_OLD_EDGE: afterUnlockOldEdge(err, resp); break;
                case RENAME_DIRECTORY_SET_OWNER: afterSetOwner(err, resp); break;
                case RENAME_DIRECTORY_ROLLBACK: afterRollback(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }
    
    // Check that changing this parent-child relationship wouldn't create
    // loops in directory structure.
    bool loopCheck() {
        std::unordered_set<InodeId> visited;
        InodeId cursor = req.targetId;
        for (;;) {
            if (visited.count(cursor) > 0) {
                LOG_INFO(env.env, "Re-encountered %s in loop check, will return false", cursor);
                return false;
            }
            LOG_DEBUG(env.env, "Performing loop check for %s", cursor);
            visited.insert(cursor);
            if (cursor == req.targetId) {
                cursor = req.newOwnerId;
            } else {
                auto k = InodeIdKey::Static(cursor);
                std::string v;
                ROCKS_DB_CHECKED(env.dbTxn.Get({}, env.parentCf, k.toSlice(), &v));
                cursor = ExternalValue<InodeIdValue>(v)().id();
            }
            if (cursor == ROOT_DIR_INODE_ID) {
                break;
            }
        }
        return true;
    }

    void start() {
        if (req.targetId.type() != InodeType::DIRECTORY) {
            env.finishWithError(EggsError::TYPE_IS_NOT_DIRECTORY);
        } else if (req.oldOwnerId == req.newOwnerId) {
            env.finishWithError(EggsError::SAME_DIRECTORIES);
        } else if (!loopCheck()) {
            // First, check if we'd create a loop
            env.finishWithError(EggsError::LOOP_IN_DIRECTORY_RENAME);
        } else {
            // Now, actually start by locking the old edge
            lockOldEdge();
        }
    }

    void lockOldEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_LOCK_OLD_EDGE, req.oldOwnerId.shard(), repeated).setLockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.name = req.oldName;
        shardReq.targetId = req.targetId;
        shardReq.creationTime = req.oldCreationTime;
    }

    void afterLockOldEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lockOldEdge(true); // retry
        } else if (
            err == EggsError::DIRECTORY_NOT_FOUND || err == EggsError::EDGE_NOT_FOUND || err == EggsError::MISMATCHING_CREATION_TIME
        ) {
            if (err == EggsError::DIRECTORY_NOT_FOUND) {
                err = EggsError::OLD_DIRECTORY_NOT_FOUND;
            }
            env.finishWithError(err);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            lookupOldCreationTime();
        }
    }

    void lookupOldCreationTime(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_FILE_LOOKUP_OLD_CREATION_TIME, req.newOwnerId.shard(), repeated).setFullReadDir();
        shardReq.dirId = req.newOwnerId;
        shardReq.flags = FULL_READ_DIR_BACKWARDS | FULL_READ_DIR_SAME_NAME | FULL_READ_DIR_CURRENT;
        shardReq.limit = 1;
        shardReq.startName = req.newName;
        shardReq.startTime = 0; // we have current set
    }

    void afterLookupOldCreationTime(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            lookupOldCreationTime(true); // retry
        } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
            // we've failed hard and we need to unlock the old edge.
            state.setExitError(err);
            rollback();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            // there might be no existing edge
            const auto& fullReadDir = resp->getFullReadDir();
            ALWAYS_ASSERT(fullReadDir.results.els.size() < 2); // we have limit=1
            if (fullReadDir.results.els.size() == 0) {
                state.setNewOldCreationTime(0); // there was nothing present for this name
            } else {
                state.setNewOldCreationTime(fullReadDir.results.els[0].creationTime);
            }
            // keep going
            createLockedNewEdge();
        }
    }

    void createLockedNewEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_CREATE_LOCKED_NEW_EDGE, req.newOwnerId.shard(), repeated).setCreateLockedCurrentEdge();
        shardReq.dirId = req.newOwnerId;
        shardReq.name = req.newName;
        shardReq.targetId = req.targetId;
        shardReq.oldCreationTime = state.newOldCreationTime();
    }

    void afterCreateLockedEdge(EggsError err, const ShardRespContainer* resp) {
        if (createCurrentLockedEdgeRetry(err)) {
            createLockedNewEdge(true);
        } else if (err == EggsError::MISMATCHING_CREATION_TIME) {
            // we need to lookup the creation time again.
            lookupOldCreationTime();
        } else if (err == EggsError::CANNOT_OVERRIDE_NAME) {
            state.setExitError(err);
            rollback();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            state.setNewCreationTime(resp->getCreateLockedCurrentEdge().creationTime);
            unlockNewEdge();
        }
    }

    void unlockNewEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_UNLOCK_NEW_EDGE, req.newOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.newOwnerId;
        shardReq.name = req.newName;
        shardReq.targetId = req.targetId;
        shardReq.wasMoved = false;
        shardReq.creationTime = state.newCreationTime();
    }

    void afterUnlockNewEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            unlockNewEdge(true);
        } else if (err == EggsError::EDGE_NOT_FOUND) {
            // This can only be because of repeated calls from here: we have the edge locked,
            // and only the CDC does changes.
            // TODO it would be cleaner to verify this with a lookup
            unlockOldEdge();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            unlockOldEdge();
        }
    }

    void unlockOldEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_UNLOCK_OLD_EDGE, req.oldOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.name = req.oldName;
        shardReq.targetId = req.targetId;
        shardReq.wasMoved = true;
        shardReq.creationTime = req.oldCreationTime;
    }

    void afterUnlockOldEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            unlockOldEdge(true);
        } else if (err == EggsError::EDGE_NOT_FOUND) {
            // This can only be because of repeated calls from here: we have the edge locked,
            // and only the CDC does changes.
            // TODO it would be cleaner to verify this with a lookup
            setOwner();
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            setOwner();
        }
    }

    void setOwner(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_SET_OWNER, req.targetId.shard(), repeated).setSetDirectoryOwner();
        shardReq.ownerId = req.newOwnerId;
        shardReq.dirId = req.targetId;
    }

    void afterSetOwner(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            setOwner(true);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            auto& resp = env.finish().setRenameDirectory();
            resp.creationTime = state.newCreationTime();
            // update cache
            {
                auto k = InodeIdKey::Static(req.targetId);
                auto v = InodeIdValue::Static(req.newOwnerId);
                ROCKS_DB_CHECKED(env.dbTxn.Put(env.parentCf, k.toSlice(), v.toSlice()));
            }
        }
    }

    void rollback(bool repeated = false) {
        auto& shardReq = env.needsShard(RENAME_DIRECTORY_ROLLBACK, req.oldOwnerId.shard(), repeated).setUnlockCurrentEdge();
        shardReq.dirId = req.oldOwnerId;
        shardReq.name = req.oldName;
        shardReq.targetId = req.targetId;
        shardReq.wasMoved = false;
        shardReq.creationTime = state.newCreationTime();
    }

    void afterRollback(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            rollback(true);
        } else {
            env.finishWithError(state.exitError());
        }
    }
};

enum CrossShardHardUnlinkFileStep : uint8_t {
    CROSS_SHARD_HARD_UNLINK_FILE_REMOVE_EDGE = 1,
    CROSS_SHARD_HARD_UNLINK_FILE_MAKE_TRANSIENT = 2,
};

// Steps:
//
// 1. Remove owning edge in one shard
// 2. Make file transient in other shard
//
// Step 2 cannot fail.
struct CrossShardHardUnlinkFileStateMachine {
    StateMachineEnv& env;
    const CrossShardHardUnlinkFileReq& req;
    CrossShardHardUnlinkFileState state;

    CrossShardHardUnlinkFileStateMachine(StateMachineEnv& env_, const CrossShardHardUnlinkFileReq& req_, CrossShardHardUnlinkFileState state_):
        env(env_), req(req_), state(state_)
    {}

    void resume(EggsError err, const ShardRespContainer* resp) {
        if (env.txnStep == TXN_START) {
            start();
            return;
        }
        if (unlikely(err == NO_ERROR && resp == nullptr)) { // we're resuming with no response
            switch (env.txnStep) {
                case CROSS_SHARD_HARD_UNLINK_FILE_REMOVE_EDGE: removeEdge(); break;
                case CROSS_SHARD_HARD_UNLINK_FILE_MAKE_TRANSIENT: makeTransient(); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        } else {
            switch (env.txnStep) {
                case CROSS_SHARD_HARD_UNLINK_FILE_REMOVE_EDGE: afterRemoveEdge(err, resp); break;
                case CROSS_SHARD_HARD_UNLINK_FILE_MAKE_TRANSIENT: afterMakeTransient(err, resp); break;
                default: throw EGGS_EXCEPTION("bad step %s", env.txnStep);
            }
        }
    }

    void start() {
        if (req.ownerId.shard() == req.targetId.shard()) {
            env.finishWithError(EggsError::SAME_SHARD);
        } else {
            removeEdge();
        }
    }

    void removeEdge(bool repeated = false) {
        auto& shardReq = env.needsShard(CROSS_SHARD_HARD_UNLINK_FILE_REMOVE_EDGE, req.ownerId.shard(), repeated).setRemoveOwnedSnapshotFileEdge();
        shardReq.ownerId = req.ownerId;
        shardReq.targetId = req.targetId;
        shardReq.name = req.name;
        shardReq.creationTime = req.creationTime;
    }

    void afterRemoveEdge(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT || err == EggsError::MTIME_IS_TOO_RECENT) {
            removeEdge(true);
        } else if (err == EggsError::DIRECTORY_NOT_FOUND) {
            env.finishWithError(err);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            makeTransient();
        }
    }

    void makeTransient(bool repeated = false) {
        auto& shardReq = env.needsShard(CROSS_SHARD_HARD_UNLINK_FILE_MAKE_TRANSIENT, req.targetId.shard(), repeated).setMakeFileTransient();
        shardReq.id = req.targetId;
        shardReq.note = req.name;
    }

    void afterMakeTransient(EggsError err, const ShardRespContainer* resp) {
        if (err == EggsError::TIMEOUT) {
            makeTransient(true);
        } else {
            ALWAYS_ASSERT(err == NO_ERROR);
            env.finish().setCrossShardHardUnlinkFile();
        }
    }
};

// Wrapper types to pack/unpack with kind
struct PackCDCReq {
    const CDCReqContainer& req;

    PackCDCReq(const CDCReqContainer& req_): req(req_) {}

    void pack(BincodeBuf& buf) const {
        buf.packScalar<CDCMessageKind>(req.kind());
        req.pack(buf);
    }

    size_t packedSize() const {
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

    CDCDBImpl(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& path) : _env(logger, xmon, "cdc_db") {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.create_missing_column_families = true;
        options.compression = rocksdb::kLZ4Compression;
        // In the shards we set this given that 1000*256 = 256k, doing it here also
        // for symmetry although it's probably not needed.
        options.max_open_files = 1000;
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

    // ----------------------------------------------------------------
    // retrying txns

    void commitTransaction(rocksdb::Transaction& txn) {
        XmonNCAlert alert(10_sec);
        for (;;) {
            auto status = txn.Commit();
            if (likely(status.ok())) {
                _env.clearAlert(alert);
                return;
            }
            if (likely(status.IsTryAgain())) {
                _env.updateAlert(alert, "got try again in CDC transaction, will sleep for a second and try again");
                sleepFor(1_sec);
                continue;
            }
            // We don't expect any other kind of error. The docs state:
            //
            //     If this transaction was created by an OptimisticTransactionDB(),
            //     Status::Busy() may be returned if the transaction could not guarantee
            //     that there are no write conflicts.  Status::TryAgain() may be returned
            //     if the memtable history size is not large enough
            //      (See max_write_buffer_size_to_maintain).
            //
            // However we never run transactions concurrently. So we should never get busy.
            //
            // This is just a way to throw the right exception.
            ROCKS_DB_CHECKED(status);
        }
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
            LOG_DEBUG(_env, "txn queue empty, returning 0");
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
        LOG_DEBUG(_env, "popped txn %s from queue", first);
        return first;
    }

    // Moves the state forward, filling in `step` appropriatedly, and writing
    // out the updated state.
    //
    // This will _not_ start a new transaction automatically if the old one
    // finishes. It does return whether the txn was finished though.
    template<template<typename> typename V>
    bool _advance(
        EggsTime time,
        rocksdb::Transaction& dbTxn,
        uint64_t txnId,
        const CDCReqContainer& req,
        // If `shardRespError` and `shardResp` are null, we're starting to execute.
        // Otherwise, (err == NO_ERROR) == (req != nullptr).
        EggsError shardRespError,
        const ShardRespContainer* shardResp,
        V<TxnState>& state,
        CDCStep& step
    ) {
        LOG_DEBUG(_env, "advancing txn %s of kind %s", txnId, req.kind());
        StateMachineEnv sm(_env, _db, _defaultCf, _parentCf, dbTxn, time, txnId, state().step(), step);
        switch (req.kind()) {
        case CDCMessageKind::MAKE_DIRECTORY:
            MakeDirectoryStateMachine(sm, req.getMakeDirectory(), state().getMakeDirectory()).resume(shardRespError, shardResp);
            break;
        case CDCMessageKind::HARD_UNLINK_DIRECTORY:
            HardUnlinkDirectoryStateMachine(sm, req.getHardUnlinkDirectory(), state().getHardUnlinkDirectory()).resume(shardRespError, shardResp);
            break;
        case CDCMessageKind::RENAME_FILE:
            RenameFileStateMachine(sm, req.getRenameFile(), state().getRenameFile()).resume(shardRespError, shardResp);
            break;
        case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
            SoftUnlinkDirectoryStateMachine(sm, req.getSoftUnlinkDirectory(), state().getSoftUnlinkDirectory()).resume(shardRespError, shardResp);
            break;
        case CDCMessageKind::RENAME_DIRECTORY:
            RenameDirectoryStateMachine(sm, req.getRenameDirectory(), state().getRenameDirectory()).resume(shardRespError, shardResp);
            break;
        case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
            CrossShardHardUnlinkFileStateMachine(sm, req.getCrossShardHardUnlinkFile(), state().getCrossShardHardUnlinkFile()).resume(shardRespError, shardResp);
            break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", req.kind());
        }
        state().setStep(sm.txnStep);

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
            return true;
        } else {
            // we're _not_ done, write out the state
            ROCKS_DB_CHECKED(dbTxn.Put(_defaultCf, cdcMetadataKey(&EXECUTING_TXN_STATE_KEY), state.toSlice()));
            return false;
        }
    }

    // Starts executing the next transaction in line, if possible. If it managed
    // to start something, it immediately advances it as well (no point delaying
    // that). Returns the txn id that was started, if any.
    uint64_t _startExecuting(EggsTime time, rocksdb::Transaction& dbTxn, CDCStep& step, CDCStatus& status) {
        uint64_t executingTxn = _executingTxn(dbTxn);
        if (executingTxn != 0) {
            LOG_DEBUG(_env, "another transaction %s is already executing, can't start", executingTxn);
            return 0;
        }

        uint64_t txnToExecute = _reqQueuePeek(dbTxn, _cdcReq);
        if (txnToExecute == 0) {
            LOG_DEBUG(_env, "no transactions in queue found, can't start");
            return 0;
        }

        _setExecutingTxn(dbTxn, txnToExecute);
        LOG_DEBUG(_env, "starting to execute txn %s with req %s", txnToExecute, _cdcReq);
        StaticValue<TxnState> txnState;
        txnState().start(_cdcReq.kind());
        bool stillRunning = _advance(time, dbTxn, txnToExecute, _cdcReq, NO_ERROR, nullptr, txnState, step);

        if (stillRunning) {
            status.runningTxn = txnToExecute;
            status.runningTxnKind = _cdcReq.kind();
        }

        return txnToExecute;
    }

    uint64_t processCDCReq(
        bool sync,
        EggsTime time,
        uint64_t logIndex,
        const CDCReqContainer& req,
        CDCStep& step,
        CDCStatus& status
    ) {
        status.reset();
    
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        std::unique_ptr<rocksdb::Transaction> dbTxn(_db->BeginTransaction(options));

        step.clear();

        _advanceLastAppliedLogEntry(*dbTxn, logIndex);

        // Enqueue req
        uint64_t txnId;
        {
            // Generate new txn id
            txnId = _lastTxn(*dbTxn) + 1;
            _setLastTxn(*dbTxn, txnId);
            // Push to queue
            _reqQueuePush(*dbTxn, txnId, req);
            LOG_DEBUG(_env, "enqueued CDC req %s with txn id %s", req.kind(), txnId);
        }

        // Start executing, if we can
        _startExecuting(time, *dbTxn, step, status);

        LOG_DEBUG(_env, "committing transaction");
        commitTransaction(*dbTxn);

        return txnId;
    }

    void processShardResp(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        EggsError respError,
        const ShardRespContainer* resp,
        CDCStep& step,
        CDCStatus& status
    ) {
        status.reset();
    
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        std::unique_ptr<rocksdb::Transaction> dbTxn(_db->BeginTransaction(options));

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
        bool stillRunning = _advance(time, *dbTxn, txnId, _cdcReq, respError, resp, txnState, step);

        if (stillRunning) {
            // Store status
            status.runningTxn = txnId;
            status.runningTxnKind = _cdcReq.kind();
        }

        commitTransaction(*dbTxn);
    }

    void startNextTransaction(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        CDCStep& step,
        CDCStatus& status
    ) {
        status.reset();
    
        auto locked = _processLock.lock();

        rocksdb::WriteOptions options;
        options.sync = sync;
        std::unique_ptr<rocksdb::Transaction> dbTxn(_db->BeginTransaction(options));

        step.clear();

        _advanceLastAppliedLogEntry(*dbTxn, logIndex);
        uint64_t txnId = _startExecuting(time, *dbTxn, step, status);
        if (txnId == 0) {
            // no txn could be started, see if one is executing already to fill in the `step`
            txnId = _executingTxn(*dbTxn);
            if (txnId != 0) {
                LOG_DEBUG(_env, "transaction %s is already executing, will resume it", txnId);
                // Get the req
                {
                    auto k = U64Key::Static(txnId);
                    std::string reqV;
                    ROCKS_DB_CHECKED(dbTxn->Get({}, _reqQueueCf, k.toSlice(), &reqV));
                    UnpackCDCReq ureq(_cdcReq);
                    bincodeFromRocksValue(reqV, ureq);
                }
                // Store status
                status.runningTxn = txnId;
                status.runningTxnKind = _cdcReq.kind();
                // Get the state
                std::string txnStateV;
                ROCKS_DB_CHECKED(dbTxn->Get({}, _defaultCf, cdcMetadataKey(&EXECUTING_TXN_STATE_KEY), &txnStateV));
                ExternalValue<TxnState> txnState(txnStateV);
                // Advance
                _advance(time, *dbTxn, txnId, _cdcReq, NO_ERROR, nullptr, txnState, step);
            }
        }
         
        commitTransaction(*dbTxn);
    }
};

CDCDB::CDCDB(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& path) {
    _impl = new CDCDBImpl(logger, xmon, path);
}

void CDCDB::close() {
    ((CDCDBImpl*)_impl)->close();
}

CDCDB::~CDCDB() {
    delete ((CDCDBImpl*)_impl);
}

uint64_t CDCDB::processCDCReq(bool sync, EggsTime time, uint64_t logIndex, const CDCReqContainer& req, CDCStep& step, CDCStatus& status) {
    return ((CDCDBImpl*)_impl)->processCDCReq(sync, time, logIndex, req, step, status);
}

void CDCDB::processShardResp(bool sync, EggsTime time, uint64_t logIndex, EggsError respError, const ShardRespContainer* resp, CDCStep& step, CDCStatus& status) {
    return ((CDCDBImpl*)_impl)->processShardResp(sync, time, logIndex, respError, resp, step, status);
}

void CDCDB::startNextTransaction(bool sync, EggsTime time, uint64_t logIndex, CDCStep& step, CDCStatus& status) {
    return ((CDCDBImpl*)_impl)->startNextTransaction(sync, time, logIndex, step, status);
}

uint64_t CDCDB::lastAppliedLogEntry() {
    return ((CDCDBImpl*)_impl)->_lastAppliedLogEntry();
}
