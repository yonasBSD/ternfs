#include <fstream>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <atomic>
#include <sys/epoll.h>
#include <fcntl.h>

#include "Bincode.hpp"
#include "CDC.hpp"
#include "CDCDB.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Undertaker.hpp"
#include "CDCDB.hpp"

static uint16_t CDC_PORT = 36137;

struct Server : Undertaker::Reapable {
private:
    Env _env;
    std::atomic<bool> _stop;
    CDCDB& _db;
    uint64_t _logIndex;
    std::vector<char> _recvBuf;
    std::vector<char> _sendBuf;
    std::unique_ptr<CDCReqContainer> _cdcReqContainer;
    BincodeBytesScratchpad _processScratch;
    std::unique_ptr<CDCStep> _step;

    void _handleCDCReq(int sock) {
        struct sockaddr_in clientAddr;
        CDCReqDestination dest;

        for (;;) {
            // Read one request
            memset(&clientAddr, 0, sizeof(clientAddr));
            socklen_t addrLen = sizeof(clientAddr);
            int read = recvfrom(sock, &_recvBuf[0], _recvBuf.size(), 0, (struct sockaddr*)&clientAddr, &addrLen);
            if (read < 0 && errno == EAGAIN) {
                return;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recvfrom");
            }
            LOG_DEBUG(_env, "received message from %s", clientAddr);

            memcpy(&dest.destIp, &clientAddr.sin_addr, sizeof(clientAddr.sin_addr));
            dest.destPort = clientAddr.sin_port;

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
                _cdcReqContainer->unpack(reqBbuf, reqHeader.kind);
                LOG_DEBUG(_env, "parsed request: %s", *_cdcReqContainer);
            } catch (BincodeException exc) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", exc.what(), exc.getStackTrace());
                RAISE_ALERT(_env, "could not parse CDC request of kind %s from %s, will reply with error.", reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            // Make sure nothing is left
            if (reqBbuf.remaining() != 0) {
                RAISE_ALERT(_env, "%s bytes remaining after parsing CDC request of kind %s from %s, will reply with error", reqBbuf.remaining(), reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            // Actually process the request
            if (err == NO_ERROR) {
                err = _db.processCDCReq(eggsNow(), _logIndex, dest, *_cdcReqContainer, _processScratch, *_step);
            }

            // If all went well, process the step, otherwise, respond immediately.
            if (err == NO_ERROR) {
                LOG_DEBUG(_env, "successfully processed request %s with kind %s: %s", reqHeader.requestId, _cdcReqContainer->kind(), *_step);
                _handleStep(*_step);
            } else {
                LOG_DEBUG(_env, "request %s failed with error %s", _cdcReqContainer->kind(), err);
                _sendError(sock, reqHeader.requestId, err, clientAddr);
            }
        }
    }

    void _handleShardResp(ShardId shid, int sock) {
        throw EGGS_EXCEPTION("UNIMPLEMENTED");
    }

    void _handleStep(const CDCStep& step) {
        return; // TODO
    }
    
    void _sendError(int sock, uint64_t requestId, EggsError err, struct sockaddr_in& clientAddr) {
        BincodeBuf respBbuf(&_sendBuf[0], _sendBuf.size());
        CDCResponseHeader(requestId, CDCMessageKind::ERROR).pack(respBbuf);
        respBbuf.packScalar<uint16_t>((uint16_t)err);
        _send(sock, clientAddr, (const char*)respBbuf.data, respBbuf.len());
        LOG_DEBUG(_env, "sent error %s to %s", err, clientAddr);
    }

    void _send(int sock, struct sockaddr_in& clientAddr, const char* data, size_t len) {
        if (sendto(sock, data, len, 0, (struct sockaddr*)&clientAddr, sizeof(clientAddr)) != len) {
            throw SYSCALL_EXCEPTION("sendto");
        }
    }
public:
    Server(Logger& logger, CDCDB& db):
        _env(logger, "req_server"),
        _stop(false),
        _db(db),
        _recvBuf(UDP_MTU),
        _sendBuf(UDP_MTU)
    {}

    virtual ~Server() = default;

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
        std::array<int, 257> socks;
        for (int i = 0; i < socks.size(); i++) {
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                throw SYSCALL_EXCEPTION("cannot create socket");
            }
            if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
                throw SYSCALL_EXCEPTION("fcntl");
            }
            if (i == 0) {
                struct sockaddr_in serverAddr;
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                serverAddr.sin_port = htons(CDC_PORT);
                if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
                    throw SYSCALL_EXCEPTION("cannot bind socket to port %s", CDC_PORT);
                }
            }
        }

        // create epoll structure
        int epoll = epoll_create1(0);
        if (epoll < 0) {
            throw SYSCALL_EXCEPTION("epoll");
        }
        struct epoll_event events[socks.size()];
        for (int i = 0; i < socks.size(); i++) {
            auto& event = events[i];
            event.data.u64 = i;
            event.events = EPOLLIN | EPOLLET;
            if (epoll_ctl(epoll, EPOLL_CTL_ADD, socks[i], &event) == -1) {
                throw SYSCALL_EXCEPTION("epoll_ctl");
            }
        }

        // Start processing CDC requests and shard responses
        for (;;) {
            if (_stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }

            // 10ms timeout for prompt termination
            int nfds = epoll_wait(epoll, events, socks.size(), 10 /* milliseconds */);
            if (nfds < 0) {
                throw SYSCALL_EXCEPTION("epoll_wait");
            }

            for (int i = 0; i < nfds; i++) {
                const auto& event = events[i];
                if (event.data.u64 == 0) {
                    _handleCDCReq(socks[0]);
                } else {
                    _handleShardResp(ShardId(i-1), socks[i]);
                }
            }
        }

        _db.close();
    }
};

static void* runServer(void* server) {
    ((Server*)server)->run();
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
        auto server = std::make_unique<Server>(logger, db);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "server");
    }

    undertaker->reap();
}