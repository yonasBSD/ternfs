#!/usr/bin/env bash
set -eu -o pipefail

rsync -vaxAXL --include '*.py' --include '*.c' --include '*.h' --include 'Makefile' --exclude '*' ./ $1:fmazzol/eggs-kmod/
