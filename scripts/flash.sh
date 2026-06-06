#!/usr/bin/env bash
# Flash the built Pi image to an SD card.
# Usage: ./scripts/flash.sh --device /dev/sdX
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIGEN_DEPLOY="$REPO_ROOT/deploy/pi-gen/deploy"

DEVICE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --device|-d) DEVICE="$2"; shift 2 ;;
        *) echo "Unknown argument: $1"; echo "Usage: $0 --device /dev/sdX"; exit 1 ;;
    esac
done

if [[ -z "$DEVICE" ]]; then
    echo "Usage: $0 --device /dev/sdX"
    echo ""
    echo "Available block devices:"
    lsblk -d -o NAME,SIZE,MODEL | grep -v loop
    exit 1
fi

# Pick the newest image regardless of compression format.
IMG=$(ls -t "$PIGEN_DEPLOY"/*.img.gz "$PIGEN_DEPLOY"/*.img.xz 2>/dev/null | head -1)
if [[ -z "$IMG" ]]; then
    echo "ERROR: No image found in $PIGEN_DEPLOY. Run build-image.sh first."
    exit 1
fi

case "$IMG" in
    *.img.gz)  DECOMPRESS="zcat" ;;
    *.img.xz)  DECOMPRESS="xzcat" ;;
esac

echo "==> Image:  $IMG"
echo "==> Device: $DEVICE"
echo ""
echo "WARNING: This will ERASE all data on $DEVICE."
read -r -p "Type 'yes' to continue: " confirm
if [[ "$confirm" != "yes" ]]; then
    echo "Aborted."
    exit 0
fi

echo "==> Flashing (this may take several minutes)..."
$DECOMPRESS "$IMG" | sudo dd of="$DEVICE" bs=4M status=progress conv=fsync
sudo sync

echo "==> Done. Remove the SD card and insert into the Raspberry Pi 3."
