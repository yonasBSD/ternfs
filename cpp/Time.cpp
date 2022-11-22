#include <bits/types/struct_timespec.h>
#include <cstdio>
#include <time.h>

#include "Exception.hpp"
#include "Time.hpp"
#include "Assert.hpp"

EggsTime eggsNow() {
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        throw SYSCALL_EXCEPTION("clock_gettime");
    }

    return EggsTime(now.tv_nsec + (now.tv_sec * 1'000'000'000ull));
}

std::ostream& operator<<(std::ostream& out, EggsTime eggst) {
    time_t secs =  (EGGS_EPOCH + eggst.ns) / 1'000'000'000ull;
    uint64_t nsecs = eggst.ns % 1'000'000'00ull;
    struct tm tm;
    if (gmtime_r(&secs, &tm) == nullptr) {
        throw SYSCALL_EXCEPTION("gmtime_r");
    }
    // "2006-01-02T15:04:05.999999999"
    char buf[31];
    ALWAYS_ASSERT(strftime(buf, 29, "%Y-%m-%dT%H:%M:%S.", &tm) == 20);
    ALWAYS_ASSERT(snprintf(buf+20, 10, "%09lu", nsecs) == 9);
    buf[30] = '\0';
    out << buf;
    return out;
}