#!/usr/bin/env bash
set -eu -o pipefail

base_img=$(realpath ${1})

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

export https_proxy=http://REDACTED

# prepare linux sources
./fetchlinux.sh

# build linux kernel
(cd linux && make oldconfig && make prepare && make -j)

# build kernel module
make "KDIR=${SCRIPT_DIR}/linux-5.4.237" -j kmod

# create vm image
./createimg.sh "$base_img"

# start VM in the background
trap "echo 'Terminating QEMU'; pkill qemu" EXIT
./startvm.sh &>vm-out &

# Wait for VM to go up by trying to copy the kernel module to it
chmod 0600 image-key
scp_attempts=0
while ! scp -P 2222 -o StrictHostKeyChecking=no -i image-key eggsfs.ko fmazzol@localhost: ; do 
    sleep 1
    scp_attempts=$((scp_attempts + 1))
    if [ $scp_attempts -ge 20 ]; then
        echo "Couldn't reach qemu"
        exit 1
    fi
done

# Deploy eggsfs
../deploy/deploy.py --vm --upload --build-type alpine

# Insert module
ssh -p 2222 -i image-key fmazzol@localhost "sudo insmod eggsfs.ko"

ssh -p 2222 -i image-key fmazzol@localhost "eggs/eggstests -kmod -verbose -filter 'mounted|rsync|large' -drop-cached-spans-every 100ms -outgoing-packet-drop 0.1 -short -binaries-dir eggs"
