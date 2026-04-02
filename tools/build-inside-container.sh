#!/bin/bash
set -e

echo "=== Building Serenade Music Converter AppImage ==="

# Source is mounted at /src, output at /output
SRC=/src
OUT=/output

# Run PyInstaller
cd /tmp
pyinstaller --onefile --noconsole --name "Serenade Music Converter" \
    --add-data "$SRC/serenade-icon.png:." \
    "$SRC/midi2ahk.py"

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

# Copy Qt platform theme plugins from the venv's PyQt6 (must match bundled Qt version)
PYQT6_PLUGINS=$(python3 -c "import os, PyQt6; print(os.path.join(os.path.dirname(PyQt6.__file__), 'Qt6', 'plugins'))" 2>/dev/null || true)
if [ -d "$PYQT6_PLUGINS/platformthemes" ]; then
    mkdir -p "$APPDIR/usr/plugins/platformthemes"
    cp "$PYQT6_PLUGINS/platformthemes/libqxdgdesktopportal.so" "$APPDIR/usr/plugins/platformthemes/" 2>/dev/null || true
    echo "Bundled platformthemes plugin from PyQt6"
elif [ -d "$SRC/AppDir/usr/plugins" ]; then
    cp -r "$SRC/AppDir/usr/plugins" "$APPDIR/usr/plugins"
    echo "Bundled platformthemes plugin from source AppDir (fallback)"
fi

chmod +x "$APPDIR/AppRun"

# Build AppImage (--appimage-extract-and-run avoids FUSE requirement inside container)
ARCH=x86_64 appimagetool --appimage-extract-and-run "$APPDIR" "$OUT/Serenade_Music_Converter-x86_64.AppImage"

echo "=== Done! AppImage written to output directory ==="
