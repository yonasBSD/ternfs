#include "Common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include "Backtrace.hpp"

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

Globals::Globals()
{
    // Checks whether program info collection is requested on static initialization,
    // If so, does it and exits the process.
    {
        const char *exePath = getenv("EGGS_GET_PROGRAM_INFO");
        if (exePath != nullptr) {
            _collectingProgramInfo = true;
            try {
                int out_fd = std::stoi(getenv("EGGS_GET_PROGRAM_INFO_OUT_MEMFD"));
                loadAndSendProgramInfo(exePath, out_fd);
                exit(0);
            } catch (std::exception &e) {
                dieWithError(e.what());
            } catch (...) {
            }
            _exit(1);
        }
    }
}
Globals _globals;

std::ostream& operator<<(std::ostream& out, struct sockaddr_in& addr) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    out << buf << ":" << ntohs(addr.sin_port);
    return out;
}