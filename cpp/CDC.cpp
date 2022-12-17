#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <sys/epoll.h>
#include <fcntl.h>
#include <optional>
#include <unordered_map>
#include <arpa/inet.h>

#include "Bincode.hpp"
#include "CDC.hpp"
#include "CDCDB.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Time.hpp"
#include "Undertaker.hpp"
#include "CDCDB.hpp"
#include "Crypto.hpp"
#include "CDCKey.hpp"

static uint16_t CDC_PORT = 36137;

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
};

struct CDCServer : Undertaker::Reapable {
private:
    Env _env;
    std::atomic<bool> _stop;
    CDCDB& _db;
    uint64_t _currentLogIndex;
    std::vector<char> _recvBuf;
    std::vector<char> _sendBuf;
    CDCReqContainer _cdcReqContainer;
    ShardRespContainer _shardRespContainer;
    CDCStep _step;
    std::array<int, 257> _socks;
    // The requests we've enqueued, but haven't completed yet, with
    // where to send the response. Indexed by txn id.
    std::unordered_map<uint64_t, InFlightCDCRequest> _inFlightTxns;
    // The _shard_ request we're currently waiting for, if any.
    std::optional<InFlightShardRequest> _inFlightShardReq;

public:
    CDCServer(Logger& logger, CDCDB& db) :
        _env(logger, "req_server"),
        _stop(false),
        _db(db),
        _recvBuf(UDP_MTU),
        _sendBuf(UDP_MTU)
    {
        _currentLogIndex = _db.lastAppliedLogEntry();
        memset(&_socks[0], 0, sizeof(_socks));
    }

    virtual ~CDCServer() = default;

    virtual void terminate() override {
        _env.flush();
        _stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        // Create sockets
        // First sock: the CDC sock
        // Next 256 socks: the socks we use to communicate with the shards
        for (int i = 0; i < _socks.size(); i++) {
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
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (i == 0) { // CDC
                addr.sin_port = htons(CDC_PORT);
            } else { // just to communicate with the shard
                addr.sin_port = 0;
            }
            if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
                if (i == 0) {
                    throw SYSCALL_EXCEPTION("cannot bind socket to port %s", CDC_PORT);
                } else {
                    throw SYSCALL_EXCEPTION("cannot bind socket");
                }
            }
            {
                socklen_t addrLen = sizeof(addr);
                if (getsockname(sock, (struct sockaddr*)&addr, &addrLen) < 0) {
                    throw SYSCALL_EXCEPTION("getsockname");
                }
            }
            if (i == 0) {
                LOG_DEBUG(_env, "bound CDC sock to port %s", ntohs(addr.sin_port));
            } else {
                LOG_DEBUG(_env, "bound shard %s sock to port %s", i-1, ntohs(addr.sin_port));
            }
        }

        // create epoll structure
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

        LOG_INFO(_env, "running on port %s", CDC_PORT);

        // If we've got a dangling transaction, immediately start processing it
        _db.startNextTransaction(true, eggsNow(), _advanceLogIndex(), _step);
        _processStep(_step);

        // Start processing CDC requests and shard responses
        for (;;) {
            if (_stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }

            // timeout after 5 secs
            if (_inFlightShardReq && (eggsNow() - _inFlightShardReq->sentAt) > 5'000'000'000ull) {
                _handleShardError(_inFlightShardReq->shid, EggsError::TIMEOUT);
                _inFlightShardReq.reset();
            }

            // 10ms timeout for prompt termination and for timeouts
            int nfds = epoll_wait(epoll, events, _socks.size(), 10 /* milliseconds */);
            if (nfds < 0) {
                throw SYSCALL_EXCEPTION("epoll_wait");
            }

            for (int i = 0; i < nfds; i++) {
                const auto& event = events[i];
                if (event.data.u64 == 0) {
                    _drainCDCSock();
                } else {
                    _drainShardSock(ShardId(event.data.u64-1));
                }
            }
        }

        _db.close();
    }

