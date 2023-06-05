#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <sys/epoll.h>
#include <fcntl.h>
#include <optional>
#include <thread>
#include <unordered_map>
#include <arpa/inet.h>

#include "Bincode.hpp"
#include "CDC.hpp"
#include "CDCDB.hpp"
#include "Env.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "Shard.hpp"
#include "Time.hpp"
#include "Undertaker.hpp"
#include "CDCDB.hpp"
#include "Crypto.hpp"
#include "CDCKey.hpp"
#include "Shuckle.hpp"

struct CDCShared {
    CDCDB& db;
    std::atomic<bool> stop;
    std::array<std::atomic<uint16_t>, 2> ownPorts;
    std::mutex shardsMutex;
    std::array<ShardInfo, 256> shards;

    CDCShared(CDCDB& db_) :
        db(db_),
        stop(false)
    {
        for (auto& shard: shards) {
            memset(&shard, 0, sizeof(shard));
        }
        ownPorts[0].store(0);
        ownPorts[1].store(0);
    }
};

struct InFlightShardRequest {
    uint64_t txnId; // the txn id that requested this shard request
    EggsTime sentAt;
    uint64_t shardRequestId;
    ShardId shid;
};

struct InFlightCDCRequest {
    uint64_t cdcRequestId;
    struct sockaddr_in clientAddr;
    CDCMessageKind kind;
    int sock;
};

struct CDCServer : Undertaker::Reapable {
private:
    Env _env;
    CDCShared& _shared;
    std::array<IpPort, 2> _ipPorts;
    uint64_t _currentLogIndex;
    std::vector<char> _recvBuf;
    std::vector<char> _sendBuf;
    CDCReqContainer _cdcReqContainer;
    ShardRespContainer _shardRespContainer;
    CDCStep _step;
    uint64_t _shardRequestIdCounter;
    std::array<int, 4> _socks;
    AES128Key _expandedCDCKey;
    // The requests we've enqueued, but haven't completed yet, with
    // where to send the response. Indexed by txn id.
    std::unordered_map<uint64_t, InFlightCDCRequest> _inFlightTxns;
    // The _shard_ request we're currently waiting for, if any.
    std::optional<InFlightShardRequest> _inFlightShardReq;

public:
    CDCServer(Logger& logger, const CDCOptions& options, CDCShared& shared) :
        _env(logger, "req_server"),
        _shared(shared),
        _ipPorts(options.ipPorts),
        _recvBuf(UDP_MTU),
        _sendBuf(UDP_MTU),
        _shardRequestIdCounter(0)
    {
        _currentLogIndex = _shared.db.lastAppliedLogEntry();
        memset(&_socks[0], 0, sizeof(_socks));
        expandKey(CDCKey, _expandedCDCKey);
    }

    virtual ~CDCServer() = default;

    virtual void terminate() override {
        _env.flush();
        _shared.stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        _waitForShards();

        // Create sockets. We create one socket for listening to client requests and one for listening
        // the the shard's responses. If we have two IPs we do this twice.
        for (int i = 0; i < _socks.size(); i++) {
            if (i > 1 && _ipPorts[1].ip == 0) { // we don't have a second IP
                _socks[i] = -1;
                continue;
            }
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            _socks[i] = sock;
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
            }
        }

        // create epoll structure
        // TODO I did this when I had more sockets, we could just use select now that it's 4
        // of them...
        int epoll = epoll_create1(0);
        if (epoll < 0) {
            throw SYSCALL_EXCEPTION("epoll");
        }
        struct epoll_event events[_socks.size()];
        for (int i = 0; i < _socks.size(); i++) {
            auto& event = events[i];
            event.data.u64 = i;
            event.events = EPOLLIN | EPOLLET;
            if (epoll_ctl(epoll, EPOLL_CTL_ADD, _socks[i], &event) == -1) {
                throw SYSCALL_EXCEPTION("epoll_ctl");
            }
        }

        LOG_INFO(_env, "running on ports %s and %s", _shared.ownPorts[0].load(), _shared.ownPorts[1].load());

