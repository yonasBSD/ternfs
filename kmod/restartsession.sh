#!/usr/bin/env bash

set -eu -o pipefail

run=false
deploy=false
build_type='alpine-debug'

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -run)
            run=true
            shift
            ;;
        -deploy)
            deploy=true
            shift
            ;;
        -build-type)
            shift
            build_type="$1"
            shift
            ;;
        *)
            echo "Bad usage"
            exit 2
            ;;
    esac
done

# Kills the current VM, starts it again, deploys to it and builds kmod, loads kmod, opens a tmux
# session with dmesg in one pane and a console in ~/eggs in the other.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

make KDIR=$SCRIPT_DIR/linux-5.4.237 -j kmod

pkill qemu || true
tmux kill-session -t uovo || true

sleep 1 # sometimes qemu lingers?

# Windows:
#
# 0. dmesg
# 1. free shell
# 2. shell with eggsfs running
# 3. qemu running

tmux new-session -d -s uovo
tmux new-window -t uovo:3 './startvm.sh'

# Wait for VM to go up, and build
while ! scp eggsfs.ko uovo: ; do sleep 1; done

# Start dmesg as soon as it's booted (before we insert module)
tmux send-keys -t uovo:0 "ssh -t uovo dmesg -wHT" Enter

# Insert module
ssh uovo 'sudo insmod eggsfs.ko'

if [[ "$deploy" = true ]]; then
    # Deploy binaries
    (cd ../deploy && ./deploy.py --build-type "$build_type" --upload --host fmazzol@uovo)
fi

# Create shells
tmux new-window -t uovo:1 'ssh -t uovo'
if [[ "$run" = true ]]; then
    tmux new-window -t uovo:2 'ssh -t uovo "cd eggs && ./eggsrun -verbose -binaries-dir ~/eggs -data-dir ~/eggs-data/"'
fi

# Attach
tmux attach-session -t uovo:1

# sudo sh -c 'echo eggsfs_metadata_request >> /sys/kernel/debug/tracing/set_event' && sudo sh -c 'echo eggsfs_block_write >> /sys/kernel/debug/tracing/set_event'
# sudo sysctl fs.eggsfs.prefetch=0
# ./eggs/eggstests -kmod -filter 'mounted fs$' -drop-cached-spans-every 100 -cfg fsTest.checkThreads=1 -cfg fsTest.numFiles=1000 -cfg fsTest.numDirs=1 -short -binaries-dir $(pwd)/eggs
# sudo cat /sys/kernel/debug/tracing/trace_pipe