#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <sys/ioctl.h>

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
#include "ErrorCount.hpp"
#include "Loop.hpp"
#include "Metrics.hpp"

// Data needed to synchronize between the different threads
struct ShardShared {
private:
    uint64_t _currentLogIndex;
    std::mutex _applyLock;
public:
    ShardDB& db;
    std::atomic<uint32_t> ip1;
    std::atomic<uint16_t> port1;
    std::atomic<uint32_t> ip2;
    std::atomic<uint16_t> port2;
    std::array<Timings, maxShardMessageKind+1> timings;
    std::array<ErrorCount, maxShardMessageKind+1> errors;
    std::array<std::atomic<double>, 2> sockBytes;

    ShardShared() = delete;
    ShardShared(ShardDB& db_): db(db_), ip1(0), port1(0), ip2(0), port2(0) {
        _currentLogIndex = db.lastAppliedLogEntry();
        for (ShardMessageKind kind : allShardMessageKind) {
            timings[(int)kind] = Timings::Standard();
        }
        sockBytes[0] = 0.0;
        sockBytes[1] = 0.0;
    }

private:
    EggsError _applyLogEntryLocked(ShardMessageKind reqKind, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        EggsError err = db.applyLogEntry(true, reqKind, _currentLogIndex+1, logEntry, resp);
        _currentLogIndex++;
        return err;
    }

public:    
    EggsError applyLogEntry(ShardMessageKind reqKind, const ShardLogEntry& logEntry, ShardRespContainer& resp) {
        std::lock_guard<std::mutex> lock(_applyLock);
        return _applyLogEntryLocked(reqKind, logEntry, resp);
    }

