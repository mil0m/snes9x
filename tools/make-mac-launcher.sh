#!/bin/bash
# Puts "Snes9x Corruptions.app" in ~/Applications: a tiny launcher for this clone's
# corruption app (build/qt/snes9x-qt), so it opens from Spotlight, Launchpad or the Dock.
# It starts whatever was built last, so rebuild with ./build-mac.sh qt and it's picked up.
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
bin="$repo/build/qt/snes9x-qt"
if [ ! -x "\$bin" ]; then
  osascript -e 'display alert "Snes9x Corruptions isn'"'"'t built yet" message "In the snes9x folder, run: ./build-mac.sh qt"'
  exit 1
fi
cd "$repo" && exec "\$bin" "\$@"
EOF
chmod +x "$app/Contents/MacOS/launch"
touch "$app"
echo "Made $app"
