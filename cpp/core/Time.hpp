#pragma once

#include "Common.hpp"
#include "Bincode.hpp"

struct Duration {
    int64_t ns;

    constexpr  Duration(): ns(0) {}
    constexpr Duration(uint64_t ns_): ns(ns_) {}

    bool operator==(Duration rhs) const {
        return ns == rhs.ns;
    }

    bool operator>(Duration rhs) const {
        return ns > rhs.ns;
    }

    bool operator>=(Duration rhs) const {
        return ns >= rhs.ns;
    }

    bool operator<(Duration rhs) const {
        return ns < rhs.ns;
    }

    bool operator<=(Duration rhs) const {
        return ns <= rhs.ns;
    }

    Duration operator+(Duration d) {
        return Duration(ns + d.ns);
    }

    Duration operator-(Duration d) {
        return ns - d.ns;
    }
};

constexpr Duration operator "" _ns   (unsigned long long t) { return Duration(t); }
constexpr Duration operator "" _us   (unsigned long long t) { return Duration(t*1'000); }
constexpr Duration operator "" _ms   (unsigned long long t) { return Duration(t*1'000'000); }
constexpr Duration operator "" _sec  (unsigned long long t) { return Duration(t*1'000'000'000ull); }
constexpr Duration operator "" _mins (unsigned long long t) { return Duration(t*1'000'000'000ull*60); }
constexpr Duration operator "" _hours(unsigned long long t) { return Duration(t*1'000'000'00'000ull*60*60); }

std::ostream& operator<<(std::ostream& out, Duration d);

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

    bool operator<(EggsTime rhs) const {
        return ns < rhs.ns;
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint64_t>(ns);
    }

    void unpack(BincodeBuf& buf) {
        ns = buf.unpackScalar<uint64_t>();
    }

    EggsTime operator+(Duration d) {
        return EggsTime(ns + d.ns);
    }

    Duration operator-(EggsTime d) {
        return Duration(ns - d.ns);
    }
};

std::ostream& operator<<(std::ostream& out, EggsTime t);

EggsTime eggsNow();

void sleepFor(Duration dt);