#!/bin/bash
# Explore every kept moment of a game (#85): runs snes9x-explore on each moment in the
# library made on this ROM (its game folder), at most three at once and niced, each into the
# game's explored/ folder, where the Explored page shows them. Meant for overnight: rate
# the finds on the Explored page afterwards, then build/tests/explore-ratings <library>.
#
#   tools/explore_moments.sh <library> <rom> [minutes per moment = 30] [--user NAME]
#
# The library is read and written in place: point it at a copy to try it out.
set -u
if [ $# -lt 2 ]; then
  echo "usage: $0 <library> <rom> [minutes per moment = 30] [--user NAME]" >&2
  exit 2
fi
library=$1
rom=$2
minutes=${3:-30}
user=()
if [ $# -ge 5 ] && [ "$4" = "--user" ]; then user=(--user "$5"); fi
here=$(cd "$(dirname "$0")/.." && pwd)
explore=${SNES9X_EXPLORE:-$here/build/qt/snes9x-explore}
if [ ! -x "$explore" ]; then
  echo "no explorer at $explore: ./build-mac.sh qt builds it (or set SNES9X_EXPLORE)" >&2
  exit 1
fi
if [ ! -f "$rom" ]; then
  echo "no ROM at $rom" >&2
  exit 1
fi

# Only this ROM's game folder: its name ends in the start of the ROM's fingerprint.
folder=$("$explore" "$rom" --game-folder 2>/dev/null | tail -1)
if [ -z "$folder" ] || [ ! -d "$library/$folder/moments" ]; then
  echo "no kept moments of this game in $library${folder:+ (looked in $folder/moments)}: keep some with F8 while playing" >&2
  exit 1
fi

logs=$(mktemp -d "${TMPDIR:-/tmp}/explore-moments.XXXXXX")
count=0
started=0
for moment in "$library/$folder"/moments/*.s9xmoment; do
  [ -f "$moment" ] || continue
  case "$moment" in *.sync-conflict-*) continue ;; esac
  count=$((count + 1))
  # At most three at once (macOS bash 3.2 has no wait -n).
  while [ "$(jobs -rp | wc -l)" -ge 3 ]; do sleep 5; done
  log="$logs/$count.log"
  echo "exploring $(basename "$moment") for $minutes min (log: $log)"
  nice -n 10 "$explore" "$rom" "$moment" --library "$library" --minutes "$minutes" ${user[@]+"${user[@]}"} > "$log" 2>&1 &
  started=$((started + 1))
done
wait
if [ $count -eq 0 ]; then
  echo "no kept moments in $library/$folder/moments: keep some with F8 while playing" >&2
  exit 1
fi
explored=0
other=0
for log in "$logs"/*.log; do
  if grep -q "^done " "$log"; then
    explored=$((explored + 1))
    grep "^done " "$log" | sed 's/^done */  /'
  elif grep -q "another version\|another game" "$log"; then
    other=$((other + 1)) # a moment of another game or ROM version
  else
    echo "  failed: $(tail -1 "$log")"
  fi
done
note=""
[ $other -gt 0 ] && note=" ($other kept on another version of the ROM)"
echo "explored $explored of $count moments$note; logs in $logs"
