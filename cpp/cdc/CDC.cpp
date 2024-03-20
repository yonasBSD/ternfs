#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <fcntl.h>
#include <optional>
#include <thread>
#include <unordered_map>
#include <arpa/inet.h>
#include <unordered_set>
#include <map>
#include <poll.h>

#include "Bincode.hpp"
#include "CDC.hpp"
#include "CDCDB.hpp"
#include "Env.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Shard.hpp"
#include "Time.hpp"
#include "CDCDB.hpp"
#include "Crypto.hpp"
#include "CDCKey.hpp"
#include "Shuckle.hpp"
#include "XmonAgent.hpp"
#include "wyhash.h"
#include "Xmon.hpp"
#include "Timings.hpp"
#include "PeriodicLoop.hpp"
#include "Loop.hpp"
#include "ErrorCount.hpp"

struct CDCShared {
    CDCDB& db;
    std::array<std::atomic<uint16_t>, 2> ownPorts;
    std::mutex replicasLock;
    std::array<AddrsInfo, 5> replicas;
    std::mutex shardsMutex;
    std::array<ShardInfo, 256> shards;
    // How long it took us to process the entire request, from parse to response.
    std::array<Timings, maxCDCMessageKind+1> timingsTotal;
    std::array<ErrorCount, maxCDCMessageKind+1> errors;
    std::atomic<double> inFlightTxns;
    std::atomic<double> updateSize;
    ErrorCount shardErrors;

    CDCShared(CDCDB& db_) : db(db_), inFlightTxns(0), updateSize(0) {
        ownPorts[0].store(0);
        ownPorts[1].store(0);
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
    struct sockaddr_in clientAddr;
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
    uint32_t ip;
    uint16_t port;

    InFlightCDCRequestKey(uint64_t requestId_, struct sockaddr_in clientAddr_) :
        requestId(requestId_), ip(clientAddr_.sin_addr.s_addr), port(clientAddr_.sin_port)
    {}

    bool operator==(const InFlightCDCRequestKey& other) const {
        return requestId == other.requestId && ip == other.ip && port == other.port;
    }
};

template <>
struct std::hash<InFlightCDCRequestKey> {
    std::size_t operator()(const InFlightCDCRequestKey& key) const {
        return std::hash<uint64_t>{}(key.requestId ^ (((uint64_t)key.port << 32) | ((uint64_t)key.ip)));
    }
};

struct InFlightShardRequests {
private:
    using RequestsMap = std::unordered_map<uint64_t, InFlightShardRequest>;
    RequestsMap _reqs;

    std::map<EggsTime, uint64_t> _pq;

public:
    size_t size() const {
        return _reqs.size();
    }

    RequestsMap::const_iterator oldest() const {
        ALWAYS_ASSERT(size() > 0);
        auto reqByTime = _pq.begin();
        return _reqs.find(reqByTime->second);
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
    struct sockaddr_in clientAddr;
    EggsTime receivedAt;
    int sockIx;
};

constexpr int MAX_UPDATE_SIZE = 500;

struct CDCServer : Loop {
private:
    CDCShared& _shared;
    bool _seenShards;
    std::array<IpPort, 2> _ipPorts;
    uint64_t _currentLogIndex;
    CDCStep _step;
    uint64_t _shardRequestIdCounter;
    AES128Key _expandedCDCKey;
    Duration _shardTimeout;

    // order: CDC, shard, CDC, shard
    // length will be 2 or 4 depending on whether we have a second ip
    std::vector<struct pollfd> _socks;

    // reqs data
    std::vector<CDCReqContainer> _cdcReqs;
    std::vector<CDCReqInfo> _cdcReqsInfo;
    std::vector<CDCTxnId> _cdcReqsTxnIds;
    std::vector<CDCShardResp> _shardResps;

    // recvmmsg data
    std::vector<char> _recvBuf;
    std::vector<struct mmsghdr> _recvHdrs;
    std::vector<struct sockaddr_in> _recvAddrs;
    std::vector<struct iovec> _recvVecs;

    // sendmmsg data
    std::vector<char> _sendBuf;
    std::array<std::vector<struct mmsghdr>, 4> _sendHdrs; // one per socket
    std::array<std::vector<struct sockaddr_in>, 4> _sendAddrs;
    std::array<std::vector<struct iovec>, 4> _sendVecs;

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

public:
    CDCServer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const CDCOptions& options, CDCShared& shared) :
        Loop(logger, xmon, "req_server"),
        _shared(shared),
        _seenShards(false),
        _ipPorts(options.ipPorts),
        // important to not catch stray requests from previous executions
        _shardRequestIdCounter(wyhash64_rand()),
        _shardTimeout(options.shardTimeout)
    {
        _currentLogIndex = _shared.db.lastAppliedLogEntry();
        expandKey(CDCKey, _expandedCDCKey);
        LOG_INFO(_env, "Waiting for shard info to be filled in");
    }

