#!/usr/bin/env bash
set -x -eu -o pipefail
(cd python && ./tests.py)
(cd go && go test ./...)
