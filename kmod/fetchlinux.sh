#!/usr/bin/env bash
set -eu -o pipefail

version=linux-5.4.237

# Download or resume
# use artifactory so that this works in iceland as well
# curl -C - -O "https://cdn.kernel.org/pub/linux/kernel/v5.x/${version}.tar.gz"
curl -C - -O "https://REDACTED${version}.tar.gz"

# Check
sha512sum -c "${version}.tar.gz.sha512"

# Extract
tar xf "${version}.tar.gz"

# Copy config
cp config-kasan ${version}/.config

# Create symlink
ln -sf ${version} linux
