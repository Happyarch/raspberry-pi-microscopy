#!/usr/bin/env bash
# Build a .deb package from the artifacts already in deploy/install/.
# Run scripts/build-app.sh first to populate deploy/install/.
#
# dpkg-deb is sourced from tools/bin/ (project-local, not system-installed).
# To refresh it: scripts/bootstrap-tools.sh
#
# Output: deploy/microscopi_<version>_arm64.deb
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/deploy"
INSTALL_DIR="$DEPLOY_DIR/install"
FILES_DIR="$REPO_ROOT/config/pi-gen/stage3/01-microscopi/files"

# Prefer the project-local dpkg-deb; fall back to whatever is on PATH.
DPKG_DEB="$REPO_ROOT/tools/bin/dpkg-deb"
if [[ ! -x "$DPKG_DEB" ]]; then
    DPKG_DEB=$(command -v dpkg-deb 2>/dev/null || true)
fi
[[ -x "$DPKG_DEB" ]] || {
    echo "ERROR: dpkg-deb not found."
    echo "  Run: ./scripts/bootstrap-tools.sh"
    exit 1
}

[[ -x "$INSTALL_DIR/usr/local/bin/microscopi" ]] || {
    echo "ERROR: $INSTALL_DIR/usr/local/bin/microscopi not found."
    echo "       Run ./scripts/build-app.sh first."
    exit 1
}

# --- Version ---
# Produces e.g. "1.2.3", "1.2.3.4.gabc1234", or "0.0.gabc1234"
VERSION=$(git -C "$REPO_ROOT" describe --tags --always 2>/dev/null \
    | sed 's/^v//; s/-/./g' \
    || date +%Y%m%d)

PKG_NAME="microscopi"
ARCH="arm64"
DEB_FILENAME="${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "==> Building $DEB_FILENAME ..."

# --- Staging tree ---
STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

# Binary
install -D -m 755 "$INSTALL_DIR/usr/local/bin/microscopi" \
                  "$STAGING/usr/local/bin/microscopi"

# Assets
install -d "$STAGING/usr/local/share/microscopi/icons"
cp "$INSTALL_DIR/usr/local/share/microscopi/icons/"*.svg \
   "$STAGING/usr/local/share/microscopi/icons/"

install -d "$STAGING/usr/local/share/microscopi/fonts"
cp "$INSTALL_DIR/usr/local/share/microscopi/fonts/"*.ttf \
   "$STAGING/usr/local/share/microscopi/fonts/"

# Default config (installed as a conffile so dpkg preserves local edits on upgrade)
install -D -m 644 "$INSTALL_DIR/usr/local/etc/microscopi.conf" \
                  "$STAGING/etc/microscopi.conf"

# Systemd service
install -D -m 644 "$FILES_DIR/microscopi.service" \
                  "$STAGING/usr/lib/systemd/system/microscopi.service"

# Autologin drop-in (tty1 → microscopi)
install -D -m 644 "$FILES_DIR/autologin.conf" \
                  "$STAGING/usr/lib/systemd/system/getty@tty1.service.d/autologin.conf"

# --- DEBIAN control files ---
mkdir -p "$STAGING/DEBIAN"

cat > "$STAGING/DEBIAN/control" << EOF
Package: $PKG_NAME
Version: $VERSION
Architecture: $ARCH
Maintainer: microscopi project <https://github.com/your-repo/raspberry-pi-microscopy>
Description: Raspberry Pi microscopy camera application
 A minimal C++/libcamera/SDL2 application for driving a Raspberry Pi
 camera module as a microscope. Features include MJPEG streaming,
 timelapse recording, MKV video with H.264, and a Sony-Alpha-style OSD.
Depends: libcamera0.5,
         libcamera-ipa,
         libavformat59,
         libavcodec59,
         libavutil57,
         libsdl2-2.0-0,
         libsdl2-ttf-2.0-0,
         libsdl2-image-2.0-0,
         librsvg2-bin,
         libturbojpeg0,
         libssl3,
         v4l-utils
EOF

# List of files dpkg treats as user-editable configs.
# dpkg will ask before overwriting these on upgrade if they were modified.
cat > "$STAGING/DEBIAN/conffiles" << 'EOF'
/etc/microscopi.conf
EOF

# postinst: run after package files are installed
cat > "$STAGING/DEBIAN/postinst" << 'POSTINST'
#!/bin/bash
set -e
case "$1" in
    configure)
        # Create the dedicated system user if it doesn't exist.
        if ! id microscopi &>/dev/null; then
            useradd -r -m -d /home/microscopi -s /bin/bash microscopi
        fi

        # Ensure membership in all required groups (best-effort; groups may
        # not exist on non-Pi systems but the package must still install).
        for g in video render audio input dialout; do
            getent group "$g" >/dev/null 2>&1 && usermod -aG "$g" microscopi || true
        done

        # Create output directories owned by microscopi.
        mkdir -p /home/microscopi/videos \
                 /home/microscopi/stills \
                 /home/microscopi/.cache/microscopi
        chown -R microscopi:microscopi /home/microscopi

        # Enable the service. Only start it immediately if systemd is running
        # (it won't be during chroot installs, e.g. pi-gen or debootstrap).
        systemctl daemon-reload || true
        systemctl enable microscopi.service || true
        if systemctl is-system-running 2>/dev/null | grep -qE '^(running|degraded)$'; then
            systemctl start microscopi.service || true
        fi
        ;;
esac
POSTINST
chmod 755 "$STAGING/DEBIAN/postinst"

# prerm: stop the service before files are removed
cat > "$STAGING/DEBIAN/prerm" << 'PRERM'
#!/bin/bash
set -e
case "$1" in
    remove|upgrade|deconfigure)
        systemctl stop microscopi.service  2>/dev/null || true
        systemctl disable microscopi.service 2>/dev/null || true
        ;;
esac
PRERM
chmod 755 "$STAGING/DEBIAN/prerm"

# postrm: clean up after the package files are gone
cat > "$STAGING/DEBIAN/postrm" << 'POSTRM'
#!/bin/bash
set -e
case "$1" in
    remove)
        systemctl daemon-reload || true
        ;;
    purge)
        systemctl daemon-reload || true
        # Remove assets not tracked as conffiles.
        rm -rf /usr/local/share/microscopi
        # Leave /home/microscopi and /etc/microscopi.conf to dpkg's conffile
        # handling; dpkg purge removes conffiles automatically.
        ;;
esac
POSTRM
chmod 755 "$STAGING/DEBIAN/postrm"

# --- Build ---
# --root-owner-group: sets uid/gid to 0:0 (root:root) in the archive,
# regardless of who owns the staging files on the host.
"$DPKG_DEB" --root-owner-group --build "$STAGING" "$DEPLOY_DIR/$DEB_FILENAME"

echo "==> Done."
echo "    Package: $DEPLOY_DIR/$DEB_FILENAME"
echo ""
echo "    Install on Pi:"
echo "      scp $DEPLOY_DIR/$DEB_FILENAME microscopi@192.168.1.220:~/"
echo "      ssh microscopi@192.168.1.220 'sudo dpkg -i ~/$DEB_FILENAME'"
