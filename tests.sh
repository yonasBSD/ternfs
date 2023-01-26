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

echo "$(tput bold)integration tests, sanitized, packet drop$(tput sgr0)"
set -x
./go/integrationtest/integrationtest -build-type sanitized -outgoing-packet-drop 0.1 -short
set +x

echo "$(tput bold)integration tests, valgrind$(tput sgr0)"
set -x
./go/integrationtest/integrationtest -build-type valgrind -short
set +x
