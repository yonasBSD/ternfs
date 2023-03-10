#!/usr/bin/env bash
set -eu -o pipefail

# generate C++ files
(cd go/msgs && go generate ./...) # build cpp files

# build C++, all variants
./cpp/build.py alpine
./cpp/build.py release
./cpp/build.py debug
./cpp/build.py sanitized
./cpp/build.py valgrind

# build go
./go/build.py