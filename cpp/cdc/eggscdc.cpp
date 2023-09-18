#include <pthread.h>
#include <stdio.h>
#include <filesystem>
#include <arpa/inet.h>

#include "Shuckle.hpp"
#include "CDC.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

void usage(const char* binary) {
    fprintf(stderr, "Usage: %s DIRECTORY\n\n", binary);
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
    fprintf(stderr, " -shard-timeout-ms milliseconds\n");
    fprintf(stderr, "    	How much to wait for shard responses. Right now this is a simple loop.\n");
    fprintf(stderr, " -xmon\n");
    fprintf(stderr, "    	Enable Xmon alerts.\n");
    fprintf(stderr, " -metrics\n");
    fprintf(stderr, "    	Enable metrics.\n");
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

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const auto dieWithUsage = [&argv]() { 
        usage(argv[0]);
        exit(2);
    };

    CDCOptions options;
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
        } else if (arg == "-shard-timeout-ms") {
            size_t idx;
            auto msStr = getNextArg();
            unsigned long long ms = std::stoull(msStr, &idx);
            if (idx != msStr.size()) {
                die("Runoff character in number %s", msStr.c_str());
            }
            options.shardTimeout = Duration(ms * 1'000'000);
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
        } else {
            args.emplace_back(std::move(arg));
        }
    }

    if (args.size() != 1) {
        fprintf(stderr, "Expecting one positional argument (DIRECTORY), got %ld.\n", args.size());
        dieWithUsage();
    }

#ifndef EGGS_DEBUG
    if (options.logLevel <= LogLevel::LOG_TRACE) {
        die("Cannot use log level trace trace for non-debug builds (it won't work).");
    }
#endif

    if (!parseShuckleAddress(shuckleAddress, options.shuckleHost, options.shucklePort)) {
        fprintf(stderr, "Bad shuckle address '%s'.\n\n", shuckleAddress.c_str());
        dieWithUsage();
    }

    if (options.ipPorts[0].ip == 0) {
        fprintf(stderr, "Please provide -own-ip.\n\n");
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

    runCDC(dbDir, options);

    return 0;
}
