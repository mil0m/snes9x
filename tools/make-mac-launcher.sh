#!/bin/bash
# Puts "Snes9x Corruptions.app" in ~/Applications: a tiny launcher for this clone's
# corruption app (build/qt/snes9x-qt), so it opens from Spotlight, Launchpad or the Dock.
# When the clone has commits the build lacks, it rebuilds first (a notification says so).
# Run once per clone (again only if the clone moves).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
app="$HOME/Applications/Snes9x Corruptions.app"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$repo/macosx/APPL.icns" "$app/Contents/Resources/AppIcon.icns"

cat > "$app/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Snes9x Corruptions</string>
  <key>CFBundleIdentifier</key><string>local.snes9x.corruptions.launcher</string>
  <key>CFBundleExecutable</key><string>launch</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
EOF

cat > "$app/Contents/MacOS/launch" <<EOF
#!/bin/bash
# Rebuilds first when the clone has new commits (yours, Erik's after a pull, an agent's),
# then starts the app. A failed build says so and opens the last good one.
repo="$repo"
EOF
cat >> "$app/Contents/MacOS/launch" <<'EOF'
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"   # Finder starts apps with a bare PATH
cd "$repo" || exit 1
bin="build/qt/snes9x-qt"
head="$(git rev-parse HEAD 2>/dev/null)"
built="$(cat build/qt/built-from 2>/dev/null)"
if [ ! -x "$bin" ] || [ "$head" != "$built" ]; then
  osascript -e 'display notification "Building the newest features, this takes a minute or two…" with title "Snes9x Corruptions"'
  if ! ./build-mac.sh qt > build/launcher-build.log 2>&1; then
    if [ -x "$bin" ]; then
      osascript -e 'display alert "Couldn'"'"'t build the newest version" message "Opening the last one that built. What went wrong is in build/launcher-build.log."'
    else
      osascript -e 'display alert "Snes9x Corruptions couldn'"'"'t be built" message "What went wrong is in build/launcher-build.log in the snes9x folder."'
      exit 1
    fi
  fi
fi
exec "$bin" "$@"
EOF
chmod +x "$app/Contents/MacOS/launch"
touch "$app"
echo "Made $app"
