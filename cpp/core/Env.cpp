#include "Env.hpp"
#include "Assert.hpp"

std::ostream& operator<<(std::ostream& out, LogLevel ll) {
    switch (ll) {
    case LogLevel::LOG_TRACE:
        out << "TRACE";
        break;
    case LogLevel::LOG_DEBUG:
        out << "DEBUG";
        break;
    case LogLevel::LOG_INFO:
        out << "INFO";
        break;
    case LogLevel::LOG_ERROR:
        out << "ERROR";
        break;
    default:
        throw EGGS_EXCEPTION("bad log level %s", (uint32_t)ll);
    }
    return out;
}
