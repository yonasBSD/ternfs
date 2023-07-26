#pragma once

#include <atomic>
#include <mutex>

struct Stopper {
private:
    std::atomic<bool> _stop;
    std::mutex _stopped;

public:
    Stopper() : _stop(false) {
        _stopped.lock();
    }

    bool shouldStop() const {
        return _stop.load();
    }

    void stopDone() {
        _stopped.unlock();
    }

    void stop() {
        _stop.store(true);
        _stopped.lock();
    }
};