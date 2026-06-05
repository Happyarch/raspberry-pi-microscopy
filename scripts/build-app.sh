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
    --load \
    "$REPO_ROOT"

echo "==> Extracting build artifacts..."
rm -rf "$DEPLOY_DIR/install"
mkdir -p "$DEPLOY_DIR/install/usr/local"
CONTAINER=$(docker create --platform linux/arm64 "$IMAGE_TAG")
docker cp "$CONTAINER:/usr/local/." "$DEPLOY_DIR/install/usr/local/"
docker rm "$CONTAINER"

echo "==> Done. Artifacts in $DEPLOY_DIR/install/usr/local/"
echo "    Binary: $DEPLOY_DIR/install/usr/local/bin/microscopi"
