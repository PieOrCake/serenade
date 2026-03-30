#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="serenade-appimage-builder"

echo "=== Building container image (Ubuntu 22.04) ==="
podman build -t "$IMAGE_NAME" "$SCRIPT_DIR"

echo "=== Running AppImage build inside container ==="
podman run --rm \
    -v "$SCRIPT_DIR:/src:ro" \
    -v "$SCRIPT_DIR:/output:rw" \
    "$IMAGE_NAME"

echo ""
echo "=== AppImage ready: $SCRIPT_DIR/Serenade_Music_Converter-x86_64.AppImage ==="
ls -lh "$SCRIPT_DIR/Serenade_Music_Converter-x86_64.AppImage"
