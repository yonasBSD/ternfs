#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "CDC.hpp"
#include "CDCDB.hpp"
#include "CDCKey.hpp"
#include "Crypto.hpp"
#include "Env.hpp"
#include "ErrorCount.hpp"
#include "Exception.hpp"
#include "LogsDB.hpp"
#include "Loop.hpp"
#include "MultiplexedChannel.hpp"
#include "PeriodicLoop.hpp"
#include "Protocol.hpp"
#include "SharedRocksDB.hpp"
#include "Shuckle.hpp"
#include "Time.hpp"
#include "Timings.hpp"
#include "UDPSocketPair.hpp"
#include "wyhash.h"
#include "Xmon.hpp"
#include "XmonAgent.hpp"

static constexpr uint8_t CDC_SOCK = 0;
static constexpr uint8_t SHARD_SOCK = 1;

struct CDCShared {
    SharedRocksDB& sharedDb;
    CDCDB& db;
    LogsDB& logsDB;
    std::array<UDPSocketPair, 2> socks;
    std::atomic<bool> isLeader;
    std::shared_ptr<std::array<AddrsInfo, LogsDB::REPLICA_COUNT>> replicas;
    std::mutex shardsMutex;
    std::array<ShardInfo, 256> shards;
    // How long it took us to process the entire request, from parse to response.
    std::array<Timings, maxCDCMessageKind+1> timingsTotal;
    std::array<ErrorCount, maxCDCMessageKind+1> errors;
    std::atomic<double> inFlightTxns;
    std::atomic<double> updateSize;
    ErrorCount shardErrors;

    CDCShared(SharedRocksDB& sharedDb_, CDCDB& db_, LogsDB& logsDB_, std::array<UDPSocketPair, 2>&& socks_) : sharedDb(sharedDb_), db(db_), logsDB(logsDB_), socks(std::move(socks_)), isLeader(false), inFlightTxns(0), updateSize(0) {
        for (CDCMessageKind kind : allCDCMessageKind) {
            timingsTotal[(int)kind] = Timings::Standard();
        }
    }
};

struct InFlightShardRequest {
    CDCTxnId txnId; // the txn id that requested this shard request
    EggsTime sentAt;
    ShardId shid;
};

struct InFlightCDCRequest {
    bool hasClient;
    uint64_t lastSentRequestId;
    // if hasClient=false, the following is all garbage.
    uint64_t cdcRequestId;
    EggsTime receivedAt;
    IpPort clientAddr;
    CDCMessageKind kind;
    int sockIx;
};

// these can happen through normal user interaction
//
// MISMATCHING_CREATION_TIME can happen if we generate a timeout
// in CDC.cpp, but the edge was actually created, and when we
// try to recreate it we get a bad creation time.
static bool innocuousShardError(EggsError err) {
    return err == EggsError::NAME_NOT_FOUND || err == EggsError::EDGE_NOT_FOUND || err == EggsError::DIRECTORY_NOT_EMPTY || err == EggsError::MISMATCHING_CREATION_TIME;
}

// These can happen but should be rare.
//
// DIRECTORY_HAS_OWNER can happen in gc (we clean it up and then remove
// it, but somebody else might have created stuff in it in the meantime)
static bool rareInnocuousShardError(EggsError err) {
    return err == EggsError::DIRECTORY_HAS_OWNER;
}

struct InFlightCDCRequestKey {
    uint64_t requestId;
    uint64_t portIp;

    InFlightCDCRequestKey(uint64_t requestId_, IpPort clientAddr) :
        requestId(requestId_)
    {
        sockaddr_in addr;
        clientAddr.toSockAddrIn(addr);
        portIp = ((uint64_t) addr.sin_port << 32) | ((uint64_t) addr.sin_addr.s_addr);
    }

    bool operator==(const InFlightCDCRequestKey& other) const {
        return requestId == other.requestId && portIp == other.portIp;
    }
};

template <>
struct std::hash<InFlightCDCRequestKey> {
    std::size_t operator()(const InFlightCDCRequestKey& key) const {
        return std::hash<uint64_t>{}(key.requestId ^ key.portIp);
    }
};

struct InFlightShardRequests {
private:
    using RequestsMap = std::unordered_map<uint64_t, InFlightShardRequest>;
    RequestsMap _reqs;

    std::map<EggsTime, uint64_t> _pq;

public:

    struct TimeIterator {
        RequestsMap::const_iterator operator->() const {
            return _reqs.find(_it->second);
        }
        TimeIterator& operator++() {
            ++_it;
            return *this;
        }

        TimeIterator(const RequestsMap& reqs, const std::map<EggsTime, uint64_t>& pq) : _reqs(reqs), _pq(pq), _it(_pq.begin()) {}
        bool operator==(const TimeIterator& other) const {
            return _it == other._it;
        }
        bool operator!=(const TimeIterator& other) const {
            return!(*this == other);
        }
        TimeIterator end() const {
            return TimeIterator{_reqs, _pq, _pq.end()};
        }
    private:
        TimeIterator(const RequestsMap& reqs, const std::map<EggsTime, uint64_t>& pq, std::map<EggsTime, uint64_t>::const_iterator it) : _reqs(reqs), _pq(pq), _it(it) {}
        const RequestsMap& _reqs;
        const std::map<EggsTime, uint64_t>& _pq;
        std::map<EggsTime, uint64_t>::const_iterator _it;
    };
    size_t size() const {
        return _reqs.size();
    }

    TimeIterator oldest() const {
        return TimeIterator(_reqs, _pq);
    }

    RequestsMap::const_iterator find(uint64_t reqId) const {
        return _reqs.find(reqId);
    }

    RequestsMap::const_iterator end() {
        return _reqs.end();
    }

    void erase(RequestsMap::const_iterator iterator) {
        _pq.erase(iterator->second.sentAt);
        _reqs.erase(iterator);
    }

