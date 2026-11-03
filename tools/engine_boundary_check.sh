#!/bin/bash
# The engine (corrupt/) reaches the Snes9x core through two glue files only (#75, parked):
# core_access.cpp and core_memory.cpp, plus the hook pointer in hook.h. Anything else in
# corrupt/ that includes a core header, or declares a core global itself, fails here, so
# the engine stays something another emulator could host. See
# docs/corruption/2026-11-03-platforms-plan.md.
#
#   tools/engine_boundary_check.sh [engine dir]   (default: corrupt)
set -u
dir="${1:-corrupt}"
glue="core_access.cpp core_memory.cpp hook.h"
failed=0
for f in "$dir"/*.cpp "$dir"/*.h; do
  [ -e "$f" ] || continue
  name=$(basename "$f")
  case " $glue " in *" $name "*) continue ;; esac
  # Includes of anything that isn't the engine's own header or a bundled library.
  while IFS= read -r line; do
    inc=$(printf '%s' "$line" | sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*"([^"]+)".*/\1/')
    case "$inc" in
      nlohmann/* | doctest/*) continue ;;
    esac
    if [ ! -e "$dir/$inc" ]; then
      echo "FAIL $f: includes \"$inc\", which isn't part of the engine (only the glue files may reach the core)"
      failed=1
    fi
  done < <(grep -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*"' "$f")
  # A core global declared by hand gets around the include rule.
  if grep -nE '^[[:space:]]*extern[^;(]*\b(S9x[A-Za-z]*|Memory|CPU|PPU|Registers|Settings|IPPU)\b' "$f" >/dev/null; then
    echo "FAIL $f: declares a Snes9x global itself (use the glue files)"
    failed=1
  fi
done
[ $failed -eq 0 ] && echo "ok   engine boundary: only the glue files reach the Snes9x core"
exit $failed
