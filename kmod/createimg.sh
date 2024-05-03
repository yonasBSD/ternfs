 #!/usr/bin/env bash
set -eu -o pipefail

base_img=$(realpath ${1})

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

set -x

rm -f ubuntu.img
qemu-img create -f qcow2 -F qcow2 -b $base_img ubuntu.img 100G
cloud-localds init.img image-init.yml

# remove existing key if any
ssh-keygen -R '[localhost]:2223' >/dev/null
