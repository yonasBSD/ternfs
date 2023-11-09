#pragma once

#include "unordered_map"

#include "Bincode.hpp"
#include "Msgs.hpp"
#include "Env.hpp"
#include "Shuckle.hpp"

struct CDCShardReq {
    ShardId shid;
    bool repeated; // This request is exactly the same as the previous one.
    ShardReqContainer req;

    void clear() {
        shid = ShardId(0);
        repeated = false;
        req.clear();
    }
};

std::ostream& operator<<(std::ostream& out, const CDCShardReq& x);

struct CDCStep {
    // If non-zero, a transaction has just finished, and here we have
    // the response (whether an error or a response).
    uint64_t txnFinished;
    EggsError err; // if NO_ERROR, resp is contains the response.
    CDCRespContainer resp;

    // If non-zero, a transaction is running, but we need something
    // from a shard to have it proceed.
    //
    // We have !((finishedTxn != 0) && (txnNeedsShard != 0)) as an invariant
    // -- we can't have finished and be running a thing in the same step.
    uint64_t txnNeedsShard;
    CDCShardReq shardReq;

    // If non-zero, there is a transaction after the current one waiting
    // to be executed. Only filled in if `txnNeedsShard == 0`.
    // Useful to decide when to call `startNextTransaction` (although
    // calling it is safe in any case).
    uint64_t nextTxn;

    void clear() {
        txnFinished = 0;
        txnNeedsShard = 0;
        nextTxn = 0;
    }
};

// Only used for timing purposes, there is some overlap with CDCStep,
// but we separate it out for code robustness (we just get this info
// every time we touch the db).
struct CDCStatus {
    uint64_t runningTxn; // 0 if nothing
    CDCMessageKind runningTxnKind; // only relevant if it's running

    void reset() {
        runningTxn = 0;
        runningTxnKind = (CDCMessageKind)0;
    }
};

std::ostream& operator<<(std::ostream& out, const CDCStep& x);

struct CDCDB {
private:
    void* _impl;

public:
    CDCDB() = delete;
    CDCDB& operator=(const CDCDB&) = delete;

    CDCDB(Logger& env, std::shared_ptr<XmonAgent>& xmon, const std::string& path);
    ~CDCDB();
    void close();

    // Unlike with ShardDB, we don't have an explicit log preparation step here,
    // because at least for now logs are simply either CDC requests, or shard
    // responses.
    //
    // The functions below cannot be called concurrently.
    //
    // TODO one thing that we'd like to do (outside the deterministic state
    // machine) is apply backpressure when too many txns are enqueued. It's
    // not good to do it inside the state machine, because then we'd be marrying
    // the deterministic state update function to such heuristics, which seems
    // imprudent. So we'd like some function returning the length of the queue.

    // Enqueues a cdc request, and immediately starts it if the system is currently
    // idle. Returns the txn id that got assigned to the txn.
    uint64_t processCDCReq(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        const CDCReqContainer& req,
        CDCStep& step,
        CDCStatus& status
    );

    // Advances the CDC state using the given shard response.
    //
    // This function crashes hard if the caller passes it a response it's not expecting. So the caller
    // should track responses and make sure only the correct one is passed in.
    void processShardResp(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        // (err == NO_ERROR) == (req != nullptr)
        EggsError err,
        const ShardRespContainer* req,
        CDCStep& step,
        CDCStatus& status
    );

    // Does what it can to advance the state of the system, by starting the next
    // transaction in line (if any). In any case, it returns what there is to do next
    // in `step`.
    //
    // It's fine to call this function even if there's nothing to do -- and in fact
    // you should do that when starting up the CDC, to make sure to finish
    // in-flight CDC transactions.
    void startNextTransaction(
        bool sync, // Whether to persist synchronously. Unneeded if log entries are persisted already.
        EggsTime time,
        uint64_t logIndex,
        CDCStep& step,
        CDCStatus& status
    );

    // The index of the last log entry persisted to the DB
    uint64_t lastAppliedLogEntry();

    void rocksDBMetrics(std::unordered_map<std::string, uint64_t>& values);
};