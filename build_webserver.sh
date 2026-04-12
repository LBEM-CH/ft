#!/usr/bin/env bash
# Package the built WASM server into a tarball and optionally deploy it to a
# remote Ubuntu host running Caddy (for public HTTPS).
#
# Usage:
#   ./build_webserver.sh                              # just create ft-wasm.tar.gz
#   ./build_webserver.sh user@host                    # deploy to /srv/ft and reload caddy
#   ./build_webserver.sh user@host /custom/path       # deploy to a custom path
#
# The remote path defaults to /srv/ft (matches Caddyfile in this repo).
# The script uses sudo on the remote to write into /srv and to reload caddy —
# the remote user must have passwordless sudo or be prepared to type a password.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
TARBALL="ft-wasm.tar.gz"
REMOTE="${1:-}"
REMOTE_DIR="${2:-/srv/ft}"

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
tar czhf "$TARBALL" "${ARTIFACTS[@]}"
echo "Created $BUILD_DIR/$TARBALL ($(du -h "$TARBALL" | cut -f1))"

if [[ -z "$REMOTE" ]]; then
    cat <<EOF

To deploy manually, copy $TARBALL to the target and run:

    sudo mkdir -p /srv/ft && sudo chown \$USER /srv/ft
    tar xzf $TARBALL -C /srv/ft
    sudo systemctl reload caddy    # if Caddy is already configured

Caddy setup (one-time): see Caddyfile in this repo and the public-HTTPS notes.
EOF
    exit 0
fi

echo "Deploying to $REMOTE:$REMOTE_DIR …"

# Stage the tarball in the remote user's home, then sudo-extract into $REMOTE_DIR.
scp "$TARBALL" "$REMOTE:~/$TARBALL"

ssh -tt "$REMOTE" bash -s <<EOF
set -euo pipefail
sudo mkdir -p "$REMOTE_DIR"
# Wipe only the app artifacts, not the whole directory, in case it holds other files.
sudo rm -rf "$REMOTE_DIR"/{ft.html,ft.js,ft.wasm,qtloader.js,qtlogo.svg,images}
sudo tar xzf ~/$TARBALL -C "$REMOTE_DIR"
sudo chown -R root:root "$REMOTE_DIR"
rm ~/$TARBALL
if systemctl list-unit-files | grep -q '^apache2\.service'; then
    sudo systemctl reload apache2
    echo "Apache reloaded."
else
    echo "note: apache2 is not installed on the remote — install it and copy ft-apache.conf to /etc/apache2/sites-available/ft.conf"
fi
EOF

echo "Deployed to $REMOTE:$REMOTE_DIR"
