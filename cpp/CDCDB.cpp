#include <rocksdb/db.h>

#include "Assert.hpp"
#include "CDCDB.hpp"
#include "AssertiveLock.hpp"
// #include "CDCDBData.hpp"
#include "Env.hpp"
#include "Exception.hpp"
#include "RocksDBUtils.hpp"

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
// For now, we just store a queue, where the head of the queue also contains
// the state of the curren tx in its value, after the client request.
//
// The queue is just indexed by a uint64_t transaction id, which monotonically
// increases. We store this txn id in the database so that we can keep generating
// unique ones even when the queue is empty. We also need to remember it so that
// we can just read/insert without seeking (see
// <https://github.com/facebook/rocksdb/wiki/Implement-Queue-Service-Using-RocksDB>).
//
// Note that not persisting the txn ids might mean that they get reset when the
// queue is empty and the CDC is restarted, but this should be fine since these
// txn ids are opaque to the rest of the system.
//
// So we have one CF which is just a queue with this schema:
//
// * txnid -> cdc req
//
// Then we have another CF with some metadata, specifically:
//
// * executingTxn -> txn id (0 if none)
// * lastQueuedTxn -> txn id
// * executingTxnState -> txn state
//
// The txn state would look something like this:
//
// * uint8_t request type
// * uint8_t step
// * char[4] destIp // where to send the response to
// * uin16_t destPort
// * uint64_t shard request id we're waiting on
// * ... additional state depending on the request type ...
//
// Note how we're always in the process of waiting for some shard request id
// we're waiting on.
//
// Finally, we store another CF with directory child -> directory parent
// cache, which is updated in directory moves operations. We never delete
// data from here for now, since even if we stored everything in it it
// wouldn't be very big anyway (~100GiB, assuming an average of 100 files
// per directory).
//
// With regards to log entries, we essentially have two sort of updates: a new
// cdc request arrives, and a shard response arrives which we need to advance
// the current transaction.

/*
void CDCLogEntry::pack(BincodeBuf& buf) const {
    time.pack(buf);
    buf.packScalar<uint16_t>((uint16_t)body.kind());
    body.pack(buf);
}

void CDCLogEntry::unpack(BincodeBuf& buf) {
    time.unpack(buf);
    CDCLogEntryKind kind = (CDCLogEntryKind)buf.unpackScalar<uint16_t>();
    body.unpack(buf, kind);
}

struct CDCDBImpl {
    Env& _env;

    rocksdb::DB* _db;
    rocksdb::ColumnFamilyHandle* _defaultCf;
    rocksdb::ColumnFamilyHandle* _queueCf;

    AssertiveLock _prepareEntryLock;

    uint64_t _txnFirst; // Currently executed txn. If 0, queue is empty (we start from one)
    uint64_t _txnLast;  // end of the txn queue. Again if 0, queue is empty.

    // ----------------------------------------------------------------
    // initialization

    CDCDBImpl() = delete;

    CDCDBImpl(Env& env, const std::string& path) : _env(env) {
        rocksdb::Options options;
        options.create_if_missing = true;
        options.create_missing_column_families = true;
        std::vector<rocksdb::ColumnFamilyDescriptor> familiesDescriptors{
            {rocksdb::kDefaultColumnFamilyName, {}},
            {"queue", {}},
        };
        std::vector<rocksdb::ColumnFamilyHandle*> familiesHandles;
        auto dbPath = path + "/db";
        LOG_INFO(_env, "initializing CDC RocksDB in %s", dbPath);
        ROCKS_DB_CHECKED_MSG(
            rocksdb::DB::Open(options, dbPath, familiesDescriptors, &familiesHandles, &_db),
            "could not open RocksDB %s", dbPath
        );
        ALWAYS_ASSERT(familiesDescriptors.size() == familiesHandles.size());
        _defaultCf = familiesHandles[0];
        _queueCf = familiesHandles[1];
        
        _initDb();
    }

    void close() {
        LOG_INFO(_env, "destroying column families and closing database");

        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_defaultCf));
        ROCKS_DB_CHECKED(_db->DestroyColumnFamilyHandle(_queueCf));

        ROCKS_DB_CHECKED(_db->Close());
    }

    ~CDCDBImpl() {
        delete _db;
    }

    void _initDb() {
        WrappedIterator it(_db->NewIterator({}, _queueCf));
        it->SeekToFirst();
        if (!it->Valid()) { // empty queue
            ROCKS_DB_CHECKED(it->status());
            LOG_INFO(_env, "Initializing CDC db with empty queue");
            _txnFirst = 0;
            _txnLast = 0;
            return;
        }
        {
            TxnIdKey k(it->key());
            _txnFirst = k.txnId();
        }
        it->SeekToLast();
        ALWAYS_ASSERT(it->Valid());
        {
            TxnIdKey k(it->key());
            _txnLast = k.txnId();
        }
        LOG_INFO(_env, "Initializing CDC db, first txn %s, last txn %s", _txnFirst, _txnLast);
    }

    // ----------------------------------------------------------------
    // log preparation

    EggsError prepareLogEntryFromReq(const CDCReqContainer& req, BincodeBytesScratchpad& scratch, CDCLogEntry& logEntry) {
        LOG_DEBUG(_env, "preparing CDC log entry following request %s", req.kind());
        auto locked = _prepareEntryLock.lock();
        scratch.reset();
        logEntry.clear();
        EggsError err = NO_ERROR;

        rocksdb::WriteBatch batch;

        EggsTime time = eggsNow();
        logEntry.time = time;
        auto& logEntryBody = logEntry.body;

        switch (req.kind()) {
        default:
            throw EGGS_EXCEPTION("UNIMPLEMENTED %s", req.kind());
        }

        if (err == NO_ERROR) {
            LOG_INFO(_env, "prepared log entry of kind %s, for request of kind %s", logEntryBody.kind(), req.kind());
            // Here we must always sync, we can't lose updates to the next directory id (or similar)
            // which then might be persisted in the log but lost here.
            rocksdb::WriteOptions options;
            options.sync = true;
            ROCKS_DB_CHECKED(_db->Write(options, &batch));
        } else {
            LOG_INFO(_env, "could not prepare log entry for request of kind %s: %s", req.kind(), err);
        }

        return err;
    }
};
*/

CDCDB::CDCDB(Logger& logger, const std::string& path) {
    _impl = nullptr;
}
void CDCDB::close() {}
CDCDB::~CDCDB() {}

EggsError CDCDB::processCDCReq(
    EggsTime time,
    uint64_t logIndex,
    const CDCReqDestination& dest,
    const CDCReqContainer& req,
    BincodeBytesScratchpad& scratch,
    CDCStep& step
) {
    return EggsError::INTERNAL_ERROR;
}