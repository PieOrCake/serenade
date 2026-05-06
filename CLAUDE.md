# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Serenade is a Guild Wars 2 addon for the [Raidcore Nexus](https://raidcore.gg/Nexus) platform. It automates in-game instrument playback by reading song files and simulating keyboard input. It builds as a Windows DLL (`Serenade.dll`) cross-compiled from Linux using MinGW.

## Build Commands

```bash
# Configure (only needed once or after CMakeLists.txt changes)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cd build && make

# Output: build/Serenade.dll
```

The build is already configured in `build/`; typically only `make` is needed.

There are no automated tests — this project is tested manually by running it inside GW2 with Nexus loaded.

## Toolchain

Cross-compiles from Linux targeting Windows x86_64 via the `cmake/mingw-w64-x86_64.cmake` toolchain file. Requires `x86_64-w64-mingw32-gcc/g++` and CMake 3.20+. Static linking is used throughout — the final DLL has no external Windows DLL dependencies beyond OS-provided libraries (`gdi32`, `winmm`, `wininet`).

## Architecture

Source is organized into `src/` with three subdirectories. Class interfaces remain in `src/*.h`; implementations are split by concern.

### Addon lifecycle — `src/dllmain.cpp`
Nexus entry point (`GetAddonDef`, `AddonLoad`, `AddonUnload`, `ProcessKeybind`). Owns global state (player, editor, path strings). All UI functions are forward-declared here and defined in `src/ui/`. Shared globals are declared `extern` in **[Addon.h](src/Addon.h)** for use by UI files.

### UI layer — `src/ui/`
- **[GW2Theme.cpp](src/ui/GW2Theme.cpp)** — Dark slate + gold ImGui style. `BuildGW2Theme()` called once at load; `PushGW2Theme()`/`PopGW2Theme()` wrap every window.
- **[PlayerWindow.cpp](src/ui/PlayerWindow.cpp)** — `AddonRender()`: player HUD, transport controls, progress bar, scrolling title, Up Next row. All icon draw functions live here.
- **[OptionsPanel.cpp](src/ui/OptionsPanel.cpp)** — `AddonOptions()`: BPM override, chat announce, music directory, key rebinding UI.
- **[PlaylistEditor.cpp](src/ui/PlaylistEditor.cpp)** — `PlaylistEditor::Render()` shell: window frame, top bar, child pane layout, Edit/Delete modal popups.
- **[LibraryPane.cpp](src/ui/LibraryPane.cpp)** — `RenderLibraryPane()` + `RenderActionButtons()`: sortable library table, instrument/artist/text filters, cached filtered list.
- **[PlaylistPane.cpp](src/ui/PlaylistPane.cpp)** — `RenderPlaylistPane()`: active playlist table, drag-and-drop reorder, smooth auto-scroll to current track.
- **[DownloaderPane.cpp](src/ui/DownloaderPane.cpp)** — `RenderOnlinePane()` + `FetchOnlineSongs()` + `DownloadSong()`: downloads `music/index.json` from GitHub, shows filtered song list, downloads individual files via WinInet.

### Playback engine — `src/player/`
All files implement methods of the `MusicPlayer` class defined in **[MusicPlayer.h](src/MusicPlayer.h)**.
- **[Playback.cpp](src/player/Playback.cpp)** — `PlaybackThread()`, `SendNoteKeys()`, `SendOctaveChange()`, `SendChatMessage()`, `AnnounceCurrentSong()`. Contains the absolute-timing loop and Enter-key chat protection.
- **[Navigation.cpp](src/player/Navigation.cpp)** — `Play()`, `Pause()`, `Stop()`, `Next()`, `Previous()`, `SeekTo()`, `JumpToTrack()`, `ResolveNextTrack()`, `ResolvePrevTrack()`, `AdvanceTrack()`, weighted shuffle logic.
- **[Library.cpp](src/player/Library.cpp)** — Library/playlist CRUD, `SavePlaylist()`/`LoadPlaylist()`, `SaveKeyConfig()`/`LoadKeyConfig()`, `VKToDisplayName()`, state queries (`GetCurrentSong()`, `GetElapsedSeconds()`, etc.), constructor/destructor.

### Song parsers — `src/parsers/`
All files implement functions declared in **[SongParser.h](src/SongParser.h)**.
- **[AHKParser.cpp](src/parsers/AHKParser.cpp)** — `LoadAHKFile()`: parses `SendInput {NumpadN}` + `Sleep ms` scripts.
- **[NotationParser.cpp](src/parsers/NotationParser.cpp)** — `ParseNotation()` + `ExtractFirstPart()`: gw2opus/gw2-songbook number notation, unicode helpers, BPM inline directives.
- **[SongFiles.cpp](src/parsers/SongFiles.cpp)** — `LoadNotationFile()`, `LoadSongFile()`, `SaveSongFile()`, `UpdateSongMetadata()`, `ScanMusicDirectory()`, `Song::GetTotalDurationSeconds()`.

## Key Libraries

- **`include/nexus/Nexus.h`** — Raidcore Nexus API v6 (event loop, ImGui integration, keybinds, Quick Access, data links for GW2 game state)
- **`lib/imgui/`** — ImGui source files compiled directly into the DLL
- **`lib/nlohmann_json.hpp`** — Single-header JSON library for settings and playlist persistence

## Song Library

Songs live in `music/` as `.ahk` files with metadata comment headers. `music/index.json` is the auto-generated metadata index — regenerate it with:

```bash
bash generate_song_index.sh
```

Always regenerate and commit `music/index.json` whenever songs are added, removed, or renamed in `music/`.

## Converter Tool (separate)

`tools/midi2ahk.py` is a standalone PyQt6 GUI tool for converting MIDI files to the AHK song format. It has its own CI workflow (`.github/workflows/build-converter.yml`) and is independent of the DLL build.

```bash
cd tools
pip install -r requirements.txt
python midi2ahk.py
```
