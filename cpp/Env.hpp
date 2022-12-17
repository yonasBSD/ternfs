#pragma once

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

#include "Common.hpp"
#include "Time.hpp"

enum class LogLevel : uint32_t {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_ERROR = 2,
};

std::ostream& operator<<(std::ostream& out, LogLevel ll);

struct Logger {
private:
    LogLevel _logLevel;
    std::ostream& _out;
    std::mutex _mutex;
public:
    Logger(LogLevel logLevel, std::ostream& out): _logLevel(logLevel), _out(out) {}

    template<typename ...Args>
    void _log(LogLevel level, const std::string& prefix, const char* fmt, Args&&... args) {
        if (level < _logLevel) {
            return;
        }

        std::scoped_lock lock(_mutex);
        std::stringstream ss;
        format_pack(ss, fmt, args...);
        std::string line;
        auto t = eggsNow();
        while (std::getline(ss, line)) {
            _out << t << " " << prefix << " [" << level << "] " << line << std::endl;
        }
    }

    void flush() {
        std::scoped_lock lock(_mutex);
        _out.flush();
    }
};

struct Env {
private:
    Logger& _logger;
    std::string _prefix;
public:
    Env(Logger& logger, const std::string& prefix): _logger(logger), _prefix(prefix) {}

    template<typename ...Args>
    void _log(LogLevel level, const char* fmt, Args&&... args) {
        _logger._log(level, _prefix, fmt, std::forward<Args>(args)...);
    }

    template<typename ...Args>
    void _raiseAlert(const char* fmt, Args&&... args) {
        _log(LogLevel::LOG_ERROR, fmt, std::forward<Args>(args)...);
    }

    void flush() {
        _logger.flush();
    }
};

#ifdef EGGS_DEBUG
    #define LOG_DEBUG(env, ...) \
        do { \
            (env)._log(LogLevel::LOG_DEBUG, VALIDATE_FORMAT(__VA_ARGS__)); \
        } while (false)
#else
    #define LOG_DEBUG(env, ...) do {} while (false)
#endif

#define LOG_INFO(env, ...) \
    do { \
        (env)._log(LogLevel::LOG_INFO, VALIDATE_FORMAT(__VA_ARGS__)); \
    } while (false)

#define LOG_ERROR(env, ...) \
    do { \
        (env)._log(LogLevel::LOG_ERROR, VALIDATE_FORMAT(__VA_ARGS__)); \
    } while (false)

// The interface for this will be different -- we want some kind of alert object
// like the rest of the C++ codebase. But for now use this as a marker to find
// where alerts are needed.
#define RAISE_ALERT(env, ...) \
    do { \
        (env)._raiseAlert(VALIDATE_FORMAT(__VA_ARGS__)); \
    } while (false)
