#!/usr/bin/env bash
# Build the full Raspberry Pi OS image using pi-gen.
# Run build-app.sh first to produce deploy/install/.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/deploy"
PIGEN_DIR="$DEPLOY_DIR/pi-gen"
STAGE3_SRC="$REPO_ROOT/config/pi-gen/stage3"

if [[ ! -f "$DEPLOY_DIR/install/usr/local/bin/microscopy" ]]; then
    echo "ERROR: Run scripts/build-app.sh first (binary not found at deploy/install/usr/local/bin/microscopy)."
    exit 1
fi

echo "==> Cloning pi-gen..."
if [[ ! -d "$PIGEN_DIR" ]]; then
    git clone --depth=1 https://github.com/RPi-Distro/pi-gen.git "$PIGEN_DIR"
fi

echo "==> Configuring pi-gen..."
cp "$REPO_ROOT/config/pi-gen/config" "$PIGEN_DIR/config"

# Skip stages 3–5 (we provide our own stage3).
for s in 3 4 5; do
    touch "$PIGEN_DIR/stage${s}/SKIP" 2>/dev/null || true
    touch "$PIGEN_DIR/stage${s}/SKIP_IMAGES" 2>/dev/null || true
done
rm -f "$PIGEN_DIR/stage2/SKIP_IMAGES"  # We want the stage2 base image too for testing.

echo "==> Installing custom stage3..."
rm -rf "$PIGEN_DIR/stage3-microscopy"
cp -r "$STAGE3_SRC" "$PIGEN_DIR/stage3-microscopy"

# Copy compiled artifacts into stage3 files directory.
STAGE3_FILES="$PIGEN_DIR/stage3-microscopy/files"
mkdir -p "$STAGE3_FILES"
cp -r "$DEPLOY_DIR/install/usr/local" "$STAGE3_FILES/"

echo "==> Running pi-gen build (Docker mode — no root required; takes 20–40 minutes)..."
PIGEN_LINK="/tmp/microscopy-pi-gen-$$"
ln -sfn "$PIGEN_DIR" "$PIGEN_LINK"
cd "$PIGEN_LINK"
CLEAN=1 ./build-docker.sh
cd "$REPO_ROOT"
rm -f "$PIGEN_LINK"

echo "==> Image ready in $PIGEN_DIR/deploy/"
ls -lh "$PIGEN_DIR/deploy/"
