#pragma once

#include <vector>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <linux/futex.h>

#include "Common.hpp"
#include "Exception.hpp"
#include "Time.hpp"

// This queue is designed for batching shard writes. The intended
// usage is to set _maxSize to be the maximum write queue we want
// to have before dropping requests.
//
// Won't work unless it's a single thread pushing serially and a
// single thread pulling serially.
template<typename A>
struct SPSC {
private:
    uint32_t _maxSize;
    uint32_t _sizeMask;
    alignas(64) std::atomic<uint32_t> _size;
    alignas(64) uint64_t _head;
    alignas(64) uint64_t _tail;
    std::vector<A> _elements;

public:
    SPSC(uint32_t maxSize) :
        _maxSize(maxSize),
        _sizeMask(maxSize-1),
        _head(0),
        _tail(0),
        _elements(maxSize)
    {
        ALWAYS_ASSERT(_maxSize > 0);
        ALWAYS_ASSERT((_maxSize&_sizeMask) == 0);
    }

    // Tries to push all the elements. Returns how many were actually
    // pushed. First element in `els` gets pushed first.
    uint32_t push(std::vector<A>& els) {
        uint32_t sz = _size.load(std::memory_order_acquire);

        // push as much as possible
        uint32_t toPush = std::min<uint64_t>(els.size(), _maxSize-sz);
        for (uint32_t i = 0; i < toPush; i++, _head++) {
            _elements[_head&_sizeMask] = std::move(els[i]);
        }

        // update size and wake up puller, if necessary
        uint32_t szBefore = _size.fetch_add(toPush, std::memory_order_release);
        if (szBefore == 0) {
            long ret = syscall(SYS_futex, &_size, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
            if (unlikely(ret < 0)) {
                throw SYSCALL_EXCEPTION("futex");
            }
        }

        return toPush;
    }

    // Drains at least one element, blocking if there are no elements.
    // Returns how many we've drained.
    uint32_t pull(std::vector<A>& els, uint32_t max) {
        for (;;) {
            uint32_t sz = _size.load(std::memory_order_acquire);

            if (sz == 0) { // nothing yet, let's wait
                long ret = syscall(SYS_futex, &_size, FUTEX_WAIT_PRIVATE, 0, nullptr, nullptr, 0);
                if (likely(ret == 0 || errno == EAGAIN)) {
                    continue; // try again
                }
                throw SYSCALL_EXCEPTION("futex");
            } else { // we've got something to drain
                uint32_t toDrain = std::min(sz, max);
                for (uint64_t i = _tail; i < _tail+toDrain; i++) {
                    els.emplace_back(std::move(_elements[i&_sizeMask]));
                }
                _size.fetch_sub(toDrain, std::memory_order_release);
                _tail += toDrain;
                return toDrain;
            }
        }
    }

    uint32_t size() const {
        return _size.load();
    }
};