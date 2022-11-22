#include <pthread.h>
#include <stdio.h>
#include <filesystem>
#include <unordered_set>
#include <fstream>

#include "Shard.hpp"
#include "Env.hpp"
#include "Undertaker.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void* runShard(void* shard) {
    ((Shard*)shard)->run();
    return nullptr;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const auto dieWithUsage = [&argv]() {
        die("Usage: %s [-v|--verbose] [--log-level debug|info|error] [--log-file <file_path>] db_dir shard_id\n", argv[0]);
    };

    LogLevel level = LogLevel::LOG_INFO;
    std::string logFile;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        const auto getNextArg = [argc, &argv, &dieWithUsage, &i]() {
            if (i+1 >= argc) {
                dieWithUsage();
            }
            std::string arg(argv[i+1]);
            i++;
            return arg;
        };
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            dieWithUsage();
        } else if (arg == "-v" || arg == "--verbose") {
            level = std::min<LogLevel>(LogLevel::LOG_DEBUG, level);
        } else if (arg == "--log-level") {
            std::string logLevel = getNextArg();
            if (logLevel == "debug") {
                level = LogLevel::LOG_DEBUG;
            } else if (logLevel == "info") {
                level = LogLevel::LOG_INFO;
            } else if (logLevel == "erro") {
                level = LogLevel::LOG_ERROR;
            } else {
                die("Bad log level `%s'", logLevel.c_str());
            }
        } else if (arg == "--log-file") {
            logFile = getNextArg();
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (args.size() != 2) {
        dieWithUsage();
    }

    fs::path dbDir(args.at(0));
    auto dbDirStatus = fs::status(dbDir);
    if (dbDirStatus.type() == fs::file_type::not_found) {
        die("Could not find DB directory `%s'.\n", dbDir.c_str());
    }
    if (dbDirStatus.type() != fs::file_type::directory) {
        die("DB directory `%s' is not a directory.\n", dbDir.c_str());
    }

    size_t processed;
    int shardId = std::stoi(args.at(1), &processed);
    if (processed != args.at(1).size() || shardId < 0 || shardId > 255) {
        die("Invalid shard `%s', expecting a number between 0 and 255.\n", args.at(1).c_str());
    }
    ShardId shid(shardId);

    auto undertaker = Undertaker::acquireUndertaker();

    std::ostream* logOut = &std::cout;
    std::ofstream fileOut;
    if (!logFile.empty()) {
        fileOut = std::ofstream(logFile, std::ios::out | std::ios::app);
        if (!fileOut.is_open()) {
            die("Could not open log file `%s'\n", logFile.c_str());
        }
        logOut = &fileOut;
    }
    Logger logger(*logOut);

    {
        auto shard = std::make_unique<Shard>(logger, level, shid, dbDir);
        pthread_t tid;
        if (pthread_create(&tid, nullptr, &runShard, &*shard) != 0) {
            throw SYSCALL_EXCEPTION("pthread_create");
        }
        undertaker->checkin(std::move(shard), tid, "shard");
    }

    undertaker->reap();

    return 0;
}
