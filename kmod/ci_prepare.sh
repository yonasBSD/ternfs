#!/usr/bin/env bash
set -eu -o pipefail

base_img=""

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        *)
            if [[ -n "$base_img" ]]; then
                echo "Bad usage -- base image specified twice"
                exit 2
            else
                base_img=$(realpath ${1})
                shift
            fi
            ;;
    esac
done

if [[ -z "$base_img" ]]; then
    echo "Bad usage -- no base image"
    exit 2
fi

echo "Preparing kmod CI environment with base image $base_img"

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

export https_proxy=http://REDACTED

# prepare linux sources
./fetchlinux.sh

# build linux kernel
(cd linux && make oldconfig && make prepare && make -j)

# create vm image
./createimg.sh "$base_img"
