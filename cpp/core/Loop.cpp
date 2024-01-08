#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "Assert.hpp"
#include "Loop.hpp"
#include "Exception.hpp"

// The sigset we run with normally, with SIGINT/SIGTERM masked.
static sigset_t baseSigset;
// The sigset to run when running blocking syscalls, with SIGINT/SIGTERM unmasked.
static sigset_t blockingSigset;

__attribute__((constructor))
static void setupSigsets() {
    sigemptyset(&baseSigset);
    blockingSigset = baseSigset;
    sigaddset(&baseSigset, SIGINT);
    sigaddset(&baseSigset, SIGTERM);
}

thread_local std::atomic<bool> stopLoop;

static void stopLoopHandler(int signum) {
    stopLoop.store(true, std::memory_order_release);
}

void LoopThread::stop() {
    pthread_kill(thread, SIGTERM);
}

Loop::Loop(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& name) : _env(logger, xmon, name), _name(name) {}

static void* startLoop(void* rawLoop) {
    std::unique_ptr<Loop> loop((Loop*)rawLoop);
    // there's a race between starting the thread and getting the
    // sigmask, but then again we haven't setup the handler at
    // that point yet, so the whole process would just go down.
    //
    // it would be good to use `pthread_attr_setsigmask_np` but it's
    // a recent glibc feature (~2020) which musl does not support
    // yet.
    {
        int ret = pthread_sigmask(SIG_SETMASK, &baseSigset, nullptr);
        if (ret != 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(ret, "pthread_sigmask");
        }
    }
    loop->run();
    return nullptr;
}

std::unique_ptr<LoopThread> Loop::Spawn(std::unique_ptr<Loop>&& loop) {
    Loop* rawLoop = loop.release();
    pthread_t thr;
    {
        int res = pthread_create(&thr, nullptr, &startLoop, rawLoop);
        if (res != 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(res, "pthread_create");
        }
    }
    return std::make_unique<LoopThread>(thr);
}

void Loop::run() {
    while (!stopLoop.load()) { step(); }
}

int Loop::poll(struct pollfd* fds, nfds_t nfds) {
    return ppoll(fds, nfds, nullptr, &blockingSigset);
}

void Loop::stop() {
    stopLoop.store(true, std::memory_order_release);
}

int Loop::sleep(Duration d) {
    auto tspec = d.timespec();
    return ppoll(nullptr, 0, &tspec, &blockingSigset);
}

void waitUntilStopped(std::vector<std::unique_ptr<LoopThread>>& loops) {
    ALWAYS_ASSERT(getpid() == gettid(), "You can only run this function from the main thread");

    // mask signals here too
    {
        int ret = pthread_sigmask(SIG_SETMASK, &baseSigset, nullptr);
        if (ret < 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(ret, "pthread_sigmask");
        }
    }

    // setup signal handler
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = &stopLoopHandler;
        if (sigaction(SIGTERM, &sa, nullptr) < 0) {
            throw SYSCALL_EXCEPTION("sigaction");
        }
        if (sigaction(SIGINT, &sa, nullptr) < 0) {
            throw SYSCALL_EXCEPTION("sigaction");
        }
    }

    // start waiting
    while (!stopLoop.load()) {
        struct timespec timeout { .tv_sec = 60*60*24 };
        int ret = ppoll(nullptr, 0, &timeout, &blockingSigset);
        if (ret < 0 && errno != EINTR) {
            throw SYSCALL_EXCEPTION("ppoll");
        }
    }

    // we've been told to stop, tear down all threads
    for (int i = loops.size()-1; i >= 0; i--) {
        loops[i]->stop();
        struct timespec timeout;
        if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
            throw SYSCALL_EXCEPTION("clock_gettime");
        }
        timeout.tv_sec += 10;
        int ret = pthread_timedjoin_np(loops[i]->thread, nullptr, &timeout);
        if (ret != 0 && ret == ETIMEDOUT) {
            char name[16];
            {
                int ret = pthread_getname_np(loops[i]->thread, name, sizeof(name));
                if (ret != 0) {
                    throw EXPLICIT_SYSCALL_EXCEPTION(ret, "pthread_getname_np");
                }
            }
            throw EGGS_EXCEPTION("loop %s has not terminated in time, aborting", name);
        }
    }
}