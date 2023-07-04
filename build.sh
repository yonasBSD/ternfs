#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

build_variant=$1
out_dir=build/$build_variant 
mkdir -p build/$build_variant

./cpp/build.py alpine rs crc32c # build libs for go
(cd go/msgs && go generate ./...) # generate C++ files

# build C++
./cpp/build.py $build_variant

# build go
./go/build.py

# copy binaries
binaries=(
    cpp/build/$build_variant/shard/eggsshard
    cpp/build/$build_variant/cdc/eggscdc
    cpp/build/$build_variant/ktools/eggsktools
    go/eggsshuckle/eggsshuckle
    go/eggsrun/eggsrun
    go/eggsblocks/eggsblocks
    go/eggsfuse/eggsfuse
    go/eggscli/eggscli
    go/eggsgc/eggsgc
    go/eggstests/eggstests
)

for binary in "${binaries[@]}"; do
    cp $binary $out_dir
done
