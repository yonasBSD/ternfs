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

    // changing stuff
    std::array<std::atomic<uint64_t>, BINS> _hist;
    std::atomic<uint64_t> _count;
    std::atomic<uint64_t> _sum;
    std::atomic<uint64_t> _sumSquares;

public:
    Timings(Duration firstUpperBound, double growth) :
        _growth(growth),
        _invLogGrowth(1.0/log(growth)),
        _firstUpperBound(firstUpperBound.ns),
        _growthDivUpperBound(growth / (double)firstUpperBound.ns),
        _count(0),
        _sum(0),
        _sumSquares(0)
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
        uint64_t count = _count.load();
        if (count == 0) { return 0; }
        return Duration(_sum.load() / count);
    }

    Duration stddev() const {
        uint64_t count = _count.load();
        uint64_t sum = _sum.load();
        uint64_t sumSquares = _sumSquares.load();
        if (count == 0) { return 0; }
        uint64_t mean = sum / count;
        return Duration((uint64_t)sqrt((sumSquares / count) - mean*mean));
    }

    void add(Duration d) {
        int64_t inanos = d.ns;
        if (inanos < 0) { return; }
        uint64_t nanos = inanos;
        int bin = std::min<int>(BINS, std::max<int>(0, log((double)nanos * _growthDivUpperBound) * _invLogGrowth));
        _count++;
        _sum += nanos;
        _sumSquares += nanos*2;
        _hist[bin]++;
    }

    void toStats(const std::string& prefix, std::vector<Stat>& stats) const {
        static_assert(std::endian::native == std::endian::little);
        auto now = eggsNow();
        {
            auto& meanStat = stats.emplace_back();
            meanStat.time = now;
            meanStat.name = BincodeBytes(prefix + ".mean");
            uint64_t x = mean().ns;
            meanStat.value.els.resize(sizeof(x));
            memcpy(meanStat.value.els.data(), &x, sizeof(x));
        }
        {
            auto& stddevStat = stats.emplace_back();
            stddevStat.time = now;
            stddevStat.name = BincodeBytes(prefix + ".stddev");
            uint64_t x = stddev().ns;
            stddevStat.value.els.resize(sizeof(x));
            memcpy(stddevStat.value.els.data(), &x, sizeof(x));
        }
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
    }

    void reset() {
        for (int i = 0; i < BINS; i++) {
            _hist[i].store(0);
        }
        _count.store(0);
        _sum.store(0);
        _sumSquares.store(0);
    }
};
