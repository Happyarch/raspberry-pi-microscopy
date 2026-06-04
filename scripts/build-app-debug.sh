#!/usr/bin/env bash
# Build a debug binary (symbols, no optimisation) for remote GDB over the network.
# The binary is NOT stripped. Produces deploy/install-debug/.
#
# On the Pi, run:  microscopy-gdbserver [port]
# On your host:    gdb deploy/install-debug/usr/local/bin/microscopy
#                  (gdb) target remote microscopy-debug.local:2345
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="microscopy-builder-debug:latest"
DEPLOY_DIR="$REPO_ROOT/deploy"

echo "==> Building debug Docker image ($IMAGE_TAG)..."
docker buildx build \
    --platform linux/arm64 \
    --tag "$IMAGE_TAG" \
    --file "$REPO_ROOT/docker/Dockerfile.build" \
    --build-arg CMAKE_BUILD_TYPE=Debug \
    --load \
    "$REPO_ROOT"

echo "==> Extracting debug artifacts..."
mkdir -p "$DEPLOY_DIR/install-debug"
CONTAINER=$(docker create --platform linux/arm64 "$IMAGE_TAG")
docker cp "$CONTAINER:/install/." "$DEPLOY_DIR/install-debug/"
docker rm "$CONTAINER"

echo "==> Done. Debug binary: $DEPLOY_DIR/install-debug/usr/local/bin/microscopy"
echo "    (contains full debug symbols; do NOT flash this as a release image)"
