#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <thread>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "Crypto.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Shard.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"
#include "CDCKey.hpp"
#include "Shuckle.hpp"
#include "Time.hpp"
#include "Undertaker.hpp"
#include "splitmix64.hpp"

// Data needed to synchronize between the different threads
struct ShardShared {
private:
    uint64_t _currentLogIndex;
    std::mutex _applyLock;
public:
    ShardDB& db;
    std::atomic<bool> gotBlockServices;

    ShardShared() = delete;
    ShardShared(ShardDB& db_): db(db_), gotBlockServices(false) {
        _currentLogIndex = db.lastAppliedLogEntry();
    }

    EggsError applyLogEntry(const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        std::lock_guard<std::mutex> lock(_applyLock);
        auto res = db.applyLogEntry(true, _currentLogIndex+1, logEntry, resp);
        _currentLogIndex++;
        return res;
    }
};

struct ShardServer : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    ShardId _shid;
    uint16_t _port;
    std::atomic<bool> _stop;
    std::string _shuckleAddr;
    uint16_t _shucklePort;
    bool _waitForBlockServices;
    uint64_t _packetDropRand;
    uint64_t _incomingPacketDropProbability; // probability * 10,000
    uint64_t _outgoingPacketDropProbability; // probability * 10,000
public:
    ShardServer(Logger& logger, ShardShared& shared, ShardId shid, const ShardOptions& options):
        _env(logger, "server"),
        _shared(shared),
        _shid(shid),
        _port(options.port),
        _stop(false),
        _shuckleAddr(options.shuckleAddr),
        _shucklePort(options.shucklePort),
        _waitForBlockServices(options.waitForBlockServices),
        _packetDropRand((int)shid.u8 + 1), // CDC is 0
        _incomingPacketDropProbability(0),
        _outgoingPacketDropProbability(0)
    {
        auto convertProb = [this](const std::string& what, double prob, uint64_t& iprob) {
            if (prob != 0.0) {
                LOG_INFO(_env, "Will drop %s%% of %s packets", prob*100.0, what);
                iprob = prob * 10'000.0;
                ALWAYS_ASSERT(iprob > 0 && iprob < 10'000);
            }
        };
        convertProb("incoming", options.simulateIncomingPacketDrop, _incomingPacketDropProbability);
        convertProb("outgoing", options.simulateOutgoingPacketDrop, _outgoingPacketDropProbability);
    }

    virtual ~ShardServer() = default;

    virtual void terminate() override {
        _env.flush();
        _stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        AES128Key expandedCDCKey;
        expandKey(CDCKey, expandedCDCKey);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            throw SYSCALL_EXCEPTION("cannot create socket");
        }
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(0);
        if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
            throw SYSCALL_EXCEPTION("cannot bind socket to port %s", _port);
        }
        {
            socklen_t addrLen = sizeof(serverAddr);
            if (getsockname(sock, (struct sockaddr*)&serverAddr, &addrLen) < 0) {
                throw SYSCALL_EXCEPTION("getsockname");
            }
        }
        // 10ms timeout for prompt termination
        {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10'000;
            if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
                throw SYSCALL_EXCEPTION("setsockopt");
            }
        }

        _port = ntohs(serverAddr.sin_port);
        LOG_INFO(_env, "Bound shard %s to port %s", _shid, ntohs(serverAddr.sin_port));

        _registerWithShuckle();

        struct sockaddr_in clientAddr;
        std::vector<char> recvBuf(UDP_MTU);
        std::vector<char> sendBuf(UDP_MTU);
        auto reqContainer = std::make_unique<ShardReqContainer>();
        auto respContainer = std::make_unique<ShardRespContainer>();
        auto logEntry = std::make_unique<ShardLogEntry>();

        for (;;) {
            if (_stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }

            if (_waitForBlockServices && !_shared.gotBlockServices.load()) {
                LOG_DEBUG(_env, "need to wait for shuckle, waiting");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Read one request
            memset(&clientAddr, 0, sizeof(clientAddr));
            socklen_t addrLen = sizeof(clientAddr);
            int read = recvfrom(sock, recvBuf.data(), recvBuf.size(), 0, (struct sockaddr*)&clientAddr, &addrLen);
            if (read < 0 && errno == EAGAIN) {
                continue;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recvfrom");
            }
            LOG_DEBUG(_env, "received message from %s", clientAddr);

            BincodeBuf reqBbuf(recvBuf.data(), read);
            
            // First, try to parse the header
            ShardRequestHeader reqHeader;
            try {
                reqHeader.unpack(reqBbuf);
            } catch (BincodeException err) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", err.what(), err.getStackTrace());
                RAISE_ALERT(_env, "could not parse request header from %s, dropping it.", clientAddr);
                continue;
            }

            if (splitmix64(_packetDropRand) % 10'000 < _incomingPacketDropProbability) {
                LOG_DEBUG(_env, "artificially dropping request %s", reqHeader.requestId);
                continue;
            }

            LOG_DEBUG(_env, "received request id %s, kind %s, from %s", reqHeader.requestId, reqHeader.kind, clientAddr);

            // If this will be filled in with an actual code, it means that we couldn't process
            // the request.
            EggsError err = NO_ERROR;

            // Now, try to parse the body
            try {
                reqContainer->unpack(reqBbuf, reqHeader.kind);
                LOG_DEBUG(_env, "parsed request: %s", *reqContainer);
            } catch (BincodeException exc) {
                LOG_ERROR(_env, "%s\nstacktrace:\n%s", exc.what(), exc.getStackTrace());
                RAISE_ALERT(_env, "could not parse request of kind %s from %s, will reply with error.", reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            // authenticate, if necessary
            if (isPrivilegedRequestKind(reqHeader.kind)) {
                auto expectedMac = cbcmac(expandedCDCKey, reqBbuf.data, reqBbuf.cursor - reqBbuf.data);
                BincodeFixedBytes<8> receivedMac;
                reqBbuf.unpackFixedBytes<8>(receivedMac);
                if (expectedMac != receivedMac.data) {
                    err = EggsError::NOT_AUTHORISED;
                }
            }

            // Make sure nothing is left
            if (err == NO_ERROR && reqBbuf.remaining() != 0) {
                RAISE_ALERT(_env, "%s bytes remaining after parsing request of kind %s from %s, will reply with error", reqBbuf.remaining(), reqHeader.kind, clientAddr);
                err = EggsError::MALFORMED_REQUEST;
            }

            // Actually process the request
            auto t0 = eggsNow();
            if (err == NO_ERROR) {
                if (readOnlyShardReq(reqContainer->kind())) {
                    err = _shared.db.read(*reqContainer, *respContainer);
                } else {
                    err = _shared.db.prepareLogEntry(*reqContainer, *logEntry);
                    if (err == NO_ERROR) {
                        err = _shared.applyLogEntry(*logEntry, *respContainer);
                    }
                }
            }
            Duration elapsed = eggsNow() - t0;

            BincodeBuf respBbuf(sendBuf.data(), sendBuf.size());
            if (err == NO_ERROR) {
                LOG_DEBUG(_env, "successfully processed request %s with kind %s in %s: %s", reqHeader.requestId, respContainer->kind(), elapsed, *respContainer);
                ShardResponseHeader(reqHeader.requestId, respContainer->kind()).pack(respBbuf);
                respContainer->pack(respBbuf);
            } else {
                LOG_DEBUG(_env, "request %s failed with error %s in %s", reqContainer->kind(), err, elapsed);
                ShardResponseHeader(reqHeader.requestId, ShardMessageKind::ERROR).pack(respBbuf);
                respBbuf.packScalar<uint16_t>((uint16_t)err);
            }

            if (splitmix64(_packetDropRand) % 10'000 < _outgoingPacketDropProbability) {
                LOG_DEBUG(_env, "artificially dropping response %s", reqHeader.requestId);
                continue;
            }

            if (sendto(sock, respBbuf.data, respBbuf.len(), 0, (struct sockaddr*)&clientAddr, sizeof(clientAddr)) != respBbuf.len()) {
                throw SYSCALL_EXCEPTION("sendto");
            }
            LOG_DEBUG(_env, "sent response %s to %s", respContainer->kind(), clientAddr);
        }

        // If we're terminating gracefully we're the last ones, close the db nicely
        _shared.db.close();
    }

private:
    void _registerWithShuckle() {
        ALWAYS_ASSERT(_port != 0);
        for (;;) {
            if (_stop.load()) {
                return;
            }
            LOG_INFO(_env, "Registering ourselves (shard %s, port %s) with shuckle", _shid, _port);
            std::array<uint8_t, 4> addr{127,0,0,1};
            std::string err = registerShard(_shuckleAddr, _shucklePort, 100_ms, _shid, addr, _port);
            if (!err.empty()) {
                RAISE_ALERT(_env, "Couldn't register ourselves with shuckle: %s", err);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        LOG_INFO(_env, "Successfully registered with shuckle");
    }
};

static void* runShardServer(void* server) {
    ((ShardServer*)server)->run();
    return nullptr;
}

struct ShardShuckleUpdater : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    std::atomic<bool> _stop;
    ShardId _shid;
    std::string _shuckleAddr;
    uint16_t _shucklePort;
    bool _waitForShuckle;
public:
    ShardShuckleUpdater(Logger& logger, ShardShared& shared, ShardId shid, const ShardOptions& options):
        _env(logger, "shuckle_updater"),
        _shared(shared),
        _stop(false),
        _shid(shid),
        _shuckleAddr(options.shuckleAddr),
        _shucklePort(options.shucklePort),
        _waitForShuckle(options.waitForBlockServices)
    {}

    virtual ~ShardShuckleUpdater() = default;

    virtual void terminate() override {
        _env.flush();
        _stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        EggsTime t0 = eggsNow();
        EggsTime lastRequestT = 0;
        bool lastRequestSuccessful = false;
        bool reachedButEmpty = false;
        auto respContainer = std::make_unique<ShardRespContainer>();
        auto logEntry = std::make_unique<ShardLogEntry>();

        #define GO_TO_NEXT_ITERATION \
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); \
            continue; \

        for (;;) {
            if (_stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }

            if (_waitForShuckle && !_shared.gotBlockServices.load() && (eggsNow() - t0) > 5_sec) {
                // if we refuse to start the server and we haven't reached shuckle within 5 secs,
                // fail hard
                if (reachedButEmpty) {
                    throw EGGS_EXCEPTION("reached shuckle, but got no block services, and _waitForShuckle=true, terminating");
                } else {
                    throw EGGS_EXCEPTION("could not reach shuckle in time, and _waitForShuckle=true, terminating");
                }
            }

            EggsTime t = eggsNow();
            if (lastRequestSuccessful && (t - lastRequestT) < 1_mins) {
                // wait 60 secs before requesting again
                GO_TO_NEXT_ITERATION
            }
            if (!lastRequestSuccessful && (t - lastRequestT) < 100_ms) {
                // if the last request failed, wait at least 100ms before retrying
                GO_TO_NEXT_ITERATION
            }

            logEntry->time = eggsNow();
            LOG_INFO(_env, "about to perform shuckle requests to %s:%s", _shuckleAddr, _shucklePort);
            std::string err;

            err = fetchBlockServices(_shuckleAddr, _shucklePort, 100_ms, _shid, logEntry->body.setUpdateBlockServices());
            lastRequestSuccessful = err.empty();
            if (lastRequestSuccessful && logEntry->body.getUpdateBlockServices().blockServices.els.empty()) {
                lastRequestSuccessful = false;
                reachedButEmpty = true;
                err = "no block services";
            }
            lastRequestT = t;
            if (!lastRequestSuccessful) {
                RAISE_ALERT(_env, "could not reach shuckle: %s", err);
                GO_TO_NEXT_ITERATION
            }
            
            {
                EggsError err = _shared.applyLogEntry(*logEntry, *respContainer);
                if (err != NO_ERROR) {
                    RAISE_ALERT(_env, "unexpected failure when trying to update block services: %s", err);
                    lastRequestSuccessful = false;
                    GO_TO_NEXT_ITERATION
                }
            }

            _shared.gotBlockServices.store(true);
        }

        #undef GO_TO_NEXT_ITERATION
    }
};

static void* runShardShuckleUpdater(void* server) {
    ((ShardShuckleUpdater*)server)->run();
    return nullptr;
}

void runShard(ShardId shid, const std::string& dbDir, const ShardOptions& options) {
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

    ShardDB db(logger, shid, dbDir);

    ShardShared shared(db);

    {
        auto server = std::make_unique<ShardServer>(logger, shared, shid, options);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "shard");
    }

    {
        auto shuckleUpdater = std::make_unique<ShardShuckleUpdater>(logger, shared, shid, options);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardShuckleUpdater, &*shuckleUpdater) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shuckleUpdater), tid, "shuckle_updater");
    }

    undertaker->reap();
}