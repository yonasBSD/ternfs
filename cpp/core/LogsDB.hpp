#pragma once

#include <ostream>
#include <vector>
#include <rocksdb/db.h>

#include "Env.hpp"
#include "Msgs.hpp"
#include "SharedRocksDB.hpp"
#include "Time.hpp"

// ** Releases **
// Released records are records which have been confirmed by the leader to have been at some point correctly replicated.
// Leader only confirms record with LogIdx X if all records up to and including X have been correctly replicated.
// This guarantee simplifies the message structure as we can move release point with a single message.
// Releases are not required from correctness perspective but they improve performance as we have a guarantee that any record
// before the release point can be read without having a quorum of replicas. It also allows followers to apply release records
// to a state machine wihout first checking if the record has been correctly replicated.
// It also simplifies cleanup during leader election. Imagine following scenario:
// Leader is replica 0, R indicates that a record is replicated to specific replica X that it's not
// Leader replication window
// ReplicaId   LogIdx(2) LogIdx(3) LogIdx(4)
//         0          R         R         R
//         1          X         R         X
//         2          R         X         R
//         3          R         X         R
//         4          R         X         R
// Replicas (0,1) suddenly go away
// Replicas (2,3,4) elect 2 as new leader
// During "recovery" (a process which cleans up after previous leader from LastReleasedIdx)
// They don't see Record with LogIdx(3). Because they got a majority of nodes saying there is no record
// They know it couldn't have been correctly replicated. It is safe to drop it and all records after it.
// Because we don't want holes or conflicts we rewind logIndex to 3 and next appended record will come with 3
// What happens when replicas (0,1) come back. They will receive a movement of release point to arbitraty point in the future
// However they will, based on the leader token, notice it comes from a different leader.
// Since they were not part of the leader election they know their records after last released point have not been taken into
// account and could have been overwriten. They at this point drop these records and catch up from lastReleased point.


struct LogsDBLogEntry {
    LogIdx idx;
    std::vector<uint8_t> value;
    bool operator==(const LogsDBLogEntry& oth) const {
        return idx == oth.idx && value == oth.value;
    }
};

std::ostream& operator<<(std::ostream& out, const LogsDBLogEntry& entry);

struct LogsDBRequest {
    ReplicaId replicaId;
    EggsTime sentTime;
    LogRequestHeader header;
    LogReqContainer requestContainer;
};

std::ostream& operator<<(std::ostream& out, const LogsDBRequest& entry);

struct LogsDBResponse {
    ReplicaId replicaId;
    LogResponseHeader header;
    LogRespContainer responseContainer;
};

std::ostream& operator<<(std::ostream& out, const LogsDBResponse& entry);

class LogsDBImpl;

class LogsDB {
public:
    static constexpr size_t REPLICA_COUNT = 5;
    static constexpr Duration PARTITION_TIME_SPAN = 12_hours;
    static constexpr Duration RESPONSE_TIMEOUT = 10_ms;
    static constexpr Duration SEND_RELEASE_INTERVAL = 300_ms;
    static constexpr Duration LEADER_INACTIVE_TIMEOUT = 1_sec;
    static constexpr size_t IN_FLIGHT_APPEND_WINDOW = 1 << 8;
    static constexpr size_t CATCHUP_WINDOW = 1 << 4 ;

    LogsDB() = delete;

    // On start we verify last released data is less than 1.5 * PARTITION_TIME_SPAN old to guarantee we can catchup.
    // If initialStart is set to true we skip the checks. In this case user is responsible to have their
    // own state sufficiently up to date to be able to catch up
    LogsDB(
        Env& env,
        SharedRocksDB& sharedDB,
        ReplicaId replicaId,
        LogIdx lastRead,
        bool dontWaitForReplication,
        bool dontDoReplication,
        bool forceLeader,
        bool avoidBeingLeader,
        bool initialStart,
        LogIdx forcedLastReleased);

    ~LogsDB();

    void close();

    void flush(bool sync);

    void processIncomingMessages(std::vector<LogsDBRequest>& requests, std::vector<LogsDBResponse>& responses);
    void getOutgoingMessages(std::vector<LogsDBRequest*>& requests, std::vector<LogsDBResponse>& responses);

    bool isLeader() const;

    EggsError appendEntries(std::vector<LogsDBLogEntry>& entries);

    void readEntries(std::vector<LogsDBLogEntry>& entries);

    Duration getNextTimeout() const;

    static std::vector<rocksdb::ColumnFamilyDescriptor> getColumnFamilyDescriptors();
    static void clearAllData(SharedRocksDB& shardDB);
private:
    LogsDBImpl* _impl;
};
