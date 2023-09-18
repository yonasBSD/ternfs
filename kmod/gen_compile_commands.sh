#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

set -x

./linux/scripts/gen_compile_commands.py
sed -i 's:-I.:-I./linux:g' ./compile_commands.json
sed -i 's:-include .:-include ./linux:g' ./compile_commands.json