#include <arpa/inet.h>
#include <cstdint>
#include <pthread.h>
#include <stdio.h>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <unordered_map>

#include "Bincode.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "Shard.hpp"
#include "Shuckle.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void usage(const char* binary) {
    fprintf(stderr, "Usage: %s DIRECTORY SHARD_ID REPLICA_ID [LOCATION_ID]\n\n", binary);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, " -log-level trace|debug|info|error\n");
    fprintf(stderr, "    	Note that 'trace' will only work for debug builds.\n");
    fprintf(stderr, " -verbose\n");
    fprintf(stderr, "    	Same as '-log-level debug'.\n");
    fprintf(stderr, " -shuckle host:port\n");
    fprintf(stderr, "    	How to reach shuckle, default '%s'\n", defaultShuckleAddress.c_str());
    fprintf(stderr, " -addr ipv4 ip:port\n");
    fprintf(stderr, "    	Addresses we bind ourselves too and advertise to shuckle. At least one needs to be provided and at most 2\n");
    fprintf(stderr, " -log-file string\n");
    fprintf(stderr, "    	If not provided, stdout.\n");
    fprintf(stderr, " -outgoing-packet-drop [0, 1)\n");
    fprintf(stderr, "    	Drop given ratio of packets after processing them.\n");
    fprintf(stderr, " -xmon qa|prod\n");
    fprintf(stderr, "    	Enable Xmon alerts.\n");
    fprintf(stderr, " -metrics\n");
    fprintf(stderr, "    	Enable metrics.\n");
    fprintf(stderr, " -transient-deadline-interval\n");
    fprintf(stderr, "    	Tweaks the interval with wich the deadline for transient file gets bumped.\n");
    fprintf(stderr, " -logsdb-leader\n");
    fprintf(stderr, "    	Allow replica to become leader. Default is false\n");
    fprintf(stderr, " -logsdb-no-replication\n");
    fprintf(stderr, "    	Don't wait for acks from other replicas when becoming leader or replicating.\n");
    fprintf(stderr, "    	Can only be set if -logsdb-leader is also set. Default is false\n");
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

static bool parseIpv4(const std::string& arg, sockaddr_in& addrOut) {
    addrOut.sin_family = AF_INET;
    int res = inet_pton(AF_INET, arg.c_str(), &addrOut.sin_addr);
    if (res == 0) {
        fprintf(stderr, "Invalid ipv4 address '%s'\n\n", arg.c_str());
        return false;
    }
    return true;
}

static bool parsePort(const std::string& arg, uint16_t& portOut) {
    size_t idx;
    unsigned long port = std::stoul(arg, &idx);
    if (idx != arg.size()) {
        fprintf(stderr, "Runoff character in number %s", arg.c_str());
        return false;
    }
    if (port > 0xFFFFul) {
        fprintf(stderr, "Bad port %s", arg.c_str());
        return false;
    }
    portOut = port;
    return true;
}

static bool parseIpv4Addr(const std::string& arg, IpPort& addrOut) {
    size_t colon = arg.find(':');
    if (colon == std::string::npos) {
        fprintf(stderr, "Invalid ipv4 ip:port address '%s'\n\n", arg.c_str());
        return false;
    }
    sockaddr_in addr;
    if (!parseIpv4(arg.substr(0, colon), addr)) {
        return false;
    }
    uint16_t port;
    if (!parsePort(arg.substr(colon+1),port)) {
        return false;
    }
    addr.sin_port = htons(port);
    addrOut = IpPort::fromSockAddrIn(addr);
    return true;
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

static uint8_t parseReplicaId(const std::string& arg) {
    size_t idx;
    unsigned long replicaId = std::stoul(arg, &idx);
    if (idx != arg.size()) {
        die("Runoff character in number %s", arg.c_str());
    }
    if (replicaId > 4) {
        die("Bad replicaId %s", arg.c_str());
    }
    return replicaId;
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
    uint8_t numAddressesFound = 0;
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
        } else if (arg == "-outgoing-packet-drop") {
            options.simulateOutgoingPacketDrop = parseProbability(getNextArg());
        } else if (arg == "-shuckle") {
            shuckleAddress = getNextArg();
        } else if (arg == "-addr") {
            if (numAddressesFound == options.shardAddrs.size()) {
                dieWithUsage();
            }
            if(!parseIpv4Addr(getNextArg(), options.shardAddrs[numAddressesFound++])) {
                dieWithUsage();
            }
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
        } else if (arg == "-logsdb-leader") {
            options.avoidBeingLeader = false;
        } else if (arg == "-logsdb-no-replication") {
            options.noReplication = true;
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (options.noReplication && options.avoidBeingLeader) {
        fprintf(stderr, "-logsdb-leader needs to be set if -logsdb-no-replication is set\n");
         dieWithUsage();
    }

    if (args.size() < 3 || args.size() > 4) {
        fprintf(stderr, "Expecting three or four positional arguments (DIRECTORY SHARD_ID REPLICA_ID [LOCATION_ID]), got %ld.\n", args.size());
        dieWithUsage();
    }

    // Add default location ID if not specified
    if (args.size() == 3) {
        args.emplace_back("0");
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

    if ( numAddressesFound == 0 || options.shardAddrs[0].ip == Ip({0,0,0,0})) {
        fprintf(stderr, "Please provide at least one valid address\n\n");
        dieWithUsage();
    }

    fs::path dbDir(args.at(0));
    {
        std::error_code err;
        if (!fs::create_directory(dbDir, err) && err.value() != 0) {
            throw EXPLICIT_SYSCALL_EXCEPTION(err.value(), "mkdir");
        }
    }
    options.dbDir = dbDir;

    size_t processed;
    int shardId = std::stoi(args.at(1), &processed);
    if (processed != args.at(1).size() || shardId < 0 || shardId > 255) {
        die("Invalid shard '%s', expecting a number between 0 and 255.\n", args.at(1).c_str());
    }
    options.shrid = {shardId, parseReplicaId(args.at(2))} ;

    int location = std::stoi(args.at(3), &processed);
    if (processed != args.at(3).size() || location < 0 || location > 255) {
        die("Invalid location '%s', expecting a number between 0 and 255.\n", args.at(3).c_str());
    }
    options.location = location;

    runShard(options);

    return 0;
}
