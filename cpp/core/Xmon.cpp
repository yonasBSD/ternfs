#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <bit>
#include <sys/socket.h>
#include <unordered_set>
#include <sys/timerfd.h>

#include "Env.hpp"
#include "Exception.hpp"
#include "Time.hpp"
#include "Xmon.hpp"
#include "Connect.hpp"
#include "XmonAgent.hpp"
#include "strerror.h"

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
    Loop(logger, agent, "xmon"),
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

    // xmon socket
    _fds[SOCK_FD].fd = -1;
    _fds[SOCK_FD].events = POLLIN;
    // xmon agent read end
    _fds[PIPE_FD].fd = agent->readFd();
    _fds[PIPE_FD].events = POLLIN;
    // timer fd
    _fds[TIMER_FD].fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (_fds[TIMER_FD].fd < 0) {
        throw SYSCALL_EXCEPTION("timerfd_create");
    }
    _fds[TIMER_FD].events = POLLIN;

    // arm initial timer
    {
        auto now = eggsNow();
        _timerExpiresAt = -1;
        _ensureTimer(now, now);
    }
}

Xmon::~Xmon() {
    if (_fds[SOCK_FD].fd >= 0) {
        if (close(_fds[0].fd) < 0) {
            std::cerr << "Could not close xmon socket (" << errno << ")" << std::endl;
        }
    }
    if (close(_fds[TIMER_FD].fd) < 0) {
        std::cerr << "Could not close timer fd (" << errno << ")" << std::endl;
    }
}

void Xmon::_ensureTimer(EggsTime now, EggsTime t) {
    if (_timerExpiresAt <= t) { return; }
    Duration d = std::max<Duration>(1, t - now);
    LOG_DEBUG(_env, "arming timer in %s", d);
    struct itimerspec utmr;
    memset(&utmr, 0, sizeof(utmr));
    utmr.it_value = d.timespec();
    if (timerfd_settime(_fds[TIMER_FD].fd, 0, &utmr, nullptr) < 0) {
        throw SYSCALL_EXCEPTION("timerfd_settime");
    }
    _timerExpiresAt = t;
}

const Duration HEARTBEAT_INTERVAL = 10_sec;
const uint8_t HEARTBEAT_INTERVAL_SECS = HEARTBEAT_INTERVAL.ns/1'000'000'000ull;

