#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

imgs_dir=${1:-"."}

qemu-system-x86_64 \
    -machine accel=kvm,type=q35 \
    -enable-kvm \
    -cpu host \
    -kernel "${SCRIPT_DIR}/linux-5.4.237/arch/x86/boot/bzImage" \
    -append "root=/dev/sda1 single console=ttyS0 systemd.unit=graphical.target" \
    -hda "${imgs_dir}/ubuntu-20.04.img" \
    -hdb "${imgs_dir}/init.img" \
    -m 64G \
    -smp 50 \
    -nographic \
    -nic user,hostfwd=tcp::2222-:22,model=virtio-net-pci