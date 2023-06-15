#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

# prepare linux sources
./fetchlinux.sh

# build linux kernel
(cd linux && make oldconfig && make prepare && make -j)

# build kernel module
make "KDIR=${SCRIPT_DIR}/linux-5.4.237" -j kmod


