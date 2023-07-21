#pragma once

#include <stdint.h>
#include <array>
#include <atomic>
#include <math.h>

#include "Exception.hpp"
#include "Time.hpp"
#include "Msgs.hpp"

template<size_t BINS>
struct Timings {
    static_assert(BINS > 0);
private:
    // static stuff
    double _growth;
    double _invLogGrowth;
    uint64_t _firstUpperBound;
    double _growthDivUpperBound;

    std::array<std::atomic<uint64_t>, BINS> _hist;
    // mean/variance
    std::mutex _mu;
    int64_t _count;
    double _meanMs;
    double _m2MsSq;
public:
    Timings(Duration firstUpperBound, double growth) :
        _growth(growth),
        _invLogGrowth(1.0/log(growth)),
        _firstUpperBound(firstUpperBound.ns),
        _growthDivUpperBound(growth / (double)firstUpperBound.ns),
        _count(0),
        _meanMs(0),
        _m2MsSq(0)
    {
        if (firstUpperBound < 1) {
            throw EGGS_EXCEPTION("non-positive first upper bound %s", firstUpperBound);
        }
        if (growth <= 1) {
            throw EGGS_EXCEPTION("growth %s <= 1.0", growth);
        }
        for (auto& bin: _hist) {
            bin.store(0);
        }
    }

    Duration mean() const {
        return Duration(_meanMs*1e6);
    }

    Duration stddev() const {
        if (_count == 0) { return 0; }
        return Duration(1e6 * sqrt(_m2MsSq/_count));
    }

    void add(Duration d) {
        int64_t inanos = d.ns;
        if (inanos <= 0) { return; }
        // add to bins/total
        {
            uint64_t nanos = inanos;
            int bin = std::min<int>(BINS, std::max<int>(0, log((double)nanos * _growthDivUpperBound) * _invLogGrowth));
            _hist[bin]++;
        }
        // mean/variance
        {
            std::lock_guard<std::mutex> guard(_mu); // should be almost always uncontended.
            _count++;
            double nanosMs = (double)(inanos/1'000'000ll) + (double)(inanos%1'000'000ll)/1e6;
            double delta = nanosMs - _meanMs;
            _meanMs += delta / _count;
            double delta2 = nanosMs - _meanMs;
            _m2MsSq += delta * delta2;
        }
    }

    void toStats(const std::string& prefix, std::vector<Stat>& stats) {
        static_assert(std::endian::native == std::endian::little);
        auto now = eggsNow();
        {
            auto& histStat = stats.emplace_back();
            histStat.time = now;
            histStat.name = BincodeBytes(prefix + ".histogram");
            constexpr auto elSz = sizeof(uint64_t);
            histStat.value.els.resize(elSz*BINS*2);
            double upperBound = _firstUpperBound;
            for (int i = 0; i < BINS; i++) {
                uint64_t x = upperBound;
                memcpy(histStat.value.els.data() + i*2*elSz, &x, elSz);
                x = _hist[i].load();
                memcpy(histStat.value.els.data() + i*2*elSz + elSz, &x, elSz);
                upperBound *= _growth;
            }
        }
        {
            std::lock_guard<std::mutex> guard(_mu);
            auto& countStat = stats.emplace_back();
            countStat.time = now;
            countStat.name = BincodeBytes(prefix + ".count");
            // count, mean, stddev
            constexpr auto elSz = sizeof(uint64_t);
            countStat.value.els.resize(elSz*3);
            uint8_t* data = countStat.value.els.data();
            Duration m = mean();
            Duration s = stddev();
            memcpy(data+elSz*0, &_count, elSz);
            memcpy(data+elSz*1, &m, elSz);
            memcpy(data+elSz*2, &s, elSz);
        }
    }

    void reset() {
        for (int i = 0; i < BINS; i++) {
            _hist[i].store(0);
        }
        std::lock_guard<std::mutex> guard(_mu);
        _count = 0;
        _meanMs = 0;
        _m2MsSq = 0;
    }
};
