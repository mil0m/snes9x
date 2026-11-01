#!/bin/bash
# End to end for the remote page (#53): the real app, offscreen, with remote control on (a
# scratch config with a random key and port), and a headless Chromium driving the page at
# phone width, light and dark. Screenshots land in <out dir>.
#
#   tests/remote/remote_e2e.sh <out dir> <rom>
#
# The browser comes from BROWSER_HELPER: an ES module exporting openContext(options) ->
# { page, close } (Playwright). Default: ~/scripts/kh-browser.mjs.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:?usage: $0 <out dir> <rom>}"; rom="${2:?usage: $0 <out dir> <rom>}"
app="$here/build/qt/snes9x-qt"
[ -x "$app" ] || { echo "build the app first: ./build-mac.sh qt"; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/snes9x-remote.XXXXXX")"
mkdir -p "$scratch/config/snes9x" "$scratch/library" "$scratch/rom"
port=$(( 20000 + RANDOM % 20000 ))
token="$(openssl rand -base64 32 | tr '+/' '-_' | tr -d '=')"
cat > "$scratch/config/snes9x/snes9x-qt.conf" <<CONF
[%General]
PauseEmulationWhenUnfocused=false

[Sound]
Mute=true

[Corruption]
LibraryRoot=$scratch/library
PanelVisible=true
RemoteEnabled=true
RemotePort=$port
RemoteToken=$token
CONF
cp "$rom" "$scratch/rom/"
XDG_CONFIG_HOME="$scratch/config" QT_QPA_PLATFORM=offscreen "$app" "$scratch/rom/$(basename "$rom")" \
    > "$scratch/log.txt" 2>&1 &
pid=$!
for _ in $(seq 60); do
    nc -z 127.0.0.1 "$port" 2>/dev/null && break
    sleep 0.5
done
rc=0
REMOTE_URL="http://127.0.0.1:$port/#$token" OUT="$out" \
    BROWSER_HELPER="${BROWSER_HELPER:-$HOME/scripts/kh-browser.mjs}" \
    node "$here/tests/remote/remote_e2e.mjs" || rc=$?
kill -KILL $pid 2>/dev/null
wait $pid 2>/dev/null
if [ $rc -ne 0 ]; then echo "log: $scratch/log.txt"; tail -5 "$scratch/log.txt"; else rm -rf "$scratch"; fi
exit $rc
