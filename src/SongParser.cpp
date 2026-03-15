#include "SongParser.h"
#include <fstream>

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <regex>
#include <cstring>

namespace Serenade {

float Song::GetTotalDurationSeconds() const {
    if (bpm <= 0) return 0.0f;
    float totalBeats = 0.0f;
    for (const auto& ev : events) {
        totalBeats += ev.durationBeats;
    }
    return totalBeats * 60.0f / static_cast<float>(bpm);
}

// Helper: trim whitespace
static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Helper: case-insensitive starts-with
static bool StartsWithCI(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

// Helper: check if a UTF-8 sequence at position i is a zero-width character
// Returns bytes consumed (0 if not a zero-width char)
static int IsZeroWidth(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return 0;
    unsigned char b0 = s[i], b1 = s[i+1], b2 = s[i+2];
    // U+200B zero-width space
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8B) return 3;
    // U+200C zero-width non-joiner
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8C) return 3;
    // U+200D zero-width joiner
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8D) return 3;
    // U+FEFF BOM / zero-width no-break space
    if (i + 2 < s.size() && b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3;
    return 0;
}

// Helper: check if UTF-8 sequence at i is circled digit ①-⑧ (U+2460-U+2467)
// Returns the digit 1-8 or 0 if not matched
static int IsCircledDigit(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return 0;
    unsigned char b0 = s[i], b1 = s[i+1], b2 = s[i+2];
    if (b0 == 0xE2 && b1 == 0x91 && b2 >= 0xA0 && b2 <= 0xA7) {
        return (b2 - 0xA0) + 1; // ① = 1, ② = 2, ... ⑧ = 8
    }
    return 0;
}

// Helper: check if UTF-8 sequence at i is ━ (U+2501, box drawing heavy horizontal = full-beat rest)
static bool IsFullRest(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return false;
    return (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i+1] == 0x94 &&
           (unsigned char)s[i+2] == 0x81;
}

// Helper: check if UTF-8 sequence at i is · (U+00B7, middle dot = dotted note modifier)
static bool IsMiddleDot(const std::string& s, size_t i) {
    if (i + 1 >= s.size()) return false;
    return (unsigned char)s[i] == 0xC2 && (unsigned char)s[i+1] == 0xB7;
}

// Helper: check if UTF-8 sequence at i is – (U+2013, en dash = half-beat rest in some notation)
static bool IsEnDash(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return false;
    return (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i+1] == 0x80 &&
           (unsigned char)s[i+2] == 0x93;
}

// Pre-process notation: strip zero-width chars, convert circled digits, strip section labels
static std::string PreprocessNotation(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        // Skip zero-width characters
        int zw = IsZeroWidth(raw, i);
        if (zw > 0) { i += zw; continue; }

        // Convert circled digits ①-⑧ to ASCII digits 1-8
        int cd = IsCircledDigit(raw, i);
        if (cd > 0) {
            out += ('0' + cd);
            i += 3;
            continue;
        }

        // Skip section labels: single uppercase letter immediately followed by |
        // e.g. "A|", "B|", etc. at start of section
        if (i + 1 < raw.size() && std::isupper((unsigned char)raw[i]) && raw[i+1] == '|') {
            i += 2;
            continue;
        }

        out += raw[i];
        i++;
    }
    return out;
}

