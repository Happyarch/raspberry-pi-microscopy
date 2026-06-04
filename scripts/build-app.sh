#!/usr/bin/env bash
# Build the microscopy binary inside a QEMU-emulated ARM64 Docker container.
# Prerequisites on Arch Linux:
#   sudo pacman -S qemu-user-static binfmt-qemu-static docker
#   sudo systemctl restart systemd-binfmt
#   sudo systemctl start docker
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="microscopy-builder:latest"
DEPLOY_DIR="$REPO_ROOT/deploy"

echo "==> Building Docker image ($IMAGE_TAG)..."
docker buildx build \
    --platform linux/arm64 \
    --tag "$IMAGE_TAG" \
    --file "$REPO_ROOT/docker/Dockerfile.build" \
    --load \
    "$REPO_ROOT"

echo "==> Extracting build artifacts..."
mkdir -p "$DEPLOY_DIR"
CONTAINER=$(docker create --platform linux/arm64 "$IMAGE_TAG")
docker cp "$CONTAINER:/install/." "$DEPLOY_DIR/install/"
docker rm "$CONTAINER"

echo "==> Done. Artifacts in $DEPLOY_DIR/install/"
echo "    Binary: $DEPLOY_DIR/install/usr/local/bin/microscopy"
