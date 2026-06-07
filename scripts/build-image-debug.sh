#!/usr/bin/env bash
# Build a debug Pi OS image: debug binary + SSH + avahi + gdbserver.
# Run build-app-debug.sh first to produce deploy/install-debug/.
#
# Usage: build-image-debug.sh [--clean]
#   --clean  Wipe and rebuild all stages from scratch (slower, ~40 min).
#            Default is CONTINUE=1 which skips stage0-2 on subsequent builds (~8-10 min).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/deploy"
PIGEN_DIR="$DEPLOY_DIR/pi-gen"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        *) echo "Unknown argument: $arg"; echo "Usage: $0 [--clean]"; exit 1 ;;
    esac
done

if [[ ! -f "$DEPLOY_DIR/install-debug/usr/local/bin/microscopi" ]]; then
    echo "ERROR: Run scripts/build-app-debug.sh first (binary not found at deploy/install-debug/usr/local/bin/microscopi)."
    exit 1
fi

echo "==> Cloning pi-gen (if needed)..."
if [[ ! -d "$PIGEN_DIR" ]]; then
    git clone --depth=1 --branch bookworm-arm64 https://github.com/RPi-Distro/pi-gen.git "$PIGEN_DIR"
fi

# Use a separate pi-gen config for the debug image.
cat > "$PIGEN_DIR/config" << 'EOF'
IMG_NAME="microscopi-pi-debug"
RELEASE="bookworm"
DEPLOY_COMPRESSION="gz"
LOCALE_DEFAULT="en_US.UTF-8"
TARGET_HOSTNAME="microscopi-debug"
KEYBOARD_KEYMAP="us"
TIMEZONE_DEFAULT="UTC"
FIRST_USER_NAME="microscopi"
FIRST_USER_PASS="password"
DISABLE_FIRST_BOOT_USER_RENAME="1"
ENABLE_SSH="1"
STAGE_LIST="stage0 stage1 stage2 stage3-microscopi stage3-debug"
EOF

echo "==> Copying stage3-microscopi..."
rm -rf "$PIGEN_DIR/stage3-microscopi"
cp -r "$REPO_ROOT/config/pi-gen/stage3" "$PIGEN_DIR/stage3-microscopi"
# Install the debug binary (with symbols) under files/usr/local/.
STAGE3_FILES="$PIGEN_DIR/stage3-microscopi/01-microscopi/files"
mkdir -p "$STAGE3_FILES"
cp -r "$DEPLOY_DIR/install-debug/usr" "$STAGE3_FILES/"

echo "==> Copying stage3-debug..."
rm -rf "$PIGEN_DIR/stage3-debug"
cp -r "$REPO_ROOT/config/pi-gen/stage3-debug" "$PIGEN_DIR/stage3-debug"

echo "==> Skipping default stages 3–5..."
for s in 3 4 5; do
    touch "$PIGEN_DIR/stage${s}/SKIP" 2>/dev/null || true
    touch "$PIGEN_DIR/stage${s}/SKIP_IMAGES" 2>/dev/null || true
done
# stage2 has EXPORT_IMAGE but we don't want it exported — our stage3-debug has
# its own EXPORT_IMAGE and produces the correct rootfs for the final image.
touch "$PIGEN_DIR/stage2/SKIP_IMAGES"

echo "==> Running pi-gen build (Docker mode — no root required)..."
# pi-gen's build-docker.sh breaks on paths containing spaces. Use a symlink.
PIGEN_LINK="/tmp/microscopi-pi-gen-$$"
ln -sfn "$PIGEN_DIR" "$PIGEN_LINK"
cd "$PIGEN_LINK"

if [[ "$CLEAN" == "1" ]]; then
    echo "==> Full clean rebuild (--clean passed)."
    CLEAN=1 ./build-docker.sh
else
    echo "==> Incremental build (stage0-2 skipped if already built). Pass --clean to rebuild all."
    CONTINUE=1 ./build-docker.sh
fi

cd "$REPO_ROOT"
rm -f "$PIGEN_LINK"

echo "==> Debug image ready in $PIGEN_DIR/deploy/"
ls -lh "$PIGEN_DIR/deploy/"*debug* 2>/dev/null || ls -lh "$PIGEN_DIR/deploy/"