Song ParseNotation(const std::string& notation, const std::string& title, int bpm,
                   const std::string& instrument) {
    Song song;
    song.title = title;
    song.bpm = bpm;
    song.instrument = instrument;

    // Extract first part if multi-part notation
    std::string firstPart = ExtractFirstPart(notation);
    // Pre-process: strip zero-width chars, convert circled digits, strip section labels
    std::string input = PreprocessNotation(firstPart);

    // Parse BPM from inline "BPM = 100" or "BPM: 100" directives
    {
        std::string upper = input;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto bpmPos = upper.find("BPM");
        if (bpmPos != std::string::npos) {
            size_t j = bpmPos + 3;
            while (j < input.size() && (input[j] == ' ' || input[j] == '=' || input[j] == ':'))
                j++;
            std::string numStr;
            while (j < input.size() && std::isdigit((unsigned char)input[j])) {
                numStr += input[j];
                j++;
            }
            if (!numStr.empty()) {
                try { song.bpm = std::stoi(numStr); } catch (...) {}
            }
            // Remove the BPM directive from input
            size_t endBpm = j;
            input.erase(bpmPos, endBpm - bpmPos);
        }
    }

    Octave currentOctave = Octave::Mid;
    Octave octaveBeforeBracket = Octave::Mid;
    bool inLowBracket = false;
    bool inHighBracket = false;

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = (unsigned char)input[i];

        // Skip bar lines (|)
        if (c == '|') { i++; continue; }

        // Full-beat rest: ━ (U+2501)
        if (IsFullRest(input, i)) {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 1.0f;
            rest.targetOctave = currentOctave;
            song.events.push_back(rest);
            i += 3;
            continue;
        }

        // En dash rest: – (U+2013) used as half-beat rest
        if (IsEnDash(input, i)) {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.5f;
            rest.targetOctave = currentOctave;
            song.events.push_back(rest);
            i += 3;
            continue;
        }

        // Half-beat rest: - (ASCII dash)
        if (c == '-') {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.5f;
            rest.targetOctave = currentOctave;
            song.events.push_back(rest);
            i++;
            continue;
        }

        // Low octave bracket open [
        if (c == '[') {
            octaveBeforeBracket = currentOctave;
            inLowBracket = true;
            if (currentOctave != Octave::Low) {
                NoteEvent oc;
                oc.type = EventType::OctaveSet;
                oc.targetOctave = Octave::Low;
                oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = Octave::Low;
            }
            i++;
            continue;
        }

        // Low octave bracket close ]
        if (c == ']') {
            inLowBracket = false;
            if (currentOctave != octaveBeforeBracket) {
                NoteEvent oc;
                oc.type = EventType::OctaveSet;
                oc.targetOctave = octaveBeforeBracket;
                oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = octaveBeforeBracket;
            }
            i++;
            continue;
        }

        // High octave bracket open (
        if (c == '(') {
            octaveBeforeBracket = currentOctave;
            inHighBracket = true;
            if (currentOctave != Octave::High) {
                NoteEvent oc;
                oc.type = EventType::OctaveSet;
                oc.targetOctave = Octave::High;
                oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = Octave::High;
            }
            i++;
            continue;
        }

        // High octave bracket close )
        if (c == ')') {
            inHighBracket = false;
            if (currentOctave != octaveBeforeBracket) {
                NoteEvent oc;
                oc.type = EventType::OctaveSet;
                oc.targetOctave = octaveBeforeBracket;
                oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = octaveBeforeBracket;
            }
            i++;
            continue;
        }

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i++;
            continue;
        }

        // Skip comma (sometimes used in notation)
        if (c == ',') { i++; continue; }

        // Quarter-beat rest: . (period)
        if (c == '.') {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.25f;
            rest.targetOctave = currentOctave;
            song.events.push_back(rest);
            i++;
            continue;
        }

        // Digit 1-8: single note or start of chord (digit/digit/...)
        // In Powerina notation each digit is a separate note; consecutive digits
        // like "35784" are five individual quarter-beat notes, NOT one number.
        if (c >= '1' && c <= '8') {
            std::vector<int> noteKeys;
            noteKeys.push_back(c - '0');
            i++;

            // Check for chord: /digit pairs
            while (i + 1 < input.size() && input[i] == '/') {
                unsigned char nc = (unsigned char)input[i + 1];
                if (nc >= '1' && nc <= '8') {
                    noteKeys.push_back(nc - '0');
                    i += 2;
                } else {
                    break;
                }
            }

            // Check for dotted note (·) immediately after
            bool dotted = false;
            if (IsMiddleDot(input, i)) {
                dotted = true;
                i += 2;
            }

            // Count trailing spaces for duration, skipping brackets transparently
            // Brackets () [] are octave markers and don't consume rhythmic space
            int spaceCount = 0;
            size_t j = i;
            while (j < input.size()) {
                unsigned char jc = (unsigned char)input[j];
                if (jc == ' ') {
                    spaceCount++;
                    j++;
                } else if (jc == '(' || jc == ')' || jc == '[' || jc == ']') {
                    j++;
                } else {
                    break;
                }
            }

            NoteEvent ev;
            ev.targetOctave = currentOctave;
            ev.type = (noteKeys.size() > 1) ? EventType::Chord : EventType::Note;
            ev.keys = std::move(noteKeys);

            // Duration from spacing: 0 spaces = 1/4 beat, 1 = 1/2 beat, 2+ = 1 beat
            if (spaceCount == 0) ev.durationBeats = 0.25f;
            else if (spaceCount == 1) ev.durationBeats = 0.5f;
            else ev.durationBeats = 1.0f;

            if (dotted) ev.durationBeats *= 1.5f;

            song.events.push_back(ev);
            continue;
        }

        // Skip other digits (0, 9) that aren't note values
        if (std::isdigit(c)) { i++; continue; }

        // Skip any other multi-byte UTF-8 sequences we don't recognize
        if (c >= 0x80) {
            if (c >= 0xF0 && i + 3 < input.size()) { i += 4; continue; }
            if (c >= 0xE0 && i + 2 < input.size()) { i += 3; continue; }
            if (c >= 0xC0 && i + 1 < input.size()) { i += 2; continue; }
            i++;
            continue;
        }

        // Skip unrecognized ASCII
        i++;
    }

    return song;
}

