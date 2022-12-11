#pragma once

#include <atomic>

#include "Common.hpp"
#include "Exception.hpp"

struct AssertiveLocked {
private:
    std::atomic<bool>& _held;
public:
    AssertiveLocked(std::atomic<bool>& held): _held(held) {
        bool expected = false;
        if (!_held.compare_exchange_strong(expected, true)) {
            throw EGGS_EXCEPTION("could not aquire lock, are you using this function concurrently?");
        }
    }

    AssertiveLocked(const AssertiveLocked&) = delete;
    AssertiveLocked& operator=(AssertiveLocked&) = delete;

    ~AssertiveLocked() {
        _held.store(false);
    }
};

struct AssertiveLock {
private:
    std::atomic<bool> _held;
public:
    AssertiveLock(): _held(false) {}

    AssertiveLock(const AssertiveLock&) = delete;
    AssertiveLock& operator=(AssertiveLock&) = delete;

    AssertiveLocked lock() {
        return AssertiveLocked(_held);
    }
};
