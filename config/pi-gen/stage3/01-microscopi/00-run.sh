#!/bin/bash -e
# Runs inside the pi-gen chroot to set up the microscopi system.

# STAGE_DIR is the stage root (stage3-microscopi/), not this substage dir.
# Use $0 to find our own files directory reliably.
SUBSTAGE_DIR="$(dirname "$(realpath "$0")")"
USRLOCAL="${SUBSTAGE_DIR}/files/usr/local"
FILES_DIR="${SUBSTAGE_DIR}/files"

echo "==> stage3-microscopi: installing from ${USRLOCAL}"
ls -la "${USRLOCAL}/bin/" || { echo "ERROR: ${USRLOCAL}/bin/ not found — did build-app.sh run?"; exit 1; }

install -d "${ROOTFS_DIR}/usr/local/bin"
install -d "${ROOTFS_DIR}/usr/local/share/microscopi/icons"
install -d "${ROOTFS_DIR}/usr/local/share/microscopi/fonts"
install -d "${ROOTFS_DIR}/etc"

install -m 755 "${USRLOCAL}/bin/microscopi" \
               "${ROOTFS_DIR}/usr/local/bin/microscopi"

cp "${USRLOCAL}/share/microscopi/icons/"*.svg \
   "${ROOTFS_DIR}/usr/local/share/microscopi/icons/"

cp "${USRLOCAL}/share/microscopi/fonts/"*.ttf \
   "${ROOTFS_DIR}/usr/local/share/microscopi/fonts/"

install -m 644 "${USRLOCAL}/etc/microscopi.conf" \
               "${ROOTFS_DIR}/etc/microscopi.conf"

# ---- systemd service ----
install -d "${ROOTFS_DIR}/etc/systemd/system"
install -m 644 "${FILES_DIR}/microscopi.service" \
               "${ROOTFS_DIR}/etc/systemd/system/microscopi.service"

on_chroot << EOF
systemctl enable microscopi.service
EOF

# ---- Autologin for user microscopi on tty1 ----
install -d "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d"
install -m 644 "${FILES_DIR}/autologin.conf" \
               "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d/autologin.conf"

# ---- User accounts ----
on_chroot << EOF
# Ensure the microscopi user exists; pi-gen should have created it, but be defensive.
id microscopi &>/dev/null || useradd -m -s /bin/bash -G adm,dialout,cdrom,sudo microscopi

# Remove password (autologin — no password prompt needed).
passwd -d microscopi

# Set a root password. Change before deployment!
echo "root:microscopi" | chpasswd

# Grant microscopi access to camera (/dev/video*) and DRM display (/dev/dri/*).
usermod -aG video,render,audio,input microscopi

# Create output directories owned by microscopi.
mkdir -p /home/microscopi/videos /home/microscopi/stills /home/microscopi/.cache/microscopi
chown -R microscopi:microscopi /home/microscopi

# Remove the first-boot user-creation wizard — we pre-create the user above.
apt-get remove -y userconf-pi 2>/dev/null || dpkg --remove userconf-pi 2>/dev/null || true
EOF

# ---- WiFi AP fallback (NetworkManager + avahi) ----
# NM connection profile (must be 0600 root:root or NM ignores it)
install -d "${ROOTFS_DIR}/etc/NetworkManager/system-connections"
install -m 600 \
    "${FILES_DIR}/microscopi-ap.nmconnection" \
    "${ROOTFS_DIR}/etc/NetworkManager/system-connections/microscopi-ap.nmconnection"
chown root:root \
    "${ROOTFS_DIR}/etc/NetworkManager/system-connections/microscopi-ap.nmconnection"

# dnsmasq host overrides — resolve microscopi.local on the AP interface
install -d "${ROOTFS_DIR}/etc/NetworkManager/dnsmasq-shared.d"
install -m 644 "${FILES_DIR}/nm-dnsmasq-microscopi.conf" \
    "${ROOTFS_DIR}/etc/NetworkManager/dnsmasq-shared.d/microscopi.conf"

# Fallback-AP systemd service and check script
install -m 644 "${FILES_DIR}/microscopi-wifi-ap.service" \
    "${ROOTFS_DIR}/etc/systemd/system/microscopi-wifi-ap.service"
install -m 755 "${FILES_DIR}/microscopi-wifi-ap-check.sh" \
    "${ROOTFS_DIR}/usr/local/bin/microscopi-wifi-ap-check.sh"

on_chroot << EOF
systemctl enable microscopi-wifi-ap.service
# Enable avahi-daemon for .local mDNS on all interfaces (LAN access)
systemctl enable avahi-daemon
EOF

# ---- boot/firmware/config.txt additions ----
cat "${FILES_DIR}/config.txt.append" >> "${ROOTFS_DIR}/boot/firmware/config.txt"