std::string ExtractFirstPart(const std::string& notation) {
    // gw2opus band tabs have parts labeled like:
    //   "Piano Melody:A|..." or "Bass:A|..." or just "Melody:" on its own line
    // We detect part boundaries by looking for patterns like "Word Word:A|" or "Word:" at line starts

    // First check if notation contains multiple parts
    // Part labels typically match: word(s) followed by colon, then section label or newline
    std::vector<size_t> partStarts;
    std::string lower = notation;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Known part label prefixes (case-insensitive)
    const char* partLabels[] = {
        "melody", "piano melody", "piano bg", "bass", "harp", "lute", "bell",
        "flute", "horn", "minstrel", "verdarach", "guitar", "drum",
        "lead", "rhythm", "harmony", "accompaniment", nullptr
    };

    for (size_t pos = 0; pos < lower.size(); ) {
        // Check if current position matches a part label followed by ':'
        bool found = false;
        for (int p = 0; partLabels[p] != nullptr; p++) {
            size_t len = strlen(partLabels[p]);
            if (pos + len < lower.size() && lower.substr(pos, len) == partLabels[p]) {
                // Check for ':' after optional whitespace
                size_t afterLabel = pos + len;
                while (afterLabel < lower.size() && lower[afterLabel] == ' ') afterLabel++;
                if (afterLabel < lower.size() && lower[afterLabel] == ':') {
                    partStarts.push_back(pos);
                    pos = afterLabel + 1;
                    found = true;
                    break;
                }
            }
        }
        if (!found) pos++;
    }

    if (partStarts.size() <= 1) {
        // Single part or no part labels found - return as-is
        // But strip leading part label if present
        if (partStarts.size() == 1) {
            size_t colonPos = notation.find(':', partStarts[0]);
            if (colonPos != std::string::npos) {
                return notation.substr(colonPos + 1);
            }
        }
        return notation;
    }

    // Multiple parts: extract from first part label's colon to next part label
    size_t firstColon = notation.find(':', partStarts[0]);
    if (firstColon == std::string::npos) return notation;

    size_t endPos = partStarts[1];
    return notation.substr(firstColon + 1, endPos - (firstColon + 1));
}

