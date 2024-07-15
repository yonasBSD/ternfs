#!/bin/bash

set -e

VERSION=${1:?}

BASE_DIR="eggsfs-client-${VERSION}"
MOD_DIR="${BASE_DIR}/lib/modules/$(uname -r)/fs/eggsfs"
SVC_DIR="${BASE_DIR}/etc/systemd/system"
MOD_LOAD_DIR="${BASE_DIR}/etc/modules-load.d"

mkdir -p ${BASE_DIR}/DEBIAN
cat debian/control | sed "s/##VERSION##/${VERSION}/g" > ${BASE_DIR}/DEBIAN/control

mkdir -p ${MOD_LOAD_DIR}
cp debian/eggsfs_load.conf ${MOD_LOAD_DIR}/eggsfs.conf

mkdir -p ${MOD_DIR}
cp eggsfs.ko ${MOD_DIR}

mkdir -p ${SVC_DIR}
cp debian/eggsfs-client.service ${SVC_DIR}

mkdir -p ${BASE_DIR}/mnt/eggsfs

dpkg-deb --build --root-owner-group ${BASE_DIR}