    EggsError prepareAndApplyLogEntry(ShardReqContainer& req, ShardLogEntry& logEntry, ShardRespContainer& resp) {
        // we wrap everything in a lock (even if only the apply is strictly required)
        // because we fill in expected directory times when preparing the log entry,
        // which might change if we do two prepareLogEntry in one order and then
        // two applyLogEntry in another order.
        std::lock_guard<std::mutex> lock(_applyLock);
        EggsError err = db.prepareLogEntry(req, logEntry);
        if (err == NO_ERROR) {
            err = _applyLogEntryLocked(req.kind(), logEntry, resp);
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

struct ShardServer : Loop {
private:
    // init data
    ShardShared& _shared;
    ShardId _shid;
    int _ipPortIx;
    uint32_t _ownIp;
    uint16_t _desiredPort;
    uint64_t _packetDropRand;
    uint64_t _incomingPacketDropProbability; // probability * 10,000
    uint64_t _outgoingPacketDropProbability; // probability * 10,000

    // run data
    AES128Key _expandedCDCKey;
    int _sock;
    struct sockaddr_in _serverAddr;
    struct sockaddr_in _clientAddr;
    std::vector<char> _recvBuf;
    std::vector<char> _sendBuf;
    std::unique_ptr<ShardReqContainer> _reqContainer;
    std::unique_ptr<ShardRespContainer> _respContainer;
    std::unique_ptr<ShardLogEntry> _logEntry;
    std::atomic<double>& _sockBytes;

public:
    ShardServer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, int ipPortIx, ShardShared& shared) :
        Loop(logger, xmon, "server_" + std::to_string(ipPortIx+1)),
        _shared(shared),
        _shid(shid),
        _ipPortIx(ipPortIx),
        _ownIp(options.ipPorts[ipPortIx].ip),
        _desiredPort(options.ipPorts[ipPortIx].port),
        _packetDropRand(eggsNow().ns),
        _incomingPacketDropProbability(0),
        _outgoingPacketDropProbability(0),
        _sockBytes(_shared.sockBytes[ipPortIx])
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

    virtual void init() override {
        expandKey(CDCKey, _expandedCDCKey);

        _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_sock < 0) {
            throw SYSCALL_EXCEPTION("cannot create socket");
        }
        _serverAddr.sin_family = AF_INET;
        uint32_t ipn = htonl(_ownIp);
        memcpy(&_serverAddr.sin_addr.s_addr, &ipn, sizeof(ipn));
        _serverAddr.sin_port = htons(_desiredPort);
        if (bind(_sock, (struct sockaddr*)&_serverAddr, sizeof(_serverAddr)) != 0) {
            char ip[INET_ADDRSTRLEN];
            throw SYSCALL_EXCEPTION("cannot bind socket to addr %s:%s", inet_ntop(AF_INET, &_serverAddr.sin_addr, ip, INET_ADDRSTRLEN), _desiredPort);
        }
        {
            socklen_t addrLen = sizeof(_serverAddr);
            if (getsockname(_sock, (struct sockaddr*)&_serverAddr, &addrLen) < 0) {
                throw SYSCALL_EXCEPTION("getsockname");
            }
        }
        // 10ms timeout for prompt termination
        {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10'000;
            if (setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
                throw SYSCALL_EXCEPTION("setsockopt");
            }
        }

        if (_ipPortIx == 0) {
            _shared.ip1.store(ntohl(_serverAddr.sin_addr.s_addr));
            _shared.port1.store(ntohs(_serverAddr.sin_port));
        } else {
            _shared.ip2.store(ntohl(_serverAddr.sin_addr.s_addr));
            _shared.port2.store(ntohs(_serverAddr.sin_port));
        }
        LOG_INFO(_env, "Bound shard %s to %s", _shid, _serverAddr);

        _recvBuf.resize(DEFAULT_UDP_MTU);
        _sendBuf.resize(MAX_UDP_MTU);
        _reqContainer = std::make_unique<ShardReqContainer>();
        _respContainer = std::make_unique<ShardRespContainer>();
        _logEntry = std::make_unique<ShardLogEntry>();
    }

    virtual void step() override {
        // Read one request
        memset(&_clientAddr, 0, sizeof(_clientAddr));
        socklen_t addrLen = sizeof(_clientAddr);
        int read = recvfrom(_sock, _recvBuf.data(), _recvBuf.size(), 0, (struct sockaddr*)&_clientAddr, &addrLen);
        if (read < 0 && (errno == EAGAIN || errno == EINTR)) {
            return;
        }
        if (read < 0) {
            throw SYSCALL_EXCEPTION("recvfrom");
        }
        LOG_DEBUG(_env, "received message from %s", _clientAddr);

        {
            int bytes;
            if (ioctl(_sock, FIONREAD, &bytes) < 0) {
                throw SYSCALL_EXCEPTION("ioctl");
            }
            _sockBytes = _sockBytes*0.95 + (double)bytes*0.05;
        }

        BincodeBuf reqBbuf(_recvBuf.data(), read);
        
        // First, try to parse the header
        ShardRequestHeader reqHeader;
        try {
            reqHeader.unpack(reqBbuf);
        } catch (const BincodeException& err) {
            LOG_ERROR(_env, "Could not parse: %s", err.what());
            RAISE_ALERT(_env, "could not parse request header from %s, dropping it.", _clientAddr);
            return;
        }

        if (wyhash64(&_packetDropRand) % 10'000 < _incomingPacketDropProbability) {
            LOG_DEBUG(_env, "artificially dropping request %s", reqHeader.requestId);
            return;
        }

        auto t0 = eggsNow();

        LOG_DEBUG(_env, "received request id %s, kind %s, from %s", reqHeader.requestId, reqHeader.kind, _clientAddr);

        // If this will be filled in with an actual code, it means that we couldn't process
        // the request.
        EggsError err = NO_ERROR;

        // Now, try to parse the body
        try {
            _reqContainer->unpack(reqBbuf, reqHeader.kind);
            if (bigRequest(reqHeader.kind)) {
                if (unlikely(_env._shouldLog(LogLevel::LOG_TRACE))) {
                    LOG_TRACE(_env, "parsed request: %s", *_reqContainer);
                } else {
                    LOG_DEBUG(_env, "parsed request: <omitted>");
                }
            } else {
                LOG_DEBUG(_env, "parsed request: %s", *_reqContainer);
            }
        } catch (const BincodeException& exc) {
            LOG_ERROR(_env, "Could not parse: %s", exc.what());
            RAISE_ALERT(_env, "could not parse request of kind %s from %s, will reply with error.", reqHeader.kind, _clientAddr);
            err = EggsError::MALFORMED_REQUEST;
        }

        // authenticate, if necessary
        if (isPrivilegedRequestKind(reqHeader.kind)) {
            auto expectedMac = cbcmac(_expandedCDCKey, reqBbuf.data, reqBbuf.cursor - reqBbuf.data);
            BincodeFixedBytes<8> receivedMac;
            reqBbuf.unpackFixedBytes<8>(receivedMac);
            if (expectedMac != receivedMac.data) {
                err = EggsError::NOT_AUTHORISED;
            }
        }

        // Make sure nothing is left
        if (err == NO_ERROR && reqBbuf.remaining() != 0) {
            RAISE_ALERT(_env, "%s bytes remaining after parsing request of kind %s from %s, will reply with error", reqBbuf.remaining(), reqHeader.kind, _clientAddr);
            err = EggsError::MALFORMED_REQUEST;
        }

        // Actually process the request
        if (err == NO_ERROR) {
            if (readOnlyShardReq(_reqContainer->kind())) {
                err = _shared.db.read(*_reqContainer, *_respContainer);
            } else {
                err = _shared.prepareAndApplyLogEntry(*_reqContainer, *_logEntry, *_respContainer);
            }
        }
        Duration processElapsed = eggsNow() - t0;

        BincodeBuf respBbuf(_sendBuf.data(), _sendBuf.size());
        if (err == NO_ERROR) {
            LOG_DEBUG(_env, "successfully processed request %s with kind %s in %s", reqHeader.requestId, _respContainer->kind(), processElapsed);
            if (bigResponse(reqHeader.kind)) {
                if (unlikely(_env._shouldLog(LogLevel::LOG_TRACE))) {
                    LOG_TRACE(_env, "resp body: %s", *_respContainer);
                } else {
                    LOG_DEBUG(_env, "resp body: <omitted>");
                }
            } else {
                LOG_DEBUG(_env, "resp body: %s", *_respContainer);
            }
            ShardResponseHeader(reqHeader.requestId, _respContainer->kind()).pack(respBbuf);
            _respContainer->pack(respBbuf);
        } else {
            LOG_DEBUG(_env, "request %s failed with error %s in %s", _reqContainer->kind(), err, processElapsed);
            ShardResponseHeader(reqHeader.requestId, ShardMessageKind::ERROR).pack(respBbuf);
            respBbuf.packScalar<uint16_t>((uint16_t)err);
        }

        Duration elapsed = eggsNow() - t0;

        _shared.timings[(int)reqHeader.kind].add(elapsed);
        _shared.errors[(int)reqHeader.kind].add(err);

        if (wyhash64(&_packetDropRand) % 10'000 < _outgoingPacketDropProbability) {
            LOG_DEBUG(_env, "artificially dropping response %s", reqHeader.requestId);
            return;
        }

        if (sendto(_sock, respBbuf.data, respBbuf.len(), 0, (struct sockaddr*)&_clientAddr, sizeof(_clientAddr)) != respBbuf.len()) {
            // we get this when nf drops packets
            if (errno != EPERM) {
                throw SYSCALL_EXCEPTION("sendto");
            } else {
                LOG_INFO(_env, "dropping response %s to %s because of EPERM", _respContainer->kind(), _clientAddr);
            }
        }
        LOG_DEBUG(_env, "sent response %s to %s", _respContainer->kind(), _clientAddr);
    }

    // If we're terminating gracefully we're the last ones, close the db nicely
    void virtual finish() override {
        if (_ipPortIx == 0) {
            _shared.db.close();
        }
    }
};

struct ShardRegisterer : PeriodicLoop {
private:
    ShardShared& _shared;
    Stopper _stopper;
    ShardId _shid;
    std::string _shuckleHost;
    uint16_t _shucklePort;
    bool _hasSecondIp;
    XmonNCAlert _alert;
public:
    ShardRegisterer(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared) :
        PeriodicLoop(logger, xmon, "registerer", {1_sec, 1_mins}),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort),
        _hasSecondIp(options.ipPorts[1].port != 0)
    {}

