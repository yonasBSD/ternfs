#pragma once

#include "Common.hpp"
#include "Msgs.hpp"
#include "Env.hpp"

struct ShardLogEntry {
    EggsTime time;
    ShardLogEntryContainer body;

    void clear() {
        time = 0;
        body.clear();
    }

    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
};

bool readOnlyShardReq(const ShardMessageKind kind);

BincodeBytes defaultDirectoryInfo(char (&buf)[255]);

struct ShardDB {
private:
    void* _impl;

public:
    ShardDB() = delete;

    // init/teardown
    ShardDB(Env& env, ShardId shid, const std::string& path);
    ~ShardDB();

    // Stuff which might throw, and therefore not well suited to destructor.
    void close();

    // In the functions below `scratch` is used to write BincodeBytes that the response/logEntry
    // needs.

    // Performs a read-only request, responding immediately. If an error is returned,
    // the contents of `resp` should be ignored.
    EggsError read(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardRespContainer& resp);

    // Prepares and persists a log entry to be applied.
    //
    // This function can be called concurrently. We only read from the database to
    // do this. Note that the reading is limited to data which is OK to be a bit
    // stale, such as block service information, and which is not used in a way
    // which relies on the log entries being successfully applied.
    //
    // The log entry is not persisted in any way -- the idea is that a consensus module
    // can be built on top of this to first persist the log entry across a consensus
    // of machines, and then the log entries can be applied.
    //
    // Morally this function always succeeds, the "real" error checking is all done at
    // log application time. The reasoning here is that log preparation is not reading
    // from the latest state anyway, since there might be many log entries in flight which
    // we have prepared but not applied, and therefore this function does not have the latest
    // view of the state. We are still allowed to read from the state at log preparation time
    // (e.g. to gather needed info about block services), but knowing that we might lag a bit
    // behind.
    //
    // However we still do some "type checking" here (e.g. we have ids in the right
    // shard and of the right type, good transient file cookies), and we might still
    // return errors for some corner cases (e.g. if we cannot allocate block services
    // for some span request or something like that).
    //
    // As usual, if an error is returned, the contents of `logEntry` should be ignored.
    EggsError prepareLogEntry(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardLogEntry& logEntry);

    // The index of the last log entry persisted to the 
    uint64_t lastAppliedLogEntry();

    // Applies the log entry at the given index, and fills in the client response.
    // The log entry index should be the next in line to be applied to the state --
    // `lastAppliedLogEntry() + 1`.
    //
    // It is the job of the caller to keep track of indices, this state machine  only
    // remembers which index it last applied.
    //
    // This function will fail if called concurrently -- like `prepareLogEntry` we
    // rely on having a single writer, and here it's actually a lot more important.
    //
    // The state is written atomically (you can never witness an half-applied log entry).
    //
    // The `sync` parameter determines whether `fsync` is called when persisting the
    // state. If the log entry is persisted before `applyLogEntry` is applied, this might
    // not be necessary. If it is not, then it is certainly necessary.
    //
    // As usual, if an error is returned, the contents of `resp` should be ignored.
    EggsError applyLogEntry(bool sync, uint64_t logEntryIx, const ShardLogEntry& logEntry, BincodeBytesScratchpad& scratch, ShardRespContainer& resp);

    // For internal testing
    const std::array<uint8_t, 16>& secretKey() const;
};