    void insert(uint64_t reqId, const InFlightShardRequest& req) {
        auto [reqIt, inserted] = _reqs.insert({reqId, req});

        // TODO i think we can just assert inserted, we never need this
        // functionality

        if (inserted) {
            // we have never seen this shard request.
            // technically we could get the same time twice, but in practice
            // we won't, so just assert it.
            ALWAYS_ASSERT(_pq.insert({req.sentAt, reqId}).second);
        } else {
            // we had already seen this. make sure it's for the same stuff, and update pq.
            ALWAYS_ASSERT(reqIt->second.txnId == req.txnId);
            ALWAYS_ASSERT(reqIt->second.shid == req.shid);
            ALWAYS_ASSERT(_pq.erase(reqIt->second.sentAt) == 1);   // must be already present
            ALWAYS_ASSERT(_pq.insert({req.sentAt, reqId}).second); // insert with new time
            reqIt->second.sentAt = req.sentAt; // update time in existing entry
        }
    }
};

struct CDCReqInfo {
    uint64_t reqId;
    IpPort clientAddr;
    EggsTime receivedAt;
    int sockIx;
};

constexpr int MAX_UPDATE_SIZE = 500;
constexpr uint64_t MAX_MSG_RECEIVE = (LogsDB::CATCHUP_WINDOW + LogsDB::IN_FLIGHT_APPEND_WINDOW) * LogsDB::REPLICA_COUNT + MAX_UPDATE_SIZE;

struct CDCServer : Loop {
private:
    CDCShared& _shared;
    const std::string _basePath;
    bool _seenShards;
    uint64_t _currentLogIndex;
    LogIdx _logsDBLogIndex;
    CDCStep _step;
    uint64_t _shardRequestIdCounter;
    AES128Key _expandedCDCKey;
    Duration _shardTimeout;

    // We receive everything at once, but we send stuff from
    // separate threads.
    UDPReceiver<2> _receiver;
    MultiplexedChannel<4, std::array<uint32_t, 4>{CDC_REQ_PROTOCOL_VERSION, SHARD_CHECK_POINTED_RESP_PROTOCOL_VERSION, LOG_REQ_PROTOCOL_VERSION, LOG_RESP_PROTOCOL_VERSION}> _channel;
    UDPSender _cdcSender;
    UDPSender _shardSender;

    // reqs data
    std::vector<CDCReqContainer> _cdcReqs;
    std::vector<CDCReqInfo> _cdcReqsInfo;
    std::vector<CDCTxnId> _cdcReqsTxnIds;
    std::vector<CDCShardResp> _shardResps;
    std::vector<uint64_t> _shardRespReqIds;
    std::unordered_map<uint64_t, std::vector<uint64_t>> _entryIdxToRespIds;
    std::unordered_set<uint64_t> _receivedResponses;
    std::unordered_map<uint64_t, CDCLogEntry> _inFlightEntries;

    // The requests we've enqueued, but haven't completed yet, with
    // where to send the response. Indexed by txn id.
    std::unordered_map<CDCTxnId, InFlightCDCRequest> _inFlightTxns;
    // The enqueued requests, but indexed by req id + ip + port. We
    // store this so that we can drop repeated requests which are
    // still queued, and which will therefore be processed in due
    // time anyway. This relies on clients having unique req ids. It's
    // kinda unsafe anyway (if clients get restarted), but it's such
    // a useful optimization for now that we live with it.
    std::unordered_set<InFlightCDCRequestKey> _inFlightCDCReqs;
    // The _shard_ request we're currently waiting for, if any.
    InFlightShardRequests _inFlightShardReqs;