void Xmon::_packLogon(XmonBuf& buf) {
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

void Xmon::_packUpdate(XmonBuf& buf) {
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

void Xmon::_packRequest(XmonBuf& buf, const XmonRequest& req) {
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
        if (unlikely(r < 0)) {
            errString = generateErrString("read in xmon buf", errno);
            return false;
        }
        if (unlikely(r == 0)) {
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

EggsTime Xmon::_stepNextWakeup() {
    std::string errString;
    EggsTime nextWakeup = -1;

#define CHECK_ERR_STRING(__what) \
    if (errString.size()) { \
        LOG_INFO(_env, "could not %s: %s, will reconnect", __what, errString); \
        if (_fds[SOCK_FD].fd >= 0 && close(_fds[SOCK_FD].fd) < 0) { \
            LOG_INFO(_env, "could not close xmon socket after error: %s (%s)", errno, safe_strerror(errno)); \
        } \
        _fds[SOCK_FD].fd = -1; \
        (1_sec).sleep(); \
        return nextWakeup; \
    }

    errString.clear();

    // Not connected, connect
    if (_fds[SOCK_FD].fd < 0) {
        _fds[SOCK_FD].fd = connectToHost(_xmonHost, _xmonPort, errString);
        CHECK_ERR_STRING(errString);
        LOG_INFO(_env, "connected to xmon %s:%s", _xmonHost, _xmonPort);
         
        // non blocking receive
        if (fcntl(_fds[SOCK_FD].fd, F_SETFL, O_NONBLOCK) < 0) {
            throw SYSCALL_EXCEPTION("fcntl");
        }

        // Send logon message
        _packLogon(_buf);
        errString = _buf.writeOut(_fds[SOCK_FD].fd);
        CHECK_ERR_STRING(errString);
        LOG_INFO(_env, "sent logon to xmon, appType=%s appInstance=%s", _appType, _appInstance);

        _gotHeartbeatAt = 0;
        nextWakeup = std::min(nextWakeup, eggsNow() + HEARTBEAT_INTERVAL*2);
    }

    if (poll(_fds, NUM_FDS, -1) < 0) {
        throw SYSCALL_EXCEPTION("poll");
    }

    auto now = eggsNow();

    if (_fds[SOCK_FD].revents & (POLLIN|POLLHUP|POLLERR)) {
        LOG_DEBUG(_env, "got event in sock fd");
        for (;;) {
            // Receive everything
            errString.clear();
            bool success = _buf.readIn(_fds[SOCK_FD].fd, 4, errString);
            CHECK_ERR_STRING("read message type");
            if (!success) { break; }
            int32_t msgType = _buf.unpackScalar<int32_t>();
            switch (msgType) {
            case 0x0: {
                if (_gotHeartbeatAt == 0) {
                    LOG_INFO(_env, "got first xmon heartbeat, will start sending requests");
                } else {
                    LOG_DEBUG(_env, "got xmon heartbeat");
                }
                _gotHeartbeatAt = now;
                nextWakeup = std::min(nextWakeup, now + HEARTBEAT_INTERVAL*2);
                _packUpdate(_buf);
                errString = _buf.writeOut(_fds[SOCK_FD].fd);
                CHECK_ERR_STRING("send heartbeat");
                break; }
            case 0x1: {
                bool success = false;
                do {
                    errString.clear();
                    success = _buf.readIn(_fds[SOCK_FD].fd, 8, errString);
                    CHECK_ERR_STRING("reading alert binned id");
                } while (!success);
                int64_t alertId = _buf.unpackScalar<int64_t>();
                LOG_INFO(_env, "got alert %s binned from UI", alertId);
                _binnableAlerts.erase(alertId);
                break; }
            default:
                throw EGGS_EXCEPTION("unknown message type %s", msgType);
            }
        }
    }

    if (_fds[TIMER_FD].revents & POLLIN) {
        LOG_DEBUG(_env, "got event in timer FD");
        // drain timer
        {
            char buf[8];
            if (read(_fds[TIMER_FD].fd, buf, sizeof(buf)) != sizeof(buf)) {
                throw SYSCALL_EXCEPTION("read");
            }
        }
        _timerExpiresAt = -1; // it just fired

        // check if we're past the deadline
        if (_gotHeartbeatAt > 0) {
            nextWakeup = std::min(nextWakeup, _gotHeartbeatAt + HEARTBEAT_INTERVAL*2);
            LOG_DEBUG(_env, "nextWakeup=%s", nextWakeup);
            if ((now-_gotHeartbeatAt > HEARTBEAT_INTERVAL*2)) {
                LOG_INFO(_env, "heartbeat deadline has passed, will reconnect");
                if (close(_fds[SOCK_FD].fd) < 0) {
                    throw SYSCALL_EXCEPTION("close");
                }
                _fds[SOCK_FD].fd = -1;
                return nextWakeup;
            }
        }

        // unquiet alerts that are due
        for (auto it = _quietAlerts.begin(); it != _quietAlerts.end();) {
            if (now >= it->second.quietUntil) {
                nextWakeup = std::min(nextWakeup, it->second.quietUntil);
                _queuedRequests.emplace_back(XmonRequest{
                    .msgType = XmonRequestType::CREATE,
                    .alertId = it->first,
                    .quietPeriod = 0,
                    .binnable = false,
                    .message = std::move(it->second.message),
                });
                it = _quietAlerts.erase(it);
            } else {
                it++;
            }
        }
    }

    if (_fds[PIPE_FD].revents & (POLLIN|POLLHUP|POLLERR)) {
        LOG_DEBUG(_env, "got event in pipe FD");
        // drain pipe
        for (;;) {
            XmonRequest req;
            if (!req.read(_fds[PIPE_FD].fd)) { break; }
            _queuedRequests.emplace_back(std::move(req));
        }
    }

    // Write out all requests, if socket is alive anyway
    if (_gotHeartbeatAt > 0) {
        while (!_queuedRequests.empty()) {
            const auto& req = _queuedRequests.front();
            LOG_INFO(_env, "got req of type %s alertId=%s", (int)XmonRequestType::CREATE, req.alertId);
            int64_t alertIdToInsert = -1;
            if (req.msgType == XmonRequestType::CREATE) {
                if (req.quietPeriod > 0) {
                    ALWAYS_ASSERT(!req.binnable, "got alert with quietPeriod=%s, but it is binnable", req.quietPeriod);
                    LOG_INFO(_env, "got non-binnable alertId=%s message=%s quietPeriod=%s, will wait", req.alertId, req.message, req.quietPeriod);
                    EggsTime quietUntil = now + req.quietPeriod;
                    nextWakeup = std::min(nextWakeup, quietUntil);
                    _quietAlerts[req.alertId] = QuietAlert{
                        .quietUntil = quietUntil,
                        .message = std::move(req.message),
                    };
                    goto skip_request;
                } else if (req.binnable) {
                    alertIdToInsert = req.alertId;
                    if (_binnableAlerts.size() > MAX_BINNABLE_ALERTS) {
                        LOG_ERROR(_env, "not creating alert, aid=%s binnable=%s message=%s, we're full", req.alertId, req.binnable, req.message);
                        if (_binnableAlerts.count(XmonAgent::TOO_MANY_ALERTS_ALERT_ID) == 0) {
                            XmonRequest req{
                                .msgType = XmonRequestType::CREATE,
                                .alertId = XmonAgent::TOO_MANY_ALERTS_ALERT_ID,
                                .quietPeriod = 0,
                                .binnable = true,
                                .message = "too many alerts, alerts dropped",
                            };
                            _packRequest(_buf, req);
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
                auto quiet = _quietAlerts.find(req.alertId);
                if (quiet != _quietAlerts.end()) {
                    LOG_INFO(_env, "skipping update alertId=%s message=%s since it's still quiet", req.alertId, req.message);
                    quiet->second.message = std::move(req.message);
                    goto skip_request;
                }
                LOG_INFO(_env, "updating alert, aid=%s binnable=%s message=%s", req.alertId, req.binnable, req.message);
            } else if (req.msgType == XmonRequestType::CLEAR) {
                ALWAYS_ASSERT(!req.binnable);
                auto quiet = _quietAlerts.find(req.alertId);
                if (quiet != _quietAlerts.end()) {
                    LOG_INFO(_env, "skipping clear alertId=%s since it's still quiet", req.alertId);
                    _quietAlerts.erase(quiet);
                    goto skip_request;
                }                    
                LOG_INFO(_env, "clearing alert, aid=%s", req.alertId);
            } else {
                ALWAYS_ASSERT(false, "bad req type %s", (int)req.msgType);
            }
            _packRequest(_buf, req);
        write_request:
            errString = _buf.writeOut(_fds[SOCK_FD].fd);
            CHECK_ERR_STRING(errString);
            LOG_DEBUG(_env, "sent request to xmon");
            if (alertIdToInsert >= 0) {
                _binnableAlerts.insert(alertIdToInsert);
            }
        skip_request:
            _queuedRequests.pop_front();
        }
    }

    return nextWakeup;
}

void Xmon::step() {
    EggsTime nextTimer = _stepNextWakeup();
    _ensureTimer(eggsNow(), nextTimer);
}