#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

qemu-system-x86_64 \
    -machine accel=kvm,type=q35 \
    -enable-kvm \
    -cpu host \
    -kernel "${SCRIPT_DIR}/linux/arch/x86/boot/bzImage" \
    -append "root=/dev/sda1 single console=ttyS0 systemd.unit=multi-user.target" \
    -hda "${SCRIPT_DIR}/ubuntu.img" \
    -hdb "${SCRIPT_DIR}/init.img" \
    -m 64G \
    -smp 50 \
    -nographic \
    -nic user,hostfwd=tcp::2222-:22
