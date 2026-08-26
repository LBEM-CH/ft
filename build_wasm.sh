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
#      (select "WebAssembly (multi-threaded)" under your Qt version — the
#      multithreaded FFT requires this kit, NOT "wasm_singlethread")
#
#   3. Set these environment variables (or edit paths below):
#        QT_VERSION    – Qt version to use for default paths, e.g. 6.8.3
#        QT_WASM_PATH  – path to the Qt WASM kit, e.g. ~/Qt/$QT_VERSION/wasm_multithread
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
QT_WASM_PATH="${QT_WASM_PATH:-$HOME/Qt/$QT_VERSION/wasm_multithread}"
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

# Create build directory (wipe any previous build to avoid stale EM_ASM /
# JS-glue mismatches between ft.wasm and ft.js)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
rm -rf "$BUILD_DIR"
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
# App icon: used as favicon, home-screen icon and loading-screen logo instead
# of Qt's qtlogo.svg.
cp "$SCRIPT_DIR/icon-ft.png" .
# The qtstatus stage below also plants a plain <a> to the manual on the loading
# screen. The manual lives on its own path (/ft-manual/), and a page no crawled
# HTML links to is an orphan search engines index poorly even when sitemapped —
# this anchor is the app page's link to it. It sits inside the spinner figure,
# so it shows while the app loads (and to no-JS visitors) and vanishes when the
# app takes over. The href is relative so it resolves at /ft/ in production and
# at the served build-dir root locally (the ft-manual symlink below).
BUILD_STAMP="$(date +%s)"
sed -e 's/@APPNAME@/ft/g' \
    -e 's/@APPEXPORTNAME@/createQtAppInstance/g' \
    -e 's/@PRELOAD@//g' \
    "$QT_WASM_PATH/plugins/platforms/wasm_shell.html" \
  | sed -e "s|ft\\.js|ft.js?v=${BUILD_STAMP}|g" \
  | sed -e 's|</title>|</title>\
    <meta name="apple-mobile-web-app-capable" content="yes">\
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">\
    <meta name="apple-mobile-web-app-title" content="ft">\
    <link rel="icon" type="image/png" href="icon-ft.png">\
    <link rel="apple-touch-icon" href="icon-ft.png">\
    <script>window.__FT_BUILD_STAMP="'"${BUILD_STAMP}"'";</script>|' \
  | sed -e 's|<img src="qtlogo.svg" width="320" height="200"|<img src="icon-ft.png" width="100" height="100"|' \
  | sed -e 's|<div id="qtstatus"></div>|<div id="qtstatus"></div>\
        <p><a href="../ft-manual/manual.html">Fourier Analyzer user manual</a></p>|' \
  | sed -e 's|await qtLoad({|await qtLoad({ locateFile: (p) => p.endsWith(".wasm") ? p + "?v=" + window.__FT_BUILD_STAMP : p,|' \
  > ft.html
if [ -d "$SCRIPT_DIR/EXAMPLE_IMAGES" ]; then
    if [ -e images ] || [ -L images ]; then
        rm -rf images
    fi
    ln -sfn "$SCRIPT_DIR/EXAMPLE_IMAGES" images
fi

# The manual is no longer part of the app payload — it is deployed on its own
# path (/ft-manual/, see ft-manual-apache.conf) and packed by build_webserver.sh
# straight out of the source tree. It is symlinked in here all the same, under
# the name it has in production, so that a locally served build reaches its
# pages at the same relative path the deployed app uses.
if [ -d "$SCRIPT_DIR/ft-manual" ]; then
    ln -sfn "$SCRIPT_DIR/ft-manual" ft-manual
fi

echo ""
echo "=== Build complete ==="
echo "Output files in: $BUILD_DIR"
echo ""
echo "To serve locally (multithreading needs the COOP/COEP headers, which the"
echo "stock 'python3 -m http.server' does NOT send — use the bundled server):"
echo "  cd $BUILD_DIR && python3 $SCRIPT_DIR/serve_wasm.py 8080"
echo ""
echo "Then open http://localhost:8080/ft.html in your browser. Confirm threads"
echo "are active in the JS console: typeof SharedArrayBuffer !== 'undefined'."
echo ""
echo "For production deployment, serve with these HTTP headers (required for"
echo "SharedArrayBuffer / pthreads):"
echo "  Cross-Origin-Opener-Policy: same-origin"
echo "  Cross-Origin-Embedder-Policy: require-corp"
