#pragma once

#include <pthread.h>

#include "Env.hpp"
#include "Exception.hpp"

struct Loop {
protected:
    Env _env;

private:
    std::string _name;

public:
    Loop(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const std::string& name) : _env(logger, xmon, name), _name(name) {
        int ret = pthread_setname_np(pthread_self(), name.c_str());
        if (ret != 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(ret, "pthreat_setname_np %s", name);
        }
    }

    const std::string& name() const {
        return _name;
    }

    virtual void step() = 0;

    void run() {
        for (;;) { step(); }
    }
};
