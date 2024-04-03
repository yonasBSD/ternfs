#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <unistd.h>

#include "Assert.hpp"
#include "Time.hpp"

class Sock {
public:
    static Sock SockError(int errnum) {
        ALWAYS_ASSERT(errnum > 0);
        Sock err{};
        err._fd = -errnum;
        return err;
    }

    Sock(): _fd(-1) {}
    explicit Sock(int fd) : _fd(fd) { ALWAYS_ASSERT(_fd > 0); }
    Sock(const Sock&) = delete;
    Sock(Sock&& s) : _fd(s._fd) { s._fd = -1; }
    ~Sock() { if (_fd > 0) { close(_fd); _fd = -1; } }
    bool error() const {
        return _fd < 0;
    }
    int getErrno() const {
        ALWAYS_ASSERT(_fd < 0);
        return -_fd;
    }
    int get() const {
        ALWAYS_ASSERT(_fd > 0);
        return _fd;
    }
    int release() {
        int fd = _fd;
        _fd = -1;
        return fd;
    }
private:
    int _fd;
};

// For errors that we probably shouldn't retry, an exception is thrown. Otherwise
// -errno fd and error string.
std::pair<Sock, std::string> connectToHost(
    const std::string& host,
    uint16_t port,
    Duration timeout = 10_sec
);
