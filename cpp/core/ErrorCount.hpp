#pragma once

#include <atomic>
#include <vector>

#include "Msgs.hpp"

struct ErrorCount {
private:
    std::vector<std::atomic<uint64_t>> _count;

public:
    ErrorCount() : _count(maxEggsError) {
        for (int i = 0; i < _count.size(); i++) {
            _count[i].store(0);
        }
    }

    void add(EggsError err) {
        _count[(int)err]++;
    }

    void toStats(const std::string& prefix, std::vector<Stat>& stats);
    void reset();
};