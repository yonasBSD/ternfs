#include "ErrorCount.hpp"

void ErrorCount::toStats(const std::string& prefix, std::vector<Stat>& stats) {
    static_assert(std::endian::native == std::endian::little);
    auto now = eggsNow();
    {
        std::vector<std::pair<EggsError, uint64_t>> seen;
        for (int i = 0; i < _count.size(); i++) {
            uint64_t c = _count[i].load();
            if (c == 0) { continue; }
            seen.push_back({ (EggsError)i, c });
        }
        auto& errStat = stats.emplace_back();
        errStat.time = now;
        errStat.name = BincodeBytes(prefix + ".errors");
        // error + count
        errStat.value.els.resize(seen.size() * (2 + 8));
        for (int i = 0; i < seen.size(); i++) {
            const auto& errCount = seen[i];
            memcpy(errStat.value.els.data() + i*(2 + 8), &errCount.first, 2);
            memcpy(errStat.value.els.data() + i*(2 + 8) + 2, &errCount.second, 8);
        }
    }
}

void ErrorCount::reset() {
    for (int i = 0; i < _count.size(); i++) {
        _count[i].store(0);
    }
}