    virtual void step() override {
        if (!_seenShards) {
            if (!_waitForShards()) {
                return;
            }
            _seenShards = true;
            _initAfterShardsSeen();
        }

        // clear internal buffers
        _cdcReqs.clear();
        _cdcReqsInfo.clear();
        _cdcReqsTxnIds.clear();
        _shardResps.clear();
        _sendBuf.clear();
        for (int i = 0; i < _sendHdrs.size(); i++) {
            _sendHdrs[i].clear();
            _sendAddrs[i].clear();
            _sendVecs[i].clear();
        }

        // Process CDC requests and shard responses
        {
            auto now = eggsNow();
            while (_updateSize() < MAX_UPDATE_SIZE) {
                if (_inFlightShardReqs.size() == 0) { break; }
                auto oldest = _inFlightShardReqs.oldest();
                if ((now - oldest->second.sentAt) < _shardTimeout) { break; }

                LOG_DEBUG(_env, "in-flight shard request %s was sent at %s, it's now %s, will time out (%s > %s)", oldest->first, oldest->second.sentAt, now, (now - oldest->second.sentAt), _shardTimeout);
                uint64_t requestId = oldest->first;
                auto resp = _prepareCDCShardResp(requestId); // erases `oldest`
                ALWAYS_ASSERT(resp != nullptr); // must be there, we've just timed it out
                _recordCDCShardRespError(requestId, *resp, EggsError::TIMEOUT);
            }
        }

        LOG_DEBUG(_env, "Blocking to wait for readable sockets");
        // Only timeout if there are outstanding shard requests
        if (unlikely(Loop::poll(_socks.data(), _socks.size(), (_inFlightShardReqs.size() > 0) ? _shardTimeout : -1) < 0)) {
            if (errno == EINTR) { return; }
            throw SYSCALL_EXCEPTION("poll");
        }

        // Drain sockets, first the shard resps ones (so we clear existing txns
        // first), then the CDC reqs ones.
        for (int i = 1; i < _socks.size(); i += 2) {
            const auto& sock = _socks[i];
            if (sock.revents & (POLLIN|POLLHUP|POLLERR)) {
                _drainShardSock(i);
            }
        }
        for (int i = 0; i < _socks.size(); i += 2) {
            const auto& sock = _socks[i];
            if (sock.revents & (POLLIN|POLLHUP|POLLERR)) {
                _drainCDCSock(i);
            }
        }
        _shared.updateSize = 0.95*_shared.updateSize + 0.05*_updateSize();
        // If anything happened, update the db and write down the in flight CDCs
        if (_cdcReqs.size() > 0 || _shardResps.size() > 0) {
            // process everything in a single batch
            _shared.db.update(true, _advanceLogIndex(), _cdcReqs, _shardResps, _step, _cdcReqsTxnIds);
            // record txn ids etc. for newly received requests
            for (int i = 0; i < _cdcReqs.size(); i++) {
                const auto& req = _cdcReqs[i];
                const auto& reqInfo = _cdcReqsInfo[i];
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
        }
        // send everything there is to send in one go
        for (int i = 0; i < _sendHdrs.size(); i++) {
            if (_sendHdrs[i].size() == 0) { continue; }
            for (int j = 0; j < _sendHdrs[i].size(); j++) {
                auto& vec = _sendVecs[i][j];
                vec.iov_base = &_sendBuf[(size_t)vec.iov_base];
                auto& hdr = _sendHdrs[i][j];
                hdr.msg_hdr.msg_iov = &vec;
                hdr.msg_hdr.msg_name = &_sendAddrs[i][j];
            }
            int ret = sendmmsg(_socks[i].fd, &_sendHdrs[i][0], _sendHdrs[i].size(), 0);
            if (unlikely(ret < 0)) {
                // we get EPERM when nf drops packets
                if (errno == EPERM) {
                    LOG_INFO(_env, "we got EPERM when trying to send %s messages, will drop them, they'll time out and get resent", _sendHdrs[i].size());
                } else {
                    throw SYSCALL_EXCEPTION("sendmmsg");
                }
            }
            if (unlikely(ret < _sendHdrs[i].size())) {
                LOG_INFO(_env, "could only send %s out of %s messages, is the send buffer jammed?", ret, _sendHdrs[i].size());
            }
        }
    }

private:
    void _updateInFlightTxns() {
        _shared.inFlightTxns = _shared.inFlightTxns*0.95 + ((double)_inFlightTxns.size())*0.05;
    }

    bool _waitForShards() {
        bool badShard = false;
        {
            const std::lock_guard<std::mutex> lock(_shared.shardsMutex);
            for (int i = 0; i < _shared.shards.size(); i++) {
                const auto sh = _shared.shards[i];
                if (sh.port1 == 0) {
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
        CDCTxnId txnId = reqIt->second.txnId;
        auto& resp = _shardResps.emplace_back();
        resp.txnId = reqIt->second.txnId;
        _inFlightShardReqs.erase(reqIt); // not in flight anymore
        return &resp;
    }

    void _recordCDCShardRespError(uint64_t requestId, CDCShardResp& resp, EggsError err) {
        resp.err = err;
        _shared.shardErrors.add(err);
        if (resp.err == EggsError::TIMEOUT) {
            LOG_DEBUG(_env, "txn %s shard req %s, timed out", resp.txnId, requestId);
        } else if (innocuousShardError(resp.err)) {
            LOG_DEBUG(_env, "txn %s shard req %s, finished with innocuous error %s", resp.txnId, requestId, resp.err);
        } else if (rareInnocuousShardError(resp.err)) {
            LOG_INFO(_env, "txn %s shard req %s, finished with rare innocuous error %s", resp.txnId, requestId, resp.err);
        } else {
            RAISE_ALERT(_env, "txn %s, req id %s, finished with error %s", resp.txnId, requestId, resp.err);
        }
    }

    void _initAfterShardsSeen() {
        // initialize everything after having seen the shards
        // Create sockets. We create one socket for listening to client requests and one for listening
        // the the shard's responses. If we have two IPs we do this twice.
        _socks.resize((_ipPorts[1].ip == 0) ? 2 : 4);
        LOG_DEBUG(_env, "initializing %s sockets", _socks.size());
        for (int i = 0; i < _socks.size(); i++) {
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            _socks[i].fd = sock;
            _socks[i].events = POLLIN;
            if (sock < 0) {
                throw SYSCALL_EXCEPTION("cannot create socket");
            }
            if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
                throw SYSCALL_EXCEPTION("fcntl");
            }
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            {
                uint32_t ipN = htonl(_ipPorts[i/2].ip);
                memcpy(&addr.sin_addr.s_addr, &ipN, 4);
            }
            if (i%2 == 0 && _ipPorts[i/2].port != 0) { // CDC with specified port
                addr.sin_port = htons(_ipPorts[i/2].port);
            } else { // automatically assigned port
                addr.sin_port = 0;
            }
            if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                throw SYSCALL_EXCEPTION("cannot bind socket to addr %s", addr);
            }
            {
                socklen_t addrLen = sizeof(addr);
                if (getsockname(sock, (struct sockaddr*)&addr, &addrLen) < 0) {
                    throw SYSCALL_EXCEPTION("getsockname");
                }
            }
            if (i%2 == 0) {
                LOG_DEBUG(_env, "bound CDC %s sock to port %s", i/2, ntohs(addr.sin_port));
                _shared.ownPorts[i/2].store(ntohs(addr.sin_port));
            } else {
                LOG_DEBUG(_env, "bound shard %s sock to port %s", i/2, ntohs(addr.sin_port));
                // CDC req/resps are very small (say 50bytes), so this gives us space for
                // 20k responses, which paired with the high timeout we currently set in production
                // (1s) should give us high throughput without retrying very often.
                int bufSize = 1<<20;
                if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&bufSize, sizeof(bufSize)) < 0) {
                    throw SYSCALL_EXCEPTION("setsockopt");
                }
            }
        }

        LOG_INFO(_env, "running on ports %s and %s", _shared.ownPorts[0].load(), _shared.ownPorts[1].load());

        // setup receive buffers
        _recvBuf.resize(DEFAULT_UDP_MTU*MAX_UPDATE_SIZE);
        _recvHdrs.resize(MAX_UPDATE_SIZE);
        memset(_recvHdrs.data(), 0, sizeof(_recvHdrs[0])*MAX_UPDATE_SIZE);
        _recvAddrs.resize(MAX_UPDATE_SIZE);
        _recvVecs.resize(MAX_UPDATE_SIZE);
        for (int j = 0; j < _recvVecs.size(); j++) {
            _recvVecs[j].iov_base = &_recvBuf[j*DEFAULT_UDP_MTU];
            _recvVecs[j].iov_len = DEFAULT_UDP_MTU;
            _recvHdrs[j].msg_hdr.msg_iov = &_recvVecs[j];
            _recvHdrs[j].msg_hdr.msg_iovlen = 1;
            _recvHdrs[j].msg_hdr.msg_namelen = sizeof(_recvAddrs[j]);
            _recvHdrs[j].msg_hdr.msg_name = &_recvAddrs[j];
        }

        // If we've got dangling transactions, immediately start processing it
        _shared.db.bootstrap(true, _advanceLogIndex(), _step);
        _processStep();
    }

