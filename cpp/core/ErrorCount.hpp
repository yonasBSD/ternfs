#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "MsgsGen.hpp"
#include "Metrics.hpp"

struct ErrorCount {
    std::vector<std::atomic<uint64_t>> count;

    ErrorCount() : count(maxEggsError) {
        for (int i = 0; i < count.size(); i++) {
            count[i].store(0);
        }
    }

    void add(EggsError err) {
        count[(int)err]++;
    }

    void toStats(const std::string& prefix, std::vector<Stat>& stats);
    // void toMetrics(const MetricsBuilder& builder); // will add tags and then fields
    void reset();
};
