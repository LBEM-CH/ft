#!/bin/bash
#
# Usage report for the FT WASM app. Run on the web server as root:
#   sudo ./ft-report.sh
#
# Requires: goaccess, mmdb-bin (mmdblookup), GeoLite2 databases in /var/lib/GeoIP.
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
REPORT="/home/henning/ft-report.html"
REPORT_RECENT="/home/henning/ft-report-last6months.html"

# Last 6 calendar months (current month + 5 prior) as Apache-format Mon/YYYY
# tokens. Anchor to the 15th so "-N month" arithmetic never lands in the wrong
# month when the current day is the 29th–31st.
ANCHOR=$(date +%Y-%m-15)
MONTHS=()
for i in 0 1 2 3 4 5; do
  MONTHS+=("$(date -d "$ANCHOR -$i month" +%b/%Y)")
done
RANGE_LABEL="$(date -d "$ANCHOR -5 month" +'%b %Y') – $(date -d "$ANCHOR" +'%b %Y')"
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

# --- HTML report (GoAccess, with country/geo panel) — all available logs ---
zcat -f $LOGS | grep -E "$FT_RE" \
  | goaccess --log-format=COMBINED --geoip-database="$CITYDB" -o "$REPORT" -
chown henning:henning "$REPORT"

# --- HTML report — previous 6 calendar months only ---
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
echo "Top countries accessing /ft/ and /ft-manual/ in last 6 months (${RANGE_LABEL}):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | grep -E "$RECENT_RE" | top_countries

echo "##############################################################"
echo "Top cities accessing /ft/ and /ft-manual/ in last 6 months (${RANGE_LABEL}):"
echo "##############################################################"
zcat -f $LOGS | grep -E "$FT_RE" | grep -E "$RECENT_RE" | top_cities
