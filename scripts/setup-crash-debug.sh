#!/usr/bin/env bash
# Install kernel crash-capture configuration on this build host.
# Run once as root: sudo bash scripts/setup-crash-debug.sh
set -euo pipefail

echo "==> Writing /etc/sysctl.d/99-crash-debug.conf ..."
cat > /etc/sysctl.d/99-crash-debug.conf << 'EOF'
# Convert CPU lockups into kernel panics so kdump/pstore can capture them.
# Without these, a frozen system produces no crash data at all.
kernel.softlockup_panic = 1
kernel.hardlockup_panic = 1
kernel.unknown_nmi_panic = 1
kernel.panic_on_oops = 1

# Reboot 30 seconds after panic (time for pstore write to complete).
kernel.panic = 30
EOF
sysctl --system

echo "==> Writing /etc/fstab entry for pstore ..."
if ! grep -q pstore /etc/fstab; then
    echo "pstore  /sys/fs/pstore  pstore  defaults  0  0" >> /etc/fstab
fi
mkdir -p /sys/fs/pstore
mount -a 2>/dev/null || true
ls /sys/fs/pstore/ && echo "pstore mounted OK" || echo "pstore mount: check dmesg"

echo "==> Installing kexec-tools and makedumpfile ..."
pacman -Sy --noconfirm kexec-tools makedumpfile crash 2>/dev/null || \
    echo "WARN: pacman install failed — install manually: sudo pacman -S kexec-tools makedumpfile crash"

echo ""
echo "==> MANUAL STEP REQUIRED: add crashkernel= to your boot parameters."
echo "    This reserves RAM for the crash kernel so kdump can save a full vmcore."
echo ""
echo "    For systemd-boot:"
echo "      Edit /boot/loader/entries/*.conf"
echo "      Append to 'options' line:  crashkernel=512M"
echo ""
echo "    For GRUB:"
echo "      Edit /etc/default/grub"
echo "      Add to GRUB_CMDLINE_LINUX:  crashkernel=512M"
echo "      Then run: grub-mkconfig -o /boot/grub/grub.cfg"
echo ""
echo "    Reboot after making that change."
echo ""
echo "==> After reboot with crashkernel=512M, run this to load the crash kernel:"
echo "    sudo kexec -p /boot/vmlinuz-linux \\"
echo "      --initrd=/boot/initramfs-linux-fallback.img \\"
echo '      --command-line="root=UUID=$(findmnt -n -o UUID /) rw rootfstype=ext4 irqpoll maxcpus=1 reset_devices"'
echo "    (Or create a systemd service — see scripts/kdump.service)"

# --- Service 1: load crash kernel at normal boot (runs in primary kernel) ---
cat > /etc/systemd/system/kdump.service << 'EOF'
[Unit]
Description=Load kexec crash kernel
After=local-fs.target
# Only meaningful if crashkernel= was passed at boot and memory was reserved.
ConditionPathExists=/sys/kernel/kexec_crash_size

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/bash -c '\
  ROOT_UUID=$(findmnt -n -o UUID /) && \
  cat /boot/intel-ucode.img /boot/initramfs-linux.img > /tmp/kdump-initrd.img && \
  kexec -p /boot/vmlinuz-linux \
    --initrd=/tmp/kdump-initrd.img \
    --append="root=UUID=$${ROOT_UUID} rw rootfstype=ext4 \
              irqpoll maxcpus=1 reset_devices \
              systemd.unit=kdump-save.service"'

[Install]
WantedBy=multi-user.target
EOF

# --- Service 2: save filtered dump (runs in crash kernel after a panic) ---
# ConditionPathExists=/proc/vmcore is only true in the crash kernel, so this
# service is a silent no-op on every normal boot.
cat > /etc/systemd/system/kdump-save.service << 'EOF'
[Unit]
Description=Save kernel crash dump (filtered, kernel pages only)
DefaultDependencies=no
After=local-fs.target
Before=network.target shutdown.target
ConditionPathExists=/proc/vmcore

[Service]
Type=oneshot
# -d 31 : exclude zero, free, cache, and user pages — kernel structures only.
# -l    : LZO compression (roughly halves output size).
# On a mostly-idle 512 GB machine this typically produces a 1–3 GB dump.
ExecStartPre=/bin/bash -c '\
  AVAIL_KB=$(df --output=avail /var/crash | tail -1); \
  NEED_KB=$((6 * 1024 * 1024)); \
  if [ "$AVAIL_KB" -lt "$NEED_KB" ]; then \
    echo "kdump-save: only ${AVAIL_KB} KB free in /var/crash, need ${NEED_KB} KB — aborting dump"; \
    exit 1; \
  fi'
ExecStart=/usr/bin/makedumpfile -d 31 -l /proc/vmcore /var/crash/vmcore
ExecStartPost=/bin/systemctl reboot
TimeoutSec=600

[Install]
WantedBy=multi-user.target
EOF

echo "==> Wrote /etc/systemd/system/kdump.service (loads crash kernel)"
echo "==> Wrote /etc/systemd/system/kdump-save.service (saves dump on crash)"
echo "    Enable with: sudo systemctl enable kdump.service kdump-save.service"
echo "    Then start:  sudo systemctl start kdump.service"
echo "    (kdump-save only activates in the crash kernel, harmless on normal boots)"

echo ""
echo "==> Crash dump output: /var/crash/vmcore"
mkdir -p /var/crash
echo ""
echo "Done. Panic-on-lockup is active NOW (no reboot needed for sysctl)."
echo "Full vmcore capture requires the crashkernel= boot param + reboot."
