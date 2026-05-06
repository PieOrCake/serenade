#include "SongParser.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Serenade {

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool StartsWithCI(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

static int IsZeroWidth(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return 0;
    unsigned char b0 = s[i], b1 = s[i+1], b2 = s[i+2];
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8B) return 3; // U+200B
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8C) return 3; // U+200C
    if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x8D) return 3; // U+200D
    if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3; // U+FEFF BOM
    return 0;
}

static int IsCircledDigit(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return 0;
    unsigned char b0 = s[i], b1 = s[i+1], b2 = s[i+2];
    if (b0 == 0xE2 && b1 == 0x91 && b2 >= 0xA0 && b2 <= 0xA7)
        return (b2 - 0xA0) + 1;
    return 0;
}

static bool IsFullRest(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return false;
    return (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i+1] == 0x94 &&
           (unsigned char)s[i+2] == 0x81;
}

static bool IsMiddleDot(const std::string& s, size_t i) {
    if (i + 1 >= s.size()) return false;
    return (unsigned char)s[i] == 0xC2 && (unsigned char)s[i+1] == 0xB7;
}

static bool IsEnDash(const std::string& s, size_t i) {
    if (i + 2 >= s.size()) return false;
    return (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i+1] == 0x80 &&
           (unsigned char)s[i+2] == 0x93;
}

static std::string PreprocessNotation(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        int zw = IsZeroWidth(raw, i);
        if (zw > 0) { i += zw; continue; }

        int cd = IsCircledDigit(raw, i);
        if (cd > 0) { out += ('0' + cd); i += 3; continue; }

        // Strip section labels: single uppercase letter followed by |
        if (i + 1 < raw.size() && std::isupper((unsigned char)raw[i]) && raw[i+1] == '|') {
            i += 2; continue;
        }

        out += raw[i];
        i++;
    }
    return out;
}

