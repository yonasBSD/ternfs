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
#include "Time.hpp"

// Data needed to synchronize between the different threads
struct ShardShared {
private:
    uint64_t _currentLogIndex;
    std::mutex _applyLock;
public:
    ShardDB& db;
    std::atomic<bool> stop;
    std::atomic<uint16_t> ownPort;

    ShardShared() = delete;
    ShardShared(ShardDB& db_): db(db_), stop(false), ownPort(0) {
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
    uint16_t _desiredPort;
    uint64_t _packetDropRand;
    uint64_t _incomingPacketDropProbability; // probability * 10,000
    uint64_t _outgoingPacketDropProbability; // probability * 10,000
public:
    ShardServer(Logger& logger, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, "server"),
        _shared(shared),
        _shid(shid),
        _desiredPort(options.port),
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
        _shared.stop.store(true);
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
            throw SYSCALL_EXCEPTION("cannot bind socket to port %s", _desiredPort);
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

        _shared.ownPort.store(ntohs(serverAddr.sin_port));
        LOG_INFO(_env, "Bound shard %s to port %s", _shid, _shared.ownPort.load());

        struct sockaddr_in clientAddr;
        std::vector<char> recvBuf(UDP_MTU);
        std::vector<char> sendBuf(UDP_MTU);
        auto reqContainer = std::make_unique<ShardReqContainer>();
        auto respContainer = std::make_unique<ShardRespContainer>();
        auto logEntry = std::make_unique<ShardLogEntry>();

        for (;;) {
            if (_shared.stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
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
            } catch (const BincodeException& err) {
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
            } catch (const BincodeException& exc) {
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
};

static void* runShardServer(void* server) {
    ((ShardServer*)server)->run();
    return nullptr;
}

struct ShardRegisterer : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    ShardId _shid;
    std::array<uint8_t, 4> _ownIp;
    std::string _shuckleHost;
    uint16_t _shucklePort;
public:
    ShardRegisterer(Logger& logger, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, "registerer"),
        _shared(shared),
        _shid(shid),
        _ownIp(options.ownIp),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual ~ShardRegisterer() = default;

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
            uint16_t port = _shared.ownPort.load();
            if (port == 0) {
                // shard server isn't up yet
                continue;
            }
            LOG_INFO(_env, "Registering ourselves (shard %s, port %s) with shuckle", _shid, port);
            std::string err = registerShard(_shuckleHost, _shucklePort, 100_ms, _shid, _ownIp, port);
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

static void* runShardRegisterer(void* server) {
    ((ShardRegisterer*)server)->run();
    return nullptr;
}

struct ShardBlockServiceUpdater : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    ShardId _shid;
    std::string _shuckleHost;
    uint16_t _shucklePort;
public:
    ShardBlockServiceUpdater(Logger& logger, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, "block_service_updater"),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual ~ShardBlockServiceUpdater() = default;

    virtual void terminate() override {
        _env.flush();
        _shared.stop.store(true);
    }

    virtual void onAbort() override {
        _env.flush();
    }

    void run() {
        EggsTime t0 = eggsNow();
        EggsTime lastRequestT = 0;
        bool lastRequestSuccessful = false;
        auto respContainer = std::make_unique<ShardRespContainer>();
        auto logEntry = std::make_unique<ShardLogEntry>();

        #define GO_TO_NEXT_ITERATION \
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); \
            continue; \

        for (;;) {
            if (_shared.stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
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
            LOG_INFO(_env, "about to perform shuckle requests to %s:%s", _shuckleHost, _shucklePort);
            std::string err;

            err = fetchBlockServices(_shuckleHost, _shucklePort, 100_ms, _shid, logEntry->body.setUpdateBlockServices());
            lastRequestSuccessful = err.empty();
            if (lastRequestSuccessful && logEntry->body.getUpdateBlockServices().blockServices.els.empty()) {
                lastRequestSuccessful = false;
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
        }

        #undef GO_TO_NEXT_ITERATION
    }
};

static void* runShardBlockServiceUpdater(void* server) {
    ((ShardBlockServiceUpdater*)server)->run();
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
        auto server = std::make_unique<ShardServer>(logger, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "shard");
    }

    {
        auto shuckleRegisterer = std::make_unique<ShardRegisterer>(logger, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardRegisterer, &*shuckleRegisterer) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shuckleRegisterer), tid, "registerer");
    }

    {
        auto shuckleUpdater = std::make_unique<ShardBlockServiceUpdater>(logger, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardBlockServiceUpdater, &*shuckleUpdater) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shuckleUpdater), tid, "block_service_updater");
    }

    undertaker->reap();
}