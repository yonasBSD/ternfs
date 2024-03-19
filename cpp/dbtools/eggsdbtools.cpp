

#include <cstdio>
#include <string>

#include "ShardDBTools.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)

static void usage(const char* binary) {
    fprintf(stderr, "Usage: %s verify-equal DB1_PATH DB2_PATH\n\n", binary);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "    	Verifies two databases are the same.\n");
    fprintf(stderr, "  Options:\n");
    fprintf(stderr, "   -compare-logsdb\n");
    fprintf(stderr, "    	Include logsdb comparison in verification. Only checks that common tail before release point is equal.\n");
}

int main(int argc, char** argv) {
    const auto dieWithUsage = [&argv]() {
        usage(argv[0]);
        exit(2);
    };
    if (argc == 1) {
        dieWithUsage();
    }

    std::string db1Path;
    std::string db2Path;
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
            db1Path = getNextArg();
            db2Path = getNextArg();
        } else {
            dieWithUsage();
        }
    }

    ShardDBTools::verifyEqual(db1Path, db2Path);
    return 0;
}
