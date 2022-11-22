#pragma once

#include "Common.hpp"
#include "Bincode.hpp"

constexpr uint64_t EGGS_EPOCH = 1'577'836'800'000'000'000;

struct EggsTime {
    uint64_t ns;

    EggsTime(): ns(0) {}
    EggsTime(uint64_t ns_): ns(ns_) {}

    bool operator==(EggsTime rhs) const {
        return ns == rhs.ns;
    }

    bool operator>(EggsTime rhs) const {
        return ns > rhs.ns;
    }

    bool operator>=(EggsTime rhs) const {
        return ns >= rhs.ns;
    }

    bool operator<=(EggsTime rhs) const {
        return ns <= rhs.ns;
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint64_t>(ns);
    }

    void unpack(BincodeBuf& buf) {
        ns = buf.unpackScalar<uint64_t>();
    }

    EggsTime operator+(uint64_t delta) {
        return EggsTime(ns + delta);
    }
};

std::ostream& operator<<(std::ostream& out, EggsTime t);

EggsTime eggsNow();