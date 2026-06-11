#!/usr/bin/env bash
# Build a .deb package from pre-built artifacts.
#
# Default usage (after build-app.sh):
#   ./scripts/build-deb.sh
#   Output: deploy/microscopi_<version>_arm64.deb
#
# Inside the Docker build container (paths override via env):
#   MICROSCOPI_INSTALL_DIR=/install MICROSCOPI_OUTPUT_DIR=/output ./scripts/build-deb.sh
#
# Requires only: ar (binutils), tar, gzip — present on any Linux host.
# No dpkg installation needed; the .deb format is assembled directly.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/deploy"

# Path overrides for running inside the Docker build container.
INSTALL_DIR="${MICROSCOPI_INSTALL_DIR:-$DEPLOY_DIR/install}"
OUT_DIR="${MICROSCOPI_OUTPUT_DIR:-$DEPLOY_DIR}"

FILES_DIR="$REPO_ROOT/config/pi-gen/stage3/01-microscopi/files"

# --- Sanity checks ---
for tool in ar tar gzip; do
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool not found (install binutils/coreutils)"; exit 1; }
done

[[ -x "$INSTALL_DIR/usr/local/bin/microscopi" ]] || {
    echo "ERROR: $INSTALL_DIR/usr/local/bin/microscopi not found."
    echo "       Run ./scripts/build-app.sh first (or set MICROSCOPI_INSTALL_DIR)."
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

# Default config (conffile: dpkg preserves local edits on upgrade)
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
         libcamera-base0.5,
         libcamera-ipa,
         libsdl2-2.0-0,
         libsdl2-ttf-2.0-0,
         libsdl2-image-2.0-0,
         libdrm2,
         libgbm1,
         libgl1,
         libegl1,
         libavformat59,
         libavcodec59,
         libturbojpeg0,
         libssl3,
         libsqlite3-0,
         libstdc++6,
         libgcc-s1,
         libc6,
         librsvg2-bin,
         v4l-utils
EOF

cat > "$STAGING/DEBIAN/conffiles" << 'EOF'
/etc/microscopi.conf
EOF

cat > "$STAGING/DEBIAN/postinst" << 'POSTINST'
#!/bin/bash
set -e
case "$1" in
    configure)
        if ! id microscopi &>/dev/null; then
            useradd -r -m -d /home/microscopi -s /bin/bash microscopi
        fi
        for g in video render audio input dialout; do
            getent group "$g" >/dev/null 2>&1 && usermod -aG "$g" microscopi || true
        done
        mkdir -p /home/microscopi/videos \
                 /home/microscopi/stills \
                 /home/microscopi/.cache/microscopi/thumbs \
                 /home/microscopi/.local/share/microscopi
        chown -R microscopi:microscopi /home/microscopi
        systemctl daemon-reload || true
        systemctl enable microscopi.service || true
        if systemctl is-system-running 2>/dev/null | grep -qE '^(running|degraded)$'; then
            systemctl start microscopi.service || true
        fi
        ;;
esac
POSTINST
chmod 755 "$STAGING/DEBIAN/postinst"

cat > "$STAGING/DEBIAN/prerm" << 'PRERM'
#!/bin/bash
set -e
case "$1" in
    remove|upgrade|deconfigure)
        systemctl stop microscopi.service    2>/dev/null || true
        systemctl disable microscopi.service 2>/dev/null || true
        ;;
esac
PRERM
chmod 755 "$STAGING/DEBIAN/prerm"

cat > "$STAGING/DEBIAN/postrm" << 'POSTRM'
#!/bin/bash
set -e
case "$1" in
    remove)
        systemctl daemon-reload || true
        ;;
    purge)
        systemctl daemon-reload || true
        rm -rf /usr/local/share/microscopi
        ;;
esac
POSTRM
chmod 755 "$STAGING/DEBIAN/postrm"

# ---------------------------------------------------------------------------
# Assemble the .deb
#
# A .deb is an ar(1) archive with exactly three members in this order:
#   debian-binary   — format version ("2.0\n")
#   control.tar.gz  — DEBIAN/ metadata (control, conffiles, maintainer scripts)
#   data.tar.gz     — the installed file tree
#
# GNU tar's --owner/--group pins uid:gid to 0:0 so the archive is clean
# regardless of who runs this script.
# ---------------------------------------------------------------------------
WORK=$(mktemp -d)
trap 'rm -rf "$STAGING" "$WORK"' EXIT

printf '2.0\n' > "$WORK/debian-binary"

tar -czf "$WORK/control.tar.gz" \
    --owner=0 --group=0 \
    -C "$STAGING/DEBIAN" .

tar -czf "$WORK/data.tar.gz" \
    --owner=0 --group=0 \
    -C "$STAGING" \
    --exclude="./DEBIAN" \
    .

mkdir -p "$OUT_DIR"
OUTPUT="$OUT_DIR/$DEB_FILENAME"
rm -f "$OUTPUT"

# ar -D: deterministic mode (no timestamps/uids in ar headers)
(cd "$WORK" && ar -rcD "$OUTPUT" debian-binary control.tar.gz data.tar.gz)

echo "==> Done. $OUTPUT"
