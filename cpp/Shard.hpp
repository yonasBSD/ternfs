#pragma once

#include <atomic>

#include "Msgs.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"
#include "Undertaker.hpp"

struct Shard : Undertaker::Reapable {
private:
    std::atomic<bool> _stop;
    ShardId _shid;
    std::string _dbDir;
    std::unique_ptr<Env> _env;
    std::unique_ptr<ShardDB> _db;
public:
    Shard(Logger& logger, LogLevel level, ShardId shid, const std::string& dbDir);

    void run();

    virtual ~Shard() = default;
    virtual void terminate() override;
    virtual void onAbort() override;

    ShardId shid() const {
        return _shid;
    }
};
