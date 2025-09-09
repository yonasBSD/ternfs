#include <stdio.h>
#include <filesystem>
#include <arpa/inet.h>

#include "Msgs.hpp"
#include "RegistryClient.hpp"
#include "CDC.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

void usage(const char* binary) {
    fprintf(stderr, "Usage: %s DIRECTORY REPLICA_ID [LOCATION_ID]\n\n", binary);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, " -log-level trace|debug|info|error\n");
    fprintf(stderr, "    	Note that 'trace' will only work for debug builds.\n");
    fprintf(stderr, " -verbose\n");
    fprintf(stderr, "    	Same as '-log-level debug'.\n");
    fprintf(stderr, " -registry host:port\n");
    fprintf(stderr, "    	How to reach registry");
    fprintf(stderr, " -addr ipv4 ip:port\n");
    fprintf(stderr, "    	Addresses we bind ourselves too and advertise to registry. At least one needs to be provided and at most 2\n");
    fprintf(stderr, " -log-file string\n");
    fprintf(stderr, "    	If not provided, stdout.\n");
    fprintf(stderr, " -shard-timeout-ms milliseconds\n");
    fprintf(stderr, "    	How much to wait for shard responses. Right now this is a simple loop.\n");
    fprintf(stderr, " -xmon host:port\n");
    fprintf(stderr, "    	Enable Xmon alerts.\n");
    fprintf(stderr, " -influx-db-origin\n");
    fprintf(stderr, " -influx-db-org\n");
    fprintf(stderr, " -influx-db-bucket\n");
    fprintf(stderr, "    	Enable metrics.\n");
    fprintf(stderr, " -logsdb-leader\n");
    fprintf(stderr, "    	Allow replica to become leader. Default is false\n");
    fprintf(stderr, " -logsdb-no-replication\n");
    fprintf(stderr, "    	Don't wait for acks from other replicas when becoming leader or replicating.\n");
    fprintf(stderr, "    	Can only be set if -logsdb-leader is also set. Default is false\n");
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

    CDCOptions options;
    std::vector<std::string> args;
    std::string registryAddress;
    std::string influxDBOrigin;
    std::string influxDBOrg;
    std::string influxDBBucket;
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
        } else if (arg == "-registry") {
            registryAddress = getNextArg();
        } else if (arg == "-addr") {
            if (numAddressesFound == 2) {
                dieWithUsage();
            }
            if (!parseIpv4Addr(getNextArg(), options.cdcAddrs[numAddressesFound])) {
                dieWithUsage();
            }
            options.cdcToShardAddress[numAddressesFound] = options.cdcAddrs[numAddressesFound];
            options.cdcToShardAddress[numAddressesFound].port = 0; // auto-assign
            numAddressesFound++;
        } else if (arg == "-syslog") {
            options.syslog = true;
        } else if (arg == "-shard-timeout-ms") {
            size_t idx;
            auto msStr = getNextArg();
            unsigned long long ms = std::stoull(msStr, &idx);
            if (idx != msStr.size()) {
                die("Runoff character in number %s", msStr.c_str());
            }
            options.shardTimeout = Duration(ms * 1'000'000);
        } else if (arg == "-xmon") {
            options.xmonAddr = getNextArg();
        } else if (arg == "-influx-db-origin") {
            influxDBOrigin = getNextArg();
        } else if (arg == "-influx-db-org") {
            influxDBOrg = getNextArg();
        } else if (arg == "-influx-db-bucket") {
            influxDBBucket = getNextArg();
        } else if (arg == "-logsdb-leader") {
            options.avoidBeingLeader = false;
        } else if (arg == "-logsdb-no-replication") {
            options.noReplication = true;
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (influxDBOrigin.empty() != influxDBOrg.empty() || influxDBOrigin.empty() != influxDBBucket.empty()) {
        fprintf(stderr, "Either all or none of the -influx-db flags must be provided\n");
        dieWithUsage();
    }
    if (!influxDBOrigin.empty()) {
        options.influxDB = InfluxDB{
            .origin = influxDBOrigin,
            .org = influxDBOrg,
            .bucket = influxDBBucket,
        };
    }

    if (args.size() < 2 || args.size() > 3) {
        fprintf(stderr, "Expecting two or three positional argument (DIRECTORY REPLICA_ID [LOCATION_ID]), got %ld.\n", args.size());
        dieWithUsage();
    }
    if (args.size() < 3) {
        args.emplace_back("0");
    }

    if (options.noReplication && options.avoidBeingLeader) {
        fprintf(stderr, "-logsdb-leader needs to be set if -logsdb-no-replication is set\n");
         dieWithUsage();
    }

#ifndef TERN_DEBUG
    if (options.logLevel <= LogLevel::LOG_TRACE) {
        die("Cannot use log level trace trace for non-debug builds (it won't work).");
    }
#endif

    if (registryAddress.empty()) {
        fprintf(stderr, "Must provide -registry.");
        dieWithUsage();
    }

    if (!parseRegistryAddress(registryAddress, options.registryHost, options.registryPort)) {
        fprintf(stderr, "Bad registry address '%s'.\n\n", registryAddress.c_str());
        dieWithUsage();
    }

    if (numAddressesFound == 0 || options.cdcAddrs[0].ip == Ip({0,0,0,0})) {
        fprintf(stderr, "Please provide at least one valid -addr.\n\n");
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
    options.replicaId = parseReplicaId(args.at(1));

    size_t processed;
    int location = std::stoi(args.at(2), &processed);
    if (processed != args.at(2).size() || location < 0 || location > 255) {
        die("Invalid location '%s', expecting a number between 0 and 255.\n", args.at(2).c_str());
    }
    options.location = location;

    runCDC(options);

    return 0;
}
