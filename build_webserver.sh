#!/usr/bin/env bash
# Package the built WASM server into tarballs and deploy them to the ft
# webserver, https://lbem-ft.epfl.ch/ (Ubuntu + Apache on lbem-app.epfl.ch).
#
# On that host one Apache vhost (lbem-ft-vhost.conf in this repo) serves:
#   - the manual pages at the site root, https://lbem-ft.epfl.ch/, from
#     /srv/ft-manual (snippet ft-manual-apache.conf). The app links to them there.
#   - the WASM app at https://lbem-ft.epfl.ch/ft/, from /srv/ft (snippet
#     ft-apache.conf).
# See lbem-ft-vhost.conf for the one-time server setup.
#
# Usage:
#   ./build_webserver.sh               # create the three tarballs and deploy them
#   ./build_webserver.sh user@host     # deploy to another host set up the same way
#
# The payload is split into three archives — the app (ft-wasm.tar.gz), the
# manual pages (ft-manual.tar.gz) and the example images (ft-examples.tar.gz) —
# and uploaded with rsync, which skips whichever archive has not changed since
# the last deploy. The manual and examples archives are only re-packed when one
# of their files actually changed, which is what leaves their bytes identical
# for rsync to recognise.
#
# The archives land in ~/ft-deploy on the host, and doit (uploaded with them) is
# then run there over ssh to unpack them into /srv/ft and /srv/ft-manual.
# Neither step needs sudo: the deploy user owns both directories.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_wasm"
# The payload is split into three archives so that neither the large,
# rarely-changing example images nor the manual are re-packed and re-uploaded on
# every app rebuild:
#   - APP_TARBALL      the WASM app, rebuilt every run
#   - MANUAL_TARBALL   the ft-manual pages, rebuilt only when a page changed
#   - EXAMPLES_TARBALL the "images" tree, rebuilt only when an image changed
# rsync (below) then skips whichever archive is byte-for-byte unchanged.
#
# The archives live in DIST_DIR, NOT in build_wasm: build_wasm.sh wipes its
# whole build directory each run, so a tarball kept there could never be reused
# across builds. DIST_DIR persists, so the manual and examples archives survive
# and rsync can recognise them as unchanged.
DIST_DIR="$SCRIPT_DIR/dist"
mkdir -p "$DIST_DIR"
APP_TARBALL="$DIST_DIR/ft-wasm.tar.gz"
MANUAL_TARBALL="$DIST_DIR/ft-manual.tar.gz"
EXAMPLES_TARBALL="$DIST_DIR/ft-examples.tar.gz"
DEPLOY_HOST="${1:-stahlber@lbem-app.epfl.ch}"
# Relative to the deploy user's home on DEPLOY_HOST.
DEPLOY_DIR="ft-deploy"

# ft.worker.js is the pthread worker bootstrap emitted by the multithreaded
# (-pthread) build; it is required at runtime or the worker pool fails to load.
# "images" is packed separately (EXAMPLES_TARBALL), and so are the manual pages
# (MANUAL_TARBALL) — they are served from the site root, not from inside the app.
APP_ARTIFACTS=(ft.html ft.js ft.wasm ft.worker.js qtloader.js icon-ft.png)
IMAGES_DIR="images"
# Taken from the source tree, not from the build: nothing is generated from them,
# and packing the originals keeps the manual deployable without a WASM build.
MANUAL_DIR="$SCRIPT_DIR/ft-manual"
MANUAL_PAGES=(manual.html manual_panel1.html manual_panel2.html manual_exercises.html)

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "error: $BUILD_DIR not found — run the WASM build first (see WASM_SERVER.txt)" >&2
    exit 1
fi

for f in "${MANUAL_PAGES[@]}"; do
    if [[ ! -e "$MANUAL_DIR/$f" ]]; then
        echo "error: missing manual page: $MANUAL_DIR/$f" >&2
        exit 1
    fi
done

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

# ---- Manual archive: rebuilt only when one of the pages changed ----
# The whole directory is packed rather than the four names checked above, so that
# an asset added to it later (a stylesheet, a figure) reaches the server without
# this script having to be edited. -C makes the entries relative to the directory,
# so doit can unpack it straight into /srv/ft-manual and match the Alias in
# ft-manual-apache.conf. Skipping the repack keeps the archive's bytes stable,
# which is what lets rsync recognise it as unchanged.
repack_manual=0
if [[ ! -e "$MANUAL_TARBALL" ]]; then
    repack_manual=1
elif [[ -n "$(find -L "$MANUAL_DIR" -type f -newer "$MANUAL_TARBALL" -print -quit 2>/dev/null)" ]]; then
    repack_manual=1
fi
if [[ "$repack_manual" -eq 1 ]]; then
    # Note the word between the variable and the ellipsis: bash 3.2 (macOS) reads
    # the multibyte "…" as part of the variable name if it abuts it, and `set -u`
    # then aborts the script on an "unbound variable".
    echo "Packing $MANUAL_TARBALL (manual pages)…"
    tar czhf "$MANUAL_TARBALL" ${TAR_OPTS[@]+"${TAR_OPTS[@]}"} \
        --exclude '.DS_Store' -C "$MANUAL_DIR" .
    echo "Created $MANUAL_TARBALL ($(du -h "$MANUAL_TARBALL" | cut -f1))"
else
    echo "Reusing $MANUAL_TARBALL (no manual page changed)"
fi

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

# The webserver upload is specific to this project's maintainers. Only attempt
# it for their accounts; skip it entirely for everyone else.
current_user=$(whoami)
if [ "$current_user" = "henning" ] || [ "$current_user" = "stahlber" ]; then
    # rsync (over ssh) instead of scp: it compares each archive against the copy
    # already on the server and transfers nothing when they match, so the large
    # examples archive is uploaded only on the builds that actually changed it.
    # That is why the archives go to a staging directory that persists between
    # deploys, rather than straight into /srv.
    #
    # Authentication is by ssh key — set it up once with
    #     ssh-copy-id stahlber@lbem-app.epfl.ch
    # Without a key, ssh asks for the password twice: for the upload and for doit.
    SSH="ssh -o StrictHostKeyChecking=accept-new"
    echo "Syncing the archives to $DEPLOY_HOST:$DEPLOY_DIR/"
    rsync -a -e "$SSH" \
        "$APP_TARBALL" "$MANUAL_TARBALL" "$EXAMPLES_TARBALL" \
        "$SCRIPT_DIR/doit" "$SCRIPT_DIR/sitemap.xml" "$SCRIPT_DIR/robots.txt" \
        "$SCRIPT_DIR/ft-apache.conf" "$SCRIPT_DIR/ft-manual-apache.conf" \
        "$SCRIPT_DIR/lbem-ft-vhost.conf" \
        "$DEPLOY_HOST:$DEPLOY_DIR/"
    echo "Unpacking on the webserver (doit)"
    $SSH "$DEPLOY_HOST" "bash $DEPLOY_DIR/doit"
    echo " "
    echo "Deployed: manual https://lbem-ft.epfl.ch/   app https://lbem-ft.epfl.ch/ft/"
    echo " "
    cat <<EOF
One-time server setup: see lbem-ft-vhost.conf.
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

