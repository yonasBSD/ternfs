#!/usr/bin/env bash
set -eu -o pipefail

cd "$(dirname "$0")"

echo "$(tput bold)C++ tests, sanitized$(tput sgr0)"
./build.py sanitized tests/tests
UBSAN_OPTIONS=print_stacktrace=1 ./build/sanitized/tests/tests

# valgrind doesn't support fnctl F_SET_RW_HINT (1036), and as far as I can
# tell there isn't a way to programmatically filter those.

echo "$(tput bold)C++ tests, valgrind$(tput sgr0)"
./build.py valgrind tests/tests
valgrind --exit-on-first-error=yes -q --suppressions=valgrind-suppressions --error-exitcode=1 ./build/valgrind/tests/tests 2> >(grep -v "Warning: unimplemented fcntl command: 1036")
