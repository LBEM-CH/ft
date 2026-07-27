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
ARTIFACTS=(ft.html ft.js ft.wasm ft.worker.js qtloader.js icon-ft.png images
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

# The manual-webserver upload is specific to this project's maintainers. Only
# attempt it for their accounts; skip it entirely for everyone else.
current_user=$(whoami)
if [ "$current_user" = "henning" ] || [ "$current_user" = "stahlber" ]; then
    if [ -e "/Users/stahlber/.pw" ]; then
        pw=$(cat /Users/stahlber/.pw)
        if command -v sshpass >/dev/null 2>&1; then
            echo "Copying manual files to webserver."
            sshpass -p "$pw" scp -o StrictHostKeyChecking=accept-new $BUILD_DIR/$TARBALL henning@lbem-status:/home/henning
        else
            echo "sshpass not found (install it with 'brew install sshpass'); falling back to an interactive copy."
            scp "$BUILD_DIR/$TARBALL" henning@lbem-status:/home/henning
        fi
    else
        echo "Password file /Users/stahlber/.pw not found. Please copy the manual files manually."
        echo "To update the manual on the web, run this locally:"
        echo " "
        echo "scp $BUILD_DIR/$TARBALL henning@lbem-status:/home/henning"
        echo " "
    fi
    echo "Run on the webserver: \"doit\""
    # 
else
    cat <<EOF
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

fi

exit 0

