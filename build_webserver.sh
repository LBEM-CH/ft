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
#   ./build_webserver.sh                              # create ft-wasm.tar.gz + ft-examples.tar.gz
#   ./build_webserver.sh user@host                    # deploy to /srv/ft and reload apache2
#   ./build_webserver.sh user@host /custom/path       # deploy to a custom path
#
# The payload is split into two archives — the app (ft-wasm.tar.gz) and the
# example images (ft-examples.tar.gz) — and uploaded with rsync, which skips
# whichever archive has not changed since the last deploy. The examples archive
# is only re-packed when an image under EXAMPLE_IMAGES actually changed.
#
# The remote path defaults to /srv/ft (matches the Alias in ft-apache.conf).
# The script uses sudo on the remote to write into /srv and to reload apache2 —
# the remote user must have sudo rights (password will be prompted).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
# The payload is split into two archives so the large, rarely-changing example
# images are not re-packed and re-uploaded on every app rebuild:
#   - APP_TARBALL      the WASM app + manual pages, rebuilt every run
#   - EXAMPLES_TARBALL the "images" tree, rebuilt only when an image changed
# rsync (below) then skips whichever archive is byte-for-byte unchanged.
#
# The archives live in DIST_DIR, NOT in build_wasm: build_wasm.sh wipes its
# whole build directory each run, so a tarball kept there could never be reused
# across builds. DIST_DIR persists, so the examples archive survives and rsync
# can recognise it as unchanged.
DIST_DIR="$SCRIPT_DIR/dist"
mkdir -p "$DIST_DIR"
APP_TARBALL="$DIST_DIR/ft-wasm.tar.gz"
EXAMPLES_TARBALL="$DIST_DIR/ft-examples.tar.gz"
REMOTE="${1:-}"
REMOTE_DIR="${2:-/srv/ft}"

# ft.worker.js is the pthread worker bootstrap emitted by the multithreaded
# (-pthread) build; it is required at runtime or the worker pool fails to load.
# "images" is packed separately (EXAMPLES_TARBALL), not with the app.
APP_ARTIFACTS=(ft.html ft.js ft.wasm ft.worker.js qtloader.js icon-ft.png
               manual.html manual_panel1.html manual_panel2.html manual_exercises.html)
IMAGES_DIR="images"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: $BUILD_DIR not found — run the WASM build first (see WASM_SERVER.txt)" >&2
    exit 1
fi

cd "$BUILD_DIR"
for f in "${APP_ARTIFACTS[@]}" "$IMAGES_DIR"; do
    if [[ ! -e "$f" ]]; then
        echo "error: missing build artifact: $BUILD_DIR/$f" >&2
        exit 1
    fi
done

# macOS ships bsdtar, which by default records Apple metadata — extended
# attributes (com.apple.provenance, com.apple.FinderInfo, …), BSD file flags and
# ACLs — as pax extended headers. GNU tar on the Linux target does not know
# those keywords and prints a screenful of "Ignoring unknown extended header
# keyword" warnings on extraction. Nothing is lost either way, but the warnings
# bury any real error, so the metadata is left out of the archive. These flags
# are bsdtar-specific; GNU tar (should this ever be packed on Linux) does not
# record the attributes in the first place, so they are only passed when the
# running tar advertises them. "-T /dev/null" makes each probe an empty archive,
# so it tests only whether the flag is accepted and touches no files.
TAR_OPTS=()
for opt in --no-xattrs --no-fflags --no-acls --no-mac-metadata; do
    if tar czf /dev/null "$opt" -T /dev/null >/dev/null 2>&1; then
        TAR_OPTS+=("$opt")
    fi
done

# ---- App archive: always rebuilt (the app changes every build) ----
# The ${...+...} guard keeps an empty TAR_OPTS from tripping `set -u` on the
# bash 3.2 that ships with macOS.
echo "Packing $APP_TARBALL (dereferencing symlinks)…"
tar czhf "$APP_TARBALL" ${TAR_OPTS[@]+"${TAR_OPTS[@]}"} "${APP_ARTIFACTS[@]}"
echo "Created $APP_TARBALL ($(du -h "$APP_TARBALL" | cut -f1))"

