#!/bin/bash -e
# Runs inside the pi-gen chroot to set up the microscopy system.

# ---- Install compiled binary and assets ----
install -d "${ROOTFS_DIR}/usr/local/bin"
install -d "${ROOTFS_DIR}/usr/local/share/microscopy/icons"
install -d "${ROOTFS_DIR}/usr/local/share/microscopy/fonts"
install -d "${ROOTFS_DIR}/etc"

install -m 755 "${STAGE_DIR}/files/usr/local/bin/microscopy" \
               "${ROOTFS_DIR}/usr/local/bin/microscopy"

cp "${STAGE_DIR}/files/usr/local/share/microscopy/icons/"*.svg \
   "${ROOTFS_DIR}/usr/local/share/microscopy/icons/"

cp "${STAGE_DIR}/files/usr/local/share/microscopy/fonts/"*.ttf \
   "${ROOTFS_DIR}/usr/local/share/microscopy/fonts/"

install -m 644 "${STAGE_DIR}/files/etc/microscopy.conf" \
               "${ROOTFS_DIR}/etc/microscopy.conf"

# ---- systemd service ----
install -d "${ROOTFS_DIR}/etc/systemd/system"
install -m 644 "${STAGE_DIR}/files/microscopy.service" \
               "${ROOTFS_DIR}/etc/systemd/system/microscopy.service"

on_chroot << EOF
systemctl enable microscopy.service
EOF

# ---- Autologin for user pi on tty1 ----
install -d "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d"
install -m 644 "${STAGE_DIR}/files/autologin.conf" \
               "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d/autologin.conf"

# ---- User accounts ----
on_chroot << EOF
# Remove password for pi (autologin, no password prompt).
passwd -d pi

# Set a root password. Change before deployment!
echo "root:microscopy" | chpasswd

# Create output directories owned by pi.
mkdir -p /home/pi/videos /home/pi/stills /home/pi/.cache/microscopy
chown -R pi:pi /home/pi/videos /home/pi/stills /home/pi/.cache
EOF

# ---- boot/firmware/config.txt additions ----
cat "${STAGE_DIR}/files/config.txt.append" >> "${ROOTFS_DIR}/boot/firmware/config.txt"
