#!/usr/bin/env bash
set -x -eu -o pipefail
make -j -C cpp
make -j -C go