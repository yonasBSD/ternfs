#include "Timings.hpp"

Timings::Timings(Duration firstUpperBound, double growth, int bins) :
    _growth(growth),
    _invLogGrowth(1.0/log(growth)),
    _firstUpperBound(firstUpperBound.ns),
    _growthDivUpperBound(growth / (double)firstUpperBound.ns),
    _startedAt(eggsNow()),
    _bins(bins)
{
    if (firstUpperBound < 1) {
        throw EGGS_EXCEPTION("non-positive first upper bound %s", firstUpperBound);
    }
    if (growth <= 1) {
        throw EGGS_EXCEPTION("growth %s <= 1.0", growth);
    }
    for (auto& bin: _bins) {
        bin.store(0);
    }
}

void Timings::toStats(const std::string& prefix, std::vector<Stat>& stats) {
    static_assert(std::endian::native == std::endian::little);
    auto now = eggsNow();
    auto elapsed = now - _startedAt;
    {
        auto& histStat = stats.emplace_back();
        histStat.time = now;
        histStat.name = BincodeBytes(prefix + ".latency");
        // elapsed, then upperbound+count
        histStat.value.els.resize(8 + 2*8*_bins.size());
        memcpy(histStat.value.els.data(), &elapsed, 8);
        double upperBound = _firstUpperBound;
        for (int i = 0; i < _bins.size(); i++) {
            uint64_t x = upperBound;
            memcpy(histStat.value.els.data() + 8 + i*2*8, &x, 8);
            x = _bins[i].load();
            memcpy(histStat.value.els.data() + 8 + i*2*8 + 8, &x, 8);
            upperBound *= _growth;
        }
    }
}

void Timings::reset() {
    _startedAt = eggsNow();
    for (auto& bin : _bins) {
        bin.store(0);
    }
}

Duration Timings::mean() const {
    double m = 0;
    double c = count();
    double upperBound = _firstUpperBound;
    for (const auto& bin : _bins) {
        m += (double)bin.load() * (upperBound / c);
        upperBound *= _growth;
    }
    return Duration(m);
}

Duration Timings::percentile(double p) const {
    uint64_t countSoFar = 0;
    uint64_t toReach = count()*p;
    double upperBound = _firstUpperBound;
    for (const auto& bin: _bins) {
        countSoFar += bin.load();
        if (countSoFar >= toReach) { break; }
        upperBound *= _growth;
    }
    return Duration(upperBound);
}