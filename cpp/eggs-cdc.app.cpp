#include <pthread.h>
#include <stdio.h>
#include <filesystem>

#include "CDC.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const auto dieWithUsage = [&argv]() {
        die("Usage: %s [-v|--verbose] [--log-level debug|info|error] [--log-file <file_path>] [--packet-drop <probability>] db_dir\n", argv[0]);
    };

    CDCOptions options;
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
        } else if (arg == "--packet-drop") {
            std::string probStr = getNextArg();
            size_t idx;
            options.simulatePacketDrop = std::stod(probStr, &idx);
            if (idx != probStr.size()) {
                die("Runoff characters in number %s", probStr.c_str());
            }
            if (options.simulatePacketDrop < 0.0 || options.simulatePacketDrop >= 1.0) {
                die("Please specify a number in the interval [0.0, 1.0) for --packet-drop.");
            }
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (args.size() != 1) {
        dieWithUsage();
    }

#ifndef EGGS_DEBUG
    if (options.level <= LogLevel::LOG_DEBUG) {
        die("Cannot use --verbose for non-debug builds (it won't work).");
    }
#endif

    fs::path dbDir(args.at(0));
    auto dbDirStatus = fs::status(dbDir);
    if (dbDirStatus.type() == fs::file_type::not_found) {
        die("Could not find DB directory `%s'.\n", dbDir.c_str());
    }
    if (dbDirStatus.type() != fs::file_type::directory) {
        die("DB directory `%s' is not a directory.\n", dbDir.c_str());
    }

    runCDC(dbDir, options);

    return 0;
}
