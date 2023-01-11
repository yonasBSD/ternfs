#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <thread>

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

// Data needed to synchronize between the different threads
struct ShardShared {
private:
    uint64_t _currentLogIndex;
    std::mutex _applyLock;
public:
    ShardDB& db;
    std::atomic<bool> shuckleReached;

    ShardShared() = delete;
    ShardShared(ShardDB& db_): db(db_), shuckleReached(false) {
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
    std::atomic<bool> _stop;
    bool _waitForShuckle;
public:
    ShardServer(Logger& logger, ShardShared& shared, ShardId shid, const ShardOptions& options):
        _env(logger, "server"),
        _shared(shared),
        _shid(shid),
        _stop(false),
        _waitForShuckle(options.waitForShuckle)
    {}

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
        {
            struct sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            serverAddr.sin_port = htons(_shid.port());
            if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
                throw SYSCALL_EXCEPTION("cannot bind socket to port %s", _shid.port());
            }
        }
        
        // 10ms timeout for prompt termination
        {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
                throw SYSCALL_EXCEPTION("setsockopt");
            }
        }

        LOG_INFO(_env, "running on port %s", _shid.port());
        
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

            if (_waitForShuckle && !_shared.shuckleReached.load()) {
                LOG_DEBUG(_env, "need to wait for shuckle, waiting");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Read one request
            memset(&clientAddr, 0, sizeof(clientAddr));
            socklen_t addrLen = sizeof(clientAddr);
            int read = recvfrom(sock, &recvBuf[0], recvBuf.size(), 0, (struct sockaddr*)&clientAddr, &addrLen);
            if (read < 0 && errno == EAGAIN) {
                continue;
            }
            if (read < 0) {
                throw SYSCALL_EXCEPTION("recvfrom");
            }
            LOG_DEBUG(_env, "received message from %s", clientAddr);

            BincodeBuf reqBbuf(&recvBuf[0], read);
            
            // First, try to parse the header
            ShardRequestHeader reqHeader;
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

            BincodeBuf respBbuf(&sendBuf[0], sendBuf.size());
            if (err == NO_ERROR) {
                LOG_DEBUG(_env, "successfully processed request %s with kind %s: %s", reqHeader.requestId, respContainer->kind(), *respContainer);
                ShardResponseHeader(reqHeader.requestId, respContainer->kind()).pack(respBbuf);
                respContainer->pack(respBbuf);
            } else {
                LOG_DEBUG(_env, "request %s failed with error %s", reqContainer->kind(), err);
                ShardResponseHeader(reqHeader.requestId, ShardMessageKind::ERROR).pack(respBbuf);
                respBbuf.packScalar<uint16_t>((uint16_t)err);
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

struct BlockServiceUpdater : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    std::atomic<bool> _stop;
    std::string _shuckleHost;
    bool _waitForShuckle;
public:
    BlockServiceUpdater(Logger& logger, ShardShared& shared, const ShardOptions& options):
        _env(logger, "block_service_updater"),
        _shared(shared),
        _stop(false),
        _shuckleHost(options.shuckleHost),
        _waitForShuckle(options.waitForShuckle)
    {}

    virtual ~BlockServiceUpdater() = default;

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

            if (_waitForShuckle && !_shared.shuckleReached.load() && (eggsNow() - t0) > 5'000'000'000) {
                // if we refuse to start the server and we haven't reached shuckle within 5 secs,
                // fail hard
                if (reachedButEmpty) {
                    throw EGGS_EXCEPTION("reached shuckle, but got no block services, and _waitForShuckle=true, terminating");
                } else {
                    throw EGGS_EXCEPTION("could not reach shuckle in time, and _waitForShuckle=true, terminating");
                }
            }

            EggsTime t = eggsNow();
            if (lastRequestSuccessful && (t - lastRequestT) < 60'000'000'000) {
                // wait 60 secs before requesting again
                GO_TO_NEXT_ITERATION
            }
            if (!lastRequestSuccessful && (t - lastRequestT) < 100'000'000) {
                // if the last request failed, wait at least 100ms before retrying
                GO_TO_NEXT_ITERATION
            }

            logEntry->time = eggsNow();
            LOG_INFO(_env, "about to perform shuckle request to %s", _shuckleHost);
            std::string err;
            lastRequestSuccessful = fetchBlockServices(_shuckleHost, 100 /* timeoutMs */, err, logEntry->body.setUpdateBlockServices());
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
                } else {
                    _shared.shuckleReached.store(true);
                }
            }
        }

        #undef GO_TO_NEXT_ITERATION
    }
};

static void* runBlockServiceUpdater(void* server) {
    ((BlockServiceUpdater*)server)->run();
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
        auto blockServiceUpdater = std::make_unique<BlockServiceUpdater>(logger, shared, options);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runBlockServiceUpdater, &*blockServiceUpdater) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(blockServiceUpdater), tid, "block_service_updater");
    }

    undertaker->reap();
}