    const bool _dontDoReplication;
    LogsDB& _logsDB;
    std::vector<LogsDBRequest> _logsDBRequests;
    std::vector<LogsDBResponse> _logsDBResponses;
    std::vector<LogsDBRequest *> _logsDBOutRequests;
    std::vector<LogsDBResponse> _logsDBOutResponses;
    std::unordered_map<uint64_t, CDCLogEntry> _inFlightLogEntries;
    std::unordered_map<uint64_t, std::vector<CDCReqInfo>> _logEntryIdxToReqInfos;
    std::shared_ptr<std::array<AddrsInfo, LogsDB::REPLICA_COUNT>> _replicas;
public:
    CDCServer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, CDCOptions& options, CDCShared& shared) :
        Loop(logger, xmon, "req_server"),
        _shared(shared),
        _basePath(options.dbDir),
        _seenShards(false),
        _currentLogIndex(_shared.db.lastAppliedLogEntry()),
        // important to not catch stray requests from previous executions
        _shardRequestIdCounter(wyhash64_rand()),
        _shardTimeout(options.shardTimeout),
        _receiver({.perSockMaxRecvMsg = MAX_MSG_RECEIVE, .maxMsgSize = MAX_UDP_MTU}),
        _cdcSender({.maxMsgSize = MAX_UDP_MTU}),
        _dontDoReplication(options.dontDoReplication),
        _logsDB(shared.logsDB)
    {
        expandKey(CDCKey, _expandedCDCKey);

        if (options.forceLeader) {
            // In case of force leader it immediately detects and promotes us to leader
            _logsDB.processIncomingMessages(_logsDBRequests, _logsDBResponses);
        }
        _shared.isLeader.store(_logsDB.isLeader(), std::memory_order_relaxed);
        _logsDBLogIndex = _logsDB.getLastReleased();
        LOG_INFO(_env, "Waiting for shard info to be filled in");
    }

    virtual void step() override {
        std::vector<LogsDBLogEntry> entries;
        if (unlikely(!_seenShards)) {
            if (!_waitForShards()) {
                return;
            }
            _seenShards = true;
            if (_shared.isLeader.load(std::memory_order_relaxed)) {
                // If we've got dangling transactions, immediately start processing it
                auto bootstrap = CDCLogEntry::prepareBootstrapEntry();
                auto& entry = entries.emplace_back();
                entry.value.resize(bootstrap.packedSize());
                BincodeBuf bbuf((char*) entry.value.data(), entry.value.size());
                bootstrap.pack(bbuf);
                ALWAYS_ASSERT(bbuf.len() == entry.value.size());
                bootstrap.logIdx(++_logsDBLogIndex.u64);
                _inFlightLogEntries[bootstrap.logIdx()] = std::move(bootstrap);
            }
        }

        // clear internal buffers
        _cdcReqs.clear();
        _cdcReqsInfo.clear();
        _cdcReqsTxnIds.clear();
        _shardResps.clear();
        _shardRespReqIds.clear();
        _receivedResponses.clear();
        _entryIdxToRespIds.clear();
        _replicas = _shared.replicas;


        // Timeout ShardRequests
        {
            auto now = eggsNow();
            auto oldest = _inFlightShardReqs.oldest();
            while (_updateSize() < MAX_UPDATE_SIZE && oldest != oldest.end()) {

                if ((now - oldest->second.sentAt) < _shardTimeout) { break; }

                LOG_DEBUG(_env, "in-flight shard request %s was sent at %s, it's now %s, will time out (%s > %s)", oldest->first, oldest->second.sentAt, now, (now - oldest->second.sentAt), _shardTimeout);
                uint64_t requestId = oldest->first;
                auto resp = _prepareCDCShardResp(requestId);
                ALWAYS_ASSERT(resp != nullptr); // must be there, we've just timed it out
                resp->checkPoint = 0;
                resp->resp.setError() = EggsError::TIMEOUT;
                _recordCDCShardResp(requestId, *resp);
                ++oldest;
            }
        }
        auto timeout = _logsDB.getNextTimeout();
        // we need to process bootstrap entry
        if (unlikely(entries.size())) {
            timeout = 0;
        }
        if (unlikely(!_channel.receiveMessages(_env,_shared.socks, _receiver, MAX_MSG_RECEIVE, timeout))) {
            return;
        };

        _processLogMessages();
        _processShardMessages();
        _processCDCMessages();

        _shared.updateSize = 0.95*_shared.updateSize + 0.05*_updateSize();

        if (_cdcReqs.size() > 0 || _shardResps.size() > 0) {
            ALWAYS_ASSERT(_shared.isLeader.load(std::memory_order_relaxed));
            std::vector<CDCLogEntry> entriesOut;
            CDCLogEntry::prepareLogEntries(_cdcReqs, _shardResps, LogsDB::DEFAULT_UDP_ENTRY_SIZE, entriesOut);

            auto reqInfoIt = _cdcReqsInfo.begin();
            auto respIdIt = _shardRespReqIds.begin();
            for (auto& entry : entriesOut) {
                auto& logEntry = entries.emplace_back();
                logEntry.value.resize(entry.packedSize());
                BincodeBuf bbuf((char*) logEntry.value.data(), logEntry.value.size());
                entry.pack(bbuf);
                ALWAYS_ASSERT(bbuf.len() == logEntry.value.size());
                entry.logIdx(++_logsDBLogIndex.u64);
                std::vector<CDCReqInfo> infos(reqInfoIt, reqInfoIt + entry.cdcReqs().size());
                std::vector<uint64_t> respIds(respIdIt, respIdIt + entry.shardResps().size());
                _logEntryIdxToReqInfos.emplace(entry.logIdx(), std::move(infos));
                _entryIdxToRespIds.emplace(entry.logIdx(), std::move(respIds));
                respIdIt += entry.shardResps().size();
                reqInfoIt += entry.cdcReqs().size();
                _inFlightLogEntries[entry.logIdx()] = std::move(entry);
            }
        }

        if (_logsDB.isLeader()) {
            auto err = _logsDB.appendEntries(entries);
            ALWAYS_ASSERT(err == EggsError::NO_ERROR);
            // we need to drop information about entries which might have been dropped due to append window being full
            bool foundLastInserted = false;
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                auto& entry = *it;
                if (entry.idx != 0) {
                    ALWAYS_ASSERT(foundLastInserted || entry.idx == _logsDBLogIndex);
                    foundLastInserted = true;
                    for (auto respId : _entryIdxToRespIds[entry.idx.u64]) {
                        auto it = _inFlightShardReqs.find(respId);
                        ALWAYS_ASSERT(it != _inFlightShardReqs.end());
                        _inFlightShardReqs.erase(it);
                    }
                } else {
                    ALWAYS_ASSERT(!foundLastInserted);
                    ALWAYS_ASSERT(_logEntryIdxToReqInfos.contains(_logsDBLogIndex.u64));
                    ALWAYS_ASSERT(_inFlightLogEntries.contains(_logsDBLogIndex.u64));
                    _logEntryIdxToReqInfos.erase(_logsDBLogIndex.u64);
                    _inFlightLogEntries.erase(_logsDBLogIndex.u64);
                    --_logsDBLogIndex.u64;
                }
            }
            entries.clear();
        }
        // Log if not active is not chaty but it's messages are higher priority as they make us progress state under high load.
        // We want to have priority when sending out
        _logsDB.getOutgoingMessages(_logsDBOutRequests, _logsDBOutResponses);

        for (auto& response : _logsDBOutResponses) {
            _packLogsDBResponse(response);
        }

        for (auto request : _logsDBOutRequests) {
            _packLogsDBRequest(*request);
        }
        if (_dontDoReplication) {
            _logsDB.processIncomingMessages(_logsDBRequests, _logsDBResponses);
        }
        _logsDB.readEntries(entries);

        // Apply replicated log entries
        for(auto& logEntry : entries) {
            ALWAYS_ASSERT(logEntry.idx == _advanceLogIndex());
            BincodeBuf bbuf((char*)logEntry.value.data(), logEntry.value.size());
            CDCLogEntry cdcEntry;
            cdcEntry.unpack(bbuf);
            cdcEntry.logIdx(logEntry.idx.u64);
            if (unlikely(_shared.isLeader.load(std::memory_order_relaxed) && cdcEntry != _inFlightLogEntries[cdcEntry.logIdx()])) {
                LOG_ERROR(_env, "Entry difference after deserialization cdcEntry(%s), original(%s)", std::move(cdcEntry), std::move(_inFlightLogEntries[cdcEntry.logIdx()]));
            }
            ALWAYS_ASSERT(!_shared.isLeader.load(std::memory_order_relaxed) || cdcEntry == _inFlightLogEntries[cdcEntry.logIdx()]);
            _inFlightLogEntries.erase(cdcEntry.logIdx());
            // process everything in a single batch
            _cdcReqsTxnIds.clear();
            _shared.db.applyLogEntry(true, cdcEntry, _step, _cdcReqsTxnIds);

            if (_shared.isLeader.load(std::memory_order_relaxed)) {
                // record txn ids etc. for newly received requests
                auto& cdcReqs = cdcEntry.cdcReqs();
                auto& reqInfos = _logEntryIdxToReqInfos[cdcEntry.logIdx()];
                ALWAYS_ASSERT(cdcReqs.size() == reqInfos.size());
                for (size_t i = 0; i < cdcReqs.size(); ++i) {
                    const auto& req = cdcReqs[i];
                    const auto& reqInfo = reqInfos[i];
                    CDCTxnId txnId = _cdcReqsTxnIds[i];
                    ALWAYS_ASSERT(_inFlightTxns.find(txnId) == _inFlightTxns.end());
                    auto& inFlight = _inFlightTxns[txnId];
                    inFlight.hasClient = true;
                    inFlight.cdcRequestId = reqInfo.reqId;
                    inFlight.clientAddr = reqInfo.clientAddr;
                    inFlight.kind = req.kind();
                    inFlight.receivedAt = reqInfo.receivedAt;
                    inFlight.sockIx = reqInfo.sockIx;
                    _updateInFlightTxns();
                    _inFlightCDCReqs.insert(InFlightCDCRequestKey(reqInfo.reqId, reqInfo.clientAddr));
                }
                _processStep();
                _logEntryIdxToReqInfos.erase(cdcEntry.logIdx());
            }
        }

        _logsDB.flush(true);

        _shardSender.sendMessages(_env, _shared.socks[SHARD_SOCK]);
        _cdcSender.sendMessages(_env, _shared.socks[CDC_SOCK]);
    }

