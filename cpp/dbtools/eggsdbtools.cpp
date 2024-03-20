

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

}

enum class Command : uint8_t {
    NONE,
    VERIFY_EQUAL,
    UNRELEASED_STATE,
};

int main(int argc, char** argv) {
    const auto dieWithUsage = [&argv]() {
        usage(argv[0]);
        exit(2);
    };
    if (argc == 1) {
        dieWithUsage();
    }
    auto command = Command::NONE;

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
            command = Command::VERIFY_EQUAL;
            db1Path = getNextArg();
            db2Path = getNextArg();
        } else if (arg == "unreleased-state") {
            command = Command::UNRELEASED_STATE;
            db1Path = getNextArg();
        } else{
            dieWithUsage();
        }
    }

    switch (command) {
    case Command::NONE:
        dieWithUsage();
    case Command::VERIFY_EQUAL:
        ShardDBTools::verifyEqual(db1Path, db2Path);
      break;
    case Command::UNRELEASED_STATE:
        ShardDBTools::outputUnreleasedState(db1Path);
      break;
    }

    return 0;
}
