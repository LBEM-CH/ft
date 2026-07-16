#!/usr/bin/env bash
# Package the built WASM server into a tarball and optionally deploy it to a
# remote Ubuntu host running Apache (for public HTTPS).
#
# The target host is expected to have Apache configured with:
#   - An existing :443 SSL vhost (e.g. /etc/apache2/sites-enabled/default-ssl.conf)
#     whose DocumentRoot points at the site's main app (e.g. /var/www/html).
#   - The snippet ft-apache.conf from this repo installed at
#     /etc/apache2/conf-available/ft-wasm.conf and Include'd from that vhost.
#     The snippet adds `Alias /ft /srv/ft`, so the WASM app is served at
#     https://<host>/ft/ without disturbing the main site.
# See ft-apache.conf for the one-time Apache setup.
#
# Usage:
#   ./build_webserver.sh                              # just create ft-wasm.tar.gz
#   ./build_webserver.sh user@host                    # deploy to /srv/ft and reload apache2
#   ./build_webserver.sh user@host /custom/path       # deploy to a custom path
#
# The remote path defaults to /srv/ft (matches the Alias in ft-apache.conf).
# The script uses sudo on the remote to write into /srv and to reload apache2 —
# the remote user must have sudo rights (password will be prompted).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
TARBALL="ft-wasm.tar.gz"
REMOTE="${1:-}"
REMOTE_DIR="${2:-/srv/ft}"

# ft.worker.js is the pthread worker bootstrap emitted by the multithreaded
# (-pthread) build; it is required at runtime or the worker pool fails to load.
ARTIFACTS=(ft.html ft.js ft.wasm ft.worker.js qtloader.js qtlogo.svg images
           manual.html manual_panel1.html manual_panel2.html manual_exercises.html)

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

To deploy manually, copy $TARBALL to the target:

    scp build_wasm/ft-wasm.tar.gz henning@lbem-status:/home/henning

Then run on target host:

    sudo \rm -rf /srv/ft2 
    sudo mkdir -p /srv/ft2
    sudo tar xzf $TARBALL -C /srv/ft2
    sudo systemctl reload apache2


    sudo \rm -rf /srv/ft
    sudo mkdir -p /srv/ft
    sudo tar xzf $TARBALL -C /srv/ft
    sudo systemctl reload apache2

One-time Apache setup: see ft-apache.conf for the snippet and instructions.

To evaluate website usage, copy ft-report.sh to the target. Run on the local machine:

    scp ft-report.sh henning@lbem-status:/home/henning

Then run on the target:

    sudo /home/henning/ft-report.sh

It writes a GoAccess HTML report to /home/henning/ft-report.html and prints the
top countries and cities accessing /ft/ to the terminal.
(Requires on the target: goaccess, mmdb-bin, and GeoLite2 DBs in /var/lib/GeoIP.)

From your local machine, fetch the HTML report:
    scp henning@lbem-status.epfl.ch:~/ft-report.html .
    open ft-report.html

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
sudo rm -rf "$REMOTE_DIR"/{ft.html,ft.js,ft.wasm,ft.worker.js,qtloader.js,qtlogo.svg,images,manual.html,manual_panel1.html,manual_panel2.html,manual_exercises.html}
sudo tar xzf ~/$TARBALL -C "$REMOTE_DIR"
sudo chown -R root:root "$REMOTE_DIR"
rm ~/$TARBALL
if systemctl list-unit-files | grep -q '^apache2\.service'; then
    sudo systemctl reload apache2
    echo "Apache reloaded."
else
    echo "note: apache2 is not installed on the remote — install it and Include ft-apache.conf from the site's SSL vhost"
fi
EOF

# Extract hostname from user@host for the URL hint. The public path is the
# basename of the deploy dir (matches the Alias: /srv/ft -> /ft, /srv/ft2 -> /ft2).
REMOTE_HOST="${REMOTE#*@}"
echo "Deployed to $REMOTE:$REMOTE_DIR"
echo "Open: https://$REMOTE_HOST/$(basename "$REMOTE_DIR")/"
