#include <arpa/inet.h>
#include <charconv>
#include <pthread.h>
#include <stdio.h>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "Exception.hpp"
#include "Shard.hpp"
#include "Shuckle.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void usage(const char* binary) {
    fprintf(stderr, "Usage: %s DIRECTORY SHARD_ID\n\n", binary);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, " -log-level trace|debug|info|error\n");
    fprintf(stderr, "    	Note that 'trace' will only work for debug builds.\n");
    fprintf(stderr, " -verbose\n");
    fprintf(stderr, "    	Same as '-log-level debug'.\n");
    fprintf(stderr, " -shuckle host:port\n");
    fprintf(stderr, "    	How to reach shuckle, default '%s'\n", defaultShuckleAddress.c_str());
    fprintf(stderr, " -own-ip-1 ipv4 address\n");
    fprintf(stderr, "    	How to advertise ourselves to shuckle.\n");
    fprintf(stderr, " -port-1 port\n");
    fprintf(stderr, "    	Port on which to listen on.\n");
    fprintf(stderr, " -own-ip-2 ipv4 address\n");
    fprintf(stderr, "    	How to advertise ourselves to shuckle (second address, optional).\n");
    fprintf(stderr, " -port-2 port\n");
    fprintf(stderr, "    	Port on which to listen on (second port).\n");
    fprintf(stderr, " -log-file string\n");
    fprintf(stderr, "    	If not provided, stdout.\n");
    fprintf(stderr, " -incoming-packet-drop [0, 1)\n");
    fprintf(stderr, "    	Drop given ratio of packets on arrival.\n");
    fprintf(stderr, " -outgoing-packet-drop [0, 1)\n");
    fprintf(stderr, "    	Drop given ratio of packets after processing them.\n");
    fprintf(stderr, " -xmon qa|prod\n");
    fprintf(stderr, "    	Enable Xmon alerts.\n");
    fprintf(stderr, " -metrics\n");
    fprintf(stderr, "    	Enable metrics.\n");
    fprintf(stderr, " -transient-deadline-interval\n");
    fprintf(stderr, "    	Tweaks the interval with wich the deadline for transient file gets bumped.\n");
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

static uint32_t parseIpv4(const char* binary, const std::string& arg) {
    struct sockaddr_in addr;
    int res = inet_pton(AF_INET, arg.c_str(), &addr.sin_addr);
    if (res == 0) {
        fprintf(stderr, "Invalid ipv4 address '%s'\n\n", arg.c_str());
        usage(binary);
        exit(2);
    }
    uint32_t out;
    static_assert(sizeof(addr.sin_addr) == sizeof(out));
    memcpy(&out, &addr.sin_addr, sizeof(addr.sin_addr));
    return ntohl(out);
}

static uint16_t parsePort(const std::string& arg) {
    size_t idx;
    unsigned long port = std::stoul(arg, &idx);
    if (idx != arg.size()) {
        die("Runoff character in number %s", arg.c_str());
    }
    if (port > 0xFFFFul) {
        die("Bad port %s", arg.c_str());
    }
    return port;
}

const std::unordered_map<std::string, uint64_t> durationUnitMap = {
	{"ns", 1ull},
	{"us", 1'000ull},
	{"µs", 1'000ull},  // U+00B5 = micro symbol
	{"μs", 1'000ull},  // U+03BC = Greek letter mu
	{"ms", 1'000'000ull},
	{"s",  1'000'000'000ull},
	{"m",  1'000'000'000ull*60},
	{"h",  1'000'000'000ull*60*60},
};

static Duration parseDuration(const std::string& arg) {
    size_t idx;
    uint64_t x = std::stoull(arg, &idx);
    return x * durationUnitMap.at(arg.substr(idx));
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
            options.logLevel = std::min<LogLevel>(LogLevel::LOG_DEBUG, options.logLevel);
        } else if (arg == "-log-level") {
            std::string logLevel = getNextArg();
            if (logLevel == "trace") {
                options.logLevel = LogLevel::LOG_TRACE;
            } else if (logLevel == "debug") {
                options.logLevel = LogLevel::LOG_DEBUG;
            } else if (logLevel == "info") {
                options.logLevel = LogLevel::LOG_INFO;
            } else if (logLevel == "error") {
                options.logLevel = LogLevel::LOG_ERROR;
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
        } else if (arg == "-own-ip-1") {
            options.ipPorts[0].ip = parseIpv4(argv[0], getNextArg());
        } else if (arg == "-port-1") {
            options.ipPorts[0].port = parsePort(getNextArg());
        } else if (arg == "-own-ip-2") {
            options.ipPorts[1].ip = parseIpv4(argv[0], getNextArg());
        } else if (arg == "-port-2") {
            options.ipPorts[1].port = parsePort(getNextArg());
        } else if (arg == "-syslog") {
            options.syslog = true;
        } else if (arg == "-xmon") {
            options.xmon = true;
            std::string xmonEnv = getNextArg();
            if (xmonEnv == "qa") {
                options.xmonProd = false;
            } else if (xmonEnv == "prod") {
                options.xmonProd = true;
            } else {
                fprintf(stderr, "Invalid xmon env %s", xmonEnv.c_str());
                dieWithUsage();
            }
        } else if (arg == "-metrics") {
            options.metrics = true;
        } else if (arg == "-transient-deadline-interval") {
            options.transientDeadlineInterval = parseDuration(getNextArg());
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (args.size() != 2) {
        fprintf(stderr, "Expecting two positional arguments (DIRECTORY SHARD_ID), got %ld.\n", args.size());
        dieWithUsage();
    }

#ifndef EGGS_DEBUG
    if (options.logLevel <= LogLevel::LOG_TRACE) {
        die("Cannot use trace for non-debug builds (it won't work).");
    }
#endif

    if (!parseShuckleAddress(shuckleAddress, options.shuckleHost, options.shucklePort)) {
        fprintf(stderr, "Bad shuckle address '%s'.\n\n", shuckleAddress.c_str());
        dieWithUsage();
    }

    if (options.ipPorts[0].ip == 0) {
        fprintf(stderr, "Please provide -own-ip-1.\n\n");
        usage(argv[0]);
        exit(2);
    }

    fs::path dbDir(args.at(0));
    {
        std::error_code err;
        if (!fs::create_directory(dbDir, err) && err.value() != 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(err.value(), "mkdir");
        }
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