        // If we've got a dangling transaction, immediately start processing it
        _shared.db.startNextTransaction(true, eggsNow(), _advanceLogIndex(), _step);
        _processStep(_step);

        // Start processing CDC requests and shard responses
        for (;;) {
            if (_shared.stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }

            // timeout after 100ms
            if (_inFlightShardReq && (eggsNow() - _inFlightShardReq->sentAt) > 100_ms) {
                _inFlightShardReq.reset();
                _handleShardError(_inFlightShardReq->shid, EggsError::TIMEOUT);
            }

            // 1ms timeout for prompt termination and for shard resps timeouts
            int nfds = epoll_wait(epoll, events, _socks.size(), 1 /*milliseconds*/);
            if (nfds < 0) {
                throw SYSCALL_EXCEPTION("epoll_wait");
            }

            for (int i = 0; i < nfds; i++) {
                const auto& event = events[i];
                if (event.data.u64%2 == 0) {
                    _drainCDCSock(_socks[event.data.u64]);
                } else {
                    _drainShardSock(_socks[event.data.u64]);
                }
            }
        }

        _shared.db.close();
    }

private:
    void _waitForShards() {
        LOG_INFO(_env, "Waiting for shard info to be filled in");
        EggsTime t0 = eggsNow();
        Duration maxWait = 1_mins;
        for (;;) {
            if (_shared.stop.load()) {
                return;
            }
            if (eggsNow() - t0 > maxWait) {
                throw EGGS_EXCEPTION("could not reach shuckle to get shards after %s, giving up", maxWait);
            }

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
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            LOG_INFO(_env, "shards found, proceeding");
            return;
        }
    }

    void _drainCDCSock(int sock) {
        struct sockaddr_in clientAddr;

        for (;;) {
            // Read one request
            memset(&clientAddr, 0, sizeof(clientAddr));
            socklen_t addrLen = sizeof(clientAddr);
            ssize_t read = recvfrom(sock, &_recvBuf[0], _recvBuf.size(), 0, (struct sockaddr*)&clientAddr, &addrLen);
            if (read < 0 && errno == EAGAIN) {
                return;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recvfrom");
            }
            LOG_DEBUG(_env, "received CDC request from %s", clientAddr);

            BincodeBuf reqBbuf(&_recvBuf[0], read);
            
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

            // If this will be filled in with an actual code, it means that we couldn't process
            // the request.
            EggsError err = NO_ERROR;

            // Now, try to parse the body
            try {
                _cdcReqContainer.unpack(reqBbuf, reqHeader.kind);
                LOG_TRACE(_env, "parsed request: %s", _cdcReqContainer);
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
                // If things went well, process the request
                LOG_DEBUG(_env, "CDC request %s successfully parsed, will now process", _cdcReqContainer.kind());
                uint64_t txnId = _shared.db.processCDCReq(true, eggsNow(), _advanceLogIndex(), _cdcReqContainer, _step);
                auto& inFlight = _inFlightTxns[txnId];
                inFlight.cdcRequestId = reqHeader.requestId;
                inFlight.clientAddr = clientAddr;
                inFlight.kind = reqHeader.kind;
                inFlight.sock = sock;
                // Go forward
                _processStep(_step);
            } else {
                // Otherwise we can immediately reply with an error
                RAISE_ALERT(_env, "request %s failed before enqueue with error %s", _cdcReqContainer.kind(), err);
                _sendError(sock, reqHeader.requestId, err, clientAddr);
            }
        }
    }

    void _drainShardSock(int sock) {
        for (;;) {
            ssize_t read = recv(sock, &_recvBuf[0], _recvBuf.size(), 0);
            if (read < 0 && errno == EAGAIN) {
                return;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recv");
            }
        
            LOG_DEBUG(_env, "received response from shard");
    
            BincodeBuf reqBbuf(&_recvBuf[0], read);

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

            // If it's not the request we wanted, skip
            if (!_inFlightShardReq) {
                LOG_INFO(_env, "got unexpected shard request id %s, kind %s, from shard, dropping", respHeader.requestId, respHeader.kind);
                continue;
            }
            if (_inFlightShardReq->shardRequestId != respHeader.requestId) {
                LOG_INFO(_env, "got unexpected shard request id %s (expected %s), kind %s, from shard %s, dropping", respHeader.requestId, _inFlightShardReq->shardRequestId, respHeader.kind, _inFlightShardReq->shid);
                continue;
            }
            uint64_t txnId = _inFlightShardReq->txnId;

            // We can forget about this, we're going to process it right now
            _inFlightShardReq.reset();

            // We got an error
            if (respHeader.kind == (ShardMessageKind)0) {
                EggsError err = reqBbuf.unpackScalar<EggsError>();
                _handleShardError(_inFlightShardReq->shid, err);
                continue;
            }

            // Otherwise, parse the body
            _shardRespContainer.unpack(reqBbuf, respHeader.kind);
            LOG_TRACE(_env, "parsed shard response: %s", _shardRespContainer);
            ALWAYS_ASSERT(reqBbuf.remaining() == 0);

            // If all went well, advance with the newly received request
            LOG_DEBUG(_env, "successfully parsed shard response %s with kind %s, will now process", respHeader.requestId, respHeader.kind);
            _shared.db.processShardResp(true, eggsNow(), _advanceLogIndex(), NO_ERROR, &_shardRespContainer, _step);
            _processStep(_step);
        }
    }

    void _handleShardError(ShardId shid, EggsError err) {
        RAISE_ALERT(_env, "got shard error %s from shard %s", err, shid);
        _shared.db.processShardResp(true, eggsNow(), _advanceLogIndex(), err, nullptr, _step);
        _processStep(_step);
    }

    void _processStep(const CDCStep& step) {
        LOG_DEBUG(_env, "processing step %s", step);
        if (step.txnFinished != 0) {
            LOG_DEBUG(_env, "txn %s finished", step.txnFinished);
            // we need to send the response back to the client
            auto inFlight = _inFlightTxns.find(step.txnFinished);
            if (inFlight == _inFlightTxns.end()) {
                RAISE_ALERT(_env, "Could not find in-flight request %s, this might be because the CDC was restarted in the middle of a transaction.", step.txnFinished);
            } else {
                if (step.err != NO_ERROR) {
                    RAISE_ALERT(_env, "txn %s, req id %s, finished with error %s", step.txnFinished, inFlight->second.cdcRequestId, step.err);
                    _sendError(inFlight->second.sock, inFlight->second.cdcRequestId, step.err, inFlight->second.clientAddr);
                } else {
                    LOG_DEBUG(_env, "sending response with req id %s, kind %s, back to %s", inFlight->second.cdcRequestId, inFlight->second.kind, inFlight->second.clientAddr);
                    BincodeBuf bbuf(&_sendBuf[0], _sendBuf.size());
                    CDCResponseHeader respHeader(inFlight->second.cdcRequestId, inFlight->second.kind);
                    respHeader.pack(bbuf);
                    step.resp.pack(bbuf);
                    _send(inFlight->second.sock, inFlight->second.clientAddr, (const char*)bbuf.data, bbuf.len());
                }
                _inFlightTxns.erase(inFlight);
            }
        }
        if (step.txnNeedsShard != 0) {
            LOG_TRACE(_env, "txn %s needs shard %s, req %s", step.txnNeedsShard, step.shardReq.shid, step.shardReq.req);
            BincodeBuf bbuf(&_sendBuf[0], _sendBuf.size());
            // Header
            ShardRequestHeader shardReqHeader;
            shardReqHeader.requestId = _shardRequestIdCounter;
            _shardRequestIdCounter++;
            shardReqHeader.kind = step.shardReq.req.kind();
            shardReqHeader.pack(bbuf);
            // Body
            step.shardReq.req.pack(bbuf);
            // MAC, if necessary
            if (isPrivilegedRequestKind(shardReqHeader.kind)) {
                bbuf.packFixedBytes<8>({cbcmac(_expandedCDCKey, bbuf.data, bbuf.len())});
            }
            // Send
            struct sockaddr_in shardAddr;
            memset(&shardAddr, 0, sizeof(shardAddr));
            _shared.shardsMutex.lock();
            ShardInfo shardInfo = _shared.shards[step.shardReq.shid.u8];
            _shared.shardsMutex.unlock();
            auto now = eggsNow(); // randomly pick one of the shard addrs and one of our sockets
            int whichShardAddr = now.ns & !!shardInfo.port2;
            int whichSock = (now.ns>>1) & !!_ipPorts[1].ip;
            shardAddr.sin_family = AF_INET;
            shardAddr.sin_port = htons(whichShardAddr ? shardInfo.port1 : shardInfo.port2);
            static_assert(sizeof(shardAddr.sin_addr) == sizeof(shardInfo.ip1));
            memcpy(&shardAddr.sin_addr, (whichShardAddr ? shardInfo.ip1 : shardInfo.ip2).data.data(), sizeof(shardAddr.sin_addr));
            LOG_DEBUG(_env, "sending request with req id %s to shard %s (%s)", shardReqHeader.requestId, step.shardReq.shid, shardAddr);
            _send(_socks[whichSock*2 + 1], shardAddr, (const char*)bbuf.data, bbuf.len());
            // Record the in-flight req
            ALWAYS_ASSERT(!_inFlightShardReq);
            auto& inFlight = _inFlightShardReq.emplace();
            inFlight.shardRequestId = shardReqHeader.requestId;
            inFlight.sentAt = now;
            inFlight.txnId = step.txnNeedsShard;
            inFlight.shid = step.shardReq.shid;
        }
        if (step.nextTxn != 0) {
            LOG_DEBUG(_env, "we have txn %s lined up, starting it", step.nextTxn);
            _shared.db.startNextTransaction(true, eggsNow(), _advanceLogIndex(), _step);
            _processStep(_step);
        }
    }
    
    void _sendError(int sock, uint64_t requestId, EggsError err, struct sockaddr_in& clientAddr) {
        BincodeBuf respBbuf(&_sendBuf[0], _sendBuf.size());
        CDCResponseHeader(requestId, CDCMessageKind::ERROR).pack(respBbuf);
        respBbuf.packScalar<uint16_t>((uint16_t)err);
        // We're sending an error back to a user, so always send it from the CDC sock.
        _send(sock, clientAddr, (const char*)respBbuf.data, respBbuf.len());
        LOG_DEBUG(_env, "sent error %s to %s", err, clientAddr);
    }

    void _send(int sock, struct sockaddr_in& dest, const char* data, size_t len) {
        // TODO we might very well get EAGAIN here since these are non-blocking sockets.
        // We should probably come up with a better strategy regarding writing stuff out,
        // but, being a bit lazy for now.
        if (sendto(sock, data, len, 0, (struct sockaddr*)&dest, sizeof(dest)) != len) {
            throw SYSCALL_EXCEPTION("sendto");
        }
    }

    uint64_t _advanceLogIndex() {
        return ++_currentLogIndex;
    }
};