Song LoadSongFile(const std::string& filepath) {
    Song song;
    song.bpm = 120;

    std::ifstream file(filepath);
    if (!file.is_open()) return song;

    std::string line;
    std::string notation;
    bool inNotation = false;

    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);

        if (trimmed.empty()) continue;

        // Parse header fields
        if (trimmed.substr(0, 6) == "title:") {
            song.title = Trim(trimmed.substr(6));
            continue;
        }
        if (trimmed.substr(0, 7) == "author:") {
            song.author = Trim(trimmed.substr(7));
            continue;
        }
        if (trimmed.substr(0, 5) == "part:") {
            song.part = Trim(trimmed.substr(5));
            continue;
        }
        if (trimmed.substr(0, 11) == "instrument:") {
            song.instrument = Trim(trimmed.substr(11));
            continue;
        }
        if (trimmed.substr(0, 4) == "bpm:") {
            try {
                song.bpm = std::stoi(Trim(trimmed.substr(4)));
            } catch (...) {
                song.bpm = 120;
            }
            continue;
        }
        if (trimmed == "---") {
            inNotation = true;
            continue;
        }

        if (inNotation) {
            if (!notation.empty()) notation += "\n";
            notation += line;
        }
    }

    if (!notation.empty()) {
        Song parsed = ParseNotation(notation, song.title, song.bpm, song.instrument);
        song.events = std::move(parsed.events);
    }

    // If title is still empty, derive from filename
    if (song.title.empty()) {
        std::filesystem::path p(filepath);
        song.title = p.stem().string();
    }

    song.filepath = filepath;
    return song;
}

bool SaveSongFile(const std::string& filepath, const Song& song) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "title: " << song.title << "\n";
    if (!song.author.empty()) file << "author: " << song.author << "\n";
    if (!song.instrument.empty()) file << "instrument: " << song.instrument << "\n";
    if (!song.part.empty()) file << "part: " << song.part << "\n";
    file << "bpm: " << song.bpm << "\n";
    file << "---\n";

    // Write events back as simplified notation
    Octave curOctave = Octave::Mid;
    for (const auto& ev : song.events) {
        switch (ev.type) {
            case EventType::OctaveSet:
                if (ev.targetOctave == Octave::Low && curOctave != Octave::Low) {
                    if (curOctave == Octave::High) file << ")";
                    file << "[";
                } else if (ev.targetOctave == Octave::High && curOctave != Octave::High) {
                    if (curOctave == Octave::Low) file << "]";
                    file << "(";
                } else if (ev.targetOctave == Octave::Mid) {
                    if (curOctave == Octave::Low) file << "]";
                    else if (curOctave == Octave::High) file << ")";
                }
                curOctave = ev.targetOctave;
                break;

            case EventType::Note:
                if (!ev.keys.empty()) {
                    file << ev.keys[0];
                    if (ev.durationBeats >= 1.0f) file << "  ";
                    else if (ev.durationBeats >= 0.5f) file << " ";
                }
                break;

            case EventType::Chord:
                for (size_t i = 0; i < ev.keys.size(); i++) {
                    if (i > 0) file << "/";
                    file << ev.keys[i];
                }
                if (ev.durationBeats >= 1.0f) file << "  ";
                else if (ev.durationBeats >= 0.5f) file << " ";
                break;

            case EventType::Rest:
                if (ev.durationBeats >= 1.0f) file << "- ";
                else if (ev.durationBeats >= 0.5f) file << "-";
                else file << ".";
                break;

            default:
                break;
        }
    }
    file << "\n";

    return true;
}

