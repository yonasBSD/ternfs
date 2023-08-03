#pragma once

#include "Undertaker.hpp"
#include "Stopper.hpp"
#include "Env.hpp"

void* runLoop(void*);

struct Loop : Undertaker::Reapable {
protected:
    Env _env;
    Stopper _stopper;
    std::string _name;

public:
    Loop(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& name) : _env(logger, xmon, name), _name(name) {}

    const std::string& name() const { return _name; }

    virtual ~Loop() = default;

    virtual void terminate() override {
        _env.flush();
        _stopper.stop();
    }

    virtual void onAbort() override {
        _env.flush();
    }

    virtual void init() {};
    virtual void step() = 0;
    virtual void finish() {};

    virtual void run() {
        init();
        for (;;) {
            if (_stopper.shouldStop()) {
                LOG_INFO(_env, "got told to stop, stopping");
                finish();
                _stopper.stopDone();
                return;
            }
            step();
        }
    }

    static void spawn(Undertaker& undertaker, std::unique_ptr<Loop> loop) {
        std::string name = loop->name();
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runLoop, &*loop) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker.checkin(std::move(loop), tid, name);
    }
};

struct PeriodicLoopConfig {
    Duration quantum = 50_ms;
    Duration failureInterval; // waiting between failures
    double failureIntervalJitter = 0.2;
    Duration successInterval; // waiting between successes
    double successIntervalJitter = 0.2;

    PeriodicLoopConfig(Duration failureInterval_, Duration successInterval_) : failureInterval(failureInterval_), successInterval(successInterval_) {}
};

struct PeriodicLoop : Loop {
private:
    PeriodicLoopConfig _config;
    uint64_t _rand;
    EggsTime _nextStepAt;
public:
    PeriodicLoop(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& name, const PeriodicLoopConfig& config) :
        Loop(logger, xmon, name),
        _config(config),
        _rand(eggsNow().ns),
        _nextStepAt(0)
    {}

    // true = success, false = failure
    virtual bool periodicStep() = 0;

    virtual void step() override {
        EggsTime t = eggsNow();
        if (t < _nextStepAt) {
            sleepFor(_config.quantum);
            return;
        }

        LOG_DEBUG(_env, "we're past %s, running periodic step", _nextStepAt);

        bool success = periodicStep();
        if (success) {
            _nextStepAt = t + _config.successInterval + Duration((double)_config.successInterval.ns * _config.successIntervalJitter);
            LOG_DEBUG(_env, "periodic step succeeded, next step at %s", _nextStepAt);
        } else {
            _nextStepAt = t + _config.failureInterval + Duration((double)_config.failureInterval.ns * _config.failureIntervalJitter);
            LOG_DEBUG(_env, "periodic step failed, next step at %s", _nextStepAt);
        }
    }
};