#pragma once


#include <memory>


#include "Env.hpp"
#include "Loop.hpp"

#include "RegistryCommon.hpp"

class Registry {
public:
    explicit Registry(Logger& logger, std::shared_ptr<XmonAgent> xmon);
    ~Registry();
    void start(const RegistryOptions& options, LoopThreads& threads);
    void close();
private:
    Env _env;
    std::unique_ptr<RegistryState> _state;
};
