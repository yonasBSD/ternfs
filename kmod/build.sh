#!/usr/bin/env bash
set -eu -o pipefail

set -x

./sync.sh $1
ssh $1 'cd fmazzol/eggs-kmod && make -j kmod writefile'