bool UpdateSongMetadata(const std::string& filepath, const std::string& title,
                        const std::string& author, const std::string& instrument,
                        const std::string& part) {
    // Read the entire file
    std::ifstream inFile(filepath);
    if (!inFile.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();

    // Determine file type by extension
    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // For .ahk and .txt files: metadata is in # comment lines at top
    // For .sng files: metadata is in plain header lines before ---
    bool isAhkOrTxt = (ext == ".ahk" || ext == ".txt");

    // Remove existing metadata lines from top of file
    std::vector<std::string> bodyLines;
    bool pastMeta = false;
    for (const auto& l : lines) {
        std::string trimmed = Trim(l);
        if (!pastMeta) {
            if (isAhkOrTxt) {
                // Skip # or ; metadata comment lines
                if (!trimmed.empty() && (trimmed[0] == '#' || trimmed[0] == ';')) {
                    std::string meta = Trim(trimmed.substr(1));
                    if (meta.substr(0, 6) == "title:" || meta.substr(0, 7) == "author:" ||
                        meta.substr(0, 11) == "instrument:" || meta.substr(0, 5) == "part:" ||
                        meta.substr(0, 4) == "bpm:")
                        continue; // skip this metadata line
                }
            } else {
                // .sng format: plain header lines
                if (trimmed.substr(0, 6) == "title:" || trimmed.substr(0, 7) == "author:" ||
                    trimmed.substr(0, 11) == "instrument:" || trimmed.substr(0, 5) == "part:")
                    continue;
            }
            pastMeta = true;
        }
        bodyLines.push_back(l);
    }

    // Write back: new metadata + remaining body
    std::ofstream outFile(filepath);
    if (!outFile.is_open()) return false;

    if (isAhkOrTxt) {
        if (!title.empty()) outFile << "# title: " << title << "\n";
        if (!author.empty()) outFile << "# author: " << author << "\n";
        if (!instrument.empty()) outFile << "# instrument: " << instrument << "\n";
        if (!part.empty()) outFile << "# part: " << part << "\n";
    } else {
        if (!title.empty()) outFile << "title: " << title << "\n";
        if (!author.empty()) outFile << "author: " << author << "\n";
        if (!instrument.empty()) outFile << "instrument: " << instrument << "\n";
        if (!part.empty()) outFile << "part: " << part << "\n";
    }

    for (const auto& l : bodyLines) {
        outFile << l << "\n";
    }

    return outFile.good();
}

Song LoadNotationFile(const std::string& filepath, int bpm) {
    std::ifstream file(filepath);
    if (!file.is_open()) return Song{};

    // Derive default title from filename
    std::filesystem::path p(filepath);
    std::string title = p.stem().string();
    std::replace(title.begin(), title.end(), '_', ' ');

    std::string author;
    std::string inst;
    std::string part;
    std::string notation;

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        // Parse metadata lines starting with #
        if (!trimmed.empty() && trimmed[0] == '#') {
            std::string meta = Trim(trimmed.substr(1));
            if (meta.substr(0, 6) == "title:")
                title = Trim(meta.substr(6));
            else if (meta.substr(0, 7) == "author:")
                author = Trim(meta.substr(7));
            else if (meta.substr(0, 11) == "instrument:")
                inst = Trim(meta.substr(11));
            else if (meta.substr(0, 5) == "part:")
                part = Trim(meta.substr(5));
            else if (meta.substr(0, 4) == "bpm:")
                try { bpm = std::stoi(Trim(meta.substr(4))); } catch (...) {}
            continue;
        }
        if (!notation.empty()) notation += "\n";
        notation += line;
    }

    Song song = ParseNotation(notation, title, bpm, inst);
    song.author = author;
    song.part = part;
    song.filepath = filepath;
    return song;
}

