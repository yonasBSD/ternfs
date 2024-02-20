#pragma once

#include <fcntl.h>
#include <stdint.h>
#include <mutex>
#include <deque>
#include <atomic>
#include <vector>

#include "Common.hpp"
#include "Exception.hpp"
#include "Time.hpp"

enum struct XmonRequestType : int32_t {
    CREATE = 0x4,
    UPDATE = 0x5,
    CLEAR  = 0x3,
};

enum struct XmonAppType : uint8_t {
    DEFAULT = 0, // same as the parent app type
    NEVER = 1,
    DAYTIME = 2,
    CRITICAL = 3,
};

struct XmonRequest {
    int64_t alertId;
    Duration quietPeriod;
    std::string message;
    XmonRequestType msgType;
    XmonAppType appType;
    bool binnable;

    // multiple writers are fine.
    void write(int fd) const;

    // Only one reader at a time, please. Return false
    // if we got EAGAIN immediately.
    bool read(int fd);
};

struct XmonNCAlert {
    int64_t alertId;
    Duration quietPeriod;
    XmonAppType appType;

    XmonNCAlert() : alertId(-1), quietPeriod(0), appType(XmonAppType::DEFAULT) {}
    XmonNCAlert(XmonAppType appType_) : alertId(-1), quietPeriod(0), appType(appType_) {}
    XmonNCAlert(XmonAppType appType_, Duration quietPeriod_) : alertId(-1), quietPeriod(quietPeriod_), appType(appType_) {}
    XmonNCAlert(Duration quietPeriod_) : alertId(-1), quietPeriod(quietPeriod_), appType(XmonAppType::DEFAULT) {}
};

struct XmonAgent {
private:
    int _pipe[2];
    std::atomic<int64_t> _alertId;

    int _writeFd() const {
        return _pipe[1];
    }

    void _sendRequest(const XmonRequest& req) {
        req.write(_writeFd());
    }

    int64_t _createAlert(XmonAppType appType, bool binnable, Duration quietPeriod, const std::string& message) {
        int64_t aid = _alertId.fetch_add(1);
        XmonRequest req;
        req.appType = appType;
        req.msgType = XmonRequestType::CREATE;
        req.alertId = aid;
        req.binnable = binnable;
        req.message = message;
        req.quietPeriod = quietPeriod;
        _sendRequest(req);
        return aid;
    }

public:
    static constexpr int64_t TOO_MANY_ALERTS_ALERT_ID = 0;

    XmonAgent() : _alertId(1) {
        if (pipe(_pipe) < 0) {
            throw SYSCALL_EXCEPTION("pipe");
        }
        if (fcntl(readFd(), F_SETFL, O_NONBLOCK) < 0) {
            throw SYSCALL_EXCEPTION("fcntl");
        }
    }

    ~XmonAgent() {
        if (close(_pipe[0]) < 0) {
            std::cerr << "Could not close read end of pipe pipe (" << errno << ")" << std::endl;
        }
        if (close(_pipe[1]) < 0) {
            std::cerr << "Could not close write end of pipe pipe (" << errno << ")" << std::endl;
        }
    }

    void raiseAlert(XmonAppType appType, const std::string& message) {
        _createAlert(appType, true, 0, message);
    }

    void updateAlert(XmonNCAlert& aid, const std::string& message) {
        if (aid.alertId < 0) {
            aid.alertId = _createAlert(aid.appType, false, aid.quietPeriod, message);
        } else {
            XmonRequest req;
            req.appType = aid.appType;
            req.msgType = XmonRequestType::UPDATE;
            req.alertId = aid.alertId;
            req.binnable = false;
            req.message = message;
            req.quietPeriod = 0;
            _sendRequest(req);
        }
    }

    void clearAlert(XmonNCAlert& aid) {
        if (likely(aid.alertId < 0)) { return; }
        XmonRequest req;
        req.appType = aid.appType;
        req.msgType = XmonRequestType::CLEAR;
        req.alertId = aid.alertId;
        req.binnable = false;
        req.message = {};
        req.quietPeriod = 0;
        _sendRequest(req);
        aid.alertId = -1;
    }

    int readFd() const {
        return _pipe[0];
    }
};
