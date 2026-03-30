#!/bin/bash
set -e

echo "=== Building Serenade Music Converter AppImage ==="

# Source is mounted at /src, output at /output
SRC=/src
OUT=/output

# Run PyInstaller
cd /tmp
pyinstaller --onefile --noconsole --name "Serenade Music Converter" "$SRC/midi2ahk.py"

# Assemble AppDir
APPDIR=/tmp/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp "/tmp/dist/Serenade Music Converter" "$APPDIR/usr/bin/serenade-midi-converter"
cp "$SRC/AppDir/AppRun"                          "$APPDIR/AppRun"
cp "$SRC/AppDir/serenade-midi-converter.desktop"  "$APPDIR/serenade-midi-converter.desktop"
cp "$SRC/AppDir/serenade-midi-converter.svg"      "$APPDIR/serenade-midi-converter.svg"
cp "$SRC/AppDir/.DirIcon"                         "$APPDIR/.DirIcon"
cp "$SRC/AppDir/serenade-midi-converter.svg"      "$APPDIR/usr/share/icons/hicolor/scalable/apps/serenade-midi-converter.svg"

# Copy plugins if they exist
if [ -d "$SRC/AppDir/usr/plugins" ]; then
    cp -r "$SRC/AppDir/usr/plugins" "$APPDIR/usr/plugins"
fi

chmod +x "$APPDIR/AppRun"

# Build AppImage (--appimage-extract-and-run avoids FUSE requirement inside container)
ARCH=x86_64 appimagetool --appimage-extract-and-run "$APPDIR" "$OUT/Serenade_Music_Converter-x86_64.AppImage"

echo "=== Done! AppImage written to output directory ==="