private:
    void _packLogsDBResponse(LogsDBResponse& response) {
        auto addrInfoPtr = addressFromReplicaId(response.replicaId);
        if (unlikely(addrInfoPtr == nullptr)) {
            LOG_DEBUG(_env, "No information for replica id %s. dropping response", response.replicaId);
            return;
        }
        auto& addrInfo = *addrInfoPtr;

        _cdcSender.prepareOutgoingMessage(
            _env,
            _shared.socks[CDC_SOCK].addr(),
            addrInfo,
            [&response,this](BincodeBuf& buf) {
                response.msg.pack(buf, _expandedCDCKey);
            });

        LOG_DEBUG(_env, "will send response for req id %s kind %s to %s", response.msg.id, response.msg.body.kind(), addrInfo);
    }

    void _packLogsDBRequest(LogsDBRequest& request) {
        auto addrInfoPtr = addressFromReplicaId(request.replicaId);
        if (unlikely(addrInfoPtr == nullptr)) {
            LOG_DEBUG(_env, "No information for replica id %s. dropping request", request.replicaId);
            return;
        }
        auto& addrInfo = *addrInfoPtr;

        _cdcSender.prepareOutgoingMessage(
            _env,
            _shared.socks[CDC_SOCK].addr(),
            addrInfo,
            [&request,this](BincodeBuf& buf) {
                request.msg.pack(buf, _expandedCDCKey);
            });

        LOG_DEBUG(_env, "will send request for req id %s kind %s to %s", request.msg.id, request.msg.body.kind(), addrInfo);
    }
    void _updateInFlightTxns() {
        _shared.inFlightTxns = _shared.inFlightTxns*0.95 + ((double)_inFlightTxns.size())*0.05;
    }

    bool _waitForShards() {
        bool badShard = false;
        {
            const std::lock_guard<std::mutex> lock(_shared.shardsMutex);
            for (int i = 0; i < _shared.shards.size(); i++) {
                const auto sh = _shared.shards[i];
                if (sh.addrs[0].port == 0) {
                    LOG_DEBUG(_env, "Shard %s isn't ready yet", i);
                    badShard = true;
                    break;
                }
            }
        }
        if (badShard) {
            (100_ms).sleep();
            return false;
        }

        LOG_INFO(_env, "shards found, proceeding");
        return true;
    }

    // To be called when we have a shard response with given `reqId`.
    // Searches it in the in flight map, removes it from it, and
    // adds a CDCShardResp to `_shardResps`.
    // nullptr if we couldn't find the in flight response. Fills in txnId,
    // and nothing else.
    CDCShardResp* _prepareCDCShardResp(uint64_t reqId) {
        // If it's not the request we wanted, skip
        auto reqIt = _inFlightShardReqs.find(reqId);
        if (reqIt == _inFlightShardReqs.end()) {
            // This is a fairly common occurrence when timing out
            LOG_DEBUG(_env, "got unexpected shard request id %s, dropping", reqId);
            return nullptr;
        }
        if (_receivedResponses.contains(reqId)) {
            LOG_DEBUG(_env, "got multipl responses for same request %s, dropping", reqId);
            return nullptr;
        }
        CDCTxnId txnId = reqIt->second.txnId;
        auto& resp = _shardResps.emplace_back();
        resp.txnId = reqIt->second.txnId;
        _shardRespReqIds.emplace_back(reqId);
        _receivedResponses.emplace(reqId);
        return &resp;
    }

    void _recordCDCShardResp(uint64_t requestId, CDCShardResp& resp) {
        auto err = resp.resp.kind() != ShardMessageKind::ERROR ? EggsError::NO_ERROR : resp.resp.getError();
        _shared.shardErrors.add(err);
        if (err == EggsError::NO_ERROR) {
            LOG_DEBUG(_env, "successfully parsed shard response %s with kind %s, process soon", requestId, resp.resp.kind());
            return;
        } else if (err == EggsError::TIMEOUT) {
            LOG_DEBUG(_env, "txn %s shard req %s, timed out", resp.txnId, requestId);
        } else if (innocuousShardError(err)) {
            LOG_DEBUG(_env, "txn %s shard req %s, finished with innocuous error %s", resp.txnId, requestId, err);
        } else if (rareInnocuousShardError(err)) {
            LOG_INFO(_env, "txn %s shard req %s, finished with rare innocuous error %s", resp.txnId, requestId, err);
        } else {
            RAISE_ALERT(_env, "txn %s, req id %s, finished with error %s", resp.txnId, requestId, err);
        }
    }

    size_t _updateSize() const {
        return _cdcReqs.size() + _shardResps.size();
    }

    AddrsInfo* addressFromReplicaId(ReplicaId id) {
        if (!_replicas) {
            return nullptr;
        }

        auto& addr = (*_replicas)[id.u8];
        if (addr[0].port == 0) {
            return nullptr;
        }
        return &addr;
    }

    uint8_t _getReplicaId(const IpPort& clientAddress) {
        if (!_replicas) {
            return LogsDB::REPLICA_COUNT;
        }

        for (ReplicaId replicaId = 0; replicaId.u8 < _replicas->size(); ++replicaId.u8) {
            if (_replicas->at(replicaId.u8).contains(clientAddress)) {
                return replicaId.u8;
            }
        }

        return LogsDB::REPLICA_COUNT;
    }

    void _processLogMessages() {
        std::vector<LogsDBRequest> requests;
        std::vector<LogsDBResponse> responses;
        auto& requestMessages = _channel.protocolMessages(LOG_REQ_PROTOCOL_VERSION);
        auto& responseMessages = _channel.protocolMessages(LOG_RESP_PROTOCOL_VERSION);
        requests.reserve(requestMessages.size());
        responses.reserve(responseMessages.size());
        for (auto& msg : requestMessages) {
            auto replicaId = _getReplicaId(msg.clientAddr);
            if (replicaId == LogsDB::REPLICA_COUNT) {
                LOG_DEBUG(_env, "We can't match this address (%s) to replica. Dropping", msg.clientAddr);
                continue;
            }
            auto& req = requests.emplace_back();
            req.replicaId = replicaId;
            try {
                req.msg.unpack(msg.buf, _expandedCDCKey);
            } catch (const BincodeException& err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse LogsDBRequest from %s, dropping it.", msg.clientAddr);
                requests.pop_back();
                continue;
            }
            LOG_DEBUG(_env, "Received request %s with requests id %s from replica id %s", req.msg.body.kind(), req.msg.id, req.replicaId);
        }
        for (auto& msg : responseMessages) {
            auto replicaId = _getReplicaId(msg.clientAddr);
            if (replicaId == LogsDB::REPLICA_COUNT) {
                LOG_DEBUG(_env, "We can't match this address (%s) to replica. Dropping", msg.clientAddr);
                continue;
            }
            auto& resp = responses.emplace_back();
            resp.replicaId = replicaId;
            try {
                resp.msg.unpack(msg.buf, _expandedCDCKey);
            } catch (const BincodeException& err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse LogsDBResponse from %s, dropping it.", msg.clientAddr);
                requests.pop_back();
                continue;
            }
            LOG_DEBUG(_env, "Received response %s with requests id %s from replica id %s", resp.msg.body.kind(), resp.msg.id, resp.replicaId);
        }
        _logsDB.processIncomingMessages(requests, responses);
    }

    void _processCDCMessages() {
        int startUpdateSize = _updateSize();
        for (auto& msg: _channel.protocolMessages(CDC_REQ_PROTOCOL_VERSION)) {
            // First, try to parse the header
            CDCReqMsg cdcMsg;
            try {
                cdcMsg.unpack(msg.buf);
            } catch (const BincodeException& err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse request from %s, dropping it.", msg.clientAddr);
                continue;
            }

            LOG_DEBUG(_env, "received request id %s, kind %s", cdcMsg.id, cdcMsg.body.kind());
            auto receivedAt = eggsNow();

            if (unlikely(cdcMsg.body.kind() == CDCMessageKind::CDC_SNAPSHOT)) {
                _processCDCSnapshotMessage(cdcMsg, msg);
                continue;
            }

            // If we're already processing this request, drop it to try to not clog the queue
            if (_inFlightCDCReqs.contains(InFlightCDCRequestKey(cdcMsg.id, msg.clientAddr))) {
                LOG_DEBUG(_env, "dropping req id %s from %s since it's already being processed", cdcMsg.id, msg.clientAddr);
                continue;
            }

            auto& cdcReq = _cdcReqs.emplace_back(std::move(cdcMsg.body));

            LOG_DEBUG(_env, "CDC request %s successfully parsed, will process soon", cdcReq.kind());
            _cdcReqsInfo.emplace_back(CDCReqInfo{
                .reqId = cdcMsg.id,
                .clientAddr = msg.clientAddr,
                .receivedAt = receivedAt,
                .sockIx = msg.socketIx,
            });
        }
    }

    void _processShardMessages() {
        for (auto& msg : _channel.protocolMessages(SHARD_CHECK_POINTED_RESP_PROTOCOL_VERSION)) {
            LOG_DEBUG(_env, "received response from shard");

            ShardCheckPointedRespMsg respMsg;
            try {
                respMsg.unpack(msg.buf, _expandedCDCKey);
            } catch (BincodeException err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse response, dropping response");
                continue;
            }

            LOG_DEBUG(_env, "received response %s", respMsg);

            auto shardResp = _prepareCDCShardResp(respMsg.id);
            if (shardResp == nullptr) {
                // we couldn't find it
                continue;
            }
            shardResp->checkPoint = respMsg.body.checkPointIdx;
            shardResp->resp = std::move(respMsg.body.resp);

            _recordCDCShardResp(respMsg.id, *shardResp);
        }
    }

    void _processCDCSnapshotMessage(CDCReqMsg& msg, const UDPMessage& udpMsg) {
            auto err = _shared.sharedDb.snapshot(_basePath +"/snapshot-" + std::to_string(msg.body.getCdcSnapshot().snapshotId));
            CDCRespMsg respMsg;
            respMsg.id = msg.id;
            if (err == EggsError::NO_ERROR) {
                respMsg.body.setCdcSnapshot();
            } else {
                respMsg.body.setError() = err;
            }
            _packCDCResponse(udpMsg.socketIx, udpMsg.clientAddr, CDCMessageKind::CDC_SNAPSHOT, respMsg);
    }

    #ifdef __clang__
    __attribute__((no_sanitize("integer"))) // might wrap around (it's initialized randomly)
    #endif
    inline uint64_t _freshShardReqId() {
        _shardRequestIdCounter++;
        return _shardRequestIdCounter;
    }

    void _processStep() {
        LOG_DEBUG(_env, "processing step %s", _step);
        // finished txns
        for (const auto& [txnId, resp]: _step.finishedTxns) {
            LOG_DEBUG(_env, "txn %s finished", txnId);
            // we need to send the response back to the client
            auto inFlight = _inFlightTxns.find(txnId);
            if (inFlight->second.hasClient) {
                _shared.timingsTotal[(int)inFlight->second.kind].add(eggsNow() - inFlight->second.receivedAt);
                _shared.errors[(int)inFlight->second.kind].add(resp.kind() != CDCMessageKind::ERROR ? EggsError::NO_ERROR : resp.getError());
                CDCRespMsg respMsg;
                respMsg.id = inFlight->second.cdcRequestId;
                respMsg.body = std::move(resp);
                LOG_DEBUG(_env, "sending response with req id %s, kind %s, back to %s", inFlight->second.cdcRequestId, inFlight->second.kind, inFlight->second.clientAddr);
                _packCDCResponse(inFlight->second.sockIx, inFlight->second.clientAddr, inFlight->second.kind, respMsg);
                _inFlightCDCReqs.erase(InFlightCDCRequestKey(inFlight->second.cdcRequestId, inFlight->second.clientAddr));
            }
            _inFlightTxns.erase(inFlight);
            _updateInFlightTxns();
        }
        // in flight txns
        for (const auto& [txnId, shardReq]: _step.runningTxns) {
            CDCShardReq prevReq;
            LOG_TRACE(_env, "txn %s needs shard %s, req %s", txnId, shardReq.shid, shardReq.req);
            ShardCheckPointedReqMsg shardReqMsg;

            // Do not allocate new req id for repeated requests, so that we'll just accept
            // the first one that comes back. There's a chance for the txnId to not be here
            // yet: if we have just restarted the CDC. In this case we fill it in here, but
            // obviously without client addr.
            auto inFlightTxn = _inFlightTxns.find(txnId);
            if (inFlightTxn == _inFlightTxns.end()) {
                LOG_INFO(_env, "Could not find in-flight transaction %s, this might be because the CDC was restarted in the middle of a transaction.", txnId);
                InFlightCDCRequest req;
                req.hasClient = false;
                req.lastSentRequestId = _freshShardReqId();
                inFlightTxn = _inFlightTxns.emplace(txnId, req).first;
                shardReqMsg.id = req.lastSentRequestId;
                _updateInFlightTxns();
            } else if (shardReq.repeated) {
                shardReqMsg.id = inFlightTxn->second.lastSentRequestId;
            } else {
                shardReqMsg.id = _freshShardReqId();
            }
            shardReqMsg.body = shardReq.req;
            // Pack
            _shared.shardsMutex.lock();
            ShardInfo shardInfo = _shared.shards[shardReq.shid.u8];
            _shared.shardsMutex.unlock();

            LOG_DEBUG(_env, "sending request for txn %s with req id %s to shard %s (%s)", txnId, shardReqMsg.id, shardReq.shid, shardInfo.addrs);
            _shardSender.prepareOutgoingMessage(_env, _shared.socks[SHARD_SOCK].addr(), shardInfo.addrs, [this, &shardReqMsg](BincodeBuf& bbuf) {
                    shardReqMsg.pack(bbuf, _expandedCDCKey);
            });
            // Record the in-flight req
            _inFlightShardReqs.insert(shardReqMsg.id, InFlightShardRequest{
                .txnId = txnId,
                .sentAt = eggsNow(),
                .shid = shardReq.shid,
            });
            inFlightTxn->second.lastSentRequestId = shardReqMsg.id;
        }
    }

    void _packCDCResponse(int sockIx, const IpPort& clientAddr, CDCMessageKind reqKind, const CDCRespMsg& respMsg) {
        if (unlikely(respMsg.body.kind() == CDCMessageKind::ERROR)) {
            auto err = respMsg.body.getError();
            LOG_DEBUG(_env, "will send error %s to %s", err, clientAddr);
            if (err != EggsError::DIRECTORY_NOT_EMPTY && err != EggsError::EDGE_NOT_FOUND) {
                RAISE_ALERT(_env, "request %s of kind %s from client %s failed with err %s", respMsg.id, reqKind, clientAddr, err);
            }
        } else {
            LOG_DEBUG(_env, "will send response to CDC req %s, kind %s, to %s", respMsg.id, reqKind, clientAddr);
        }
        _cdcSender.prepareOutgoingMessage(_env, _shared.socks[CDC_SOCK].addr(), sockIx, clientAddr, [&respMsg](BincodeBuf& respBbuf) {
            respMsg.pack(respBbuf);
        });
    }

    uint64_t _advanceLogIndex() {
        return ++_currentLogIndex;
    }
};

