#include <unistd.h>
#include <bit>
#include <sys/socket.h>
#include <unordered_set>

#include "Exception.hpp"
#include "Time.hpp"
#include "Xmon.hpp"
#include "Connect.hpp"
#include "XmonAgent.hpp"

enum struct XmonMood : int32_t {
    Happy = 0,
    Perturbed = 1,
    Upset = 2,
};

static std::string explicitGenerateErrString(const std::string& what, int err, const char* str) {
    std::stringstream ss;
    ss << "could not " << what << ": " << err << "/" << str;
    return ss.str();
}

static std::string generateErrString(const std::string& what, int err) {
    return explicitGenerateErrString(what, err, (std::string(translateErrno(err)) + "=" + safe_strerror(err)).c_str());
}

Xmon::Xmon(
    Logger& logger,
    std::shared_ptr<XmonAgent>& agent,
    const XmonConfig& config
) :
    _env(logger, agent, "xmon"),
    _agent(agent),
    _appType(config.appType),
    _xmonHost(config.prod ? "REDACTED" : "REDACTED"),
    _xmonPort(5004)
{
    if (config.appInstance.empty()) {
        throw EGGS_EXCEPTION("empty app name");
    }
    {
        char buf[HOST_NAME_MAX];
        int res = gethostname(buf, HOST_NAME_MAX);
        if (res != 0) {
            throw SYSCALL_EXCEPTION("gethostname");
        }
        _hostname = buf;
        _hostname = _hostname.substr(0, _hostname.find("."));
    }
    _appInstance = config.appInstance + "@" + _hostname;
}

const uint8_t HEARTBEAT_INTERVAL_SECS = 5; // 5 seconds

void Xmon::packLogon(XmonBuf& buf) {
    // <https://REDACTED>
    // Name 	Type 	Description
    // Magic 	int16 	always 'T'
    // Version 	int32 	version number (latest version is 4)
    // Message type 	int32 	0x0
    // (unused) 	int32 	must be 0
    // Hostname 	string 	your hostname
    // Heartbeat interval 	byte 	in seconds, 0 for default (5s))
    // (unused) 	byte 	must be 0
    // (unused) 	int16 	must be 0
    // App type 	string 	defines rota and schedule
    // App inst 	string 	arbitrary identifier
    // Heap size 	int64 	(deprecated)
    // Num threads 	int32 	(deprecated)
    // Num errors 	int32 	(deprecated)
    // Mood 	int32 	current mood
    // Details JSON 	string 	(unused)
    buf.reset();
    buf.packScalar<int16_t>('T'); // magic
    buf.packScalar<int32_t>(4);   // version
    buf.packScalar<int32_t>(0x0); // message type
    buf.packScalar<int32_t>(0x0); // unused
    buf.packString(_hostname); // hostname
    buf.packScalar<uint8_t>(HEARTBEAT_INTERVAL_SECS); // default interval
    buf.packScalar<uint8_t>(0); // unused
    buf.packScalar<int16_t>(0); // unused
    buf.packString(_appType); // app type
    buf.packString(_appInstance); // app instance
    buf.packScalar<int64_t>(0); // heap size
    buf.packScalar<int32_t>(0); // num threads
    buf.packScalar<int32_t>(0); // num errors
    buf.packScalar<XmonMood>(XmonMood::Happy); // mood
    std::string details;
    buf.packString(details); // details
}

void Xmon::packUpdate(XmonBuf& buf) {
    buf.reset();
    // Message type 	int32 	0x1
    // (unused) 	int32 	must be 0
    // Heap size 	int64 	(deprecated)
    // Num threads 	int32 	(deprecated)
    // Num errors 	int32 	(deprecated)
    // Mood 	int32 	updated mood
    buf.packScalar<int32_t>(0x1);
    buf.packScalar<int32_t>(0);
    buf.packScalar<int64_t>(0);
    buf.packScalar<int32_t>(0);
    buf.packScalar<int32_t>(0);
    buf.packScalar<XmonMood>(XmonMood::Happy);
}

void Xmon::packRequest(XmonBuf& buf, const XmonRequest& req) {
    buf.reset();
    buf.packScalar(req.msgType);
    buf.packScalar(req.alertId);
    if (req.msgType == XmonRequestType::CREATE || req.msgType == XmonRequestType::UPDATE) {
        buf.packScalar(req.binnable);
        buf.packString(req.message);
    }
}

