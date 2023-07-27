#pragma once

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <signal.h>
#include <memory>

#include "Common.hpp"
#include "Exception.hpp"
#include "Time.hpp"
#include "XmonAgent.hpp"

enum class LogLevel : uint32_t {
    LOG_TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_ERROR = 3,
};

std::ostream& operator<<(std::ostream& out, LogLevel ll);

void installLoggerSignalHandler(void* logger);
void tearDownLoggerSignalHandler(void* logger);

struct Logger {
private:
    LogLevel _logLevel;
    LogLevel _savedLogLevel;
    std::ostream& _out;
    std::mutex _mutex;
    bool _syslog;
    bool _usr2ToDebug;
public:
    Logger(LogLevel logLevel, std::ostream& out, bool syslog, bool usr2ToDebug):
        _logLevel(logLevel), _savedLogLevel(logLevel), _out(out), _syslog(syslog), _usr2ToDebug(usr2ToDebug)
    {
        if (usr2ToDebug) {
            installLoggerSignalHandler(this);
        }
    }

    ~Logger() {
        if (_usr2ToDebug) {
            tearDownLoggerSignalHandler(this);
        }
    }

    template<typename ...Args>
    void _log(LogLevel level, const std::string& prefix, const char* fmt, Args&&... args) {
        std::stringstream ss;
        format_pack(ss, fmt, args...);
        std::string line;
        std::scoped_lock lock(_mutex);
        if (likely(_syslog)) {
            int syslogLevel;
            switch (level) {
                case LogLevel::LOG_TRACE:
                case LogLevel::LOG_DEBUG:
                    syslogLevel = 7; break;
                case LogLevel::LOG_INFO:
                    syslogLevel = 6; break;
                case LogLevel::LOG_ERROR:
                    syslogLevel = 3; break;
                default:
                    syslogLevel = 3; break; // should be throw?
            }
            while (std::getline(ss, line)) {
                _out << "<" << syslogLevel << ">" << prefix << ": " << line << std::endl;
            }
        } else {
            auto t = eggsNow();
            while (std::getline(ss, line)) {
                _out << t << " " << prefix << " [" << level << "] " << line << std::endl;
            }
        }
    }

    bool _shouldLog(LogLevel level) {
        return level >= _logLevel;
    }

    void flush() {
        std::scoped_lock lock(_mutex);
        _out.flush();
    }

    void toggleDebug();
};

struct Env {
private:
    Logger& _logger;
    std::shared_ptr<XmonAgent> _xmon;
    std::string _prefix;
public:
    Env(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& prefix): _logger(logger), _xmon(xmon), _prefix(prefix) {}

    template<typename ...Args>
    void _log(LogLevel level, const char* fmt, Args&&... args) {
        _logger._log(level, _prefix, fmt, std::forward<Args>(args)...);
    }

    template<typename ...Args>
    XmonAlert raiseAlert(XmonAlert alert, bool binnable, const char* fmt, Args&&... args) {
        std::stringstream ss;
        format_pack(ss, fmt, args...);
        std::string line;
        std::string s = ss.str();
        _log(LogLevel::LOG_ERROR, s.c_str());
        if (_xmon) {
            if (alert < 0) {
                alert = _xmon->createAlert(binnable, s);
            } else {
                _xmon->updateAlert(alert, binnable, s);
            }
        }
        return alert;
    }

    void clearAlert(XmonAlert alert) {
        if (_xmon && alert >= 0) { _xmon->clearAlert(alert); }
    }

    bool _shouldLog(LogLevel level) {
        return _logger._shouldLog(level);
    }

    void flush() {
        _logger.flush();
    }
};

#ifdef EGGS_DEBUG
    #define LOG_TRACE(env, ...) \
        do { \
            if (unlikely((env)._shouldLog(LogLevel::LOG_TRACE))) { \
                (env)._log(LogLevel::LOG_TRACE, VALIDATE_FORMAT(__VA_ARGS__)); \
            } \
        } while (false)
#else
    #define LOG_TRACE(env, ...) do {} while (false)
#endif

#define LOG_DEBUG(env, ...) \
    do { \
        if (unlikely((env)._shouldLog(LogLevel::LOG_DEBUG))) { \
            (env)._log(LogLevel::LOG_DEBUG, VALIDATE_FORMAT(__VA_ARGS__)); \
        } \
    } while (false)

#define LOG_INFO(env, ...) \
    do { \
        if (likely((env)._shouldLog(LogLevel::LOG_INFO))) { \
            (env)._log(LogLevel::LOG_INFO, VALIDATE_FORMAT(__VA_ARGS__)); \
        } \
    } while (false)

#define LOG_ERROR(env, ...) \
    do { \
        if (likely((env)._shouldLog(LogLevel::LOG_ERROR))) { \
            (env)._log(LogLevel::LOG_ERROR, VALIDATE_FORMAT(__VA_ARGS__)); \
        } \
    } while (false)

#define RAISE_ALERT(env, ...) \
    do { \
        (env).raiseAlert(-1, true, VALIDATE_FORMAT(__VA_ARGS__)); \
    } while (false)
