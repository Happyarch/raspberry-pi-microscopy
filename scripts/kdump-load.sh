#!/usr/bin/env bash
# Load the kexec crash kernel. Called by kdump.service at boot.
# Must run as root. Requires crashkernel= in boot parameters.
set -euo pipefail

ROOT_UUID=$(findmnt -n -o UUID /)
if [[ -z "$ROOT_UUID" ]]; then
    echo "kdump-load: could not determine root UUID via findmnt" >&2
    exit 1
fi

cat /boot/intel-ucode.img /boot/initramfs-linux.img > /tmp/kdump-initrd.img

kexec -p /boot/vmlinuz-linux \
    --initrd=/tmp/kdump-initrd.img \
    --append="root=UUID=${ROOT_UUID} rw rootfstype=ext4 \
              irqpoll maxcpus=1 reset_devices \
              systemd.unit=kdump-save.service"

echo "kdump-load: crash kernel loaded (root=UUID=${ROOT_UUID})"
