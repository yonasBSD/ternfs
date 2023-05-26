#!/usr/bin/env bash
set -eu -o pipefail

set -x

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

./sync.sh $1
ssh $1 'cd eggs/kmod && make -j kmod'