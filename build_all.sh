#!/bin/bash
# Build both the native desktop and WebAssembly versions of ft
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ---- Native desktop build ----
echo "=== Building native desktop version ==="
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
echo "=== Native build complete === (output: $BUILD_DIR/ft)"
echo ""

# ---- WebAssembly build ----
echo "=== Building WebAssembly version ==="
cd "$SCRIPT_DIR"
./build_wasm.sh
echo ""

echo "=== All builds complete ==="
