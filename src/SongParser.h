#ifndef SERENADE_SONGPARSER_H
#define SERENADE_SONGPARSER_H

#include <string>
#include <vector>
#include <cstdint>

namespace Serenade {

// Represents a single musical event (note, chord, octave change, or rest)
enum class EventType {
    Note,           // Single note press (key 1-8)
    Chord,          // Multiple simultaneous notes
    OctaveUp,       // Press key 9 (increase octave)
    OctaveDown,     // Press key 0 (decrease octave)
    Rest,           // Silence for a beat
    OctaveSet       // Set to specific octave (low/mid/high)
};

enum class Octave {
    Low = 0,
    Mid = 1,
    High = 2
};

struct NoteEvent {
    EventType type;
    std::vector<int> keys;  // Keys 1-8 for notes/chords
    Octave targetOctave;    // For OctaveSet events
    float durationBeats;    // Duration in beats (1.0 = quarter note at given BPM)
};

struct Song {
    std::string title;
    std::string author;         // Original song artist
    std::string instrument;     // Recommended instrument (piano, harp, etc.)
    std::string part;           // Part name for group play (melody, voice, bass, etc.)
    std::string filepath;       // Source file path on disk
    int bpm;                    // Beats per minute
    std::vector<NoteEvent> events;
    mutable float cachedDuration = -1.0f; // Lazy-cached total duration

    float GetTotalDurationSeconds() const;
    bool IsValid() const { return !events.empty() && bpm > 0; }
};

// Parse GW2 Powerina-style notation into a Song
// Supports gw2opus.com and gw2-songbook.com notation:
//   Numbers 1-8 for notes, / for chords, () for high octave, [] for low octave
//   Circled numbers (U+2460-U+2467) as note aliases
//   Section labels (A|, B|, etc.), part labels ("Melody:", "Bass:", etc.)
//   ━ for full-beat rest, - for half-beat rest, . for quarter-beat rest
//   · for dotted notes, spacing for duration
Song ParseNotation(const std::string& notation, const std::string& title = "Untitled",
                   int bpm = 120, const std::string& instrument = "");

// Extract just the first part (melody) from multi-part notation
// gw2opus band tabs contain multiple parts separated by labels like "Piano Melody:", "Bass:", etc.
std::string ExtractFirstPart(const std::string& notation);

// Load a song from a .sng file (simple text format with header + notation)
Song LoadSongFile(const std::string& filepath);

// Load a .txt file as raw gw2opus notation (title from filename, instrument from # instrument: metadata)
Song LoadNotationFile(const std::string& filepath, int bpm = 120);

// Load an AutoHotkey (.ahk) script exported from gw2mb.com or Tabify
// Parses SendInput {NumpadN} for notes/octaves and Sleep for timing (ms)
// Supports # metadata comment lines at the top of the file
Song LoadAHKFile(const std::string& filepath);

// Save a song to a .sng file
bool SaveSongFile(const std::string& filepath, const Song& song);

// Update metadata (title, author, instrument, part) in an existing song file
// Rewrites the # comment metadata lines at the top of the file
bool UpdateSongMetadata(const std::string& filepath, const std::string& title,
                        const std::string& author, const std::string& instrument,
                        const std::string& part);

// Scan music/ directory for .ahk, .txt, and .sng files
// .ahk files use explicit timing (recommended). .txt/.sng use spacing-based timing.
// Instrument is parsed from # instrument: metadata in each file
std::vector<Song> ScanMusicDirectory(const std::string& musicDir);

} // namespace Serenade

#endif