struct CDCShardUpdater : PeriodicLoop {
    CDCShared& _shared;
    std::string _shuckleHost;
    uint16_t _shucklePort;

    // loop data
    std::array<ShardInfo, 256> _shards;
    XmonNCAlert _alert;
public:
    CDCShardUpdater(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const CDCOptions& options, CDCShared& shared):
        PeriodicLoop(logger, xmon, "shard_updater", {1_sec, 1_mins}),
        _shared(shared),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _alert(10_sec)
    {
        _env.updateAlert(_alert, "Waiting to get shards");
    }

    virtual ~CDCShardUpdater() = default;

    virtual bool periodicStep() override {
        LOG_INFO(_env, "Fetching shards");
        const auto [err, errStr] = fetchShards(_shuckleHost, _shucklePort, 10_sec, _shards);
        if (err == EINTR) { return false; }
        if (err) {
            _env.updateAlert(_alert, "failed to reach shuckle at %s:%s to fetch shards, will retry: %s", _shuckleHost, _shucklePort, errStr);
            return false;
        }
        bool badShard = false;
        for (int i = 0; i < _shards.size(); i++) {
            if (_shards[i].addrs[0].port == 0) {
                badShard = true;
                break;
            }
        }
        if (badShard) {
            EggsTime successfulIterationAt = 0;
            _env.updateAlert(_alert, "Shard info is still not present in shuckle, will keep trying");
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(_shared.shardsMutex);
            for (int i = 0; i < _shards.size(); i++) {
                _shared.shards[i] = _shards[i];
            }
        }
        _env.clearAlert(_alert);
        LOG_INFO(_env, "successfully fetched all shards from shuckle, will wait one minute");
        return true;
    }
};

