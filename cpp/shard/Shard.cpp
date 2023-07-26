#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <arpa/inet.h>

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
#include "Time.hpp"
#include "wyhash.h"
#include "Xmon.hpp"
#include "Timings.hpp"

// Data needed to synchronize between the different threads
struct ShardShared {
private:
    uint64_t _currentLogIndex;
    std::mutex _applyLock;
public:
    ShardDB& db;
    std::atomic<bool> stop;
    std::atomic<uint32_t> ip1;
    std::atomic<uint16_t> port1;
    std::atomic<uint32_t> ip2;
    std::atomic<uint16_t> port2;
    std::array<Timings, maxShardMessageKind+1> timings;

    ShardShared() = delete;
    ShardShared(ShardDB& db_): db(db_), stop(false), ip1(0), port1(0), ip2(0), port2(0) {
        _currentLogIndex = db.lastAppliedLogEntry();
        for (ShardMessageKind kind : allShardMessageKind) {
            timings[(int)kind] = Timings::Standard();
        }
    }

private:
    EggsError _applyLogEntryLocked(const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        EggsError err = db.applyLogEntry(true, _currentLogIndex+1, logEntry, resp);
        _currentLogIndex++;
        return err;
    }

public:    
    EggsError applyLogEntry(const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        std::lock_guard<std::mutex> lock(_applyLock);
        return _applyLogEntryLocked(logEntry, resp);
    }

    EggsError prepareAndApplyLogEntry(ShardReqContainer& req, ShardLogEntry& logEntry, ShardRespContainer& resp) {
        // we wrap everything in a lock (even if only the apply is strictly required)
        // because we fill in expected directory times when preparing the log entry,
        // which might change if we do two prepareLogEntry in one order and then
        // two applyLogEntry in another order.
        std::lock_guard<std::mutex> lock(_applyLock);
        EggsError err = db.prepareLogEntry(req, logEntry);
        if (err == NO_ERROR) {
            err = _applyLogEntryLocked(logEntry, resp);
        }
        return err;
    }
};

static bool bigRequest(ShardMessageKind kind) {
    return unlikely(
        kind == ShardMessageKind::ADD_SPAN_INITIATE ||
        kind == ShardMessageKind::ADD_SPAN_CERTIFY
    );
}

static bool bigResponse(ShardMessageKind kind) {
    return unlikely(
        kind == ShardMessageKind::READ_DIR ||
        kind == ShardMessageKind::ADD_SPAN_INITIATE ||
        kind == ShardMessageKind::FILE_SPANS ||
        kind == ShardMessageKind::VISIT_DIRECTORIES ||
        kind == ShardMessageKind::VISIT_FILES ||
        kind == ShardMessageKind::VISIT_TRANSIENT_FILES ||
        kind == ShardMessageKind::BLOCK_SERVICE_FILES ||
        kind == ShardMessageKind::FULL_READ_DIR
    );
}

struct ShardServer : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    ShardId _shid;
    int _ipPortIx;
    uint32_t _ownIp;
    uint16_t _desiredPort;
    uint64_t _packetDropRand;
    uint64_t _incomingPacketDropProbability; // probability * 10,000
    uint64_t _outgoingPacketDropProbability; // probability * 10,000
