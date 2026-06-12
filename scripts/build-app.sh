#!/usr/bin/env bash
# Build the microscopi binary inside a QEMU-emulated ARM64 Docker container.
# Prerequisites on Arch Linux:
#   sudo pacman -S qemu-user-static binfmt-qemu-static docker
#   sudo systemctl restart systemd-binfmt
#   sudo systemctl start docker
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="microscopi-builder:latest"
DEPLOY_DIR="$REPO_ROOT/deploy"

echo "==> Building Docker image ($IMAGE_TAG)..."
JOBS=$(( $(nproc) > 16 ? $(nproc) - 16 : $(nproc) ))
echo "==> Using ${JOBS} parallel jobs"

docker buildx build \
    --platform linux/arm64 \
    --tag "$IMAGE_TAG" \
    --file "$REPO_ROOT/docker/Dockerfile.build" \
    --build-arg JOBS="${JOBS}" \
    --provenance=false \
    --load \
    "$REPO_ROOT"

echo "==> Extracting build artifacts..."
CONTAINER=$(docker create --platform linux/arm64 "$IMAGE_TAG")

# Raw install tree — consumed by build-image.sh when building the full Pi OS image.
rm -rf "$DEPLOY_DIR/install"
mkdir -p "$DEPLOY_DIR/install/usr/local"
docker cp "$CONTAINER:/install/usr/local/." "$DEPLOY_DIR/install/usr/local/"

# .deb package — install directly on a Pi running Pi OS Bookworm.
docker cp "$CONTAINER:/output/." "$DEPLOY_DIR/"

docker rm "$CONTAINER"

DEB=$(ls "$DEPLOY_DIR"/microscopi_*_arm64.deb 2>/dev/null | tail -1)
echo "==> Done."
echo "    Install tree: $DEPLOY_DIR/install/usr/local/"
echo "    .deb package: $DEB"
echo ""
echo "    Install on Pi:"
echo "      scp \"$DEB\" microscopi@192.168.1.220:~/"
echo "      ssh microscopi@192.168.1.220 \"sudo dpkg -i ~/$(basename "$DEB")\""