// Automatically keeps going on EAGAIN
std::string XmonBuf::writeOut(int fd) {
    const char* curr = buf + cur;
    const char* end = buf + len;
    while (curr < end) {
        ssize_t written = write(fd, curr, end-curr);
        if (written < 0) {
            return generateErrString("writing out xmon buf", errno);
        }
        if (written == 0) {
            return "unexpected EOF";
        }
        curr += written;
    }
    return {};
}

bool XmonBuf::readIn(int fd, size_t sz, std::string& errString) {
    reset();
    ensureUnpackSizeOrPanic(sz);
    size_t readSoFar = 0;
    while (readSoFar < sz) {
        ssize_t r = read(fd, buf+cur+readSoFar, sz-readSoFar); 
        if (r < 0 && errno == EAGAIN && readSoFar == 0) {
            return false;
        }
        if (r < 0) {
            errString = generateErrString("read in xmon buf", errno);
            return false;
        }
        if (r == 0) {
            errString = "unexpected EOF";
            return false;
        }
        readSoFar += r;
    }
    len += sz;
    return true;
}

constexpr int MAX_BINNABLE_ALERTS = 20;

struct QuietAlert {
    EggsTime quietUntil;
    std::string message;
};

void Xmon::run() {
    XmonBuf buf;
    int sock = -1;
    std::deque<XmonRequest> requests;
    std::unordered_set<int64_t> binnableAlerts;
    std::unordered_map<int64_t, QuietAlert> quietAlerts;

    std::string errString;

#define CHECK_ERR_STRING(__what) \
    if (errString.size()) { \
        LOG_INFO(_env, "could not %s: %s, will reconnect", __what, errString); \
        sleepFor(1_sec); \
        goto reconnect; \
    }

reconnect:
    errString.clear();
    sock = connectToHost(_xmonHost, _xmonPort, errString);
    CHECK_ERR_STRING(errString);
    LOG_INFO(_env, "connected to xmon %s:%s", _xmonHost, _xmonPort);

    // 100ms timeout for prompt termination/processing
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100'000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
            throw SYSCALL_EXCEPTION("setsockopt");
        }
    }

    // Send logon message
    packLogon(buf);
    errString = buf.writeOut(sock);
    CHECK_ERR_STRING(errString);
    LOG_INFO(_env, "sent logon to xmon, appType=%s appInstance=%s", _appType, _appInstance);

    // Start recv loop
    EggsTime gotHeartbeatAt = 0;
    for (;;) {
        // try to send all requests before shutting down
        bool shutDown = _stopper.shouldStop();
    
        // shut down if too much time has passed
        if (gotHeartbeatAt > 0 && (eggsNow() - gotHeartbeatAt > (uint64_t)HEARTBEAT_INTERVAL_SECS * 2 * 1'000'000'000ull)) {
            LOG_INFO(_env, "heartbeat deadline has passed, will reconnect");
            gotHeartbeatAt = 0;
            goto reconnect;
        }

        // send all requests
        if (gotHeartbeatAt > 0) {
            auto now = eggsNow();
            // unquiet alerts that are due
            for (auto it = quietAlerts.begin(); it != quietAlerts.end();) {
                if (now >= it->second.quietUntil) {
                    requests.emplace_back(XmonRequest{
                        .msgType = XmonRequestType::CREATE,
                        .alertId = it->first,
                        .quietPeriod = 0,
                        .binnable = false,
                        .message = std::move(it->second.message),
                    });
                    it = quietAlerts.erase(it);
                } else {
                    it++;
                }
            }
            // get all requests
            _agent->getRequests(requests);
            while (!requests.empty()) {
                const auto& req = requests.front();
                LOG_INFO(_env, "got req of type %s alertId=%s", (int)XmonRequestType::CREATE, req.alertId);
                int64_t alertIdToInsert = -1;
                if (req.msgType == XmonRequestType::CREATE) {
                    if (req.quietPeriod > 0) {
                        ALWAYS_ASSERT(!req.binnable, "got alert with quietPeriod=%s, but it is binnable", req.quietPeriod);
                        LOG_INFO(_env, "got non-binnable alertId=%s message=%s quietPeriod=%s, will wait", req.alertId, req.message, req.quietPeriod);
                        quietAlerts[req.alertId] = QuietAlert{
                            .quietUntil = now + req.quietPeriod,
                            .message = std::move(req.message),
                        };
                        goto skip_request;
                    } else if (req.binnable) {
                        alertIdToInsert = req.alertId;
                        if (binnableAlerts.size() > MAX_BINNABLE_ALERTS) {
                            LOG_ERROR(_env, "not creating alert, aid=%s binnable=%s message=%s, we're full", req.alertId, req.binnable, req.message);
                            if (binnableAlerts.count(XmonAgent::TOO_MANY_ALERTS_ALERT_ID) == 0) {
                                XmonRequest req{
                                    .msgType = XmonRequestType::CREATE,
                                    .alertId = XmonAgent::TOO_MANY_ALERTS_ALERT_ID,
                                    .quietPeriod = 0,
                                    .binnable = true,
                                    .message = "too many alerts, alerts dropped",
                                };
                                packRequest(buf, req);
                                alertIdToInsert = XmonAgent::TOO_MANY_ALERTS_ALERT_ID;
                                goto write_request;
                            } else {
                                goto skip_request;
                            }
                        }
                    }
                    LOG_INFO(_env, "creating alert, aid=%s binnable=%s message=%s", req.alertId, req.binnable, req.message);
                } else if (req.msgType == XmonRequestType::UPDATE) {
                    ALWAYS_ASSERT(!req.binnable);
                    auto quiet = quietAlerts.find(req.alertId);
                    if (quiet != quietAlerts.end()) {
                        LOG_INFO(_env, "skipping update alertId=%s message=%s since it's still quiet", req.alertId, req.message);
                        quiet->second.message = std::move(req.message);
                        goto skip_request;
                    }
                    LOG_INFO(_env, "updating alert, aid=%s binnable=%s message=%s", req.alertId, req.binnable, req.message);
                } else if (req.msgType == XmonRequestType::CLEAR) {
                    ALWAYS_ASSERT(!req.binnable);
                    auto quiet = quietAlerts.find(req.alertId);
                    if (quiet != quietAlerts.end()) {
                        LOG_INFO(_env, "skipping clear alertId=%s since it's still quiet", req.alertId);
                        quietAlerts.erase(quiet);
                        goto skip_request;
                    }                    
                    LOG_INFO(_env, "clearing alert, aid=%s", req.alertId);
                } else {
                    ALWAYS_ASSERT(false, "bad req type %s", (int)req.msgType);
                }
                packRequest(buf, req);
            write_request:
                errString = buf.writeOut(sock);
                CHECK_ERR_STRING(errString);
                LOG_DEBUG(_env, "sent request to xmon");
                if (alertIdToInsert >= 0) {
                    binnableAlerts.insert(alertIdToInsert);
                }
            skip_request:
                requests.pop_front();
            }
        }

        // Check if we're done
        if (shutDown) {
            LOG_DEBUG(_env, "got told to stop, stopping");
            close(sock);
            _stopper.stopDone();
            return;
        }

        errString.clear();
        bool success = buf.readIn(sock, 4, errString);
        CHECK_ERR_STRING("read message type");
        if (!success) { continue; }
        int32_t msgType = buf.unpackScalar<int32_t>();
        switch (msgType) {
        case 0x0: {
            if (gotHeartbeatAt == 0) {
                LOG_INFO(_env, "got first xmon heartbeat, will start sending requests");
            } else {
                LOG_DEBUG(_env, "got xmon heartbeat");
            }
            gotHeartbeatAt = eggsNow();
            packUpdate(buf);
            errString = buf.writeOut(sock);
            CHECK_ERR_STRING("send heartbeat");
            break; }
        case 0x1: {
            bool success = false;
            do {
                errString.clear();
                success = buf.readIn(sock, 8, errString);
                CHECK_ERR_STRING("reading alert binned id");
            } while (!success);
            int64_t alertId = buf.unpackScalar<int64_t>();
            LOG_INFO(_env, "got alert %s binned from UI", alertId);
            binnableAlerts.erase(alertId);
            break; }
        default:
            throw EGGS_EXCEPTION("unknown message type %s", msgType);
        }
    }
}

void* runXmon(void* server) {
    ((Xmon*)server)->run();
    return nullptr;
}