public:
    ShardServer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, int ipPortIx, ShardShared& shared):
        _env(logger, xmon, "server_" + std::to_string(ipPortIx+1)),
        _shared(shared),
        _shid(shid),
        _ipPortIx(ipPortIx),
        _ownIp(options.ipPorts[ipPortIx].ip),
        _desiredPort(options.ipPorts[ipPortIx].port),
        _packetDropRand(eggsNow().ns),
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
        uint32_t ipn = htonl(_ownIp);
        memcpy(&serverAddr.sin_addr.s_addr, &ipn, sizeof(ipn));
        serverAddr.sin_port = htons(_desiredPort);
        if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
            char ip[INET_ADDRSTRLEN];
            throw SYSCALL_EXCEPTION("cannot bind socket to addr %s:%s", inet_ntop(AF_INET, &serverAddr.sin_addr, ip, INET_ADDRSTRLEN), _desiredPort);
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

        if (_ipPortIx == 0) {
            _shared.ip1.store(ntohl(serverAddr.sin_addr.s_addr));
            _shared.port1.store(ntohs(serverAddr.sin_port));
        } else {
            _shared.ip2.store(ntohl(serverAddr.sin_addr.s_addr));
            _shared.port2.store(ntohs(serverAddr.sin_port));
        }
        LOG_INFO(_env, "Bound shard %s to %s", _shid, serverAddr);

        struct sockaddr_in clientAddr;
        std::vector<char> recvBuf(DEFAULT_UDP_MTU);
        std::vector<char> sendBuf(MAX_UDP_MTU);
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
            if (read < 0 && (errno == EAGAIN || errno == EINTR)) {
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
                LOG_ERROR(_env, "Could not parse: %s", err.what());
                RAISE_ALERT(_env, "could not parse request header from %s, dropping it.", clientAddr);
                continue;
            }

            if (wyhash64(&_packetDropRand) % 10'000 < _incomingPacketDropProbability) {
                LOG_DEBUG(_env, "artificially dropping request %s", reqHeader.requestId);
                continue;
            }

            auto t0 = eggsNow();

            LOG_DEBUG(_env, "received request id %s, kind %s, from %s", reqHeader.requestId, reqHeader.kind, clientAddr);

            // If this will be filled in with an actual code, it means that we couldn't process
            // the request.
            EggsError err = NO_ERROR;

            // Now, try to parse the body
            try {
                reqContainer->unpack(reqBbuf, reqHeader.kind);
                if (bigRequest(reqHeader.kind)) {
                    if (unlikely(_env._shouldLog(LogLevel::LOG_TRACE))) {
                        LOG_TRACE(_env, "parsed request: %s", *reqContainer);
                    } else {
                        LOG_DEBUG(_env, "parsed request: <omitted>");
                    }
                } else {
                    LOG_DEBUG(_env, "parsed request: %s", *reqContainer);
                }
            } catch (const BincodeException& exc) {
                LOG_ERROR(_env, "Could not parse: %s", exc.what());
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
                    err = _shared.prepareAndApplyLogEntry(*reqContainer, *logEntry, *respContainer);
                }
            }
            Duration processElapsed = eggsNow() - t0;

            BincodeBuf respBbuf(sendBuf.data(), sendBuf.size());
            if (err == NO_ERROR) {
                LOG_DEBUG(_env, "successfully processed request %s with kind %s in %s", reqHeader.requestId, respContainer->kind(), processElapsed);
                if (bigResponse(reqHeader.kind)) {
                    if (unlikely(_env._shouldLog(LogLevel::LOG_TRACE))) {
                        LOG_TRACE(_env, "resp body: %s", *respContainer);
                    } else {
                        LOG_DEBUG(_env, "resp body: <omitted>");
                    }
                } else {
                    LOG_DEBUG(_env, "resp body: %s", *respContainer);
                }
                ShardResponseHeader(reqHeader.requestId, respContainer->kind()).pack(respBbuf);
                respContainer->pack(respBbuf);
            } else {
                LOG_DEBUG(_env, "request %s failed with error %s in %s", reqContainer->kind(), err, processElapsed);
                ShardResponseHeader(reqHeader.requestId, ShardMessageKind::ERROR).pack(respBbuf);
                respBbuf.packScalar<uint16_t>((uint16_t)err);
            }

            Duration elapsed = eggsNow() - t0;

            _shared.timings[(int)reqHeader.kind].add(elapsed);

            if (wyhash64(&_packetDropRand) % 10'000 < _outgoingPacketDropProbability) {
                LOG_DEBUG(_env, "artificially dropping response %s", reqHeader.requestId);
                continue;
            }

            if (sendto(sock, respBbuf.data, respBbuf.len(), 0, (struct sockaddr*)&clientAddr, sizeof(clientAddr)) != respBbuf.len()) {
                throw SYSCALL_EXCEPTION("sendto");
            }
            LOG_DEBUG(_env, "sent response %s to %s", respContainer->kind(), clientAddr);
        }

        // If we're terminating gracefully we're the last ones, close the db nicely
        if (_ipPortIx == 0) {
            _shared.db.close();
        }
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
    std::string _shuckleHost;
    uint16_t _shucklePort;
    bool _hasSecondIp;
public:
    ShardRegisterer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, xmon, "registerer"),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _hasSecondIp(options.ipPorts[1].port != 0)
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
        uint64_t rand = eggsNow().ns;
        EggsTime nextRegister = 0; // when 0, it means that the last one wasn't successful
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 + (wyhash64(&rand)%100))); // fuzz the startup busy loop
            if (_shared.stop.load()) {
                LOG_DEBUG(_env, "got told to stop, stopping");
                break;
            }
            if (eggsNow() < nextRegister) {
                continue;                
            }
            uint16_t port1 = _shared.port1.load();
            uint16_t port2 = _shared.port2.load();
            // Avoid registering with only one port, so that clients can just wait on 
            // the first port being ready and they always have both.
            if (port1 == 0 || (_hasSecondIp && port2 == 0)) {
                // shard server isn't up yet
                continue;
            }
            uint32_t ip1 = _shared.ip1.load();
            uint32_t ip2 = _shared.ip2.load();
            LOG_DEBUG(_env, "Registering ourselves (shard %s, %s:%s, %s:%s) with shuckle", _shid, in_addr{htonl(ip1)}, port1, in_addr{htonl(ip2)}, port2);
            std::string err = registerShard(_shuckleHost, _shucklePort, 100_ms, _shid, ip1, port1, ip2, port2);
            if (!err.empty()) {
                if (nextRegister == 0) { // only one alert
                    RAISE_ALERT(_env, "Couldn't register ourselves with shuckle: %s", err);
                } else {
                    LOG_DEBUG(_env, "Couldn't register ourselves with shuckle: %s", err);
                }
                nextRegister = 0;
                continue;
            }
            Duration nextRegisterD(wyhash64(&rand) % (2_mins).ns); // fuzz the successful loop
            nextRegister = eggsNow() + nextRegisterD;
            LOG_INFO(_env, "Successfully registered with shuckle (shard %s, %s:%s, %s:%s), will register again in %s", _shid, in_addr{htonl(ip1)}, port1, in_addr{htonl(ip2)}, port2, nextRegisterD);
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
    ShardBlockServiceUpdater(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, xmon, "block_service_updater"),
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
            LOG_INFO(_env, "about to fetch block services from %s:%s", _shuckleHost, _shucklePort);
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

