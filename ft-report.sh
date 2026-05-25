#!/bin/bash
#
# Usage report for the FT WASM app. Run on the web server as root:
#   sudo ./ft-report.sh
#
# Requires: goaccess, mmdb-bin (mmdblookup), GeoLite2 databases in /var/lib/GeoIP.
#
set -u

LOGS="/var/log/apache2/access.log"*          # current + rotated (.1, .gz)
CITYDB="/var/lib/GeoIP/GeoLite2-City.mmdb"
COUNTRYDB="/var/lib/GeoIP/GeoLite2-Country.mmdb"
REPORT="/home/henning/ft-report.html"

# --- HTML report (GoAccess, with country/geo panel) ---
zcat -f $LOGS | grep " /ft/" \
  | goaccess --log-format=COMBINED --geoip-database="$CITYDB" -o "$REPORT" -
chown henning:henning "$REPORT"

echo "##############################################################"
echo "Top countries accessing /ft/:"
echo "##############################################################"
zcat -f $LOGS | grep " /ft/" | awk '{print $1}' | sort -u \
  | while read -r ip; do
      mmdblookup --file "$COUNTRYDB" --ip "$ip" country names en 2>/dev/null \
        | grep -oP '"\K[^"]+(?=")' | tail -1
    done | sort | uniq -c | sort -rn

echo "##############################################################"
echo "Top cities accessing /ft/:"
echo "##############################################################"
zcat -f $LOGS | grep " /ft/" | awk '{print $1}' | sort -u \
  | while read -r ip; do
      city=$(mmdblookup --file "$CITYDB" --ip "$ip" city names en 2>/dev/null \
              | grep -oP '"\K[^"]+(?=")' | tail -1)
      country=$(mmdblookup --file "$CITYDB" --ip "$ip" country names en 2>/dev/null \
              | grep -oP '"\K[^"]+(?=")' | tail -1)
      echo "${city:-?}, ${country:-?}"
    done | sort | uniq -c | sort -rn