Song ParseNotation(const std::string& notation, const std::string& title, int bpm,
                   const std::string& instrument) {
    Song song;
    song.title      = title;
    song.bpm        = bpm;
    song.instrument = instrument;

    std::string firstPart = ExtractFirstPart(notation);
    std::string input     = PreprocessNotation(firstPart);

    // Parse optional inline BPM directive
    {
        std::string upper = input;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto bpmPos = upper.find("BPM");
        if (bpmPos != std::string::npos) {
            size_t j = bpmPos + 3;
            while (j < input.size() && (input[j] == ' ' || input[j] == '=' || input[j] == ':'))
                j++;
            std::string numStr;
            while (j < input.size() && std::isdigit((unsigned char)input[j]))
                numStr += input[j++];
            if (!numStr.empty())
                try { song.bpm = std::stoi(numStr); } catch (...) {}
            input.erase(bpmPos, j - bpmPos);
        }
    }

    Octave currentOctave        = Octave::Mid;
    Octave octaveBeforeBracket  = Octave::Mid;
    bool inLowBracket  = false;
    bool inHighBracket = false;

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = (unsigned char)input[i];

        if (c == '|') { i++; continue; }

        if (IsFullRest(input, i)) {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 1.0f;
            rest.targetOctave  = currentOctave;
            song.events.push_back(rest);
            i += 3; continue;
        }

        if (IsEnDash(input, i)) {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.5f;
            rest.targetOctave  = currentOctave;
            song.events.push_back(rest);
            i += 3; continue;
        }

        if (c == '-') {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.5f;
            rest.targetOctave  = currentOctave;
            song.events.push_back(rest);
            i++; continue;
        }

        if (c == '[') {
            octaveBeforeBracket = currentOctave;
            inLowBracket = true;
            if (currentOctave != Octave::Low) {
                NoteEvent oc; oc.type = EventType::OctaveSet;
                oc.targetOctave = Octave::Low; oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = Octave::Low;
            }
            i++; continue;
        }

        if (c == ']') {
            inLowBracket = false;
            if (currentOctave != octaveBeforeBracket) {
                NoteEvent oc; oc.type = EventType::OctaveSet;
                oc.targetOctave = octaveBeforeBracket; oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = octaveBeforeBracket;
            }
            i++; continue;
        }

        if (c == '(') {
            octaveBeforeBracket = currentOctave;
            inHighBracket = true;
            if (currentOctave != Octave::High) {
                NoteEvent oc; oc.type = EventType::OctaveSet;
                oc.targetOctave = Octave::High; oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = Octave::High;
            }
            i++; continue;
        }

        if (c == ')') {
            inHighBracket = false;
            if (currentOctave != octaveBeforeBracket) {
                NoteEvent oc; oc.type = EventType::OctaveSet;
                oc.targetOctave = octaveBeforeBracket; oc.durationBeats = 0.0f;
                song.events.push_back(oc);
                currentOctave = octaveBeforeBracket;
            }
            i++; continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
        if (c == ',') { i++; continue; }

        if (c == '.') {
            NoteEvent rest;
            rest.type = EventType::Rest;
            rest.durationBeats = 0.25f;
            rest.targetOctave  = currentOctave;
            song.events.push_back(rest);
            i++; continue;
        }

        if (c >= '1' && c <= '8') {
            std::vector<int> noteKeys;
            noteKeys.push_back(c - '0');
            i++;

            while (i + 1 < input.size() && input[i] == '/') {
                unsigned char nc = (unsigned char)input[i + 1];
                if (nc >= '1' && nc <= '8') { noteKeys.push_back(nc - '0'); i += 2; }
                else break;
            }

            bool dotted = false;
            if (IsMiddleDot(input, i)) { dotted = true; i += 2; }

            int spaceCount = 0;
            size_t j = i;
            while (j < input.size()) {
                unsigned char jc = (unsigned char)input[j];
                if (jc == ' ')                                         { spaceCount++; j++; }
                else if (jc == '(' || jc == ')' || jc == '[' || jc == ']') { j++; }
                else break;
            }

            NoteEvent ev;
            ev.targetOctave = currentOctave;
            ev.type  = (noteKeys.size() > 1) ? EventType::Chord : EventType::Note;
            ev.keys  = std::move(noteKeys);

            if      (spaceCount == 0) ev.durationBeats = 0.25f;
            else if (spaceCount == 1) ev.durationBeats = 0.5f;
            else                      ev.durationBeats = 1.0f;

            if (dotted) ev.durationBeats *= 1.5f;

            song.events.push_back(ev);
            continue;
        }

        if (std::isdigit(c)) { i++; continue; }

        if (c >= 0x80) {
            if (c >= 0xF0 && i + 3 < input.size()) { i += 4; continue; }
            if (c >= 0xE0 && i + 2 < input.size()) { i += 3; continue; }
            if (c >= 0xC0 && i + 1 < input.size()) { i += 2; continue; }
            i++; continue;
        }

        i++;
    }

    return song;
}

std::string ExtractFirstPart(const std::string& notation) {
    std::vector<size_t> partStarts;
    std::string lower = notation;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const char* partLabels[] = {
        "melody", "piano melody", "piano bg", "bass", "harp", "lute", "bell",
        "flute", "horn", "minstrel", "verdarach", "guitar", "drum",
        "lead", "rhythm", "harmony", "accompaniment", nullptr
    };

    for (size_t pos = 0; pos < lower.size(); ) {
        bool found = false;
        for (int p = 0; partLabels[p] != nullptr; p++) {
            size_t len = strlen(partLabels[p]);
            if (pos + len < lower.size() && lower.substr(pos, len) == partLabels[p]) {
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
        if (partStarts.size() == 1) {
            size_t colonPos = notation.find(':', partStarts[0]);
            if (colonPos != std::string::npos)
                return notation.substr(colonPos + 1);
        }
        return notation;
    }

    size_t firstColon = notation.find(':', partStarts[0]);
    if (firstColon == std::string::npos) return notation;

    size_t endPos = partStarts[1];
    return notation.substr(firstColon + 1, endPos - (firstColon + 1));
}

} // namespace Serenade