struct ShardStatsInserter : Undertaker::Reapable {
private:
    Env _env;
    ShardShared& _shared;
    ShardId _shid;
    std::string _shuckleHost;
    uint16_t _shucklePort;
public:
    ShardStatsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared):
        _env(logger, xmon, "stats_inserter"),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual ~ShardStatsInserter() = default;

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
        std::vector<Stat> stats;

        const auto insertShardStats = [this, &stats]() {
            std::string err;
            for (ShardMessageKind kind : allShardMessageKind) {
                std::ostringstream prefix;
                prefix << "shard." << std::setw(3) << std::setfill('0') << _shid << "." << kind;
                _shared.timings[(int)kind].toStats(prefix.str(), stats);
            }
            err = insertStats(_shuckleHost, _shucklePort, 10_sec, stats);
            stats.clear();
            if (err.empty()) {
                for (ShardMessageKind kind : allShardMessageKind) {
                    _shared.timings[(int)kind].reset();
                }
            }
            return err;
        };

        #define GO_TO_NEXT_ITERATION \
            sleepFor(10_ms); \
            continue; \

        for (;;) {
            if (_shared.stop.load()) {
                LOG_INFO(_env, "got told to stop, trying to insert stats before stopping");
                insertShardStats();
                LOG_INFO(_env, "done, goodbye.");
                break;
            }

            EggsTime t = eggsNow();
            if (lastRequestSuccessful && (t - lastRequestT) < 1_hours) {
                GO_TO_NEXT_ITERATION
            }
            if (!lastRequestSuccessful && (t - lastRequestT) < 100_ms) {
                // if the last request failed, wait at least 100ms before retrying
                GO_TO_NEXT_ITERATION
            }

            LOG_INFO(_env, "about to insert stats to %s:%s", _shuckleHost, _shucklePort);
            std::string err;

            err = insertShardStats();
            lastRequestSuccessful = err.empty();
            lastRequestT = t;
            if (!lastRequestSuccessful) {
                RAISE_ALERT(_env, "could not reach shuckle: %s", err);
                GO_TO_NEXT_ITERATION
            }
            LOG_INFO(_env, "stats inserted, will wait one hour");
        }

        #undef GO_TO_NEXT_ITERATION
    }
};

static void* runShardStatsInserter(void* server) {
    ((ShardStatsInserter*)server)->run();
    return nullptr;
}

static void* runXmon(void* server) {
    ((Xmon*)server)->run();
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
    Logger logger(options.logLevel, *logOut, options.syslog, true);

    std::shared_ptr<XmonAgent> xmon;
    if (options.xmon) {
        xmon = std::make_shared<XmonAgent>();
    }

    {
        Env env(logger, xmon, "startup");
        LOG_INFO(env, "Running shard %s with options:", shid);
        LOG_INFO(env, "  level = %s", options.logLevel);
        LOG_INFO(env, "  logFile = '%s'", options.logFile);
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
        LOG_INFO(env, "  simulateIncomingPacketDrop = %s", options.simulateIncomingPacketDrop);
        LOG_INFO(env, "  simulateOutgoingPacketDrop = %s", options.simulateOutgoingPacketDrop);
        LOG_INFO(env, "  syslog = %s", (int)options.syslog);
    }

    ShardDB db(logger, xmon, shid, dbDir);

    ShardShared shared(db);

    {
        auto server = std::make_unique<ShardServer>(logger, xmon, shid, options, 0, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "shard_1");
    }

    if (options.ipPorts[1].ip != 0) {
        auto server = std::make_unique<ShardServer>(logger, xmon, shid, options, 1, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardServer, &*server) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(server), tid, "shard_2");
    }

    {
        auto shuckleRegisterer = std::make_unique<ShardRegisterer>(logger, xmon, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardRegisterer, &*shuckleRegisterer) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shuckleRegisterer), tid, "registerer");
    }

    {
        auto shuckleUpdater = std::make_unique<ShardBlockServiceUpdater>(logger, xmon, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardBlockServiceUpdater, &*shuckleUpdater) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shuckleUpdater), tid, "block_service_updater");
    }

    {
        auto statsInserter = std::make_unique<ShardStatsInserter>(logger, xmon, shid, options, shared);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShardStatsInserter, &*statsInserter) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(statsInserter), tid, "stats_inserter");
    }

    if (xmon) {
        XmonConfig config;
        {
            std::ostringstream ss;
            ss << std::setw(3) << std::setfill('0') << shid;
            config.appInstance = "shard:" + ss.str();
        }
        auto xmonRunner = std::make_unique<Xmon>(logger, xmon, config, shared.stop);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runXmon, &*xmonRunner) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(xmonRunner), tid, "xmon");
    }

    undertaker->reap();
}