struct CDCRegisterer : PeriodicLoop {
    CDCShared& _shared;
    const ReplicaId _replicaId;
    const uint8_t _location;
    const bool _dontDoReplication;
    const std::string _shuckleHost;
    const uint16_t _shucklePort;
    XmonNCAlert _alert;
public:
    CDCRegisterer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const CDCOptions& options, CDCShared& shared):
        PeriodicLoop(logger, xmon, "registerer", { 1_sec, 1_mins }),
        _shared(shared),
        _replicaId(options.replicaId),
        _location(options.location),
        _dontDoReplication(options.dontDoReplication),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _alert(10_sec)
    {}

    virtual ~CDCRegisterer() = default;

    virtual bool periodicStep() override {
        LOG_DEBUG(_env, "Registering ourselves (CDC %s, location %s,  %s) with shuckle", _replicaId, (int)_location, _shared.socks[CDC_SOCK].addr());
        {
            const auto [err, errStr] = registerCDCReplica(_shuckleHost, _shucklePort, 10_sec, _replicaId, _location, _shared.isLeader.load(std::memory_order_relaxed), _shared.socks[CDC_SOCK].addr());
            if (err == EINTR) { return false; }
            if (err) {
                _env.updateAlert(_alert, "Couldn't register ourselves with shuckle: %s", errStr);
                return false;
            }
            _env.clearAlert(_alert);
        }

        {
            std::array<AddrsInfo, 5> replicas;
            LOG_INFO(_env, "Fetching replicas for CDC from shuckle");
            const auto [err, errStr] = fetchCDCReplicas(_shuckleHost, _shucklePort, 10_sec, replicas);
            if (err == EINTR) { return false; }
            if (err) {
                _env.updateAlert(_alert, "Failed getting CDC replicas from shuckle: %s", errStr);
                return false;
            }
            if (_shared.socks[CDC_SOCK].addr() != replicas[_replicaId.u8]) {
                _env.updateAlert(_alert, "AddrsInfo in shuckle: %s , not matching local AddrsInfo: %s", replicas[_replicaId.u8], _shared.socks[CDC_SOCK].addr());
                return false;
            }
            if (unlikely(!_shared.replicas)) {
                size_t emptyReplicas{0};
                for (auto& replica : replicas) {
                    if (replica.addrs[0].port == 0) {
                        ++emptyReplicas;
                    }
                }
                if (!_dontDoReplication && emptyReplicas > 0 ) {
                    _env.updateAlert(_alert, "Didn't get enough replicas with known addresses from shuckle");
                    return false;
                }
            }
            if (unlikely(!_shared.replicas || *_shared.replicas != replicas)) {
                LOG_DEBUG(_env, "Updating replicas to %s %s %s %s %s", replicas[0], replicas[1], replicas[2], replicas[3], replicas[4]);
                std::atomic_exchange(&_shared.replicas, std::make_shared<std::array<AddrsInfo, LogsDB::REPLICA_COUNT>>(replicas));
            }
        }
        return true;
    }
};