static void* runCDCServer(void* server) {
    ((CDCServer*)server)->run();
    return nullptr;
}

struct CDCShardUpdater : Undertaker::Reapable {
    Env _env;
    CDCShared& _shared;
    std::string _shuckleHost;
    uint16_t _shucklePort;
public:
    CDCShardUpdater(Logger& logger, const CDCOptions& options, CDCShared& shared):
        _env(logger, "shard_updater"),
        _shared(shared),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual ~CDCShardUpdater() = default;

    virtual void terminate() override {
        _env.flush();
        _shared.stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        EggsTime successfulIterationAt = 0;
        auto shards = std::make_unique<std::array<ShardInfo, 256>>();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (_shared.stop.load()) {
                return;
            }
            auto now = eggsNow();
            if (now - successfulIterationAt < 1_mins) {
                continue;
            }
            LOG_INFO(_env, "Last successful shard fetch was at %s, now we're at %s, fetching again", successfulIterationAt, now);
            std::string err = fetchShards(_shuckleHost, _shucklePort, 100_ms, *shards);
            if (!err.empty()) {
                LOG_INFO(_env, "failed to reach shuckle at %s:%s to fetch shards, will retry: %s", _shuckleHost, _shucklePort, err);
                EggsTime successfulIterationAt = 0;
                continue;
            }
            bool badShard = false;
            for (int i = 0; i < shards->size(); i++) {
                if (shards->at(i).port1 == 0) {
                    badShard = true;
                    break;
                }
            }
            if (badShard) {
                EggsTime successfulIterationAt = 0;
                continue;
            }
            {
                const std::lock_guard<std::mutex> lock(_shared.shardsMutex);
                for (int i = 0; i < shards->size(); i++) {
                    _shared.shards[i] = shards->at(i);
                }
            }
            LOG_INFO(_env, "successfully fetched all shards from shuckle, will wait one minute");
            successfulIterationAt = eggsNow();
        }
    }
};

