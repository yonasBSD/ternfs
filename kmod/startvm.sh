#!/usr/bin/env bash
set -eu -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# qemu-system-x86_64 -s -enable-kvm -cpu host -m 64G -drive "file=${SCRIPT_DIR}/ubuntu-20.04.qcow2,if=virtio" -nic user,hostfwd=tcp::2222-:22,model=virtio-net-pci -smp 50 -virtfs local,path=/data/home/fmazzol/src/eggsfs/kmod/eggs-data,mount_tag=eggs-data,security_model=mapped,id=eggs-data -vnc :1 -monitor stdio

qemu-system-x86_64 \
    -machine accel=kvm,type=q35 \
    -enable-kvm \
    -cpu host \
    -kernel /data/home/fmazzol/src/eggsfs/kmod/linux-5.4.237/arch/x86/boot/bzImage \
    -append "root=/dev/sda1 single console=ttyS0 systemd.unit=graphical.target" \
    -hda ubuntu-20.04.img \
    -hdb init.img \
    -m 64G \
    -smp 50 \
    -nographic \
    -nic user,hostfwd=tcp::2222-:22,model=virtio-net-pci \
    -virtfs local,path=/data/home/fmazzol/src/eggsfs/kmod/eggs-data,mount_tag=eggs-data,security_model=mapped,id=eggs-data
