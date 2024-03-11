#!/usr/bin/env bash
set -eu -o pipefail

short=""

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -short)
            short="-short"
            shift
            ;;
        *)
            echo "Bad usage -- only accepted flag is -short"
            exit 2
            ;;
    esac
done

echo "Running with short $short"

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

# build kernel module
make "KDIR=${SCRIPT_DIR}/linux" -j kmod

# start VM in the background
function cleanup {
    echo 'Syncing logs'
    rsync -e "ssh -p 2223 -i image-key" -avm --include='/eggs-integrationtest.**/' --include=log --include=test-log --include=stderr --include=stdout --exclude='*' fmazzol@localhost:/tmp/ ./ || echo 'Could not sync logs'
    echo 'Terminating QEMU'
    pkill qemu    
}
trap cleanup EXIT
./startvm.sh &>vm-out &

# Wait for VM to go up by trying to copy the kernel module to it
chmod 0600 image-key
scp_attempts=0
while ! scp -v -P 2223 -o StrictHostKeyChecking=no -i image-key eggsfs.ko fmazzol@localhost: ; do 
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
ssh -p 2223 -i image-key fmazzol@localhost "sudo insmod eggsfs.ko"

# Log dmesg
ssh -p 2223 -i image-key fmazzol@localhost "sudo dmesg -wTH" > dmesg &
dmesg_pid=$!

# Trace metadata requests

ssh -p 2223 -i image-key fmazzol@localhost "sudo sh -c 'echo 1 > /sys/kernel/tracing/events/eggsfs/eggsfs_metadata_request/enable'"
ssh -p 2223 -i image-key fmazzol@localhost "sudo sh -c 'echo 1 > /sys/kernel/tracing/events/eggsfs/eggsfs_fetch_stripe/enable'"
ssh -p 2223 -i image-key fmazzol@localhost "sudo cat /sys/kernel/tracing/trace_pipe" > trace &
trace_pid=$!

# Do not test migrations/scrubbing since we test this outside qemu anyway
# (it's completely independent from the kmod code)
ssh -p 2223 -i image-key fmazzol@localhost "eggs/eggstests -verbose -shuckle-beacon-port 55556 -kmod -filter 'large file|cp|utime|seek|ftruncate' -block-service-killer -cfg fsTests.dontMigrate -cfg fsTest.corruptFileProb=0 -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out
ssh -p 2223 -i image-key fmazzol@localhost "eggs/eggstests -verbose -shuckle-beacon-port 55556 -kmod -filter 'mounted' -block-service-killer -cfg fsTests.dontMigrate -cfg fsTest.corruptFileProb=0 -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out
ssh -p 2223 -i image-key fmazzol@localhost "eggs/eggstests -verbose -shuckle-beacon-port 55556 -kmod -filter 'mounted' -block-service-killer -cfg fsTest.readWithMmap -cfg fsTests.dontMigrate -cfg fsTest.corruptFileProb=0 -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out
ssh -p 2223 -i image-key fmazzol@localhost "eggs/eggstests -verbose -shuckle-beacon-port 55556 -kmod -filter 'rsync' -block-service-killer -cfg fsTests.dontMigrate -cfg fsTest.corruptFileProb=0 -drop-cached-spans-every 100ms -outgoing-packet-drop 0.02 $short -binaries-dir eggs" | tee -a test-out

echo 'Unmounting'
timeout -s KILL 300 ssh -p 2223 -i image-key fmazzol@localhost "grep eggsfs /proc/mounts | awk '{print \$2}' | xargs -r sudo umount"

echo 'Removing module'
timeout -s KILL 300 ssh -p 2223 -i image-key fmazzol@localhost "sudo rmmod eggsfs"

kill $trace_pid
kill $dmesg_pid

echo 'Checking for BUG/WARNING in dmesg'
set +e
grep -e BUG -e WARNING dmesg >/dev/null
grep_failed="$?"
set -e
if [[ "$grep_failed" -eq "0" ]]; then
    echo "BUG|WARNING found in dmesg:"
    grep -A 2 -e BUG -e WARNING dmesg | tail -10
    exit 1
fi

# sudo sysctl fs.eggsfs.debug=1
# eggs/eggstests -kmod -filter 'direct' -short -binaries-dir eggs

# ./eggs/eggstests -binaries-dir eggs -filter direct -kmod -shuckle-beacon-port 55556
