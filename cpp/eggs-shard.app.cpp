#include <pthread.h>
#include <stdio.h>
#include <filesystem>

#include "Shard.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static double parseDouble(const std::string& arg) {
    size_t idx;
    double x = std::stod(arg, &idx);
    if (idx != arg.size()) {
        die("Runoff characters in number %s", arg.c_str());
    }
    return x;
}

static double parseProbability(const std::string& arg) {
    double x = parseDouble(arg);
    if (x < 0.0 || x >= 1.0) {
        die("Please specify a number in the interval [0.0, 1.0), rather than %f", x);
    }
    return x;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const auto dieWithUsage = [&argv]() {
        die("Usage: %s [-v|--verbose] [--log-level debug|info|error] [--log-file <file_path>] [--wait-for-shuckle] [--incoming-packet-drop <probability>] [--outgoing-packet-drop <probability>] db_dir shard_id\n", argv[0]);
    };

    ShardOptions options;
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
            options.level = std::min<LogLevel>(LogLevel::LOG_DEBUG, options.level);
        } else if (arg == "--log-level") {
            std::string logLevel = getNextArg();
            if (logLevel == "debug") {
                options.level = LogLevel::LOG_DEBUG;
            } else if (logLevel == "info") {
                options.level = LogLevel::LOG_INFO;
            } else if (logLevel == "error") {
                options.level = LogLevel::LOG_ERROR;
            } else {
                die("Bad log level `%s'", logLevel.c_str());
            }
        } else if (arg == "--log-file") {
            options.logFile = getNextArg();
        } else if (arg == "--wait-for-shuckle") {
            options.waitForShuckle = true;
        } else if (arg == "--incoming-packet-drop") {
            options.simulateIncomingPacketDrop = parseProbability(getNextArg());
        } else if (arg == "--outgoing-packet-drop") {
            options.simulateOutgoingPacketDrop = parseProbability(getNextArg());
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

    runShard(shid, dbDir, options);

    return 0;
}