    virtual void init() {
        _env.updateAlert(_alert, "Waiting to register ourselves for the first time");
    }

    virtual bool periodicStep() {
        uint16_t port1 = _shared.port1.load();
        uint16_t port2 = _shared.port2.load();
        // Avoid registering with only one port, so that clients can just wait on 
        // the first port being ready and they always have both.
        if (port1 == 0 || (_hasSecondIp && port2 == 0)) {
            // shard server isn't up yet
            return false;
        }
        uint32_t ip1 = _shared.ip1.load();
        uint32_t ip2 = _shared.ip2.load();
        LOG_INFO(_env, "Registering ourselves (shard %s, %s:%s, %s:%s) with shuckle", _shid, in_addr{htonl(ip1)}, port1, in_addr{htonl(ip2)}, port2);
        std::string err = registerShard(_shuckleHost, _shucklePort, 10_sec, _shid, ip1, port1, ip2, port2);
        if (!err.empty()) {
            _env.updateAlert(_alert, "Couldn't register ourselves with shuckle: %s", err);
            return false;
        }
        _env.clearAlert(_alert);
        return true;
    }
};

struct ShardBlockServiceUpdater : PeriodicLoop {
private:
    ShardShared& _shared;
    ShardId _shid;
    std::string _shuckleHost;
    uint16_t _shucklePort;
    XmonNCAlert _alert;
    ShardRespContainer _respContainer;
    ShardLogEntry _logEntry;
public:
    ShardBlockServiceUpdater(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared):
        PeriodicLoop(logger, xmon, "block_service_updater", {1_sec, 1_mins}),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual void init() override {
        _env.updateAlert(_alert, "Waiting to fetch block services for the first time");
    }

