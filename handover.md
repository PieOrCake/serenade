# Serenade — Handover

## What it is

Serenade is a Guild Wars 2 addon for the [Raidcore Nexus](https://raidcore.gg/Nexus) platform. It automates in-game instrument playback by reading song files and simulating keyboard input. It builds as a Windows DLL (`Serenade.dll`) cross-compiled from Linux using MinGW.

**Current version:** 0.9.4.0

## Architecture

Source is split into `src/` with three subdirectories:

- **`src/dllmain.cpp`** — Nexus entry point, global state, QA button registration, texture loading
- **`src/ui/`** — All ImGui windows: PlayerWindow, PlaylistEditor (+ LibraryPane, PlaylistPane), OptionsPanel, DownloaderPane, GW2Theme
- **`src/player/`** — MusicPlayer class split across Playback.cpp, Navigation.cpp, Library.cpp
- **`src/parsers/`** — AHKParser, NotationParser, SongFiles
- **`src/icons.h`** — Embedded PNG byte arrays for QA button icons (ICON_NOTE normal, ICON_NOTE_HOVER)
- **`src/Addon.h`** — Shared extern globals for use across UI files

## Build

```bash
cd build && make
# Output: build/Serenade.dll
```

Build is pre-configured. Only `make` is needed for day-to-day work. Cross-compiles via `cmake/mingw-w64-x86_64.cmake`.

## Key features

- Plays `.ahk` song files (SendInput/Sleep format) and gw2opus number notation
- Playlist management with drag-and-drop reorder, weighted shuffle, filters
- Online song downloader (fetches `music/index.json` from GitHub, downloads individual files via WinInet)
- Chat announce on track change
- Time-based seek bar, auto-scroll playlist
- GW2-themed ImGui style (dark slate + gold)
- Quick Access button in Nexus toolbar
- Drum Kit support — drum songs skip the octave reset and inter-song gap at playback start

## Drum support

GW2 added a Drum Kit instrument. Songs with `instrument: Drums` in their metadata header skip the octave reset sequence (which presses keys 9×5 and 0×1) and the 3-second inter-song gap, since drums have no octave system. Detection is a case-sensitive string match on `song->instrument == "Drums"` in `src/player/Playback.cpp`.

## Instrument filter tabs

The library pane and downloader pane both have instrument filter tabs (All / Drums / Piano / etc.). There was a bug where the tab clicks didn't actually filter — fixed by introducing a local `artistListRebuilt` flag to correctly signal the filtered list as dirty. The bug was in `src/ui/LibraryPane.cpp` and `src/ui/DownloaderPane.cpp`.

## QA icon

`quaver.png` (in project root) is the source image. `src/icons.h` contains it embedded as two C byte arrays:
- `ICON_NOTE` — 32×32, as-is
- `ICON_NOTE_HOVER` — 35×35 (10% larger), 20% brighter

To regenerate after updating `quaver.png`:
```bash
python3 << 'EOF'
from PIL import Image, ImageEnhance
import io, textwrap

def png_to_c_array(img, name):
    buf = io.BytesIO()
    img.save(buf, format='PNG', optimize=False)
    data = buf.getvalue()
    hex_bytes = ', '.join(f'0x{b:02x}' for b in data)
    lines = textwrap.fill(hex_bytes, width=72, subsequent_indent='    ')
    return (
        f"static const unsigned char {name}[] = {{\n"
        f"    {lines}\n"
        f"}};\n"
        f"static const unsigned int {name}_size = sizeof({name});\n"
    )

src = Image.open('quaver.png').convert('RGBA')
normal = src.resize((32, 32), Image.LANCZOS)
hover_size = round(32 * 1.1)
hover = src.resize((hover_size, hover_size), Image.LANCZOS)
r, g, b, a = hover.split()
rgb = ImageEnhance.Brightness(Image.merge('RGB', (r, g, b))).enhance(1.2)
r2, g2, b2 = rgb.split()
hover = Image.merge('RGBA', (r2, g2, b2, a))

with open('src/icons.h', 'w') as f:
    f.write('#pragma once\n\n')
    f.write('// Embedded quaver icon (normal - 32x32)\n')
    f.write(png_to_c_array(normal, 'ICON_NOTE'))
    f.write('\n// Embedded quaver icon (hover - 20% brighter, 10% larger)\n')
    f.write(png_to_c_array(hover, 'ICON_NOTE_HOVER'))
EOF
```

## Song library

Songs live in `music/` as `.ahk` files. `music/index.json` is the metadata index — always regenerate and commit it when songs are added, removed, or renamed:
```bash
bash generate_song_index.sh
```

## What's left to do

- No known bugs at time of handover
