#pragma once

#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <thread>

#include "Env.hpp"
#include "Exception.hpp"

struct LoopThread {
    pthread_t thread;

    LoopThread(pthread_t thread_) : thread(thread_) {}
    virtual ~LoopThread() = default;
    LoopThread(const LoopThread&) = delete;

    virtual void stop();
};

// Each loop runs with SIGINT/SIGTERM blocked. It's expected that any
// non-time-bounded syscalls which step runs has SIGINT/SIGTERM unmasked
// (e.g. using ppoll).
struct Loop {
protected:
    Env _env;

private:
    std::string _name;

protected:
    // can be used to stop from within the thread
    void stop();

public:
    Loop(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& name);
    virtual ~Loop() = default;

    // This will remember the sigmask that it's been called with, and use it
    // to unmask stuff in poll/sleep below.
    static std::unique_ptr<LoopThread> Spawn(std::unique_ptr<Loop>&& loop);

    const std::string& name() const { return _name; }

    virtual void step() = 0;

    void run();

    // Polls forever with SIGINT/SIGTERM unmasked. If timeout < 0, waits forever.
    // If timeout == 0, returns immediately. If timeout > 0, it'll wait.
    int poll(struct pollfd* fds, nfds_t nfds, Duration timeout);

    // Sleeps with SIGINT/SIGTERM unmasked.
    int sleep(Duration d);
};

void waitUntilStopped(std::vector<std::unique_ptr<LoopThread>>& loops);