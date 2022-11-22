#include <sys/types.h>
#include <sys/wait.h>

#include "Common.hpp"
#include "SBRMUnix.hpp"

// Fuctions in this file can be called when current process is in SIGSEGV handler,
// so they must not call and non async-signal-safe functions, or throw exceptions, or allocate memory (malloc can be corrupted).
// Only simple syscall wrapper functions and functions in http://man7.org/linux/man-pages/man7/signal-safety.7.html are used below.

#define throw EXCEPTIONS_NOT_ALLOWED_HERE

static void read_all(int fd, void *buf, size_t cnt) {
    while (cnt != 0) {
        ssize_t s = read(fd, buf, cnt);
        if (s == -1) {
            if (errno == EINTR) continue;
            dieWithError("read");
        } else if (s == 0) {
            dieWithError("read EOF");
        }
        buf = reinterpret_cast<void*>(reinterpret_cast<char*>(buf) + s);
        cnt -= s;
    }
}

static char* WriteEnv(char *out, const char *key, const char *val) {
    char *res = out;
    out = stpcpy(out, key);
    out = stpcpy(out, "=");
    out = stpcpy(out, val);
    return res;
}

static char* WriteEnv(char *out, const char *key, unsigned int val) {
    std::array<char, 30> val_buf;
    char *it = val_buf.end() - 1;
    *it = '\0';
    if (val == 0) {
        --it;
        *it = '0';
    } else {
        while (val) {
            --it;
            *it = '0' + (val % 10);
            val /= 10;
        }
    }

    return WriteEnv(out, key, it);
}

static
int non_glibc_memfd_create(const char *name, unsigned int flags) {
    return syscall(319, name, flags);
}

// Launches a child process and waits for it to collect ProgramInfo, and mmap's the result.
DynamicMMapHolder<char*> MMapProgramInfo(const char *exePath, bool required) noexcept {
    if (getenv("EGGS_GET_PROGRAM_INFO")) dieWithError("fork bomb avoided");

    FDHolder fd = non_glibc_memfd_create("debug_data", 0);
    if (*fd == -1) dieWithError("memfd_create");

    {
        char e0[300];
        char e1[100];
        char *envp[] = {
            WriteEnv(e0, "EGGS_GET_PROGRAM_INFO", exePath),
            WriteEnv(e1, "EGGS_GET_PROGRAM_INFO_OUT_MEMFD", *fd),
            nullptr,
        };

        const char *argv[] = {"/proc/self/exe", nullptr};

        pid_t childPid = vfork();
        if (childPid == -1) dieWithError("vfork");

        if (childPid == 0) {
            execve(argv[0], const_cast<char* const*>(argv), envp);
            dieWithError("execve");
        }

        int wstatus;
        while (true) {
            pid_t res = waitpid(childPid, &wstatus, 0);
            if (res == -1 && errno == EINTR) continue;

            if (res == childPid) break;

            dieWithError("waitpid");
        }

        if (WEXITSTATUS(wstatus) != 0) {
            if (required) {
                dieWithError("Unable to map program info");
            } else {
                return {};
            }
        }
    }

    if (-1 == lseek(*fd, 0, SEEK_SET)) dieWithError("lseek");

    size_t sz;
    read_all(*fd, (void*)&sz, 8);

    char *data = (char*) mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == (char*)MAP_FAILED) dieWithError("mmap");
    read_all(*fd, data, sz);

    return DynamicMMapHolder<char*>(data, sz);
}