    virtual bool periodicStep() override {
        _logEntry.time = eggsNow();
        LOG_INFO(_env, "about to fetch block services from %s:%s", _shuckleHost, _shucklePort);
        std::string err = fetchBlockServices(_shuckleHost, _shucklePort, 10_sec, _shid, _logEntry.body.setUpdateBlockServices());
        if (!err.empty()) {
            _env.updateAlert(_alert, "could not reach shuckle: %s", err);
            return false;
        }
        if (_logEntry.body.getUpdateBlockServices().blockServices.els.empty()) {
            _env.updateAlert(_alert, "got no block services");
            return false;
        }

        // if we made it so far we're good with what regards to shuckle
        _env.clearAlert(_alert);

        {
            EggsError err = _shared.applyLogEntry((ShardMessageKind)0, _logEntry, _respContainer);
            if (err != NO_ERROR) {
                RAISE_ALERT(_env, "unexpected failure when trying to update block services: %s", err);
                return false;
            }
        }

        return true;
    }
};

struct ShardStatsInserter : PeriodicLoop {
private:
    ShardShared& _shared;
    ShardId _shid;
    std::string _shuckleHost;
    uint16_t _shucklePort;
    XmonNCAlert _alert;
    std::vector<Stat> _stats;
public:
    ShardStatsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, const ShardOptions& options, ShardShared& shared):
        PeriodicLoop(logger, xmon, "stats_inserter", {1_mins, 1_hours}),
        _shared(shared),
        _shid(shid),
        _shuckleHost(options.shuckleHost),
        _shucklePort(options.shucklePort)
    {}

    virtual bool periodicStep() override {
        for (ShardMessageKind kind : allShardMessageKind) {
            std::ostringstream prefix;
            prefix << "shard." << std::setw(3) << std::setfill('0') << _shid << "." << kind;
            _shared.timings[(int)kind].toStats(prefix.str(), _stats);
            _shared.errors[(int)kind].toStats(prefix.str(), _stats);
        }
        LOG_INFO(_env, "inserting stats");
        std::string err = insertStats(_shuckleHost, _shucklePort, 10_sec, _stats);
        _stats.clear();
        if (err.empty()) {
            _env.clearAlert(_alert);
            for (ShardMessageKind kind : allShardMessageKind) {
                _shared.timings[(int)kind].reset();
                _shared.errors[(int)kind].reset();
            }
            return true;
        } else {
            _env.updateAlert(_alert, "Could not insert stats: %s", err);
            return false;
        }
    }

    virtual void finish() override {
        periodicStep();
        for (ShardMessageKind kind : allShardMessageKind) {
            const auto& timings = _shared.timings[(int)kind];
            uint64_t count = timings.count();
            if (count == 0) { continue; }
            LOG_INFO(_env, "%s: count=%s mean=%s p50=%s p90=%s p99=%s", kind, count, timings.mean(), timings.percentile(0.5), timings.percentile(0.9), timings.percentile(0.99));
        }
    }
};

