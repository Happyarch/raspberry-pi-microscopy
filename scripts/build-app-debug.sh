#!/usr/bin/env bash
# Build a debug binary (symbols, no optimisation) for remote GDB over the network.
# The binary is NOT stripped. Produces deploy/install-debug/usr/local/.
#
# On the Pi, run:  microscopi-gdbserver [port]
# On your host:    gdb deploy/install-debug/usr/local/bin/microscopi
#                  (gdb) target remote microscopi-debug.local:2345
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="microscopi-builder-debug:latest"
DEPLOY_DIR="$REPO_ROOT/deploy"

echo "==> Building debug Docker image ($IMAGE_TAG)..."
JOBS=$(( $(nproc) > 16 ? $(nproc) - 16 : $(nproc) ))
echo "==> Using ${JOBS} parallel jobs"

docker buildx build \
    --platform linux/arm64 \
    --tag "$IMAGE_TAG" \
    --file "$REPO_ROOT/docker/Dockerfile.build" \
    --build-arg CMAKE_BUILD_TYPE=Debug \
    --build-arg JOBS="${JOBS}" \
    --load \
    "$REPO_ROOT"

echo "==> Extracting debug artifacts..."
rm -rf "$DEPLOY_DIR/install-debug"
mkdir -p "$DEPLOY_DIR/install-debug/usr/local"
CONTAINER=$(docker create --platform linux/arm64 "$IMAGE_TAG")
docker cp "$CONTAINER:/usr/local/." "$DEPLOY_DIR/install-debug/usr/local/"
docker rm "$CONTAINER"

echo "==> Done. Debug binary: $DEPLOY_DIR/install-debug/usr/local/bin/microscopi"
echo "    (contains full debug symbols; do NOT flash this as a release image)"