static void logsDBstatsToMetrics(struct MetricsBuilder& metricsBuilder, const LogsDBStats& stats, ReplicaId replicaId, EggsTime now) {
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldU64( "idle_time", stats.idleTime.load(std::memory_order_relaxed).ns);
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldU64( "processing_time", stats.processingTime.load(std::memory_order_relaxed).ns);
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldU64( "leader_last_active", stats.leaderLastActive.load(std::memory_order_relaxed).ns);
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "append_window", stats.appendWindow.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "entries_released", stats.entriesReleased.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "follower_lag", stats.followerLag.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "reader_lag", stats.readerLag.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "catchup_window", stats.catchupWindow.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "entries_read", stats.entriesRead.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "requests_received", stats.requestsReceived.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "responses_received", stats.requestsReceived.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "requests_sent", stats.requestsSent.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "responses_sent", stats.responsesSent.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldFloat( "requests_timedout", stats.requestsTimedOut.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
    {
        metricsBuilder.measurement("eggsfs_cdc_logsdb");
        metricsBuilder.tag("replica", replicaId);
        metricsBuilder.tag("leader", stats.isLeader.load(std::memory_order_relaxed));
        metricsBuilder.fieldU64( "current_epoch", stats.currentEpoch.load(std::memory_order_relaxed));
        metricsBuilder.timestamp(now);
    }
}

struct CDCMetricsInserter : PeriodicLoop {
private:
    CDCShared& _shared;
    ReplicaId _replicaId;
    XmonNCAlert _sendMetricsAlert;
    MetricsBuilder _metricsBuilder;
    std::unordered_map<std::string, uint64_t> _rocksDBStats;
    XmonNCAlert _updateSizeAlert;
public:
    CDCMetricsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, CDCShared& shared, ReplicaId replicaId):
        PeriodicLoop(logger, xmon, "metrics", {1_sec, 1.0, 1_mins, 0.1}),
        _shared(shared),
        _replicaId(replicaId),
        _sendMetricsAlert(XmonAppType::DAYTIME, 1_mins),
        _updateSizeAlert(XmonAppType::NEVER)
    {}

    virtual ~CDCMetricsInserter() = default;

    virtual bool periodicStep() {
        if (std::ceil(_shared.updateSize) >= MAX_UPDATE_SIZE) {
            _env.updateAlert(_updateSizeAlert, "CDC update queue is full (%s)", _shared.updateSize);
        } else {
            _env.clearAlert(_updateSizeAlert);
        }
        auto now = eggsNow();
        for (CDCMessageKind kind : allCDCMessageKind) {
            const ErrorCount& errs = _shared.errors[(int)kind];
            for (int i = 0; i < errs.count.size(); i++) {
                uint64_t count = errs.count[i].load();
                if (count == 0) { continue; }
                _metricsBuilder.measurement("eggsfs_cdc_requests");
                _metricsBuilder.tag("kind", kind);
                _metricsBuilder.tag("replica", _replicaId);
                if (i == 0) {
                    _metricsBuilder.tag("error", "NO_ERROR");
                } else {
                    _metricsBuilder.tag("error", (EggsError)i);
                }
                _metricsBuilder.fieldU64("count", count);
                _metricsBuilder.timestamp(now);
            }
        }
        {
            _metricsBuilder.measurement("eggsfs_cdc_in_flight_txns");
            _metricsBuilder.tag("replica", _replicaId);
            _metricsBuilder.fieldFloat("count", _shared.inFlightTxns);
            _metricsBuilder.timestamp(now);
        }
        {
            _metricsBuilder.measurement("eggsfs_cdc_update");
            _metricsBuilder.tag("replica", _replicaId);
            _metricsBuilder.fieldFloat("size", _shared.updateSize);
            _metricsBuilder.timestamp(now);
        }
        for (int i = 0; i < _shared.shardErrors.count.size(); i++) {
            uint64_t count = _shared.shardErrors.count[i].load();
            if (count == 0) { continue; }
            _metricsBuilder.measurement("eggsfs_cdc_shard_requests");
            _metricsBuilder.tag("replica", _replicaId);
            if (i == 0) {
                _metricsBuilder.tag("error", "NO_ERROR");
            } else {
                _metricsBuilder.tag("error", (EggsError)i);
            }
            _metricsBuilder.fieldU64("count", count);
            _metricsBuilder.timestamp(now);
        }
        {
            _rocksDBStats.clear();
            _shared.sharedDb.rocksDBMetrics(_rocksDBStats);
            for (const auto& [name, value]: _rocksDBStats) {
                _metricsBuilder.measurement("eggsfs_cdc_rocksdb");
                _metricsBuilder.tag("replica", _replicaId);
                _metricsBuilder.fieldU64(name, value);
                _metricsBuilder.timestamp(now);
            }
        }
        logsDBstatsToMetrics(_metricsBuilder, _shared.logsDB.getStats(), _replicaId, now);
        std::string err = sendMetrics(10_sec, _metricsBuilder.payload());
        _metricsBuilder.reset();
        if (err.empty()) {
            LOG_INFO(_env, "Sent metrics to influxdb");
            _env.clearAlert(_sendMetricsAlert);
            return true;
        } else {
            _env.updateAlert(_sendMetricsAlert, "Could not insert metrics: %s", err);
            return false;
        }
    }
};