Song LoadAHKFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return Song{};

    // Derive default title from filename
    std::filesystem::path p(filepath);
    std::string title = p.stem().string();
    std::replace(title.begin(), title.end(), '_', ' ');

    std::string author;
    std::string inst;
    std::string part;

    Song song;
    // Use BPM=60 so 1 beat = 1000ms; Sleep values map directly to beats
    song.bpm = 60;

    // Pending actions accumulated between Sleep commands
    // Key codes: 1-8 = natural notes, 9-13 = sharps (F1-F5)
    // Special: 100 = octave up, 101 = octave down
    static const int kOctaveUp = 100;
    static const int kOctaveDown = 101;
    std::vector<int> pending;

    // Helper: flush pending actions into song events, with given sleep duration
    auto flushPending = [&](float durationBeats) {
        if (pending.empty()) {
            // Sleep with no preceding action = rest
            if (durationBeats > 0.0f) {
                NoteEvent rest;
                rest.type = EventType::Rest;
                rest.durationBeats = durationBeats;
                rest.targetOctave = Octave::Mid;
                song.events.push_back(rest);
            }
            return;
        }

        // Split pending into groups: consecutive notes form chords,
        // octave changes break the grouping
        std::vector<int> chordKeys;
        for (size_t i = 0; i < pending.size(); i++) {
            int k = pending[i];
            if (k >= 1 && k <= 13) {
                chordKeys.push_back(k);
            } else {
                // Flush any accumulated chord before the octave change
                if (!chordKeys.empty()) {
                    NoteEvent ev;
                    ev.type = (chordKeys.size() > 1) ? EventType::Chord : EventType::Note;
                    ev.keys = std::move(chordKeys);
                    ev.durationBeats = 0.0f;
                    ev.targetOctave = Octave::Mid;
                    song.events.push_back(ev);
                    chordKeys.clear();
                }
                // Emit octave change
                NoteEvent oc;
                oc.type = (k == kOctaveUp) ? EventType::OctaveUp : EventType::OctaveDown;
                oc.durationBeats = 0.0f;
                oc.targetOctave = Octave::Mid;
                song.events.push_back(oc);
            }
        }
        // Flush remaining chord/note
        if (!chordKeys.empty()) {
            NoteEvent ev;
            ev.type = (chordKeys.size() > 1) ? EventType::Chord : EventType::Note;
            ev.keys = std::move(chordKeys);
            ev.durationBeats = 0.0f;
            ev.targetOctave = Octave::Mid;
            song.events.push_back(ev);
        }

        // Apply duration to the last event in this group
        if (!song.events.empty()) {
            song.events.back().durationBeats = durationBeats;
        }

        pending.clear();
    };

    // Helper: parse a single brace group content like "Numpad7 down" or "F2" or "Numpad9"
    // Returns the internal key code, or -1 to skip (e.g. "up" events)
    auto parseBraceContent = [&](const std::string& content) -> int {
        std::string lower = content;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Skip "up" events — we only process key-down or bare events
        if (lower.size() >= 3 && lower.substr(lower.size() - 3) == " up") {
            return -1;
        }

        // Strip " down" suffix if present
        std::string cleaned = lower;
        if (cleaned.size() >= 5 && cleaned.substr(cleaned.size() - 5) == " down") {
            cleaned = cleaned.substr(0, cleaned.size() - 5);
        }

        // Handle Numpad keys
        if (cleaned.size() >= 7 && cleaned.substr(0, 6) == "numpad") {
            std::string numPart = cleaned.substr(6);
            if (numPart.size() == 1 && std::isdigit((unsigned char)numPart[0])) {
                int digit = numPart[0] - '0';
                if (digit == 9) return kOctaveUp;
                if (digit == 0) return kOctaveDown;
                return digit; // 1-8
            }
        }

        // Handle bare digit keys (e.g. {7} or {7 down})
        if (cleaned.size() == 1 && std::isdigit((unsigned char)cleaned[0])) {
            int digit = cleaned[0] - '0';
            if (digit == 9) return kOctaveUp;
            if (digit == 0) return kOctaveDown;
            return digit;
        }

        // Handle F-keys (sharps): F1=9, F2=10, F3=11, F4=12, F5=13
        if (cleaned.size() >= 2 && cleaned[0] == 'f' && std::isdigit((unsigned char)cleaned[1])) {
            int fNum = cleaned[1] - '0';
            if (fNum >= 1 && fNum <= 5) {
                return 8 + fNum; // F1→9, F2→10, F3→11, F4→12, F5→13
            }
        }

        return -1; // Unknown key, skip
    };

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty()) continue;

        // Parse # or ; metadata comment lines
        if (trimmed[0] == '#' || trimmed[0] == ';') {
            std::string meta = Trim(trimmed.substr(1));
            if (meta.size() >= 6 && meta.substr(0, 6) == "title:")
                title = Trim(meta.substr(6));
            else if (meta.size() >= 7 && meta.substr(0, 7) == "author:")
                author = Trim(meta.substr(7));
            else if (meta.size() >= 11 && meta.substr(0, 11) == "instrument:")
                inst = Trim(meta.substr(11));
            else if (meta.size() >= 5 && meta.substr(0, 5) == "part:")
                part = Trim(meta.substr(5));
            continue;
        }

        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Parse SendInput / Send lines — iterate ALL {…} brace groups
        if (lower.find("sendinput") != std::string::npos ||
            (lower.find("send") != std::string::npos && lower.find("sleep") == std::string::npos)) {
            size_t pos = 0;
            while (pos < trimmed.size()) {
                size_t braceStart = trimmed.find('{', pos);
                if (braceStart == std::string::npos) break;
                size_t braceEnd = trimmed.find('}', braceStart + 1);
                if (braceEnd == std::string::npos) break;

                std::string content = trimmed.substr(braceStart + 1, braceEnd - braceStart - 1);
                int key = parseBraceContent(content);
                if (key >= 0) {
                    pending.push_back(key);
                }
                pos = braceEnd + 1;
            }
            continue;
        }

        // Parse Sleep, N or Sleep N
        if (lower.find("sleep") != std::string::npos) {
            size_t sleepPos = lower.find("sleep");
            size_t numStart = sleepPos + 5;
            while (numStart < trimmed.size() &&
                   (trimmed[numStart] == ',' || trimmed[numStart] == ' ' || trimmed[numStart] == '\t'))
                numStart++;
            std::string numStr;
            while (numStart < trimmed.size() && std::isdigit((unsigned char)trimmed[numStart])) {
                numStr += trimmed[numStart];
                numStart++;
            }
            int sleepMs = 0;
            if (!numStr.empty()) {
                try { sleepMs = std::stoi(numStr); } catch (...) {}
            }

            float durationBeats = sleepMs / 1000.0f;
            flushPending(durationBeats);
            continue;
        }
    }

    // Flush any remaining pending actions at end of file
    flushPending(0.0f);

    song.title = title;
    song.author = author;
    song.instrument = inst;
    song.part = part;
    song.filepath = filepath;

    return song;
}

std::vector<Song> ScanMusicDirectory(const std::string& musicDir) {
    std::vector<Song> songs;

    try {
        if (!std::filesystem::exists(musicDir)) {
            std::filesystem::create_directories(musicDir);
            return songs;
        }

        // Scan all .ahk, .txt, and .sng files directly in music/
        for (const auto& entry : std::filesystem::directory_iterator(musicDir)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            Song song;
            if (ext == ".ahk") {
                song = LoadAHKFile(entry.path().string());
            } else if (ext == ".txt") {
                song = LoadNotationFile(entry.path().string());
            } else if (ext == ".sng") {
                song = LoadSongFile(entry.path().string());
            } else {
                continue;
            }

            if (song.IsValid()) {
                songs.push_back(std::move(song));
            }
        }
    } catch (...) {}

    // Sort by instrument, then by title
    std::sort(songs.begin(), songs.end(), [](const Song& a, const Song& b) {
        if (a.instrument != b.instrument) return a.instrument < b.instrument;
        return a.title < b.title;
    });

    return songs;
}

} // namespace Serenade
