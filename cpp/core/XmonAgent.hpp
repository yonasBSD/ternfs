#pragma once

#include <stdint.h>
#include <mutex>
#include <deque>
#include <atomic>
#include <vector>

#include "Common.hpp"
#include "Time.hpp"

enum struct XmonRequestType : int32_t {
    CREATE = 0x4,
    UPDATE = 0x5,
    CLEAR  = 0x3,
};

struct XmonRequest {
    XmonRequestType msgType;
    int64_t alertId;
    Duration quietPeriod;
    bool binnable;
    std::string message;
};

struct XmonNCAlert {
    int64_t alertId;
    Duration quietPeriod;

    XmonNCAlert() : alertId(-1), quietPeriod(0) {}
    XmonNCAlert(Duration quietPeriod_) : alertId(-1), quietPeriod(quietPeriod_) {}
};

struct XmonAgent {
private:
    std::mutex _mu;
    std::deque<XmonRequest> _requests;
    std::atomic<int64_t> _alertId;

    void _addRequest(XmonRequest&& req) {
        std::lock_guard<std::mutex> lock(_mu);
        _requests.emplace_back(req);
    }

    int64_t _createAlert(bool binnable, Duration quietPeriod, const std::string& message) {
        int64_t aid = _alertId.fetch_add(1);
        XmonRequest req;
        req.msgType = XmonRequestType::CREATE;
        req.alertId = aid;
        req.binnable = binnable;
        req.message = message;
        req.quietPeriod = quietPeriod;
        _addRequest(std::move(req));
        return aid;
    }

public:
    static constexpr int64_t TOO_MANY_ALERTS_ALERT_ID = 0;

    XmonAgent() : _alertId(1) {}

    void raiseAlert(const std::string& message) {
        _createAlert(true, 0, message);
    }

    void updateAlert(XmonNCAlert& aid, const std::string& message) {
        if (aid.alertId < 0) {
            aid.alertId = _createAlert(false, aid.quietPeriod, message);
        } else {
            XmonRequest req;
            req.msgType = XmonRequestType::UPDATE;
            req.alertId = aid.alertId;
            req.binnable = false;
            req.message = message;
            req.quietPeriod = 0;
            _addRequest(std::move(req));
        }
    }

    void clearAlert(XmonNCAlert& aid) {
        if (likely(aid.alertId < 0)) { return; }
        XmonRequest req;
        req.msgType = XmonRequestType::CLEAR;
        req.alertId = aid.alertId;
        req.binnable = false;
        req.message = {};
        req.quietPeriod = 0;
        _addRequest(std::move(req));
        aid.alertId = -1;
    }

    void getRequests(std::deque<XmonRequest>& reqs) {
        std::lock_guard<std::mutex> lock(_mu);
        std::move(std::begin(_requests), std::end(_requests), std::back_inserter(reqs));
        _requests.clear();
    }
};
