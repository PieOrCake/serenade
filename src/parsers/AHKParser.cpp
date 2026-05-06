#include "SongParser.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace Serenade {

static std::string TrimAHK(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

Song LoadAHKFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return Song{};

    std::filesystem::path p(filepath);
    std::string title = p.stem().string();
    std::replace(title.begin(), title.end(), '_', ' ');

    std::string author, inst, part;

    Song song;
    song.bpm = 60; // 1 beat = 1000ms so Sleep values map directly to beats

    static const int kOctaveUp   = 100;
    static const int kOctaveDown = 101;
    std::vector<int> pending;

    auto flushPending = [&](float durationBeats) {
        if (pending.empty()) {
            if (durationBeats > 0.0f) {
                NoteEvent rest;
                rest.type          = EventType::Rest;
                rest.durationBeats = durationBeats;
                rest.targetOctave  = Octave::Mid;
                song.events.push_back(rest);
            }
            return;
        }

        std::vector<int> chordKeys;
        for (size_t i = 0; i < pending.size(); i++) {
            int k = pending[i];
            if (k >= 1 && k <= 13) {
                chordKeys.push_back(k);
            } else {
                if (!chordKeys.empty()) {
                    NoteEvent ev;
                    ev.type          = (chordKeys.size() > 1) ? EventType::Chord : EventType::Note;
                    ev.keys          = std::move(chordKeys);
                    ev.durationBeats = 0.0f;
                    ev.targetOctave  = Octave::Mid;
                    song.events.push_back(ev);
                    chordKeys.clear();
                }
                NoteEvent oc;
                oc.type          = (k == kOctaveUp) ? EventType::OctaveUp : EventType::OctaveDown;
                oc.durationBeats = 0.0f;
                oc.targetOctave  = Octave::Mid;
                song.events.push_back(oc);
            }
        }
        if (!chordKeys.empty()) {
            NoteEvent ev;
            ev.type          = (chordKeys.size() > 1) ? EventType::Chord : EventType::Note;
            ev.keys          = std::move(chordKeys);
            ev.durationBeats = 0.0f;
            ev.targetOctave  = Octave::Mid;
            song.events.push_back(ev);
        }

        if (!song.events.empty())
            song.events.back().durationBeats = durationBeats;

        pending.clear();
    };

    auto parseBraceContent = [&](const std::string& content) -> int {
        std::string lower = content;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.size() >= 3 && lower.substr(lower.size() - 3) == " up")
            return -1;

        std::string cleaned = lower;
        if (cleaned.size() >= 5 && cleaned.substr(cleaned.size() - 5) == " down")
            cleaned = cleaned.substr(0, cleaned.size() - 5);

        if (cleaned.size() >= 7 && cleaned.substr(0, 6) == "numpad") {
            std::string numPart = cleaned.substr(6);
            if (numPart.size() == 1 && std::isdigit((unsigned char)numPart[0])) {
                int digit = numPart[0] - '0';
                if (digit == 9) return kOctaveDown;
                if (digit == 0) return kOctaveUp;
                return digit;
            }
        }

        if (cleaned.size() == 1 && std::isdigit((unsigned char)cleaned[0])) {
            int digit = cleaned[0] - '0';
            if (digit == 9) return kOctaveDown;
            if (digit == 0) return kOctaveUp;
            return digit;
        }

        if (cleaned.size() >= 2 && cleaned[0] == 'f' && std::isdigit((unsigned char)cleaned[1])) {
            int fNum = cleaned[1] - '0';
            if (fNum >= 1 && fNum <= 5) return 8 + fNum; // F1→9 … F5→13
        }

        return -1;
    };

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = TrimAHK(line);
        if (trimmed.empty()) continue;

        if (trimmed[0] == '#' || trimmed[0] == ';') {
            std::string meta = TrimAHK(trimmed.substr(1));
            if (meta.size() >= 6  && meta.substr(0, 6)  == "title:")      title  = TrimAHK(meta.substr(6));
            else if (meta.size() >= 7  && meta.substr(0, 7)  == "author:")     author = TrimAHK(meta.substr(7));
            else if (meta.size() >= 11 && meta.substr(0, 11) == "instrument:") inst   = TrimAHK(meta.substr(11));
            else if (meta.size() >= 5  && meta.substr(0, 5)  == "part:")       part   = TrimAHK(meta.substr(5));
            continue;
        }

        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

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
                if (key >= 0) pending.push_back(key);
                pos = braceEnd + 1;
            }
            continue;
        }

        if (lower.find("sleep") != std::string::npos) {
            size_t sleepPos  = lower.find("sleep");
            size_t numStart  = sleepPos + 5;
            while (numStart < trimmed.size() &&
                   (trimmed[numStart] == ',' || trimmed[numStart] == ' ' || trimmed[numStart] == '\t'))
                numStart++;
            std::string numStr;
            while (numStart < trimmed.size() && std::isdigit((unsigned char)trimmed[numStart]))
                numStr += trimmed[numStart++];
            int sleepMs = 0;
            if (!numStr.empty()) try { sleepMs = std::stoi(numStr); } catch (...) {}
            flushPending(sleepMs / 1000.0f);
            continue;
        }
    }

    flushPending(0.0f);

    song.title      = title;
    song.author     = author;
    song.instrument = inst;
    song.part       = part;
    song.filepath   = filepath;
    return song;
}

} // namespace Serenade
