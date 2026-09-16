#!/bin/bash
#
# Usage report for the FT WASM app. Run on the web server as root:
#   sudo ./ft-report.sh
#
# Requires: goaccess, mmdb-bin (mmdblookup), GeoLite2 databases in /var/lib/GeoIP.
#
# NOTE ON HOW FAR BACK THE REPORTS GO
# The per-day charts — "Unique visitors per day" above all — can only be as long
# as the Apache logs still on disk. This script filters the logs; it cannot
# conjure back days that log rotation has already deleted, and the whole-history
# report below applies no date filter at all. So if that chart spans only a
# week, then a week is all that survived rotation. The span actually available
# is printed at the top of the run, so it can be checked at a glance.
#
# To keep more, raise the retention in /etc/logrotate.d/apache2 — with the usual
# weekly rotation, "rotate 26" holds half a year and "rotate 52" a full one —
# and add "compress" if the disk cost matters. The charts then lengthen as the
# logs accumulate; nothing can be recovered retrospectively.
#
set -u

LOGS="/var/log/apache2/access.log"*          # current + rotated (.1, .gz)
# The app lives under /ft/ and its manual under /ft-manual/ (it used to be served
# from inside the app). Both are counted, so the numbers stay comparable with
# reports made before the manual was split out. Matching "/ft-manual/" as well as
# "/ft/" is deliberate: " /ft/" alone would silently drop every manual hit.
FT_RE=" /ft(-manual)?/"
CITYDB="/var/lib/GeoIP/GeoLite2-City.mmdb"
COUNTRYDB="/var/lib/GeoIP/GeoLite2-Country.mmdb"
# How many calendar months the windowed report covers: the current month plus
# the MONTHS_BACK-1 before it. This one number drives the file name, the log
# filter and every heading, so changing the window is a one-line edit and the
# three cannot drift apart.
MONTHS_BACK=3
if ! [[ "$MONTHS_BACK" =~ ^[1-9][0-9]*$ ]]; then
  echo "MONTHS_BACK must be a positive integer (>= 1)" >&2
  exit 1
fi
REPORT="/home/henning/ft-report.html"
REPORT_RECENT="/home/henning/ft-report-last${MONTHS_BACK}months.html"

# The months of that window as Apache-format Mon/YYYY tokens. Anchor to the 15th
# so "-N month" arithmetic never lands in the wrong month when the current day
# is the 29th–31st.
ANCHOR=$(date +%Y-%m-15)
MONTHS=()
for ((i = 0; i < MONTHS_BACK; i++)); do
  MONTHS+=("$(date -d "$ANCHOR -$i month" +%b/%Y)")
done
RANGE_LABEL="$(date -d "$ANCHOR -$((MONTHS_BACK - 1)) month" +'%b %Y') – $(date -d "$ANCHOR" +'%b %Y')"
RECENT_RE="\[[0-9]{2}/($(IFS='|'; echo "${MONTHS[*]}")):"

top_countries() {
  awk '{print $1}' | sort -u \
    | while read -r ip; do
        mmdblookup --file "$COUNTRYDB" --ip "$ip" country names en 2>/dev/null \
          | grep -oP '"\K[^"]+(?=")' | tail -1
      done | sort | uniq -c | sort -rn
}

top_cities() {
  awk '{print $1}' | sort -u \
    | while read -r ip; do
        city=$(mmdblookup --file "$CITYDB" --ip "$ip" city names en 2>/dev/null \
                | grep -oP '"\K[^"]+(?=")' | tail -1)
        country=$(mmdblookup --file "$CITYDB" --ip "$ip" country names en 2>/dev/null \
                | grep -oP '"\K[^"]+(?=")' | tail -1)
        echo "${city:-?}, ${country:-?}"
      done | sort | uniq -c | sort -rn
}

# --- How much history the logs actually hold --------------------------------
# Printed first, because it bounds everything that follows: no report here can
# cover more days than this, whatever window is asked for.
DATES=$(zcat -f $LOGS | grep -E "$FT_RE" \
        | grep -oE '\[[0-9]{2}/[A-Za-z]{3}/[0-9]{4}' | tr -d '[' | sort -u)
if [ -n "$DATES" ]; then
  SPAN_SORTED=$(echo "$DATES" | sort -t/ -k3,3 -k2,2M -k1,1)
  SPAN_FIRST=$(echo "$SPAN_SORTED" | head -1)
  SPAN_LAST=$(echo "$SPAN_SORTED" | tail -1)
  SPAN_DAYS=$(echo "$DATES" | wc -l | tr -d ' ')
else
  SPAN_FIRST="(none)"; SPAN_LAST="(none)"; SPAN_DAYS=0
fi

echo "##############################################################"
echo "Log history on disk for /ft/ and /ft-manual/:"
echo "  ${SPAN_FIRST} – ${SPAN_LAST}   (${SPAN_DAYS} days carrying hits)"
echo "  No chart below can span more than this. If that is shorter than"
echo "  you expect, raise 'rotate' in /etc/logrotate.d/apache2 — see the"
echo "  note at the top of this script."
echo "##############################################################"
echo

# --- HTML report (GoAccess, with country/geo panel) — all available logs ---
zcat -f $LOGS | grep -E "$FT_RE" \
  | goaccess --log-format=COMBINED --geoip-database="$CITYDB" -o "$REPORT" -
chown henning:henning "$REPORT"

# --- HTML report — the previous MONTHS_BACK calendar months only ---
zcat -f $LOGS | grep -E "$FT_RE" | grep -E "$RECENT_RE" \
  | goaccess --log-format=COMBINED --geoip-database="$CITYDB" -o "$REPORT_RECENT" -
chown henning:henning "$REPORT_RECENT"

echo "##############################################################"
echo "Top countries accessing /ft/ and /ft-manual/ (all logs):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | top_countries

echo "##############################################################"
echo "Top cities accessing /ft/ and /ft-manual/ (all logs):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | top_cities

echo "##############################################################"
echo "Top countries accessing /ft/ and /ft-manual/ in last ${MONTHS_BACK} months (${RANGE_LABEL}):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | grep -E "$RECENT_RE" | top_countries

echo "##############################################################"
echo "Top cities accessing /ft/ and /ft-manual/ in last ${MONTHS_BACK} months (${RANGE_LABEL}):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | grep -E "$RECENT_RE" | top_cities
