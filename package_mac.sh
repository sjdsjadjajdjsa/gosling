#!/bin/sh
set -eu
cd "$(dirname "$0")"

command -v dylibbundler >/dev/null || { echo "needs dylibbundler: brew install dylibbundler"; exit 1; }
./build.sh
APP=build/Gosling.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks"
cp build/gosling "$APP/Contents/MacOS/gosling"
mkdir -p "$APP/Contents/Resources"
cp packaging/Gosling.icns "$APP/Contents/Resources/Gosling.icns"
VERSION=${VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}
VERSION=${VERSION#v}
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Gosling</string>
<key>CFBundleDisplayName</key><string>Gosling</string>
<key>CFBundleIconFile</key><string>Gosling</string>
<key>CFBundleVersion</key><string>${VERSION}</string>
<key>LSApplicationCategoryType</key><string>public.app-category.music</string>
<key>NSHumanReadableCopyright</key><string>FSL-1.1-MIT</string>
<key>CFBundleIdentifier</key><string>com.goslingplayer.gosling</string>
<key>CFBundleExecutable</key><string>gosling</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>${VERSION}</string>
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
[ -x build/dmgenv/bin/dmgbuild ] || { python3 -m venv build/dmgenv && build/dmgenv/bin/pip install -q dmgbuild; }
rm -f build/gosling-macos-arm64.dmg
build/dmgenv/bin/dmgbuild -s packaging/dmg.py -D app="$APP" "Gosling" build/gosling-macos-arm64.dmg >/dev/null
du -sh "$APP" build/gosling-macos-arm64.dmg
