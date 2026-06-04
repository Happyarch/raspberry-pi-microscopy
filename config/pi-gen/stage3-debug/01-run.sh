#!/bin/bash -e
# Debug additions layered on top of stage3-microscopy.

# Enable SSH.
on_chroot << EOF
systemctl enable ssh
EOF

# Enable avahi-daemon for mDNS — lets you reach the Pi as microscopy.local
# without knowing its IP address.
on_chroot << EOF
systemctl enable avahi-daemon
EOF

# Set a known password for the pi user so SSH password auth works on the LAN.
# DEBUG IMAGES ONLY — never use this password on a production image.
on_chroot << EOF
echo "pi:password" | chpasswd
EOF

# Ensure SSH password authentication is permitted (Pi OS disables it by default).
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication yes/' \
    "${ROOTFS_DIR}/etc/ssh/sshd_config"
sed -i 's/^#\?PermitEmptyPasswords.*/PermitEmptyPasswords no/' \
    "${ROOTFS_DIR}/etc/ssh/sshd_config"

# Install a gdbserver launch helper so you can start it from the Pi console
# without typing the full command every time.
cat > "${ROOTFS_DIR}/usr/local/bin/microscopy-gdbserver" << 'SCRIPT'
#!/bin/bash
# Usage: microscopy-gdbserver [port]
PORT="${1:-2345}"
echo "Starting gdbserver on :${PORT} — connect with:"
echo "  gdb /path/to/debug/microscopy"
echo "  (gdb) target remote microscopy.local:${PORT}"
exec gdbserver ":${PORT}" /usr/local/bin/microscopy "$@"
SCRIPT
chmod 755 "${ROOTFS_DIR}/usr/local/bin/microscopy-gdbserver"

# Hostname suffix so debug and release images are distinguishable on the network.
echo "microscopy-debug" > "${ROOTFS_DIR}/etc/hostname"
sed -i 's/microscopy$/microscopy-debug/' "${ROOTFS_DIR}/etc/hosts" 2>/dev/null || true
