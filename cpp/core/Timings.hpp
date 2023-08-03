#pragma once

#include <stdint.h>
#include <atomic>
#include <math.h>

#include "Exception.hpp"
#include "Time.hpp"
#include "Msgs.hpp"

struct Timings {
    // static stuff
    double _growth;
    double _invLogGrowth;
    uint64_t _firstUpperBound;
    double _growthDivUpperBound;

    // actual data
    EggsTime _startedAt;
    std::vector<std::atomic<uint64_t>> _bins;
public:
    Timings(Duration firstUpperBound, double growth, int bins);
    Timings() = default;

    static Timings Standard() {
        // 100ns to ~20mins, 10% error
        return Timings(100_ns, 1.2, 128);
    }

    inline void add(Duration d) {
        int64_t inanos = d.ns;
        if (unlikely(inanos <= 0)) { return; }
        uint64_t nanos = inanos;
        int bin = std::min<int>(_bins.size()-1, std::max<int>(0, log((double)nanos * _growthDivUpperBound) * _invLogGrowth));
        _bins[bin]++;
    }

    void toStats(const std::string& prefix, std::vector<Stat>& stats);
    void reset();
};
