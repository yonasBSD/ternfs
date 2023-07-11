#pragma once

#include <stdint.h>
#include <mutex>
#include <deque>
#include <atomic>
#include <vector>

enum struct XmonRequestType {
    CREATE = 0x4,
    UPDATE = 0x5,
    CLEAR  = 0x3,
};

struct XmonRequest {
    XmonRequestType msgType;
    int64_t alertId;
    bool binnable;
    std::string message;
};

using XmonAlert = uint64_t;

struct XmonAgent {
private:
    std::mutex _mu;
    std::deque<XmonRequest> _requests;
    std::atomic<int64_t> _alertId;

    void _addRequest(XmonRequest&& req) {
        std::lock_guard<std::mutex> lock(_mu);
        _requests.emplace_back(req);
    }

public:
    XmonAgent() : _alertId(0) {}

    XmonAlert createAlert(bool binnable, const std::string& message) {
        XmonAlert aid = _alertId.fetch_add(1);
        XmonRequest req;
        req.msgType = XmonRequestType::CREATE;
        req.alertId = aid;
        req.binnable = binnable;
        req.message = message;
        _addRequest(std::move(req));
        return aid;
    }

    void updateAlert(XmonAlert aid, bool binnable, const std::string& message) {
        XmonRequest req;
        req.msgType = XmonRequestType::UPDATE;
        req.alertId = aid;
        req.binnable = binnable;
        req.message = message;
        _addRequest(std::move(req));
    }

    void clearAlert(XmonAlert aid) {
        XmonRequest req;
        req.msgType = XmonRequestType::CLEAR;
        req.alertId = aid;
        req.binnable = false;
        req.message = {};
        _addRequest(std::move(req));
    }

    void getRequests(std::vector<XmonRequest>& reqs) {
        std::lock_guard<std::mutex> lock(_mu);
        std::move(std::begin(_requests), std::end(_requests), std::back_inserter(reqs));
        _requests.clear();
    }
};
