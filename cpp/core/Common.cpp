#include "Common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

// Throwing in static initialization is nasty, and there is no useful stacktrace
// Also use direct syscalls to write the error as iostream might not be initialized
// https://bugs.llvm.org/show_bug.cgi?id=28954
void dieWithError(const char *err) {
    size_t remaining = strlen(err);
    while (remaining) {
        size_t s = write(STDERR_FILENO, err, remaining);
        if (s == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                _exit(1); // Can't write remaining error but nothing else can be done..
            }
        }
        remaining -= s;
        err += s;
    }
    size_t _ = write(STDERR_FILENO, "\n", 1);
    _exit(1);
}

std::ostream& operator<<(std::ostream& out, struct sockaddr_in& addr) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    out << buf << ":" << ntohs(addr.sin_port);
    return out;
}

std::ostream& goLangBytesFmt(std::ostream& out, const char* str, size_t len) {
    out << "[";
    const uint8_t* data = (const uint8_t*)str;
    for (int i = 0; i < len; i++) {
        if (i > 0) {
            out << " ";
        }
        out << (int)data[i];
    }
    out << "]";
    return out;
}

std::ostream& operator<<(std::ostream& out, const GoLangBytesFmt& bytes) {
    return goLangBytesFmt(out, bytes.str, bytes.len);
}