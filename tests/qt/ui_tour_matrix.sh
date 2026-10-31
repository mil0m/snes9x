#!/bin/bash
# The UI tour in every look the visual pass checks (docs/corruption/2026-10-31-ui-pass-plan.md):
# Fusion light and dark at scale 1 and 2 (what Linux and Windows get), and with --mac the
# real macOS style in light and dark (its windows show on screen for a while, muted).
#
#   tests/qt/ui_tour_matrix.sh <out dir> [--mac]
#
# Each look lands in <out dir>/<look>/; audit.txt there lists anything a screen reader would
# miss (empty is good).
set -u
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: $0 <out dir> [--mac]}"
mac="${2:-}"
status=0
run() { # look, then ui_tour.sh arguments
    local look="$1"; shift
    "$@" || status=1
    if [ -s "$out/$look/audit.txt" ]; then
        echo "  $look audit:"; sed 's/^/    /' "$out/$look/audit.txt"
    fi
}
run fusion-light-1x "$here/ui_tour.sh" "$out/fusion-light-1x"
run fusion-dark-1x "$here/ui_tour.sh" "$out/fusion-dark-1x" --dark
run fusion-light-2x "$here/ui_tour.sh" "$out/fusion-light-2x" --scale 2
run fusion-dark-2x "$here/ui_tour.sh" "$out/fusion-dark-2x" --dark --scale 2
if [ "$mac" = "--mac" ]; then
    TOUR_PLATFORM=cocoa SNES9X_COLOR_SCHEME=light run mac-light "$here/ui_tour.sh" "$out/mac-light"
    TOUR_PLATFORM=cocoa SNES9X_COLOR_SCHEME=dark run mac-dark "$here/ui_tour.sh" "$out/mac-dark"
fi
exit $status
