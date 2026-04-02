# Serenade

A Guild Wars 2 addon for [Raidcore Nexus](https://raidcore.gg/Nexus) that automates in-game instrument playback. Only the Ornate Grand Piano has been tested, but it should work for other instruments if you acquire songs designed for them.

## AI Notice

This addon has been 100% created in [Windsurf](https://windsurf.com/) using Claude. I understand that some folks have a moral, financial or political objection to creating software using an LLM. I just wanted to make a useful tool for the GW2 community, and this was the only way I could do it.

If an LLM creating software upsets you, then perhaps this repo isn't for you. Move on, and enjoy your day.

## Screenshots

![Music Player](images/player.png)
![Playlist Editor](images/playlist.png)

## Features

- **Music Player** — play/pause, stop, next/previous, shuffle, repeat (off/all/one)
- **Playlist Editor** — dual-pane UI with song library and curated playlist
- **Ornate Grand Piano** — tuned for piano playback with chord support and 3 octaves
- **Track downloading** — download tracks from this repository's music directory from within game.

## Adding Songs

### AHK format (recommended)

AutoHotkey scripts with explicit `SendInput` and `Sleep` commands provide the most accurate playback.

You can get AHK scripts from:
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
3. Place `.ahk` song files in `<GW2>/addons/Serenade/music/`
4. Launch GW2 — Serenade appears in the Nexus quick access bar

## Usage

1. Equip the **Ornate Grand Piano** in-game
2. Click the music note icon in the Nexus toolbar (or press `Ctrl+Shift+M`)
3. Open the Playlist Editor to add songs from the library
4. Use the download button to download songs from this repository's music directory
5. Press Play — the addon sends keypresses to GW2 to play the notes
6. If you need to chat, press Enter — playback stops automatically

## Serenade Addon License

This software is provided as-is, with absolutely no warranty of any kind. Use at your own risk. It might delete your files, melt your PC, burn your house down, or cause world peace. Probably not that last one, but we can hope.


## Serenade Music Converter

The `tools/` directory contains **Serenade Music Converter** (v1.1.0), a standalone Python/PyQt6 application for converting MIDI, MusicXML, and AHK files into GW2-compatible instrument scripts.

### Converter Features

- **Piano Roll Editor** — full-featured note editor with draw/select modes, copy/paste, undo/redo
- **Multi-Track Support** — per-track visibility, color coding, merge, split by pitch
- **Melody Priority** — designate a track as melody to preserve it during conversion
- **Per-Track Simplify** — reduce chord density to treble + bass with manual overrides (Ctrl+Shift+Click)
- **Auto-Transpose** — automatically finds the transposition that minimizes octave changes
- **GW2 Chord Mode** — detects major/minor triads and substitutes GW2's built-in chord keypresses
- **Octave Smoothing** — optional smoothing of short octave excursions in fast passages
- **Smart Octave Assignment** — intelligent octave placement for multi-track arrangements
- **Playback Preview** — MIDI preview with Play, Play Here, Stop, and loop controls
- **AHK Import** — load existing AHK scripts back into the piano roll for editing
- **Drag & Drop** — drop MIDI, MusicXML, or AHK files onto the window to load
- **Batch Convert** — convert multiple files at once
- **Song Submission** — submit songs directly to the Serenade music library
- **Dark/Light Themes** — switchable UI themes

Pre-built binaries for Linux (AppImage) and Windows (.exe) are available from [Releases](../../releases).

The Serenade Music Converter is licensed under the [GNU General Public License v3.0](LICENSE).

### Third-Party Dependencies

- **[PyQt6](https://www.riverbankcomputing.com/software/pyqt/)** — GUI framework (GPL v3)
- **[mido](https://github.com/mido/mido)** — MIDI file parsing (MIT)
- **[pygame](https://www.pygame.org/)** — Audio playback (LGPL v2.1)
- **[NumPy](https://numpy.org/)** — Numerical operations (BSD-3-Clause)
- **[PyInstaller](https://pyinstaller.org/)** — Binary packaging (GPL v2+ with special exception)

## Contributing Songs

Have a song you'd like to share with the Serenade community? [Submit it here](../../issues/new?template=song-submission.yml) — just fill in the details and attach your `.ahk` file. Accepted songs will be added to the `music/` directory and become available for in-game download.