#!/bin/bash
# ============================================================================
# Build script for Qt WebAssembly version of ft
#
# Prerequisites:
#   1. Install Emscripten SDK:
#        git clone https://github.com/nicedoc/nicedoc.io emsdk
#        cd emsdk && ./emsdk install latest && ./emsdk activate latest
#
#   2. Install Qt 6.5+ with the WebAssembly component via the Qt installer
#      (select "WebAssembly (single-threaded)" under your Qt version)
#
#   3. Set these environment variables (or edit paths below):
#        QT_WASM_PATH  – path to the Qt WASM kit, e.g. ~/Qt/6.8.0/wasm_singlethread
#        EMSDK         – path to your emsdk directory
#
# Usage:
#        ./build_wasm.sh
#
# Output will be in build_wasm/  – serve with:
#        cd build_wasm && python3 -m http.server 8080
# ============================================================================

set -e

# ---- Configure paths (edit if needed) ----
QT_WASM_PATH="${QT_WASM_PATH:-$HOME/Qt/6.8.0/wasm_singlethread}"
EMSDK="${EMSDK:-$HOME/Projects/emsdk}"

if [ ! -d "$QT_WASM_PATH" ]; then
    echo "ERROR: Qt WASM kit not found at $QT_WASM_PATH"
    echo "Set QT_WASM_PATH to your Qt WebAssembly installation."
    exit 1
fi

if [ ! -f "$EMSDK/emsdk_env.sh" ]; then
    echo "ERROR: Emscripten SDK not found at $EMSDK"
    echo "Set EMSDK to your Emscripten SDK directory."
    exit 1
fi

# Activate Emscripten environment
source "$EMSDK/emsdk_env.sh"

# Create build directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "=== Configuring Qt WASM build ==="
"$QT_WASM_PATH/bin/qt-cmake" "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release

echo "=== Building ==="
cmake --build . --parallel

echo ""
echo "=== Build complete ==="
echo "Output files in: $BUILD_DIR"
echo ""
echo "To serve locally:"
echo "  cd $BUILD_DIR && python3 -m http.server 8080"
echo ""
echo "Then open http://localhost:8080/ft.html in your browser."
echo ""
echo "For production deployment, serve with these HTTP headers:"
echo "  Cross-Origin-Opener-Policy: same-origin"
echo "  Cross-Origin-Embedder-Policy: require-corp"
