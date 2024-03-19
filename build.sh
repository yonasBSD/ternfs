#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

build_variant=$1
out_dir=build/$build_variant
mkdir -p build/$build_variant

${PWD}/cpp/build.py alpine rs crc32c # build C libs we need for for go
${PWD}/go/build.py --generate # generate C++ files

# build C++
${PWD}/cpp/build.py $build_variant

# build go
${PWD}/go/build.py

# copy binaries
binaries=(
    cpp/build/$build_variant/shard/eggsshard
    cpp/build/$build_variant/dbtools/eggsdbtools
    cpp/build/$build_variant/cdc/eggscdc
    cpp/build/$build_variant/ktools/eggsktools
    go/eggsshuckle/eggsshuckle
    go/eggsrun/eggsrun
    go/eggsblocks/eggsblocks
    go/eggsfuse/eggsfuse
    go/eggscli/eggscli
    go/eggsgc/eggsgc
    go/eggstests/eggstests
    go/eggsshucklebeacon/eggsshucklebeacon
)

for binary in "${binaries[@]}"; do
    cp $binary $out_dir
done