    size_t _updateSize() const {
        return _cdcReqs.size() + _shardResps.size();
    }

    void _drainCDCSock(int sockIx) {
        int startUpdateSize = _updateSize();
        if (startUpdateSize >= MAX_UPDATE_SIZE) { return; }

        int sock = _socks[sockIx].fd;

        int ret = recvmmsg(sock, &_recvHdrs[startUpdateSize], MAX_UPDATE_SIZE-startUpdateSize, 0, nullptr);
        if (unlikely(ret < 0)) {
            throw SYSCALL_EXCEPTION("recvmmsg");
        }
        LOG_DEBUG(_env, "received %s CDC requests", ret);

        for (int i = 0; i < ret; i++) {
            auto& hdr = _recvHdrs[startUpdateSize+i];
            const struct sockaddr_in& clientAddr = *(struct sockaddr_in*)hdr.msg_hdr.msg_name;

            BincodeBuf reqBbuf((char*)hdr.msg_hdr.msg_iov[0].iov_base, hdr.msg_len);

            // First, try to parse the header
            CDCRequestHeader reqHeader;
            try {
                reqHeader.unpack(reqBbuf);
            } catch (const BincodeException& err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse request header from %s, dropping it.", clientAddr);
                continue;
            }

            LOG_DEBUG(_env, "received request id %s, kind %s", reqHeader.requestId, reqHeader.kind);
            auto receivedAt = eggsNow();

            // If we're already processing this request, drop it to try to not clog the queue
            if (_inFlightCDCReqs.contains(InFlightCDCRequestKey(reqHeader.requestId, clientAddr))) {
                LOG_DEBUG(_env, "dropping req id %s from %s since it's already being processed", reqHeader.requestId, clientAddr);
                continue;
            }

            // If this will be filled in with an actual code, it means that we couldn't process
            // the request.
            EggsError err = NO_ERROR;

            // Now, try to parse the body
            auto& cdcReq = _cdcReqs.emplace_back();
            try {
                cdcReq.unpack(reqBbuf, reqHeader.kind);
                LOG_DEBUG(_env, "parsed request: %s", cdcReq);
            } catch (const BincodeException& exc) {
                LOG_ERROR(_env, "could not parse: %s", exc.what());
                RAISE_ALERT(_env, "could not parse CDC request of kind %s from %s, will reply with error.", reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            // Make sure nothing is left
            if (err == NO_ERROR && reqBbuf.remaining() != 0) {
                RAISE_ALERT(_env, "%s bytes remaining after parsing CDC request of kind %s from %s, will reply with error", reqBbuf.remaining(), reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            if (err == NO_ERROR) {
                LOG_DEBUG(_env, "CDC request %s successfully parsed, will process soon", cdcReq.kind());
                _cdcReqsInfo.emplace_back(CDCReqInfo{
                    .reqId = reqHeader.requestId,
                    .clientAddr = clientAddr,
                    .receivedAt = receivedAt,
                    .sockIx = sockIx,
                });
            } else {
                // We couldn't parse, reply immediately with an error
                RAISE_ALERT(_env, "request %s failed before enqueue with error %s", cdcReq.kind(), err);
                _packCDCResponseError(sockIx, clientAddr, reqHeader, err);
                _cdcReqs.pop_back(); // let's just forget all about this
            }
        }
    }

    void _drainShardSock(int sockIx) {
        int startUpdateSize = _updateSize();
        if (startUpdateSize >= MAX_UPDATE_SIZE) { return; }

        int ret = recvmmsg(_socks[sockIx].fd, &_recvHdrs[startUpdateSize], MAX_UPDATE_SIZE-startUpdateSize, 0, nullptr);
        if (unlikely(ret < 0)) {
            throw SYSCALL_EXCEPTION("recvmsg");
        }
        for (int i = 0; i < ret; i++) {
            LOG_DEBUG(_env, "received response from shard");
            auto& hdr = _recvHdrs[startUpdateSize+i];
            BincodeBuf reqBbuf((char*)hdr.msg_hdr.msg_iov[0].iov_base, hdr.msg_len);

            ShardResponseHeader respHeader;
            try {
                respHeader.unpack(reqBbuf);
            } catch (BincodeException err) {
                LOG_ERROR(_env, "could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse response header, dropping response");
                continue;
            }

            LOG_DEBUG(_env, "received response id %s, kind %s", respHeader.requestId, respHeader.kind);

            // Note that below we just let the BincodeExceptions propagate upwards since we
            // control all the code in this codebase, and the header is good, and we're a
            // bit lazy.

            auto shardResp = _prepareCDCShardResp(respHeader.requestId);
            if (shardResp == nullptr) {
                // we couldn't find it
                continue;
            }

            // We got an error
            if (respHeader.kind == (ShardMessageKind)0) {
                _recordCDCShardRespError(respHeader.requestId, *shardResp, reqBbuf.unpackScalar<EggsError>());
                LOG_DEBUG(_env, "got error %s for response id %s", shardResp->err, respHeader.requestId);
                continue;
            }

            // Otherwise, parse the body
            shardResp->resp.unpack(reqBbuf, respHeader.kind);
            LOG_DEBUG(_env, "parsed shard response: %s", shardResp->resp);
            ALWAYS_ASSERT(reqBbuf.remaining() == 0);
            _shared.shardErrors.add(NO_ERROR);

            // If all went well, advance with the newly received request
            LOG_DEBUG(_env, "successfully parsed shard response %s with kind %s, process soon", respHeader.requestId, respHeader.kind);
        }
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
                _shared.errors[(int)inFlight->second.kind].add(resp.err);
                CDCResponseHeader respHeader(inFlight->second.cdcRequestId, inFlight->second.kind);
                if (resp.err != NO_ERROR) {
                    _packCDCResponseError(inFlight->second.sockIx, inFlight->second.clientAddr, CDCRequestHeader(respHeader.requestId, respHeader.kind), resp.err);
                } else {
                    LOG_DEBUG(_env, "sending response with req id %s, kind %s, back to %s", inFlight->second.cdcRequestId, inFlight->second.kind, inFlight->second.clientAddr);
                    _packCDCResponse(inFlight->second.sockIx, inFlight->second.clientAddr, respHeader, resp.resp);
                }
                _inFlightCDCReqs.erase(InFlightCDCRequestKey(inFlight->second.cdcRequestId, inFlight->second.clientAddr));
            }
            _inFlightTxns.erase(inFlight);
            _updateInFlightTxns();
        }
        // in flight txns
        for (const auto& [txnId, shardReq]: _step.runningTxns) {
            CDCShardReq prevReq;
            LOG_TRACE(_env, "txn %s needs shard %s, req %s", txnId, shardReq.shid, shardReq.req);
            // Header
            ShardRequestHeader shardReqHeader;
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
                shardReqHeader.requestId = req.lastSentRequestId;
                _updateInFlightTxns();
            } else if (shardReq.repeated) {
                shardReqHeader.requestId = inFlightTxn->second.lastSentRequestId;
            } else {
                shardReqHeader.requestId = _freshShardReqId();
            }
            shardReqHeader.kind = shardReq.req.kind();
            // Pack
            struct sockaddr_in shardAddr;
            memset(&shardAddr, 0, sizeof(shardAddr));
            _shared.shardsMutex.lock();
            ShardInfo shardInfo = _shared.shards[shardReq.shid.u8];
            _shared.shardsMutex.unlock();
            auto now = eggsNow(); // randomly pick one of the shard addrs and one of our sockets
            int whichShardAddr = now.ns & !!shardInfo.port2;
            int whichSock = (now.ns>>1) & !!_ipPorts[1].ip;
            shardAddr.sin_family = AF_INET;
            shardAddr.sin_port = htons(whichShardAddr ? shardInfo.port2 : shardInfo.port1);
            static_assert(sizeof(shardAddr.sin_addr) == sizeof(shardInfo.ip1));
            memcpy(&shardAddr.sin_addr, (whichShardAddr ? shardInfo.ip2 : shardInfo.ip1).data.data(), sizeof(shardAddr.sin_addr));
            LOG_DEBUG(_env, "sending request for txn %s with req id %s to shard %s (%s)", txnId, shardReqHeader.requestId, shardReq.shid, shardAddr);
            _packShardRequest(whichSock*2 + 1, shardAddr, shardReqHeader, shardReq.req);
            // Record the in-flight req
            _inFlightShardReqs.insert(shardReqHeader.requestId, InFlightShardRequest{
                .txnId = txnId,
                .sentAt = now,
                .shid = shardReq.shid,
            });
            inFlightTxn->second.lastSentRequestId = shardReqHeader.requestId;
        }
    }

    template<typename Fill>
    void _pack(int sockIx, const sockaddr_in& addrIn, Fill fill) {
        // serialize
        size_t sendBufBegin = _sendBuf.size();
        _sendBuf.resize(sendBufBegin + DEFAULT_UDP_MTU);
        size_t used = fill(&_sendBuf[sendBufBegin], DEFAULT_UDP_MTU);
        _sendBuf.resize(sendBufBegin + used);

        // sendmmsg structures -- we can't store references directly
        // here because the vectors might get resized
        auto& hdr = _sendHdrs[sockIx].emplace_back();
        hdr.msg_len = used;
        hdr.msg_hdr = {
            .msg_namelen = sizeof(addrIn),
            .msg_iovlen = 1,
        };
        _sendAddrs[sockIx].emplace_back(addrIn);
        auto& vec = _sendVecs[sockIx].emplace_back();
        vec.iov_len = used;
        vec.iov_base = (void*)sendBufBegin;
    }

    void _packCDCResponseError(int sockIx, const sockaddr_in& clientAddr, const CDCRequestHeader& reqHeader, EggsError err) {
        LOG_DEBUG(_env, "will send error %s to %s", err, clientAddr);
        if (err != EggsError::DIRECTORY_NOT_EMPTY && err != EggsError::EDGE_NOT_FOUND) {
            RAISE_ALERT(_env, "request %s of kind %s from client %s failed with err %s", reqHeader.requestId, reqHeader.kind, clientAddr, err);
        }
        _pack(sockIx, clientAddr, [&reqHeader, err](char* buf, size_t bufLen) {
            BincodeBuf respBbuf(buf, bufLen);
            CDCResponseHeader(reqHeader.requestId, CDCMessageKind::ERROR).pack(respBbuf);
            respBbuf.packScalar<uint16_t>((uint16_t)err);
            return respBbuf.len();
        });
    }

    void _packCDCResponse(int sockIx, const sockaddr_in& clientAddr, const CDCResponseHeader& respHeader, const CDCRespContainer& resp) {
        LOG_DEBUG(_env, "will send response to CDC req %s, kind %s, to %s", respHeader.requestId, respHeader.kind, clientAddr);
        _pack(sockIx, clientAddr, [&respHeader, &resp](char* buf, size_t bufLen) {
            BincodeBuf bbuf(buf, bufLen);
            respHeader.pack(bbuf);
            resp.pack(bbuf);
            return bbuf.len();
        });
    }

    void _packShardRequest(int sockIx, const sockaddr_in& shardAddr, const ShardRequestHeader& reqHeader, const ShardReqContainer& req) {
        LOG_DEBUG(_env, "will send shard request %s, kind %s, to %s", reqHeader.requestId, reqHeader.kind, shardAddr);
        _pack(sockIx, shardAddr, [this, &reqHeader, &req](char* buf, size_t bufLen) {
            BincodeBuf bbuf(buf, bufLen);
            reqHeader.pack(bbuf);
            req.pack(bbuf);
            if (isPrivilegedRequestKind(reqHeader.kind)) {
                bbuf.packFixedBytes<8>({cbcmac(_expandedCDCKey, bbuf.data, bbuf.len())});
            }
            return bbuf.len();
        });
    }

    void _packResp(int sock, struct sockaddr_in* dest, const char* data, size_t len) {
        // We need to handle EAGAIN/EPERM when trying to send. Here we take a ...
        // lazy approach and just loop with a delay. This seems to happen when
        // we restart everything while under load, it's not great to block here
        // but it's probably OK to do so in those cases. We should also automatically
        // clear the alert when done with this.
        XmonNCAlert alert(1_sec);
        for (;;) {
            if (likely(sendto(sock, data, len, 0, (struct sockaddr*)&dest, sizeof(dest)) == len)) {
                break;
            }
            int err = errno;
            // Note that we get EPERM on `sendto` when nf drops packets.
            if (likely(err == EAGAIN || err == EPERM)) {
                _env.updateAlert(alert, "we got %s/%s=%s when trying to send shard message, will wait and retry", err, translateErrno(err), safe_strerror(err));
                (100_ms).sleepRetry();
            } else {
                _env.clearAlert(alert);
                throw EXPLICIT_SYSCALL_EXCEPTION(err, "sendto");
            }
        }
        _env.clearAlert(alert);
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
        std::string err = fetchShards(_shuckleHost, _shucklePort, 10_sec, _shards);
        if (!err.empty()) {
            _env.updateAlert(_alert, "failed to reach shuckle at %s:%s to fetch shards, will retry: %s", _shuckleHost, _shucklePort, err);
            return false;
        }
        bool badShard = false;
        for (int i = 0; i < _shards.size(); i++) {
            if (_shards[i].port1 == 0) {
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
    std::string _shuckleHost;
    uint16_t _shucklePort;
    bool _hasSecondIp;
    XmonNCAlert _alert;
    ReplicaId _replicaId;
    ReplicaId _leaderReplicaId;
    AddrsInfo _info;
    bool _infoLoaded;
    bool _registerCompleted;
public:
    CDCRegisterer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const CDCOptions& options, CDCShared& shared):
        PeriodicLoop(logger, xmon, "registerer", { 1_sec, 1_mins }),
        _shared(shared),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _hasSecondIp(options.ipPorts[1].ip != 0),
        _alert(10_sec),
        _replicaId(options.replicaId),
        _leaderReplicaId(options.leaderReplicaId),
        _infoLoaded(false),
        _registerCompleted(false)
    {
        uint32_t ip1 = options.ipPorts[0].ip;
        uint32_t ip2 = options.ipPorts[1].ip;
        uint32_t ip = htonl(ip1);
        memcpy(_info.ip1.data.data(), &ip, 4);
        ip = htonl(ip2);
        memcpy(_info.ip2.data.data(), &ip, 4);
    }

    virtual ~CDCRegisterer() = default;

    virtual bool periodicStep() override {
        if (unlikely(!_infoLoaded)) {
            uint16_t port1 = _shared.ownPorts[0].load();
            uint16_t port2 = _shared.ownPorts[1].load();
            if (port1 == 0 || (_hasSecondIp && port2 == 0)) {
                return false;
            }
            _info.port1 = port1;
            _info.port2 = port2;
            _infoLoaded = true;
        }

        std::string err;
        if(likely(_registerCompleted)) {
            std::array<AddrsInfo, 5> replicas;
            LOG_INFO(_env, "Fetching replicas for CDC from shuckle");
            err = fetchCDCReplicas(_shuckleHost, _shucklePort, 10_sec, replicas);
            if (!err.empty()) {
                _env.updateAlert(_alert, "Failed getting CDC replicas from shuckle: %s", err);
                return false;
            }
            if (_info != replicas[_replicaId.u8]) {
                _env.updateAlert(_alert, "AddrsInfo in shuckle: %s , not matching local AddrsInfo: %s", replicas[_replicaId.u8], _info);
                return false;
            }
            {
                std::lock_guard guard(_shared.replicasLock);
                _shared.replicas = replicas;
            }
        }

        LOG_DEBUG(_env, "Registering ourselves (CDC %s, %s) with shuckle", _replicaId, _info);
        err = registerCDCReplica(_shuckleHost, _shucklePort, 10_sec, _replicaId, _replicaId == _leaderReplicaId, _info);
        if (!err.empty()) {
            _env.updateAlert(_alert, "Couldn't register ourselves with shuckle: %s", err);
            return false;
        }
        _env.clearAlert(_alert);
        _registerCompleted = true;
        return true;
    }
};

struct CDCStatsInserter : PeriodicLoop {
private:
    CDCShared& _shared;
    std::string _shuckleHost;
    uint16_t _shucklePort;
    XmonNCAlert _alert;
    std::vector<Stat> _stats;

public:
    CDCStatsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const CDCOptions& options, CDCShared& shared):
        PeriodicLoop(logger, xmon, "stats_inserter", {1_sec, 1_hours}),
        _shared(shared),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _alert(XmonAppType::DAYTIME, 10_sec)
    {}

    virtual ~CDCStatsInserter() = default;

    virtual bool periodicStep() override {
        std::string err;
        for (CDCMessageKind kind : allCDCMessageKind) {
            {
                std::ostringstream prefix;
                prefix << "cdc." << kind;
                _shared.timingsTotal[(int)kind].toStats(prefix.str(), _stats);
                _shared.errors[(int)kind].toStats(prefix.str(), _stats);
            }
        }
        err = insertStats(_shuckleHost, _shucklePort, 10_sec, _stats);
        _stats.clear();
        if (err.empty()) {
            _env.clearAlert(_alert);
            for (CDCMessageKind kind : allCDCMessageKind) {
                _shared.timingsTotal[(int)kind].reset();
                _shared.errors[(int)kind].reset();
            }
            return true;
        } else {
            _env.updateAlert(_alert, "Could not insert stats: %s", err);
            return false;
        }
    }

    // TODO restore this when we can
    // virtual void finish() override {
    //     LOG_INFO(_env, "inserting stats one last time");
    //     periodicStep();
    // }
};

struct CDCMetricsInserter : PeriodicLoop {
private:
    CDCShared& _shared;
    XmonNCAlert _sendMetricsAlert;
    MetricsBuilder _metricsBuilder;
    std::unordered_map<std::string, uint64_t> _rocksDBStats;
    XmonNCAlert _updateSizeAlert;
public:
    CDCMetricsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, CDCShared& shared):
        PeriodicLoop(logger, xmon, "metrics", {1_sec, 1.0, 1_mins, 0.1}),
        _shared(shared),
        _sendMetricsAlert(XmonAppType::DAYTIME, 10_sec),
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
            _metricsBuilder.fieldFloat("count", _shared.inFlightTxns);
            _metricsBuilder.timestamp(now);
        }
        {
            _metricsBuilder.measurement("eggsfs_cdc_update");
            _metricsBuilder.fieldFloat("size", _shared.updateSize);
            _metricsBuilder.timestamp(now);
        }
        for (int i = 0; i < _shared.shardErrors.count.size(); i++) {
            uint64_t count = _shared.shardErrors.count[i].load();
            if (count == 0) { continue; }
            _metricsBuilder.measurement("eggsfs_cdc_shard_requests");
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
            _shared.db.rocksDBMetrics(_rocksDBStats);
            for (const auto& [name, value]: _rocksDBStats) {
                _metricsBuilder.measurement("eggsfs_cdc_rocksdb");
                _metricsBuilder.fieldU64(name, value);
                _metricsBuilder.timestamp(now);
            }
        }
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


void runCDC(const std::string& dbDir, const CDCOptions& options) {
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
    LOG_INFO(env, "  leaderReplicaId = %s", options.leaderReplicaId);
    LOG_INFO(env, "  port = %s", options.port);
    LOG_INFO(env, "  shuckleHost = '%s'", options.shuckleHost);
    LOG_INFO(env, "  shucklePort = %s", options.shucklePort);
    for (int i = 0; i < 2; i++) {
        LOG_INFO(env, "  port%s = %s", i+1, options.ipPorts[0].port);
        {
            char ip[INET_ADDRSTRLEN];
            uint32_t ipN = options.ipPorts[i].ip;
            LOG_INFO(env, "  ownIp%s = %s", i+1, inet_ntop(AF_INET, &ipN, ip, INET_ADDRSTRLEN));
        }
    }
    LOG_INFO(env, "  syslog = %s", (int)options.syslog);

    std::vector<std::unique_ptr<LoopThread>> threads;

    // xmon first, so that by the time it shuts down it'll have all the leftover requests
    if (xmon) {
        XmonConfig config;
        config.appInstance = "eggscdc";
        config.appType = XmonAppType::CRITICAL;
        config.prod = options.xmonProd;

        threads.emplace_back(LoopThread::Spawn(std::make_unique<Xmon>(logger, xmon, config)));
    }

    CDCDB db(logger, xmon, dbDir);
    CDCShared shared(db);

    LOG_INFO(env, "Spawning server threads");

    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCShardUpdater>(logger, xmon, options, shared)));
    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCRegisterer>(logger, xmon, options, shared)));
    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCStatsInserter>(logger, xmon, options, shared)));
    if (options.metrics) {
        threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCMetricsInserter>(logger, xmon, shared)));
    }
    threads.emplace_back(LoopThread::Spawn(std::make_unique<CDCServer>(logger, xmon, options, shared)));

    LoopThread::waitUntilStopped(threads);

    db.close();

    LOG_INFO(env, "CDC terminating gracefully, bye.");
}
