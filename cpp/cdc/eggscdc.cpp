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
    fprintf(stderr, " -port port\n");
    fprintf(stderr, "    	Port to listen on.\n");
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
        } else if (arg == "-own-ip") {
            options.ownIp = parseIpv4(argv[0], getNextArg());
        } else if (arg == "-port") {
            options.port = parsePort(getNextArg());
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

    if (options.ownIp == std::array<uint8_t, 4>{0,0,0,0}) {
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
