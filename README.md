# Serenade

A Guild Wars 2 addon for [Raidcore Nexus](https://raidcore.gg/Nexus) that automates in-game instrument playback.

## AI Notice

This addon has been 100% created in [Windsurf](https://windsurf.com/) using Claude. I understand that some folks have a moral, financial or political objection to creating software using an LLM. I just wanted to make a useful tool for the GW2 community, and this was the only way I could do it.

## Features

- **Music Player** — play/pause, stop, next/previous, shuffle, repeat (off/all/one)
- **Playlist Editor** — dual-pane UI with song library and curated playlist
- **MIDI Converter** — standalone tool to convert MIDI files to playable AHK scripts
- **Ornate Grand Piano** — tuned for piano playback with chord support and 3 octaves
- **Customisable Keybindings** — rebind all note, sharp, and octave keys from the Nexus options panel

## Adding Songs

### AHK format (recommended)

AutoHotkey scripts with explicit `SendInput` and `Sleep` commands provide the most accurate playback.

You can get AHK scripts from:
- The included **MIDI Converter** (see above)
- [gw2opus Tabify](https://tabify.gw2opus.com/)
- [gw2mb.com](http://gw2mb.com)

Add `#` or `;` metadata comment lines at the top:

```ahk
# title: Moonlight Sonata
# author: Beethoven
# instrument: piano
SendInput {Numpad0}
SendInput {Numpad3}
Sleep, 395
SendInput {Numpad6}
Sleep, 395
...
```
### Directory structure

```
addons/Serenade/music/
├── Moonlight_Sonata.ahk
├── Fur_Elise.ahk
└── Some_Song.txt
```

## Default Keybind

| Keybind | Action |
|---------|--------|
| `Ctrl+Shift+M` | Toggle player window |

## Building

### DLL (Windows addon)

Requires CMake 3.20+ and MinGW cross-compiler (`x86_64-w64-mingw32-g++`):

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
make
```

Produces `build/Serenade.dll`.

## Installation

1. Install [Nexus](https://github.com/RaidcoreGG/Nexus/releases)
2. Copy `Serenade.dll` into `<GW2>/addons/`
3. Place `.ahk` or `.txt` song files in `<GW2>/addons/Serenade/music/`
4. Launch GW2 — Serenade appears in the Nexus quick access bar

## Usage

1. Equip the **Ornate Grand Piano** in-game
2. Click the music note icon in the Nexus toolbar (or press `Ctrl+Shift+M`)
3. Open the Playlist Editor to add songs from the library
4. Press Play — the addon sends keypresses to GW2 to play the notes
5. If you need to chat, press Enter — playback stops automatically

## License

MIT