static void* runCDCShardUpdater(void* server) {
    ((CDCShardUpdater*)server)->run();
    return nullptr;
}

struct CDCRegisterer : Undertaker::Reapable {
    Env _env;
    CDCShared& _shared;
    uint32_t _ownIp1;
    uint32_t _ownIp2;
    std::string _shuckleHost;
    uint16_t _shucklePort;
    bool _hasSecondIp;
public:
    CDCRegisterer(Logger& logger, const CDCOptions& options, CDCShared& shared):
        _env(logger, "registerer"),
        _shared(shared),
        _ownIp1(options.ipPorts[0].ip),
        _ownIp2(options.ipPorts[1].ip),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _hasSecondIp(options.ipPorts[1].ip != 0)
    {}

    virtual ~CDCRegisterer() = default;

    virtual void terminate() override {
        _env.flush();
        _shared.stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        EggsTime successfulIterationAt = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (_shared.stop.load()) {
                return;
            }
            if (eggsNow() - successfulIterationAt < 1_mins) {
                continue;                
            }
            uint16_t port1 = _shared.ownPorts[0].load();
            uint16_t port2 = _shared.ownPorts[1].load();
            if (port1 == 0 || (_hasSecondIp && port2 == 0)) {
                // server isn't up yet
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            LOG_INFO(_env, "Registering ourselves (CDC, ports %s %s) with shuckle", port1, port2);
            CDCStatus status;
            _shared.db.status(status);
            std::string err = registerCDC(_shuckleHost, _shucklePort, 100_ms, _ownIp1, port1, _ownIp2, port2, status);
            if (!err.empty()) {
                RAISE_ALERT(_env, "Couldn't register ourselves with shuckle: %s", err);
                EggsTime successfulIterationAt = 0;
                continue;
            }
            LOG_INFO(_env, "Successfully registered with shuckle, will register again in one minute");
            successfulIterationAt = eggsNow();
        }
    }
};

