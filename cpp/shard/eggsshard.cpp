#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <filesystem>

#include "Shard.hpp"
#include "Shuckle.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void usage(const char* binary) {
    fprintf(stderr, "Usage: %s DIRECTORY SHARD_ID\n\n", binary);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, " -log-level debug|info|error\n");
    fprintf(stderr, "    	Note that 'debug' will only work for debug builds.\n");
    fprintf(stderr, " -verbose\n");
    fprintf(stderr, "    	Same as '-log-level debug'.\n");
    fprintf(stderr, " -shuckle host:port\n");
    fprintf(stderr, "    	How to reach shuckle, default '%s'\n", defaultShuckleAddress.c_str());
    fprintf(stderr, " -own-ip ipv4 address\n");
    fprintf(stderr, "    	How to advertise ourselves to shuckle.\n");
    fprintf(stderr, " -log-file string\n");
    fprintf(stderr, "    	If not provided, stdout.\n");
    fprintf(stderr, " -incoming-packet-drop [0, 1)\n");
    fprintf(stderr, "    	Drop given ratio of packets on arrival.\n");
    fprintf(stderr, " -outgoing-packet-drop [0, 1)\n");
    fprintf(stderr, "    	Drop given ratio of packets after processing them.\n");
}

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

static std::array<uint8_t, 4> parseIpv4(const char* binary, const std::string& arg) {
    struct sockaddr_in addr;
    int res = inet_pton(AF_INET, arg.c_str(), &addr.sin_addr);
    if (res == 0) {
        fprintf(stderr, "Invalid ipv4 address '%s'\n\n", arg.c_str());
        usage(binary);
        exit(2);
    }
    std::array<uint8_t, 4> out;
    static_assert(sizeof(addr.sin_addr) == sizeof(out));
    memcpy(out.data(), &addr.sin_addr, sizeof(addr.sin_addr));
    return out;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const auto dieWithUsage = [&argv]() {
        usage(argv[0]);
        exit(2);
    };

    ShardOptions options;
    std::vector<std::string> args;
    std::string shuckleAddress = defaultShuckleAddress;
    for (int i = 1; i < argc; i++) {
        const auto getNextArg = [argc, &argv, &dieWithUsage, &i]() {
            if (i+1 >= argc) {
                fprintf(stderr, "Argument list ended too early.\n\n");
                dieWithUsage();
            }
            std::string arg(argv[i+1]);
            i++;
            return arg;
        };
        std::string arg = argv[i];
        if (arg == "-h" || arg == "-help") {
            dieWithUsage();
        } else if (arg == "-verbose") {
            options.level = std::min<LogLevel>(LogLevel::LOG_DEBUG, options.level);
        } else if (arg == "-log-level") {
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
        } else if (arg == "-log-file") {
            options.logFile = getNextArg();
        } else if (arg == "-incoming-packet-drop") {
            options.simulateIncomingPacketDrop = parseProbability(getNextArg());
        } else if (arg == "-outgoing-packet-drop") {
            options.simulateOutgoingPacketDrop = parseProbability(getNextArg());
        } else if (arg == "-shuckle") {
            shuckleAddress = getNextArg();
        } else if (arg == "-own-ip") {
            options.ownIp = parseIpv4(argv[0], getNextArg());
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (args.size() != 2) {
        fprintf(stderr, "Expecting two positional arguments (DIRECTORY SHARD_ID), got %ld.\n", args.size());
        dieWithUsage();
    }

#ifndef EGGS_DEBUG
    if (options.level <= LogLevel::LOG_DEBUG) {
        die("Cannot use -verbose for non-debug builds (it won't work).");
    }
#endif

    if (!parseShuckleAddress(shuckleAddress, options.shuckleHost, options.shucklePort)) {
        fprintf(stderr, "Bad shuckle address '%s'.\n\n", shuckleAddress.c_str());
        dieWithUsage();
    }

    if (options.ownIp == std::array<uint8_t, 4>{0,0,0,0}) {
        fprintf(stderr, "Please provide -own-ip.\n\n");
        usage(argv[0]);
        exit(2);
    }

    fs::path dbDir(args.at(0));
    auto dbDirStatus = fs::status(dbDir);
    if (dbDirStatus.type() == fs::file_type::not_found) {
        die("Could not find DB directory '%s'.\n", dbDir.c_str());
    }
    if (dbDirStatus.type() != fs::file_type::directory) {
        die("DB directory '%s' is not a directory.\n", dbDir.c_str());
    }

    size_t processed;
    int shardId = std::stoi(args.at(1), &processed);
    if (processed != args.at(1).size() || shardId < 0 || shardId > 255) {
        die("Invalid shard '%s', expecting a number between 0 and 255.\n", args.at(1).c_str());
    }
    ShardId shid(shardId);

    runShard(shid, dbDir, options);

    return 0;
}
