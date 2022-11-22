#pragma once

#include "Common.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
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

struct ShardLogEntryWithIndex {
    uint64_t index;
    ShardLogEntry entry;

    void clear() {
        index = 0;
        entry.clear();
    }
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
    void close();
    ~ShardDB();

    // In the functions below `scratch` is used to write BincodeBytes that the response/logEntry
    // needs.

    // Performs a read-only request, responding immediately. If an error is returned,
    // the contents of `resp` should be ignored.
    EggsError read(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardRespContainer& resp);

    // Prepares and persists a log entry to be applied. Morally this always succeeds,
    // the error checking is all done at application time. The reasoning here is that
    // log preparation is not reading from the latest state anyway, since we write the
    // log before we apply to the state (because we might do distributed consensus in
    // the meantime), and so we must do the error checking when writing. We are still
    // allowed to read from the state at log preparation time
    // (e.g. to gather needed info about block services), but knowing that we might
    // lag a bit behind.
    //
    // However we still reserve the right to return errors for some corner cases (e.g.
    // if we cannot allocate block services for some span request or something like
    // that). We might also reject requests that are clearly wrong (e.g. bad
    // inode id type or "type error" of that sorts, transient files cookies).
    //
    // This function will fail if called concurrently -- we rely on having a single
    // writer.
    //
    // The log entry is written atomically -- you can never witness a half-written
    // log entry.
    //
    // The log entry is returned so that the caller can implement distributed consensus
    // at some point (also with the help of this class, since some raft state updates
    // will have to be atomic).
    EggsError prepareLogEntry(const ShardReqContainer& req, BincodeBytesScratchpad& scratch, ShardLogEntryWithIndex& logEntry);

    // Applies the log entry at the given index, and fills in the client response.
    // The log entry index should be the next in line to be applied to the state.
    //
    // This function will fail if called concurrently -- like `prepareLogEntry` we
    // rely on having a single writer.
    //
    // The state is written atomically (you can never witness an half-applied log entry).
    EggsError applyLogEntry(uint64_t logEntryIx, BincodeBytesScratchpad& scratch, ShardRespContainer& resp);

    // Miscellanea
    const std::array<uint8_t, 16>& secretKey() const;
};
