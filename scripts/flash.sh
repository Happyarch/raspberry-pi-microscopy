#!/usr/bin/env bash
# Flash the built image to an SD card.
# Usage: ./scripts/flash.sh /dev/sdX
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIGEN_DEPLOY="$REPO_ROOT/deploy/pi-gen/deploy"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <device>  (e.g. /dev/sdb)"
    exit 1
fi

DEVICE="$1"

IMG=$(ls "$PIGEN_DEPLOY"/*.img.xz 2>/dev/null | sort -r | head -1)
if [[ -z "$IMG" ]]; then
    echo "ERROR: No .img.xz found in $PIGEN_DEPLOY. Run build-image.sh first."
    exit 1
fi

echo "==> Image: $IMG"
echo "==> Target device: $DEVICE"
echo ""
echo "WARNING: This will ERASE all data on $DEVICE."
read -r -p "Type 'yes' to continue: " confirm
if [[ "$confirm" != "yes" ]]; then
    echo "Aborted."
    exit 0
fi

echo "==> Flashing (this may take several minutes)..."
xzcat "$IMG" | sudo dd of="$DEVICE" bs=4M status=progress conv=fsync
sudo sync

echo "==> Done. Remove the SD card and insert into the Raspberry Pi 3."
