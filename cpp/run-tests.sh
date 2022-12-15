#!/usr/bin/env bash
set -eu -o pipefail

cd "$(dirname "$0")"

echo "$(tput bold)sanitized, debug$(tput sgr0)"
rm -f ./build/debug-cov-sanitize/*.gcda ./build/debug-cov-sanitize/*.gcov
make debug=yes coverage=yes sanitize=yes -j ./build/debug-cov-sanitize/tests
UBSAN_OPTIONS=print_stacktrace=1 ./build/debug-cov-sanitize/tests

# valgrind doesn't support fnctl F_SET_RW_HINT (1036), and as far as I can
# tell there isn't a way to programmatically filter those.

echo "$(tput bold)valgrind, debug$(tput sgr0)"
make debug=yes valgrind=yes -j ./build/debug-valgrind/tests
valgrind -q --suppressions=valgrind-suppressions --error-exitcode=1 ./build/debug-valgrind/tests 2> >(grep -v "Warning: unimplemented fcntl command: 1036")

echo "$(tput bold)valgrind, release$(tput sgr0)"
make valgrind=yes -j ./build/opt-valgrind/tests
valgrind -q --suppressions=valgrind-suppressions --error-exitcode=1 ./build/opt-valgrind/tests 2> >(grep -v "Warning: unimplemented fcntl command: 1036")
