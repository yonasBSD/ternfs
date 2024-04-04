

#include <cstdint>
#include <cstdio>
#include <string>

#include "ShardDBTools.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void usage(const char* binary) {
    fprintf(stderr, "Usage: %s COMMAND ARGS\n\n", binary);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  verify-equal DB1_PATH DB2_PATH\n");
    fprintf(stderr, "    	Verifies two databases are the same.\n");
    fprintf(stderr, "  unreleased-state DB_PATH\n");
    fprintf(stderr, "    	Outputs state of unreleased entries in DB.\n");
    fprintf(stderr, "  fsck DB_PATH\n");
    fprintf(stderr, "    	Performs various integrity checks on the RocksDB state. The RocksDB database will be opened as read only.\n");
}

int main(int argc, char** argv) {
    const auto dieWithUsage = [&argv]() {
        usage(argv[0]);
        exit(2);
    };
    if (argc == 1) {
        dieWithUsage();
    }

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
        if (arg == "verify-equal") {
            std::string db1Path = getNextArg();
            std::string db2Path = getNextArg();
            ShardDBTools::verifyEqual(db1Path, db2Path);
        } else if (arg == "unreleased-state") {
            std::string db1Path = getNextArg();
            ShardDBTools::outputUnreleasedState(db1Path);
        } else if (arg == "fsck") {
            std::string dbPath = getNextArg();
            ShardDBTools::fsck(dbPath);
        } else if (arg == "fixup-bad-inodes") {
            std::string dbPath = getNextArg();
            ShardDBTools::fixupBadInodes(dbPath);
        } else {
            dieWithUsage();
        }
    }

    return 0;
}