struct ShardMetricsInserter : PeriodicLoop {
private:
    ShardShared& _shared;
    ShardId _shid;
    XmonNCAlert _alert;
    MetricsBuilder _metricsBuilder;
    std::unordered_map<std::string, uint64_t> _rocksDBStats;
public:
    ShardMetricsInserter(Logger& logger, std::shared_ptr<XmonAgent>& xmon, ShardId shid, ShardShared& shared):
        PeriodicLoop(logger, xmon, "metrics_inserter", {1_sec, 1.0, 1_mins, 0.1}),
        _shared(shared),
        _shid(shid)
    {}

    virtual bool periodicStep() {
        auto now = eggsNow();
        for (ShardMessageKind kind : allShardMessageKind) {
            const ErrorCount& errs = _shared.errors[(int)kind];
            for (int i = 0; i < errs.count.size(); i++) {
                uint64_t count = errs.count[i].load();
                if (count == 0) { continue; }
                _metricsBuilder.measurement("eggsfs_shard_requests");
                _metricsBuilder.tag("shard", _shid);
                _metricsBuilder.tag("kind", kind);
                _metricsBuilder.tag("write", !readOnlyShardReq(kind));
                if (i == 0) {
                    _metricsBuilder.tag("error", "NO_ERROR");
                } else {
                    _metricsBuilder.tag("error", (EggsError)i);
                }
                _metricsBuilder.fieldU64("count", count);
                _metricsBuilder.timestamp(now);
            }
        }
        for (int i = 0; i < 2; i++) {
            _metricsBuilder.measurement("eggsfs_shard_socket_buf");
            _metricsBuilder.tag("shard", _shid);
            _metricsBuilder.tag("socket", std::to_string(i));
            _metricsBuilder.fieldFloat("size", _shared.sockBytes[i]);
            _metricsBuilder.timestamp(now);
        }
        {
            _rocksDBStats.clear();
            _shared.db.rocksDBStats(_rocksDBStats);
            for (const auto& [name, value]: _rocksDBStats) {
                _metricsBuilder.measurement("eggsfs_shard_rocksdb");
                _metricsBuilder.tag("shard", _shid);
                _metricsBuilder.fieldU64(name, value);
                _metricsBuilder.timestamp(now);
            }
        }
        std::string err = sendMetrics(10_sec, _metricsBuilder.payload());
        _metricsBuilder.reset();
        if (err.empty()) {
            LOG_INFO(_env, "Sent metrics to influxdb");
            _env.clearAlert(_alert);
            return true;
        } else {
            _env.updateAlert(_alert, "Could not insert metrics: %s", err);
            return false;
        }
    }
};

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

    Env env(logger, xmon, "startup");

    {
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

    // xmon first, so that by the time it shuts down it'll have all the leftover requests
    if (xmon) {
        XmonConfig config;
        {
            std::ostringstream ss;
            ss << std::setw(3) << std::setfill('0') << shid;
            config.appInstance = "eggsshard" + ss.str();
        }
        config.prod = options.xmonProd;
        config.appType = "restech_eggsfs.critical";
        Xmon::spawn(*undertaker, std::make_unique<Xmon>(logger, xmon, config));
    }

    XmonNCAlert dbInitAlert;
    env.updateAlert(dbInitAlert, "initializing database");
    ShardDB db(logger, xmon, shid, options.transientDeadlineInterval, dbDir);
    env.clearAlert(dbInitAlert);

    ShardShared shared(db);

    Loop::spawn(*undertaker, std::make_unique<ShardServer>(logger, xmon, shid, options, 0, shared));
    if (options.ipPorts[1].ip != 0) {
        Loop::spawn(*undertaker, std::make_unique<ShardServer>(logger, xmon, shid, options, 1, shared));
    }

    Loop::spawn(*undertaker, std::make_unique<ShardRegisterer>(logger, xmon, shid, options, shared));
    Loop::spawn(*undertaker, std::make_unique<ShardBlockServiceUpdater>(logger, xmon, shid, options, shared));
    Loop::spawn(*undertaker, std::make_unique<ShardStatsInserter>(logger, xmon, shid, options, shared));
    if (options.metrics) {
        Loop::spawn(*undertaker, std::make_unique<ShardMetricsInserter>(logger, xmon, shid, shared));
    }

    undertaker->reap();
}