#!/usr/bin/env bash
# Build a debug Pi OS image: release app + SSH + avahi + gdbserver.
# Run build-app-debug.sh first to produce deploy/install-debug/.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/deploy"
PIGEN_DIR="$DEPLOY_DIR/pi-gen"

if [[ ! -f "$DEPLOY_DIR/install-debug/usr/local/bin/microscopy" ]]; then
    echo "ERROR: Run scripts/build-app-debug.sh first (binary not found at deploy/install-debug/usr/local/bin/microscopy)."
    exit 1
fi

echo "==> Cloning pi-gen (if needed)..."
if [[ ! -d "$PIGEN_DIR" ]]; then
    git clone --depth=1 https://github.com/RPi-Distro/pi-gen.git "$PIGEN_DIR"
fi

# Use a separate pi-gen config for the debug image.
cat > "$PIGEN_DIR/config" << 'EOF'
IMG_NAME="microscopy-pi-debug"
RELEASE="bookworm"
DEPLOY_COMPRESSION="xz"
LOCALE_DEFAULT="en_US.UTF-8"
TARGET_HOSTNAME="microscopy-debug"
KEYBOARD_KEYMAP="us"
TIMEZONE_DEFAULT="UTC"
FIRST_USER_NAME="pi"
FIRST_USER_PASS=""
ENABLE_SSH="1"
STAGE_LIST="stage0 stage1 stage2 stage3-microscopy stage3-debug"
EOF

echo "==> Copying stage3-microscopy..."
rm -rf "$PIGEN_DIR/stage3-microscopy"
cp -r "$REPO_ROOT/config/pi-gen/stage3" "$PIGEN_DIR/stage3-microscopy"
# Install the debug binary (with symbols) under files/usr/local/.
STAGE3_FILES="$PIGEN_DIR/stage3-microscopy/files"
mkdir -p "$STAGE3_FILES"
cp -r "$DEPLOY_DIR/install-debug/usr/local" "$STAGE3_FILES/"

echo "==> Copying stage3-debug..."
rm -rf "$PIGEN_DIR/stage3-debug"
cp -r "$REPO_ROOT/config/pi-gen/stage3-debug" "$PIGEN_DIR/stage3-debug"

echo "==> Skipping stages 3–5 in default tree..."
for s in 3 4 5; do
    touch "$PIGEN_DIR/stage${s}/SKIP" 2>/dev/null || true
    touch "$PIGEN_DIR/stage${s}/SKIP_IMAGES" 2>/dev/null || true
done
rm -f "$PIGEN_DIR/stage2/SKIP_IMAGES"

echo "==> Running pi-gen build (Docker mode — no root required)..."
# pi-gen's build-docker.sh breaks on paths containing spaces. Use a symlink.
PIGEN_LINK="/tmp/microscopy-pi-gen-$$"
ln -sfn "$PIGEN_DIR" "$PIGEN_LINK"
cd "$PIGEN_LINK"
CLEAN=1 ./build-docker.sh
cd "$REPO_ROOT"
rm -f "$PIGEN_LINK"

echo "==> Debug image ready in $PIGEN_DIR/deploy/"
ls -lh "$PIGEN_DIR/deploy/"*debug* 2>/dev/null || ls -lh "$PIGEN_DIR/deploy/"