static void* runCDCRegisterer(void* server) {
    ((CDCRegisterer*)server)->run();
    return nullptr;
}

void runCDC(const std::string& dbDir, const CDCOptions& options) {
    auto undertaker = Undertaker::acquireUndertaker();

    std::ostream* logOut = &std::cout;
    std::ofstream fileOut;
    if (!options.logFile.empty()) {
        fileOut = std::ofstream(options.logFile, std::ios::out | std::ios::app);
        if (!fileOut.is_open()) {
            throw EGGS_EXCEPTION("Could not open log file `%s'\n", options.logFile);
        }
        logOut = &fileOut;
    }
    Logger logger(options.logLevel, *logOut, options.syslog, true);

    {
        Env env(logger, "startup");
        LOG_INFO(env, "Running CDC with options:");
        LOG_INFO(env, "  level = %s", options.logLevel);
        LOG_INFO(env, "  logFile = '%s'", options.logFile);
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
    }

    CDCDB db(logger, dbDir);
    auto shared = std::make_unique<CDCShared>(db);

    {
        auto server = std::make_unique<CDCServer>(logger, options, *shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runCDCServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "server");
    }

    {
        auto server = std::make_unique<CDCShardUpdater>(logger, options, *shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runCDCShardUpdater, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "shard_updater");
    }

    {
        auto server = std::make_unique<CDCRegisterer>(logger, options, *shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runCDCRegisterer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "registerer");
    }

    undertaker->reap();
}