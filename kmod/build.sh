#!/usr/bin/env bash
set -eu -o pipefail

set -x

./sync.sh
ssh uovo 'cd eggs-kmod && make -j kmod writefile'