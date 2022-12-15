#!/usr/bin/env bash
set -eu -o pipefail

# (cd go && go test ./...)

# Both ./cpp/run-tests.sh and the integration test will write coverage info

rm -f ./cpp/build/debug-cov-sanitize/*.gcda ./cpp/build/debug-cov-sanitize/*.gcov

set -x

./cpp/run-tests.sh
(cd go/integrationtest && go run . -coverage -debug -sanitize)

(cd cpp && llvm-cov gcov -o=./build/debug-cov-sanitize/ ./*.cpp ./*.hpp >/dev/null 2>/dev/null)

unset -x
mv ./cpp/*.gcov ./cpp/build/debug-cov-sanitize/
