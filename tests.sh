#!/usr/bin/env bash
set -eu -o pipefail

echo "$(tput bold)go tests$(tput sgr0)"
(cd go && go test ./...)

./cpp/run-tests.sh

(cd go/integrationtest && go build .)

echo "$(tput bold)integration tests$(tput sgr0)"
set -x
./go/integrationtest/integrationtest
set +x

echo "$(tput bold)integration tests, sanitized$(tput sgr0)"
set -x
./go/integrationtest/integrationtest -sanitize
set +x

echo "$(tput bold)integration tests, valgrind$(tput sgr0)"
set -x
./go/integrationtest/integrationtest -valgrind -short
set +x

echo "$(tput bold)integration tests, packet drop$(tput sgr0)"
set -x
./go/integrationtest/integrationtest -sanitize -outgoing-packet-drop 0.1 -short
set +x

# # Both ./cpp/run-tests.sh and the integration test will write coverage info
# 
# rm -f ./cpp/build/debug-cov-sanitize/*.gcda ./cpp/build/debug-cov-sanitize/*.gcov
# 
# set -x
# 
# ./cpp/run-tests.sh
# (cd go/integrationtest && go run . -coverage -debug -sanitize)
# 
# (cd cpp && llvm-cov gcov -o=./build/debug-cov-sanitize/ ./*.cpp ./*.hpp >/dev/null 2>/dev/null)
# 
# unset -x
# mv ./cpp/*.gcov ./cpp/build/debug-cov-sanitize/