# ---- Examples archive: rebuilt only when an image actually changed ----
# "images" is a symlink into EXAMPLE_IMAGES, so -L follows it to test the real
# files. Skipping the repack keeps the archive's bytes (and mtime) stable, which
# is what lets rsync recognise it as unchanged and transfer nothing.
repack_examples=0
if [[ ! -e "$EXAMPLES_TARBALL" ]]; then
    repack_examples=1
elif [[ -n "$(find -L "$IMAGES_DIR" -type f -newer "$EXAMPLES_TARBALL" -print -quit 2>/dev/null)" ]]; then
    repack_examples=1
fi
if [[ "$repack_examples" -eq 1 ]]; then
    echo "Packing $EXAMPLES_TARBALL (dereferencing symlinks)…"
    tar czhf "$EXAMPLES_TARBALL" ${TAR_OPTS[@]+"${TAR_OPTS[@]}"} "$IMAGES_DIR"
    echo "Created $EXAMPLES_TARBALL ($(du -h "$EXAMPLES_TARBALL" | cut -f1))"
else
    echo "Reusing $EXAMPLES_TARBALL (no image changed)"
fi

# The manual-webserver upload is specific to this project's maintainers. Only
# attempt it for their accounts; skip it entirely for everyone else.
current_user=$(whoami)
if [ "$current_user" = "henning" ] || [ "$current_user" = "stahlber" ]; then
    # rsync (over ssh) instead of scp: it compares each archive against the copy
    # already on the server and transfers nothing when they match, so the large
    # examples archive is uploaded only on the builds that actually changed it.
    RSYNC_SSH="ssh -o StrictHostKeyChecking=accept-new"
    REMOTE_TARGET="henning@lbem-status:/home/henning/"
    if [ -e "/Users/stahlber/.pw" ]; then
        pw=$(cat /Users/stahlber/.pw)
        if command -v sshpass >/dev/null 2>&1; then
            echo "Syncing app and example archives to webserver."
            # sshpass must be the DIRECT parent of ssh, so it is placed inside
            # rsync's transport command (-e) rather than wrapped around rsync.
            # Wrapping rsync leaves ssh a grandchild with piped stdin, which makes
            # it fall back to an X11 askpass helper (/usr/X11R6/bin/ssh-askpass)
            # that does not exist on this Mac — so the password is never supplied
            # and the connection is refused. The password travels via the SSHPASS
            # env var (sshpass -e) so it never appears in the process list.
            export SSHPASS="$pw"
            rsync -a -e "sshpass -e $RSYNC_SSH" \
                "$APP_TARBALL" "$EXAMPLES_TARBALL" "$REMOTE_TARGET"
            rsync -a -e "sshpass -e $RSYNC_SSH" "$SCRIPT_DIR/doit" "$REMOTE_TARGET"
            unset SSHPASS
        else
            echo "sshpass not found (install it with 'brew install sshpass'); falling back to an interactive rsync."
            rsync -a -e "$RSYNC_SSH" \
                "$APP_TARBALL" "$EXAMPLES_TARBALL" "$REMOTE_TARGET"
            rsync -a -e "$RSYNC_SSH" "$SCRIPT_DIR/doit" "$REMOTE_TARGET"
        fi
    else
        echo "Password file /Users/stahlber/.pw not found. Please copy the archives manually."
        echo "To update the web deployment, run this locally:"
        echo " "
        echo "rsync -a \"$APP_TARBALL\" \"$EXAMPLES_TARBALL\" \"$REMOTE_TARGET\""
        echo "rsync -a -e \"$RSYNC_SSH\" \"$SCRIPT_DIR/doit\" \"$REMOTE_TARGET\""
            echo " "
    fi
    
    echo "==============================================================="
    echo "Run on the webserver:    doit"
    echo "==============================================================="
    echo " "
    #
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

