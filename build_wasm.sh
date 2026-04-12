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
#        QT_VERSION    – Qt version to use for default paths, e.g. 6.8.3
#        QT_WASM_PATH  – path to the Qt WASM kit, e.g. ~/Qt/$QT_VERSION/wasm_singlethread
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
QT_VERSION="${QT_VERSION:-6.8.3}"
QT_WASM_PATH="${QT_WASM_PATH:-$HOME/Qt/$QT_VERSION/wasm_singlethread}"
EMSDK="${EMSDK:-$HOME/Projects/emsdk}"

# Host Qt kit (needed for cross-compilation). Pick per OS.
if [ -z "$QT_HOST_PATH" ]; then
    if [ -d "$HOME/Qt/$QT_VERSION/gcc_64" ]; then
        QT_HOST_PATH="$HOME/Qt/$QT_VERSION/gcc_64"
    elif [ -d "$HOME/Qt/$QT_VERSION/macos" ]; then
        QT_HOST_PATH="$HOME/Qt/$QT_VERSION/macos"
    fi
fi

if [ ! -d "$QT_WASM_PATH" ]; then
    echo "ERROR: Qt WASM kit not found at $QT_WASM_PATH"
    echo "Set QT_WASM_PATH to your Qt WebAssembly installation."
    exit 1
fi

if [ ! -d "$QT_HOST_PATH" ]; then
    echo "ERROR: Qt host kit not found at $QT_HOST_PATH"
    echo "Set QT_HOST_PATH to your Qt host (gcc_64 / macos) installation."
    exit 1
fi

if [ ! -f "$EMSDK/emsdk_env.sh" ]; then
    echo "ERROR: Emscripten SDK not found at $EMSDK"
    echo "Set EMSDK to your Emscripten SDK directory."
    exit 1
fi

# Ensure qt-cmake is executable (aqtinstall does not set perms)
chmod +x "$QT_WASM_PATH/bin/"* 2>/dev/null || true
chmod +x "$QT_HOST_PATH/bin/"* 2>/dev/null || true

# Activate Emscripten environment
source "$EMSDK/emsdk_env.sh"

# Create build directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "=== Configuring Qt WASM build ==="
"$QT_WASM_PATH/bin/qt-cmake" "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$QT_HOST_PATH" \
    -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST_PATH/lib/cmake"

echo "=== Building ==="
cmake --build . --parallel

echo "=== Generating ft.html and copying loader assets ==="
cp "$QT_WASM_PATH/plugins/platforms/qtloader.js" .
cp "$QT_WASM_PATH/plugins/platforms/qtlogo.svg" . 2>/dev/null || true
sed -e 's/@APPNAME@/ft/g' \
    -e 's/@APPEXPORTNAME@/createQtAppInstance/g' \
    -e 's/@PRELOAD@//g' \
    "$QT_WASM_PATH/plugins/platforms/wasm_shell.html" > ft.html
if [ -d "$SCRIPT_DIR/EXAMPLE_IMAGES" ]; then
    if [ -e images ] || [ -L images ]; then
        rm -rf images
    fi
    ln -sfn "$SCRIPT_DIR/EXAMPLE_IMAGES" images
fi

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
