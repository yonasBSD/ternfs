#!/usr/bin/env bash
set -eu -o pipefail

short=""
base_img=""

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -short)
            short="-short"
            shift
            ;;
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

echo "Running with base image $base_img, $short"

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

export https_proxy=http://REDACTED

# prepare linux sources
./fetchlinux.sh

# build linux kernel
(cd linux && make oldconfig && make prepare && make -j)

# build kernel module
make "KDIR=${SCRIPT_DIR}/linux" -j kmod

# create vm image
./createimg.sh "$base_img"

# start VM in the background
function cleanup {
    echo 'Syncing logs'
    rsync -e "ssh -p 2222 -i image-key" -avm --include='*/' --include='*integrationtest*' --exclude='*' fmazzol@localhost:/tmp/ ./ || echo 'Could not sync logs'
    echo 'Terminating QEMU'
    pkill qemu    
}
trap cleanup EXIT
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

# Log dmesg
ssh -p 2222 -i image-key fmazzol@localhost "sudo dmesg -wTH" > dmesg &
dmesg_pid=$!

# Run tests (split in multiple executions so that tmp dir doesn't get too large)
ssh -p 2222 -i image-key fmazzol@localhost "eggs/eggstests -verbose -kmod -filter 'large file|cp|utime' -block-service-killer -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out
ssh -p 2222 -i image-key fmazzol@localhost "eggs/eggstests -verbose -kmod -filter 'mounted' -block-service-killer -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out
ssh -p 2222 -i image-key fmazzol@localhost "eggs/eggstests -verbose -kmod -filter 'rsync' -block-service-killer -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out

kill $dmesg_pid

# Unmount
timeout -s KILL 10 ssh -p 2222 -i image-key fmazzol@localhost "grep eggsfs /proc/mounts | awk '{print \$2}' | xargs -r sudo umount"

# Rmmod
timeout -s KILL 10 ssh -p 2222 -i image-key fmazzol@localhost "sudo rmmod eggsfs"
