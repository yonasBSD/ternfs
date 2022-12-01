#!/usr/bin/env bash
set -eu -o pipefail

echo "$(tput bold)building requisites$(tput sgr0)"
./cpp/build.py alpine rs crc32c # build libs for go
(cd go/msgs && go generate ./...)

echo "$(tput bold)go tests$(tput sgr0)"
(cd go && go test ./...)

./cpp/tests.sh

(cd kmod && make bincode_tests && ./bincode_tests)

(cd go/eggstests && go build .)

echo "$(tput bold)integration tests$(tput sgr0)"
set -x
./go/eggstests/eggstests -repo-dir $(pwd)
set +x

echo "$(tput bold)integration tests, sanitized, packet drop$(tput sgr0)"
set -x
./go/eggstests/eggstests -repo-dir $(pwd) -build-type sanitized -outgoing-packet-drop 0.1 -short
set +x

echo "$(tput bold)integration tests, valgrind$(tput sgr0)"
set -x
./go/eggstests/eggstests -repo-dir $(pwd) -build-type valgrind -short
set +x
