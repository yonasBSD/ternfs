#include <unistd.h>
#include <bit>
#include <sys/socket.h>

#include "Exception.hpp"
#include "Xmon.hpp"
#include "Connect.hpp"

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
    _appType("restech.info"),
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
    buf.packScalar<uint8_t>(0); // default interval
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
    return readSoFar;
}

void Xmon::run() {
    int numFailures = 0;
    int maxFailures = 3;

    XmonBuf buf;
    int sock = -1;
    std::vector<XmonRequest> requests;

    std::string errString;

#define CHECK_ERR_STRING(__what) \
    if (errString.size()) { \
        if (numFailures >= maxFailures) { \
            throw EGGS_EXCEPTION("could not %s: %s", __what, errString); \
        } \
        LOG_INFO(_env, "could not %s: %s, will reconnect", __what, errString); \
        numFailures++; \
        sleepFor(1_sec); \
        goto reconnect; \
    }

reconnect:
    errString.clear();
    sock = connectToHost(_xmonHost, _xmonPort, errString);
    CHECK_ERR_STRING(errString);
    LOG_INFO(_env, "connected to xmon %s:%s", _xmonHost, _xmonPort);

    // We've got a socket, reset conn failures
    numFailures = 0;

    // 10ms timeout for prompt termination/processing
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10'000;
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
    bool gotHeartbeat = false;
    for (;;) {
        bool shutDown = _stopper.shouldStop();
    
        // send all requests
        if (gotHeartbeat) {
            _agent->getRequests(requests);
            for (int i = 0; i < requests.size(); i++) {
                const auto& req = requests[i];
                if (req.msgType == XmonRequestType::CREATE) {
                    LOG_DEBUG(_env, "creating alert, aid=%s binnable=%s message=%s", req.alertId, req.binnable, req.message);
                } else if (req.msgType == XmonRequestType::UPDATE) {
                    LOG_DEBUG(_env, "updating alert, aid=%s binnable=%s message=%s", req.alertId, req.binnable, req.message);
                } else if (req.msgType == XmonRequestType::CLEAR) {
                    LOG_DEBUG(_env, "clearing alert, aid=%s", req.alertId);
                } else {
                    ALWAYS_ASSERT(false, "bad req type %s", (int)req.msgType);
                }
                packRequest(buf, req);
                errString = buf.writeOut(sock);
                if (unlikely(!errString.empty())) {
                    // re-insert all the requests
                    for (int j = i; j < requests.size(); j++) {
                        _agent->addRequest(std::move(requests[j]));
                    }
                }
                CHECK_ERR_STRING(errString);
                LOG_DEBUG(_env, "sent request to xmon");
            }
            requests.clear();
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
            LOG_DEBUG(_env, "got xmon heartbeat");
            gotHeartbeat = true;
            packUpdate(buf);
            errString = buf.writeOut(sock);
            CHECK_ERR_STRING("send heartbeat");
            break; }
        case 0x1: {
            LOG_DEBUG(_env, "got alert binned from UI, ignoring");
            bool success = false;
            do {
                errString.clear();
                success = buf.readIn(sock, 8, errString);
                CHECK_ERR_STRING("reading alert binned id");
            } while (!success);
            break; }
        default:
            throw EGGS_EXCEPTION("unknown message type %s", msgType);
        }
    }
}