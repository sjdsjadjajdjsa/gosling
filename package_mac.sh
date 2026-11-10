#!/bin/sh
set -eu
cd "$(dirname "$0")"

command -v dylibbundler >/dev/null || { echo "needs dylibbundler: brew install dylibbundler"; exit 1; }
./build.sh
APP=build/Gosling.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks"
cp build/gosling "$APP/Contents/MacOS/gosling"
YTDLP=$(command -v yt-dlp || true)
[ -n "$YTDLP" ] && cp "$YTDLP" "$APP/Contents/MacOS/yt-dlp"
VERSION=$(git describe --tags --always 2>/dev/null || echo dev)
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Gosling</string>
<key>CFBundleIdentifier</key><string>com.goslingplayer.gosling</string>
<key>CFBundleExecutable</key><string>gosling</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>${VERSION#v}</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
dylibbundler -od -b -x "$APP/Contents/MacOS/gosling" -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks/ >/dev/null
for f in "$APP/Contents/MacOS/gosling" "$APP"/Contents/Frameworks/*.dylib; do
    otool -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}' | sort | uniq -d | while read -r dup; do
        while [ "$(otool -l "$f" | grep -c "path $dup ")" -gt 1 ]; do install_name_tool -delete_rpath "$dup" "$f" 2>/dev/null || break; done
    done
done
codesign --force --deep --sign - "$APP"
ditto -c -k --keepParent "$APP" build/gosling-macos-arm64.zip
du -sh "$APP" build/gosling-macos-arm64.zip
