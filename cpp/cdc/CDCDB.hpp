#pragma once

#include <unordered_map>

#include "Bincode.hpp"
#include "Msgs.hpp"
#include "Env.hpp"
#include "Shuckle.hpp"

// This exists purely for type safety
struct CDCTxnId {
    uint64_t x;

    CDCTxnId() : x(0) {} // txn ids are never zeros, use it as a null value
    CDCTxnId(uint64_t x_) : x(x_) {}

    bool operator==(const CDCTxnId rhs) const {
        return x == rhs.x;
    }

    bool operator!=(const CDCTxnId rhs) const {
        return x != rhs.x;
    }
};

std::ostream& operator<<(std::ostream& out, CDCTxnId id);

template <>
struct std::hash<CDCTxnId> {
    std::size_t operator()(const CDCTxnId key) const {
        return std::hash<uint64_t>{}(key.x);
    }
};

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

struct CDCFinished {
    // If err is not NO_ERROR, resp is not filled in
    EggsError err;
    CDCRespContainer resp;
};

std::ostream& operator<<(std::ostream& out, const CDCFinished& x);

struct CDCStep {
    std::vector<std::pair<CDCTxnId, CDCFinished>> finishedTxns; // txns which have finished
    std::vector<std::pair<CDCTxnId, CDCShardReq>> runningTxns;  // txns which need a new shard request

    void clear() {
        finishedTxns.clear();
        runningTxns.clear();
    }
};

std::ostream& operator<<(std::ostream& out, const CDCStep& x);

struct CDCShardResp {
    CDCTxnId txnId; // the transaction id we're getting a response for
    // if err != NO_ERROR, resp is not filled in
    EggsError err;
    ShardRespContainer resp;
};

std::ostream& operator<<(std::ostream& out, const CDCShardResp& x);

struct CDCDB {
private:
    void* _impl;

public:
    CDCDB() = delete;
    CDCDB& operator=(const CDCDB&) = delete;

    CDCDB(Logger& env, std::shared_ptr<XmonAgent>& xmon, const std::string& path);
    ~CDCDB();

    // Unlike with ShardDB, we don't have an explicit log preparation step here,
    // because at least for now logs are simply either CDC requests, or shard
    // responses.
    //
    // The functions below cannot be called concurrently.

    // Gives you what to do when the CDC is started back up. It'll basically just
    // tell you to send some requests to shards. You need to run this when starting
    // up before proceeding.
    void bootstrap(
        bool sync,
        uint64_t logIndex,
        CDCStep& step
    );

    // Enqueues some CDC requests, and immediately starts it if possible.
    // Returns the txn id that was assigned to each request.
    //
    // Also, advances the CDC state using some shard responses.
    //
    // This function crashes hard if the caller passes it a response it's not
    // expecting. So the caller should track responses and make sure only relevant
    // ones are passed in.
    void update(
        bool sync,
        uint64_t logIndex,
        const std::vector<CDCReqContainer>& cdcReqs,
        const std::vector<CDCShardResp>& shardResps,
        CDCStep& step,
        // Output txn ids for all the requests, same length as `cdcReqs`.
        std::vector<CDCTxnId>& cdcReqsTxnIds
    );

    // The index of the last log entry persisted to the DB
    uint64_t lastAppliedLogEntry();

    void rocksDBMetrics(std::unordered_map<std::string, uint64_t>& values);
    void dumpRocksDBStatistics();
};