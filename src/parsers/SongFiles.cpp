#include "SongParser.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace Serenade {

float Song::GetTotalDurationSeconds() const {
    if (cachedDuration >= 0.0f) return cachedDuration;
    if (bpm <= 0) { cachedDuration = 0.0f; return 0.0f; }
    float totalBeats = 0.0f;
    for (const auto& ev : events)
        totalBeats += ev.durationBeats;
    cachedDuration = totalBeats * 60.0f / static_cast<float>(bpm);
    return cachedDuration;
}

static std::string TrimSF(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool StartsWithCISF(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

Song LoadSongFile(const std::string& filepath) {
    Song song;
    song.bpm = 120;

    std::ifstream file(filepath);
    if (!file.is_open()) return song;

    std::string line, notation;
    bool inNotation = false;

    while (std::getline(file, line)) {
        std::string trimmed = TrimSF(line);
        if (trimmed.empty()) continue;

        if (trimmed.substr(0, 6)  == "title:")       { song.title      = TrimSF(trimmed.substr(6));  continue; }
        if (trimmed.substr(0, 7)  == "author:")      { song.author     = TrimSF(trimmed.substr(7));  continue; }
        if (trimmed.substr(0, 5)  == "part:")        { song.part       = TrimSF(trimmed.substr(5));  continue; }
        if (trimmed.substr(0, 11) == "instrument:")  { song.instrument = TrimSF(trimmed.substr(11)); continue; }
        if (trimmed.substr(0, 4)  == "bpm:") {
            try { song.bpm = std::stoi(TrimSF(trimmed.substr(4))); } catch (...) { song.bpm = 120; }
            continue;
        }
        if (trimmed == "---") { inNotation = true; continue; }

        if (inNotation) {
            if (!notation.empty()) notation += "\n";
            notation += line;
        }
    }

    if (!notation.empty()) {
        Song parsed = ParseNotation(notation, song.title, song.bpm, song.instrument);
        song.events = std::move(parsed.events);
    }

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
    if (!song.author.empty())     file << "author: "     << song.author     << "\n";
    if (!song.instrument.empty()) file << "instrument: " << song.instrument << "\n";
    if (!song.part.empty())       file << "part: "       << song.part       << "\n";
    file << "bpm: " << song.bpm << "\n---\n";

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
                    if (curOctave == Octave::Low)  file << "]";
                    else if (curOctave == Octave::High) file << ")";
                }
                curOctave = ev.targetOctave;
                break;
            case EventType::Note:
                if (!ev.keys.empty()) {
                    file << ev.keys[0];
                    if      (ev.durationBeats >= 1.0f) file << "  ";
                    else if (ev.durationBeats >= 0.5f) file << " ";
                }
                break;
            case EventType::Chord:
                for (size_t i = 0; i < ev.keys.size(); i++) {
                    if (i > 0) file << "/";
                    file << ev.keys[i];
                }
                if      (ev.durationBeats >= 1.0f) file << "  ";
                else if (ev.durationBeats >= 0.5f) file << " ";
                break;
            case EventType::Rest:
                if      (ev.durationBeats >= 1.0f) file << "- ";
                else if (ev.durationBeats >= 0.5f) file << "-";
                else                               file << ".";
                break;
            default: break;
        }
    }
    file << "\n";
    return true;
}

bool UpdateSongMetadata(const std::string& filepath, const std::string& title,
                        const std::string& author, const std::string& instrument,
                        const std::string& part) {
    std::ifstream inFile(filepath);
    if (!inFile.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line))
        lines.push_back(line);
    inFile.close();

    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool isAhkOrTxt = (ext == ".ahk" || ext == ".txt");

    std::vector<std::string> bodyLines;
    bool pastMeta = false;
    for (const auto& l : lines) {
        std::string trimmed = TrimSF(l);
        if (!pastMeta) {
            if (isAhkOrTxt) {
                if (!trimmed.empty() && (trimmed[0] == '#' || trimmed[0] == ';')) {
                    std::string meta = TrimSF(trimmed.substr(1));
                    if (meta.substr(0, 6) == "title:" || meta.substr(0, 7) == "author:" ||
                        meta.substr(0, 11) == "instrument:" || meta.substr(0, 5) == "part:" ||
                        meta.substr(0, 4) == "bpm:")
                        continue;
                }
            } else {
                if (trimmed.substr(0, 6) == "title:" || trimmed.substr(0, 7) == "author:" ||
                    trimmed.substr(0, 11) == "instrument:" || trimmed.substr(0, 5) == "part:")
                    continue;
            }
            pastMeta = true;
        }
        bodyLines.push_back(l);
    }

    std::ofstream outFile(filepath);
    if (!outFile.is_open()) return false;

    if (isAhkOrTxt) {
        if (!title.empty())      outFile << "# title: "      << title      << "\n";
        if (!author.empty())     outFile << "# author: "     << author     << "\n";
        if (!instrument.empty()) outFile << "# instrument: " << instrument << "\n";
        if (!part.empty())       outFile << "# part: "       << part       << "\n";
    } else {
        if (!title.empty())      outFile << "title: "      << title      << "\n";
        if (!author.empty())     outFile << "author: "     << author     << "\n";
        if (!instrument.empty()) outFile << "instrument: " << instrument << "\n";
        if (!part.empty())       outFile << "part: "       << part       << "\n";
    }

    for (const auto& l : bodyLines)
        outFile << l << "\n";

    return outFile.good();
}

Song LoadNotationFile(const std::string& filepath, int bpm) {
    std::ifstream file(filepath);
    if (!file.is_open()) return Song{};

    std::filesystem::path p(filepath);
    std::string title = p.stem().string();
    std::replace(title.begin(), title.end(), '_', ' ');

    std::string author, inst, part, notation;

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = TrimSF(line);
        if (!trimmed.empty() && trimmed[0] == '#') {
            std::string meta = TrimSF(trimmed.substr(1));
            if      (meta.substr(0, 6)  == "title:")      title  = TrimSF(meta.substr(6));
            else if (meta.substr(0, 7)  == "author:")     author = TrimSF(meta.substr(7));
            else if (meta.substr(0, 11) == "instrument:") inst   = TrimSF(meta.substr(11));
            else if (meta.substr(0, 5)  == "part:")       part   = TrimSF(meta.substr(5));
            else if (meta.substr(0, 4)  == "bpm:")
                try { bpm = std::stoi(TrimSF(meta.substr(4))); } catch (...) {}
            continue;
        }
        if (!notation.empty()) notation += "\n";
        notation += line;
    }

    Song song = ParseNotation(notation, title, bpm, inst);
    song.author   = author;
    song.part     = part;
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

        for (const auto& entry : std::filesystem::directory_iterator(musicDir)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            Song song;
            if      (ext == ".ahk") song = LoadAHKFile(entry.path().string());
            else if (ext == ".txt") song = LoadNotationFile(entry.path().string());
            else if (ext == ".sng") song = LoadSongFile(entry.path().string());
            else continue;

            if (song.IsValid()) songs.push_back(std::move(song));
        }
    } catch (...) {}

    std::sort(songs.begin(), songs.end(), [](const Song& a, const Song& b) {
        if (a.instrument != b.instrument) return a.instrument < b.instrument;
        return a.title < b.title;
    });

    return songs;
}

} // namespace Serenade
