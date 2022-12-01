#!/usr/bin/env bash
set -eu -o pipefail

./cpp/build.py alpine rs crc32c # build libs for go
(cd go/msgs && go generate ./...) # generate C++ files

# build C++, all variants
./cpp/build.py alpine
./cpp/build.py alpine-debug
./cpp/build.py release
./cpp/build.py debug
./cpp/build.py sanitized
./cpp/build.py valgrind

# build go
./go/build.py