#!/usr/bin/env bash
# Package the built WASM server into a tarball that can be deployed to another
# Ubuntu machine. Optionally scp's the tarball to a remote host and extracts it.
#
# Usage:
#   ./build_webserver.sh                       # just create ft-wasm.tar.gz
#   ./build_webserver.sh user@host             # also scp to host:~/ft-wasm/
#   ./build_webserver.sh user@host /srv/ft     # scp and extract to /srv/ft

set -euo pipefail

BUILD_DIR="$(cd "$(dirname "$0")" && pwd)/build_wasm"
TARBALL="ft-wasm.tar.gz"
REMOTE="${1:-}"
REMOTE_DIR="${2:-~/ft-wasm}"

ARTIFACTS=(ft.html ft.js ft.wasm qtloader.js qtlogo.svg images)

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: $BUILD_DIR not found — run the WASM build first (see WASM_SERVER.txt)" >&2
    exit 1
fi

cd "$BUILD_DIR"
for f in "${ARTIFACTS[@]}"; do
    if [[ ! -e "$f" ]]; then
        echo "error: missing build artifact: $BUILD_DIR/$f" >&2
        exit 1
    fi
done

echo "Packing $TARBALL (dereferencing symlinks)…"
# -h dereferences symlinks so the images/ symlink becomes a real directory.
tar czhf "$TARBALL" "${ARTIFACTS[@]}"
echo "Created $BUILD_DIR/$TARBALL ($(du -h "$TARBALL" | cut -f1))"

if [[ -z "$REMOTE" ]]; then
    cat <<EOF

To deploy manually, copy $TARBALL to the target machine and run:

    mkdir -p ~/ft-wasm && cd ~/ft-wasm
    tar xzf /path/to/$TARBALL
    sudo ufw allow 8080/tcp   # only if ufw is enabled
    python3 -m http.server 8080

Then open http://<target-ip>:8080/ft.html
EOF
    exit 0
fi

echo "Copying to $REMOTE:$REMOTE_DIR/ …"
ssh "$REMOTE" "mkdir -p $REMOTE_DIR"
scp "$TARBALL" "$REMOTE:$REMOTE_DIR/"
ssh "$REMOTE" "cd $REMOTE_DIR && tar xzf $TARBALL && rm $TARBALL"
echo "Deployed to $REMOTE:$REMOTE_DIR"
echo "Start the server on the remote with:"
echo "    ssh $REMOTE 'cd $REMOTE_DIR && python3 -m http.server 8080'"
