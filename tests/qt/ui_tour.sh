#!/bin/bash
# Screenshots of every corruption window (the app's SNES9X_UI_TOUR), with no screen.
#
#   tests/qt/ui_tour.sh <out dir> [rom] [--dark] [--scale N]
#
# Uses a scratch config (muted, never the real library) and a copy of the ROM, because the
# game's .srm is written next to it. --dark forces the Fusion dark palette (offscreen has no
# colour scheme to follow). --scale sets QT_SCALE_FACTOR. Pictures land in <out dir>.
#
# Offscreen draws Fusion (what Linux and Windows get). For the real Mac look:
#   TOUR_PLATFORM=cocoa SNES9X_COLOR_SCHEME=light|dark tests/qt/ui_tour.sh <out dir>
# which shows the windows on screen for about 30 seconds (muted).
# TOUR_LIBRARY=<dir> tours with a COPY of that library (its recipes, maps and names), to see
# the windows with real data; the copy is made in the scratch folder, the original is never used.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
out=""; rom=""; dark=""; scale=1
while [ $# -gt 0 ]; do
    case "$1" in
        --dark) dark="--dark" ;;
        --scale) shift; scale="$1" ;;
        *) if [ -z "$out" ]; then out="$1"; else rom="$1"; fi ;;
    esac
    shift
done
[ -n "$out" ] || { echo "usage: $0 <out dir> [rom] [--dark] [--scale N]"; exit 2; }
[ -n "$rom" ] || rom="$HOME/Library/Application Support/OpenEmu/Game Library/roms/Super Nintendo (SNES)/Final Fantasy 6 T-Edition.sfc"
app="$here/build/qt/snes9x-qt"
[ -x "$app" ] || { echo "build the app first: ./build-mac.sh qt"; exit 2; }

mkdir -p "$out"
out="$(cd "$out" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/snes9x-tour.XXXXXX")"
mkdir -p "$scratch/config/snes9x" "$scratch/library" "$scratch/rom"
if [ -n "${TOUR_LIBRARY:-}" ]; then
    rsync -a --exclude roms --exclude .git --exclude .stfolder "$TOUR_LIBRARY/" "$scratch/library/" ||
        { echo "couldn't copy $TOUR_LIBRARY"; exit 2; }
fi
# QSettings writes a group named General as [%General]; a plain [General] is ignored.
cat > "$scratch/config/snes9x/snes9x-qt.conf" <<EOF
[%General]
PauseEmulationWhenUnfocused=false

[Sound]
Mute=true

[Corruption]
LibraryRoot=$scratch/library
PanelVisible=true
EOF
cp "$rom" "$scratch/rom/"
copy="$scratch/rom/$(basename "$rom")"

rm -f "$out/done"
SNES9X_UI_TOUR="$out" XDG_CONFIG_HOME="$scratch/config" QT_QPA_PLATFORM="${TOUR_PLATFORM:-offscreen}" \
    QT_SCALE_FACTOR="$scale" "$app" $dark "$copy" > "$scratch/log.txt" 2>&1 &
pid=$!
for _ in $(seq 180); do # 90 s: the tour waits 1.5 s a step
    [ -f "$out/done" ] && break
    kill -0 $pid 2>/dev/null || break
    sleep 0.5
done
kill -KILL $pid 2>/dev/null
wait $pid 2>/dev/null
if [ -f "$out/done" ]; then
    echo "tour: $(ls "$out"/*.png | wc -l | tr -d ' ') pictures in $out"
    rm -rf "$scratch"
else
    echo "tour did not finish; log: $scratch/log.txt"
    tail -20 "$scratch/log.txt"
    exit 1
fi