void runCDC(CDCOptions& options) {
    int logOutFd = STDOUT_FILENO;
    if (!options.logFile.empty()) {
        logOutFd = open(options.logFile.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0644);
        if (logOutFd < 0) {
            throw SYSCALL_EXCEPTION("open");
        }
    }
    Logger logger(options.logLevel, logOutFd, options.syslog, true);

    std::shared_ptr<XmonAgent> xmon;
    if (options.xmon) {
        xmon = std::make_shared<XmonAgent>();
    }

    Env env(logger, xmon, "startup");
    LOG_INFO(env, "Running CDC with options:");
    LOG_INFO(env, "  level = %s", options.logLevel);
    LOG_INFO(env, "  logFile = '%s'", options.logFile);
    LOG_INFO(env, "  replicaId = %s", options.replicaId);
    LOG_INFO(env, "  port = %s", options.port);
    LOG_INFO(env, "  shuckleHost = '%s'", options.shuckleHost);
    LOG_INFO(env, "  shucklePort = %s", options.shucklePort);
    LOG_INFO(env, "  cdcAddrs = %s", options.cdcAddrs);
    LOG_INFO(env, "  syslog = %s", (int)options.syslog);
    LOG_INFO(env, "Using LogsDB with options:");
    LOG_INFO(env, "    dontDoReplication = '%s'", (int)options.dontDoReplication);
    LOG_INFO(env, "    forceLeader = '%s'", (int)options.forceLeader);
    LOG_INFO(env, "    forcedLastReleased = '%s'", options.forcedLastReleased);

    std::vector<std::unique_ptr<LoopThread>> threads;

    // xmon first, so that by the time it shuts down it'll have all the leftover requests
    if (xmon) {
        XmonConfig config;
        {
            std::ostringstream ss;
            ss << "eggscdc_" << options.replicaId;
            config.appInstance = ss.str();
        }
        config.appType = XmonAppType::CRITICAL;
        config.prod = options.xmonProd;

        threads.emplace_back(LoopThread::Spawn(std::make_unique<Xmon>(logger, xmon, config)));
    }

    SharedRocksDB sharedDb(logger, xmon, options.dbDir + "/db", options.dbDir + "/db-statistics.txt");
    sharedDb.registerCFDescriptors(LogsDB::getColumnFamilyDescriptors());
    sharedDb.registerCFDescriptors(CDCDB::getColumnFamilyDescriptors());

    rocksdb::Options dbOptions;
    dbOptions.create_if_missing = true;
    dbOptions.create_missing_column_families = true;
    dbOptions.compression = rocksdb::kLZ4Compression;
    // In the shards we set this given that 1000*256 = 256k, doing it here also
    // for symmetry although it's probably not needed.
    dbOptions.max_open_files = 1000;
    sharedDb.openTransactionDB(dbOptions);

    CDCDB db(logger, xmon, sharedDb);
    LogsDB logsDB(logger, xmon, sharedDb, options.replicaId, db.lastAppliedLogEntry(), options.dontDoReplication, options.forceLeader, !options.forceLeader, db.lastAppliedLogEntry());
    CDCShared shared(
        sharedDb, db, logsDB,
        std::array<UDPSocketPair, 2>({UDPSocketPair(env, options.cdcAddrs), UDPSocketPair(env, options.cdcToShardAddress)})
    );

    LOG_INFO(env, "Spawning server threads");

    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCShardUpdater>(logger, xmon, options, shared)));
    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCServer>(logger, xmon, options, shared)));
    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCRegisterer>(logger, xmon, options, shared)));
    if (options.metrics) {
        threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCMetricsInserter>(logger, xmon, shared, options.replicaId)));
    }

    LoopThread::waitUntilStopped(threads);

    sharedDb.close();

    LOG_INFO(env, "CDC terminating gracefully, bye.");
}