private:
    void _drainCDCSock() {
        int sock = _socks[0];

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
            LOG_DEBUG(_env, "received message from %s", clientAddr);

            BincodeBuf reqBbuf(&_recvBuf[0], read);
            
            // First, try to parse the header
            CDCRequestHeader reqHeader;
            try {
                reqHeader.unpack(reqBbuf);
            } catch (BincodeException err) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", err.what(), err.getStackTrace());
                RAISE_ALERT(_env, "could not parse request header from %s, dropping it.", clientAddr);
                continue;
            }

            LOG_DEBUG(_env, "received request id %s, kind %s, from %s", reqHeader.requestId, reqHeader.kind, clientAddr);

            // If this will be filled in with an actual code, it means that we couldn't process
            // the request.
            EggsError err = NO_ERROR;

            // Now, try to parse the body
            try {
                _cdcReqContainer.unpack(reqBbuf, reqHeader.kind);
                LOG_DEBUG(_env, "parsed request: %s", _cdcReqContainer);
            } catch (BincodeException exc) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", exc.what(), exc.getStackTrace());
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
                LOG_DEBUG(_env, "cdc request %s successfully parsed, will now process", _cdcReqContainer.kind());
                uint64_t txnId = _db.processCDCReq(true, eggsNow(), _advanceLogIndex(), _cdcReqContainer, _step);
                auto& inFlight = _inFlightTxns[txnId];
                inFlight.cdcRequestId = reqHeader.requestId;
                inFlight.clientAddr = clientAddr;
                inFlight.kind = reqHeader.kind;
                // Go forward
                _processStep(_step);
            } else {
                // Otherwise we can immediately reply with an error
                RAISE_ALERT(_env, "request %s failed before enqueue with error %s", _cdcReqContainer.kind(), err);
                _sendError(reqHeader.requestId, err, clientAddr);
            }
        }
    }

    void _drainShardSock(ShardId shid) {
        for (;;) {
            int sock = _socks[(int)shid.u8 + 1];
            ssize_t read = recv(sock, &_recvBuf[0], _recvBuf.size(), 0);
            if (read < 0 && errno == EAGAIN) {
                return;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recv");
            }
        
            LOG_DEBUG(_env, "received response from shard %s", shid);

            BincodeBuf reqBbuf(&_recvBuf[0], read);

            ShardResponseHeader respHeader;
            try {
                respHeader.unpack(reqBbuf);
            } catch (BincodeException err) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", err.what(), err.getStackTrace());
                RAISE_ALERT(_env, "could not parse response header from shard %s, dropping it.", shid);
                continue;
            }

            // Note that below we just let the BincodeExceptions propagate upwards since we
            // control all the code in this codebase, and the header is good, and we're a
            // bit lazy.

            // If it's not the request we wanted, skip
            if (!_inFlightShardReq) {
                LOG_INFO(_env, "got unexpected shard request id %s, kind %s, from shard %s, dropping", respHeader.requestId, respHeader.kind, shid);
                continue;
            }
            if (_inFlightShardReq->shardRequestId != respHeader.requestId) {
                LOG_INFO(_env, "got unexpected shard request id %s (expected %s), kind %s, from shard %s, dropping", respHeader.requestId, _inFlightShardReq->shardRequestId, respHeader.kind, shid);
                continue;
            }
            uint64_t txnId = _inFlightShardReq->txnId;

            // We can forget about this, we're going to process it right now
            _inFlightShardReq.reset();

            // We got an error
            if (respHeader.kind == (ShardMessageKind)0) {
                EggsError err = reqBbuf.unpackScalar<EggsError>();
                _handleShardError(shid, err);
                continue;
            }

            // Otherwise, parse the body
            _shardRespContainer.unpack(reqBbuf, respHeader.kind);
            LOG_DEBUG(_env, "parsed shard response: %s", _shardRespContainer);
            ALWAYS_ASSERT(reqBbuf.remaining() == 0);

            // If all went well, advance with the newly received request
            LOG_DEBUG(_env, "successfully parsed shard response %s with kind %s, will now process: %s", respHeader.requestId, respHeader.kind, _shardRespContainer);
            _db.processShardResp(true, eggsNow(), _advanceLogIndex(), NO_ERROR, &_shardRespContainer, _step);
            _processStep(_step);
        }
    }

    void _handleShardError(ShardId shid, EggsError err) {
        RAISE_ALERT(_env, "got shard error %s from shard %s", err, shid);
        _db.processShardResp(true, eggsNow(), _advanceLogIndex(), err, nullptr, _step);
        _processStep(_step);
    }

    void _processStep(const CDCStep& step) {
        LOG_DEBUG(_env, "processing step %s", step);
        if (step.txnFinished != 0) {
            LOG_DEBUG(_env, "txn %s finished, err %s, resp %s", step.txnFinished, step.err, step.resp);
            // we need to send the response back to the client
            auto inFlight = _inFlightTxns.find(step.txnFinished);
            if (inFlight == _inFlightTxns.end()) {
                RAISE_ALERT(_env, "Could not find in-flight request %s, this might be because the CDC was restarted in the middle of a transaction.", step.txnFinished);
            } else {
                if (step.err != NO_ERROR) {
                    RAISE_ALERT(_env, "txn %s, req id %s, finished with error %s", step.txnFinished, inFlight->second.cdcRequestId, step.err);
                    _sendError(inFlight->second.cdcRequestId, step.err, inFlight->second.clientAddr);
                } else {
                    LOG_DEBUG(_env, "sending response back to %s", inFlight->second.clientAddr);
                    BincodeBuf bbuf(&_sendBuf[0], _sendBuf.size());
                    CDCResponseHeader respHeader(inFlight->second.cdcRequestId, inFlight->second.kind);
                    respHeader.pack(bbuf);
                    step.resp.pack(bbuf);
                    _send(_socks[0], inFlight->second.clientAddr, (const char*)bbuf.data, bbuf.len());
                }
                _inFlightTxns.erase(inFlight);
            }
        }
        if (step.txnNeedsShard != 0) {
            LOG_DEBUG(_env, "txn %s needs shard %s, req %s", step.txnNeedsShard, step.shardReq.shid, step.shardReq.req);
            BincodeBuf bbuf(&_sendBuf[0], _sendBuf.size());
            auto t = eggsNow();
            // Header
            ShardRequestHeader shardReqHeader;
            shardReqHeader.requestId = t.ns;
            shardReqHeader.kind = step.shardReq.req.kind();
            shardReqHeader.pack(bbuf);
            // Body
            step.shardReq.req.pack(bbuf);
            // MAC, if necessary
            if (isPrivilegedRequestKind(shardReqHeader.kind)) {
                bbuf.packFixedBytes<8>({cbcmac(CDCKey, bbuf.data, bbuf.len())});
            }
            // Send
            struct sockaddr_in shardAddr;
            memset(&shardAddr, 0, sizeof(shardAddr));
            shardAddr.sin_family = AF_INET;
            shardAddr.sin_port = htons(step.shardReq.shid.port());
            inet_aton("127.0.0.1", &shardAddr.sin_addr);
            _send(_socks[(int)step.shardReq.shid.u8 + 1], shardAddr, (const char*)bbuf.data, bbuf.len());
            // Record the in-flight req
            ALWAYS_ASSERT(!_inFlightShardReq);
            auto& inFlight = _inFlightShardReq.emplace();
            inFlight.shardRequestId = shardReqHeader.requestId;
            inFlight.sentAt = t;
            inFlight.txnId = step.txnNeedsShard;
            inFlight.shid = step.shardReq.shid;
        }
        if (step.nextTxn != 0) {
            LOG_DEBUG(_env, "we have txn %s lined up, starting it", step.nextTxn);
            _db.startNextTransaction(true, eggsNow(), _advanceLogIndex(), _step);
            _processStep(_step);
        }
    }
    
    void _sendError(uint64_t requestId, EggsError err, struct sockaddr_in& clientAddr) {
        BincodeBuf respBbuf(&_sendBuf[0], _sendBuf.size());
        CDCResponseHeader(requestId, CDCMessageKind::ERROR).pack(respBbuf);
        respBbuf.packScalar<uint16_t>((uint16_t)err);
        // We're sending an error back to a user, so always send it from the CDC sock.
        _send(_socks[0], clientAddr, (const char*)respBbuf.data, respBbuf.len());
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
    Logger logger(options.level, *logOut);

    CDCDB db(logger, dbDir);

    {
        auto server = std::make_unique<CDCServer>(logger, db);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runCDCServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "server");
    }

    undertaker->reap();
}