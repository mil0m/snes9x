#!/bin/bash
# Builds the documentation page (docs/site, roadmap #87) into build/site: the page, and every
# picture made fresh from the real core and the app. Nothing it makes is committed (game art).
#
#   tools/site/build.sh [--no-clips] [--no-shots]
#
# SITE_ROMS: the folder with the ROMs named in docs/site/media.txt (default: OpenEmu's SNES
# folder). SITE_LIBRARY: the library the UI tour copies for real data (default: the app's
# usual one; only a copy is used). A missing ROM leaves its picture out, not the build.
# Needs ffmpeg and ImageMagick (brew install ffmpeg imagemagick). Clips run 3 at a time, niced.
set -u
repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"
out="$repo/build/site"
work="$repo/build/site-work"
roms="${SITE_ROMS:-$HOME/Library/Application Support/OpenEmu/Game Library/roms/Super Nintendo (SNES)}"
library="${SITE_LIBRARY:-$HOME/Documents/Snes9x Corruptions}"
clips=1; shots=1
for a in "$@"; do
  case "$a" in
    --no-clips) clips=0 ;;
    --no-shots) shots=0 ;;
    *) echo "usage: $0 [--no-clips] [--no-shots]" >&2; exit 2 ;;
  esac
done
for tool in ffmpeg magick; do
  command -v $tool >/dev/null || { echo "needs $tool: brew install ffmpeg imagemagick" >&2; exit 2; }
done
mkdir -p "$out/media" "$work"

# The clip tool (corrupt-smoke, optimized) and the app for the tour.
if [ $clips = 1 ]; then
  cmake -S tests -B build/explore -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" >/dev/null &&
    cmake --build build/explore --target corrupt-smoke >/dev/null || { echo "couldn't build corrupt-smoke" >&2; exit 1; }
fi
if [ $shots = 1 ] && [ ! -x build/qt/snes9x-qt ]; then ./build-mac.sh qt >/dev/null || exit 1; fi

# One clip: play the scene, encode clean and corrupted as looping videos plus a poster.
clip() { # name rom lead recipe seed
  local name=$1 rom="$roms/$2" lead=$3 recipe=$4 seed=$5 dir="$work/$1"
  if [ ! -f "$rom" ]; then echo "skip $name: no $2 in $roms"; return; fi
  rm -rf "$dir"
  CORRUPT_CLIP_LEAD=$lead CORRUPT_CLIP_MENUS=0.7 CORRUPT_CLIP_SKIP=120 CORRUPT_CLIP_ONLY=$seed \
    nice -n 10 timeout -s KILL 1200 build/explore/corrupt-smoke "$rom" "docs/site/recipes/$recipe.json" 480 600 \
    --clip "$dir" "$seed" > "$dir.log" 2>&1 < /dev/null
  if [ ! -f "$dir/s$seed/0479.png" ]; then echo "clip $name failed: $dir.log"; return; fi
  for which in "clean-s$seed" "s$seed"; do
    local file="$name"; [ "$which" = "clean-s$seed" ] && file="$name-clean"
    ffmpeg -nostdin -y -loglevel error -framerate 60 -i "$dir/$which/%04d.png" \
      -vf "scale=iw*3:ih*3:flags=neighbor" -c:v libx264 -preset slow -crf 22 -pix_fmt yuv420p \
      -movflags +faststart -an "$out/media/$file.mp4"
  done
  magick "$dir/s$seed/0240.png" -filter point -resize 300% -quality 88 "$out/media/$name.jpg"
  echo "clip $name: $(grep '^seed' "$dir.log" | sed 's/^seed *//')"
}

if [ $clips = 1 ]; then
  running=0
  while read -r kind name rom lead recipe seed caption; do
    [ "$kind" = clip ] || continue
    clip "$name" "${rom//_/ }" "$lead" "$recipe" "$seed" &
    running=$((running + 1))
    if [ $running -ge 3 ]; then wait; running=0; fi
  done < <(grep -v '^#' docs/site/media.txt)
  wait
fi

# The app's windows: the UI tour at 2x on a copy of the library, then the ones the page uses.
if [ $shots = 1 ]; then
  tour="$work/tour"
  rm -rf "$tour"
  tour_rom="$(grep '^tour-rom' docs/site/media.txt | awk '{print $2}' | tr _ ' ')"
  if [ -d "$library" ]; then export TOUR_LIBRARY="$library"; fi
  nice -n 5 tests/qt/ui_tour.sh "$tour" "$roms/$tour_rom" --scale 2 > "$work/tour.log" 2>&1 ||
    echo "the UI tour failed: $work/tour.log"
  while read -r kind name file crop caption; do
    [ "$kind" = shot ] || continue
    if [ -f "$tour/$file" ]; then
      if [ "$crop" = - ]; then cut=(); else cut=(-crop "$crop" +repage); fi
      magick "$tour/$file" ${cut[@]+"${cut[@]}"} -resize '1600x1600>' "$out/media/$name.png"
      echo "shot $name: $file"
    else
      echo "shot $name: no $file in the tour"
    fi
  done < <(grep -v '^#' docs/site/media.txt)
fi

# What was made, with captions, for the page's script. A picture that wasn't made is left out
# and its card says so.
python3 - "$out/media" docs/site/media.txt > "$out/media.json" <<'PY'
import json, os, sys
media, listing = sys.argv[1], sys.argv[2]
found = {}
for line in open(listing):
    parts = line.split()
    if not parts or parts[0].startswith('#'):
        continue
    if parts[0] == 'clip' and len(parts) >= 7 and os.path.exists(f'{media}/{parts[1]}.mp4'):
        found[parts[1]] = {'name': parts[1], 'caption': ' '.join(parts[6:])}
    elif parts[0] == 'shot' and len(parts) >= 5 and os.path.exists(f'{media}/{parts[1]}.png'):
        found[parts[1]] = {'name': parts[1], 'caption': ' '.join(parts[4:])}
print(json.dumps(found, indent=1))
PY

# The page itself, stamped with the commit it describes.
built="$(git log -1 --format='%h' 2>/dev/null) on $(date '+%-d %b %Y')"
sed "s|{{built}}|$built|" docs/site/index.html > "$out/index.html"
cp docs/site/site.css docs/site/site.js "$out/"
echo "site: $out/index.html"
