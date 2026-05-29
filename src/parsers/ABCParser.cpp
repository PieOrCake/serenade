// ABC notation parser — pitch mapping, key signatures, tempo, durations, and song assembly.
#include "SongParser.h"
#include "parsers/ABCParser_internal.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>

namespace Serenade {
namespace abc {

int LetterPitchClass(char letter) {
    switch (std::toupper((unsigned char)letter)) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
    }
    return -1;
}

KeyPos SemitoneToKey(int abcSemitone) {
    // GW2 octave indices: 0-11 Low, 12-23 Mid, 24-35 High; 36 = top C (High key 8)
    static const int kTopC = 36;
    int gw2 = abcSemitone + 12;       // shift so ABC uppercase C lands on GW2 Mid C (index 12)
    while (gw2 < 0)    gw2 += 12;
    while (gw2 > kTopC) gw2 -= 12;
    if (gw2 == kTopC) return { Octave::High, 8 };
    static const int keyTable[12] = {1, 9, 2, 10, 3, 4, 11, 5, 12, 6, 13, 7};
    return { static_cast<Octave>(gw2 / 12), keyTable[gw2 % 12] };
}

std::map<char, int> ParseKeySignature(const std::string& keyField) {
    std::map<char, int> acc;
    size_t i = 0;
    while (i < keyField.size() && std::isspace((unsigned char)keyField[i])) i++;
    if (i >= keyField.size()) return acc;

    char tonic = std::toupper((unsigned char)keyField[i]);
    if (tonic < 'A' || tonic > 'G') return acc;  // "none", "HP", etc.
    i++;

    int fifths;
    switch (tonic) {
        case 'F': fifths = -1; break;
        case 'C': fifths =  0; break;
        case 'G': fifths =  1; break;
        case 'D': fifths =  2; break;
        case 'A': fifths =  3; break;
        case 'E': fifths =  4; break;
        case 'B': fifths =  5; break;
        default:  fifths =  0; break;
    }

    if (i < keyField.size() && keyField[i] == '#') { fifths += 7; i++; }
    else if (i < keyField.size() && keyField[i] == 'b') { fifths -= 7; i++; }

    std::string mode;
    for (; i < keyField.size(); i++)
        if (std::isalpha((unsigned char)keyField[i]))
            mode += (char)std::tolower((unsigned char)keyField[i]);
    std::string m3 = mode.substr(0, 3);

    if (m3 == "maj" || m3 == "ion" || mode.empty())        { /* +0 */ }
    else if (m3 == "mix")                                  fifths -= 1;
    else if (m3 == "min" || m3 == "aeo" || mode == "m")    fifths -= 3;
    else if (m3 == "dor")                                  fifths -= 2;
    else if (m3 == "phr")                                  fifths -= 4;
    else if (m3 == "lyd")                                  fifths += 1;
    else if (m3 == "loc")                                  fifths -= 5;
    else if (!mode.empty() && mode[0] == 'm')              fifths -= 3;  // fallback minor

    static const char sharpOrder[] = "FCGDAEB";
    static const char flatOrder[]  = "BEADGCF";
    if (fifths > 0)
        for (int k = 0; k < fifths && k < 7; k++) acc[sharpOrder[k]] = +1;
    else if (fifths < 0)
        for (int k = 0; k < -fifths && k < 7; k++) acc[flatOrder[k]] = -1;
    return acc;
}

int ParseTempoToQuarterBPM(const std::string& tempoField) {
    std::string s;
    bool inQuote = false;
    for (char c : tempoField) {
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote) s += c;
    }

    double beatUnit = 0.25;  // assume quarter-note beat
    double bpm = 0.0;
    size_t eq = s.find('=');
    if (eq != std::string::npos) {
        std::string left  = s.substr(0, eq);
        std::string right = s.substr(eq + 1);
        size_t slash = left.find('/');
        if (slash != std::string::npos) {
            try {
                double num = std::stod(left.substr(0, slash));
                double den = std::stod(left.substr(slash + 1));
                if (den != 0) beatUnit = num / den;
            } catch (...) {}
        }
        try { bpm = std::stod(right); } catch (...) {}
    } else {
        try { bpm = std::stod(s); } catch (...) {}
    }

    if (bpm <= 0) return 120;
    long q = std::lround(bpm * (beatUnit / 0.25));
    return q > 0 ? (int)q : 120;
}

double ParseNoteLength(const std::string& lengthField) {
    std::string s;
    for (char c : lengthField)
        if (!std::isspace((unsigned char)c)) s += c;
    size_t slash = s.find('/');
    try {
        if (slash != std::string::npos) {
            double num = std::stod(s.substr(0, slash));
            double den = std::stod(s.substr(slash + 1));
            if (den != 0) return num / den;
        } else if (!s.empty()) {
            return std::stod(s);
        }
    } catch (...) {}
    return 0.0;
}

} // namespace abc

// ---------------------------------------------------------------------------
// Tokenizer helpers (anonymous namespace, inside Serenade)
// ---------------------------------------------------------------------------
namespace {

using abc::KeyPos;
using abc::SemitoneToKey;
using abc::LetterPitchClass;

// Read a length token (e.g. "2", "/2", "3/2", "/") starting at i; advances i.
double ReadLength(const std::string& s, size_t& i) {
    double num = 1.0, den = 1.0;
    std::string digits;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) digits += s[i++];
    if (!digits.empty()) { try { num = std::stod(digits); } catch (...) {} }
    while (i < s.size() && s[i] == '/') {
        i++;
        den *= 2.0;  // each slash implies division by 2
        std::string d2;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) d2 += s[i++];
        if (!d2.empty()) { try { den = den / 2.0 * std::stod(d2); } catch (...) {} }
    }
    return (den != 0.0) ? num / den : num;
}

// Parse a single pitch (accidentals + letter + octave marks) at i; advances i
// past the pitch (NOT its length). Returns false if not a pitch.
bool ParsePitch(const std::string& s, size_t& i, std::map<char, int>& barAcc,
                const std::map<char, int>& keyAcc, int& semitone) {
    int accidental = 0;
    bool hasAcc = false;
    while (i < s.size() && (s[i] == '^' || s[i] == '_' || s[i] == '=')) {
        hasAcc = true;
        if (s[i] == '^') accidental += 1;
        else if (s[i] == '_') accidental -= 1;
        else accidental = 0;  // '=' natural
        i++;
    }
    if (i >= s.size()) return false;
    char letter = s[i];
    int pc = LetterPitchClass(letter);
    if (pc < 0) return false;
    i++;
    int octShift = std::isupper((unsigned char)letter) ? 0 : 12;
    while (i < s.size() && (s[i] == ',' || s[i] == '\'')) {
        if (s[i] == ',') octShift -= 12; else octShift += 12;
        i++;
    }
    char up = (char)std::toupper((unsigned char)letter);
    int acc;
    if (hasAcc) { acc = accidental; barAcc[up] = accidental; }
    else if (barAcc.count(up)) acc = barAcc[up];
    else { auto it = keyAcc.find(up); acc = (it != keyAcc.end()) ? it->second : 0; }
    semitone = pc + acc + octShift;
    return true;
}

// Parse one voice's concatenated body text into note events.
void ParseVoiceBody(const std::string& body, const std::map<char, int>& keyAcc,
                    double defaultLen, std::vector<NoteEvent>& events) {
    Octave currentOctave = Octave::Mid;
    std::map<char, int> barAcc;
    bool tiePending = false;
    int  prevNoteSemitone = INT_MIN;
    size_t prevNoteIdx = SIZE_MAX;
    double brokenNextFactor = 1.0;

    auto ensureOctave = [&](Octave o) {
        if (o != currentOctave) {
            NoteEvent oc;
            oc.type = EventType::OctaveSet;
            oc.targetOctave = o;
            oc.durationBeats = 0.0f;
            events.push_back(oc);
            currentOctave = o;
        }
    };
    auto applyBrokenToPrev = [&](float factor) {
        for (size_t k = events.size(); k-- > 0; ) {
            EventType t = events[k].type;
            if (t == EventType::Note || t == EventType::Chord || t == EventType::Rest) {
                events[k].durationBeats *= factor;
                break;
            }
        }
    };

    size_t i = 0, n = body.size();
    while (i < n) {
        char c = body[i];
        if (std::isspace((unsigned char)c)) { i++; continue; }
        if (c == '%') { while (i < n && body[i] != '\n') i++; continue; }
        if (c == '!') { i++; while (i < n && body[i] != '!') i++; if (i < n) i++; continue; }
        if (c == '+') { i++; while (i < n && body[i] != '+') i++; if (i < n) i++; continue; }
        if (c == '"') { i++; while (i < n && body[i] != '"') i++; if (i < n) i++; continue; }
        if (c == '{') { while (i < n && body[i] != '}') i++; if (i < n) i++; continue; }
        if (c == '\\') { i++; continue; }

        // inline field [X:...] (but NOT a chord like [CEG])
        if (c == '[' && i + 2 < n && std::isalpha((unsigned char)body[i + 1]) && body[i + 2] == ':') {
            while (i < n && body[i] != ']') i++;
            if (i < n) i++;
            continue;
        }

        // chord: [CEG] with optional length after ]
        if (c == '[') {
            size_t j = i + 1;
            std::vector<int> semis;
            while (j < n && body[j] != ']') {
                if (std::isspace((unsigned char)body[j])) { j++; continue; }
                int semi;
                if (ParsePitch(body, j, barAcc, keyAcc, semi)) semis.push_back(semi);
                else j++;  // skip stray char inside chord
            }
            if (j < n) j++;  // consume ']'
            i = j;
            double mult = ReadLength(body, i);
            float beats = (float)(mult * defaultLen * 4.0 * brokenNextFactor);
            brokenNextFactor = 1.0;
            if (semis.empty()) continue;

            // Octave is a global shift, so anchor the chord on its lowest note.
            int lowest = semis[0];
            for (int sv : semis) if (sv < lowest) lowest = sv;
            ensureOctave(SemitoneToKey(lowest).octave);

            std::vector<int> keys;
            for (int sv : semis) keys.push_back(SemitoneToKey(sv).key);

            NoteEvent ev;
            ev.type = (keys.size() > 1) ? EventType::Chord : EventType::Note;
            ev.keys = std::move(keys);
            ev.durationBeats = beats;
            ev.targetOctave = currentOctave;
            events.push_back(ev);
            prevNoteIdx = events.size() - 1;
            prevNoteSemitone = INT_MIN;  // chords don't tie
            tiePending = false;
            continue;
        }

        // bar lines / repeats reset in-bar accidentals
        if (c == '|' || c == ':') {
            barAcc.clear();
            i++;
            while (i < n && (body[i] == '|' || body[i] == ':' || body[i] == ']' || body[i] == '[')) i++;
            continue;
        }

        // tuplet "(3" etc. -> skip spec, play notes at face value (timing approximated)
        if (c == '(') {
            if (i + 1 < n && std::isdigit((unsigned char)body[i + 1])) {
                i++;  // skip '('
                while (i < n && (std::isdigit((unsigned char)body[i]) || body[i] == ':')) i++;
                continue;
            }
            i++; continue;  // slur open
        }
        if (c == ')') { i++; continue; }  // slur close

        if (c == '>') { applyBrokenToPrev(1.5f); brokenNextFactor = 0.5; i++; continue; }
        if (c == '<') { applyBrokenToPrev(0.5f); brokenNextFactor = 1.5; i++; continue; }

        if (c == '-') {  // tie previous note to next same-pitch note
            tiePending = (prevNoteIdx != SIZE_MAX);
            i++; continue;
        }

        // rests
        if (c == 'z' || c == 'x' || c == 'Z' || c == 'X') {
            i++;
            double mult = ReadLength(body, i);
            float beats = (float)(mult * defaultLen * 4.0 * brokenNextFactor);
            brokenNextFactor = 1.0;
            NoteEvent ev;
            ev.type = EventType::Rest;
            ev.durationBeats = beats;
            ev.targetOctave = currentOctave;
            events.push_back(ev);
            tiePending = false;
            continue;
        }

        // pitch (note)
        if (c == '^' || c == '_' || c == '=' || LetterPitchClass(c) >= 0) {
            int semitone;
            if (!ParsePitch(body, i, barAcc, keyAcc, semitone)) { i++; continue; }
            double mult = ReadLength(body, i);
            float beats = (float)(mult * defaultLen * 4.0 * brokenNextFactor);
            brokenNextFactor = 1.0;

            KeyPos kp = SemitoneToKey(semitone);
            if (tiePending && semitone == prevNoteSemitone && prevNoteIdx < events.size()
                && events[prevNoteIdx].type == EventType::Note) {
                events[prevNoteIdx].durationBeats += beats;
                tiePending = false;
            } else {
                ensureOctave(kp.octave);
                NoteEvent ev;
                ev.type = EventType::Note;
                ev.keys = { kp.key };
                ev.durationBeats = beats;
                ev.targetOctave = kp.octave;
                events.push_back(ev);
                prevNoteIdx = events.size() - 1;
                prevNoteSemitone = semitone;
                tiePending = false;
            }
            continue;
        }

        i++;  // unknown token
    }
}

} // anonymous namespace (tokenizer helpers)

// ---------------------------------------------------------------------------
// Header/assembly helpers + ParseABC + LoadABCFile
// ---------------------------------------------------------------------------
namespace {

std::string TrimABC(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

double ParseMeterValue(const std::string& m) {
    std::string s;
    for (char c : m) if (!std::isspace((unsigned char)c)) s += c;
    if (s == "C" || s == "C|") return 1.0;
    size_t slash = s.find('/');
    if (slash != std::string::npos) {
        try {
            double num = std::stod(s.substr(0, slash));
            double den = std::stod(s.substr(slash + 1));
            if (den != 0) return num / den;
        } catch (...) {}
    }
    return 1.0;
}

// Accumulates one tune's header + per-voice body, then emits Songs.
struct TuneBuilder {
    std::string title, author, qField, lField, mField, keyField;
    bool inBody = false;
    bool sawX = false;
    std::vector<std::string> voiceOrder;             // first-seen order
    std::map<std::string, std::string> voiceBodies;  // id -> concatenated body

    std::string& voice(const std::string& id) {
        if (!voiceBodies.count(id)) voiceOrder.push_back(id);
        return voiceBodies[id];
    }

    void emit(std::vector<Song>& out, const std::string& filepath) {
        if (voiceBodies.empty()) return;

        double defaultLen;
        if (!lField.empty()) defaultLen = abc::ParseNoteLength(lField);
        else { double meter = mField.empty() ? 1.0 : ParseMeterValue(mField);
               defaultLen = (meter >= 0.75) ? 0.125 : 0.0625; }
        if (defaultLen <= 0) defaultLen = 0.125;

        int bpm = qField.empty() ? 120 : abc::ParseTempoToQuarterBPM(qField);
        auto keyAcc = abc::ParseKeySignature(keyField);
        bool multiVoice = voiceOrder.size() > 1;

        for (const auto& id : voiceOrder) {
            Song song;
            song.title = title;
            song.author = author;
            song.bpm = bpm;
            song.instrument = "";
            song.part = multiVoice ? id : "";
            song.filepath = filepath;
            ParseVoiceBody(voiceBodies[id], keyAcc, defaultLen, song.events);
            if (song.IsValid()) out.push_back(std::move(song));
        }
    }
};

} // anonymous namespace (header/assembly helpers)

std::vector<Song> ParseABC(const std::string& content, const std::string& filepath) {
    std::vector<Song> out;

    std::vector<std::string> lines;
    {
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    // File-level defaults: Q/L/M/K fields that appear before the first X: line.
    struct FileDefaults { std::string qField, lField, mField, keyField; };
    FileDefaults fileDefaults;
    bool defaultsCaptured = false;

    TuneBuilder tune;
    std::string currentVoice = "1";

    auto isFieldLine = [](const std::string& t) {
        return t.size() >= 2 && std::isalpha((unsigned char)t[0]) && t[1] == ':';
    };

    for (const auto& raw : lines) {
        std::string t = TrimABC(raw);
        if (t.empty()) continue;

        if (t.size() >= 2 && (t[0] == 'X' || t[0] == 'x') && t[1] == ':') {
            if (!tune.sawX && !defaultsCaptured) {
                // The builder accumulated pre-X: fields — save them as defaults.
                fileDefaults = { tune.qField, tune.lField, tune.mField, tune.keyField };
                defaultsCaptured = true;
            } else {
                tune.emit(out, filepath);
            }
            tune = TuneBuilder();
            tune.qField    = fileDefaults.qField;
            tune.lField    = fileDefaults.lField;
            tune.mField    = fileDefaults.mField;
            tune.keyField  = fileDefaults.keyField;
            tune.sawX = true;
            currentVoice = "1";
            continue;
        }

        if (!tune.inBody) {
            if (isFieldLine(t)) {
                char f = (char)std::toupper((unsigned char)t[0]);
                std::string v = TrimABC(t.substr(2));
                switch (f) {
                    case 'T': if (tune.title.empty()) tune.title = v; break;
                    case 'C': if (tune.author.empty()) tune.author = v; break;
                    case 'Q': tune.qField = v; break;
                    case 'L': tune.lField = v; break;
                    case 'M': tune.mField = v; break;
                    case 'K': tune.keyField = v; tune.inBody = true; break;
                    default: break;
                }
                continue;
            }
            continue;
        }

        // Body region.
        if ((t[0] == 'V' || t[0] == 'v') && t.size() >= 2 && t[1] == ':') {
            std::string rest = TrimABC(t.substr(2));
            std::string id;
            for (char ch : rest) { if (std::isspace((unsigned char)ch)) break; id += ch; }
            if (!id.empty()) currentVoice = id;
            continue;
        }
        if (isFieldLine(t)) continue;

        std::string musicLine = raw;
        {
            std::string lt = TrimABC(raw);
            if (lt.rfind("[V:", 0) == 0) {
                size_t close = lt.find(']');
                if (close != std::string::npos) {
                    std::string id = TrimABC(lt.substr(3, close - 3));
                    std::string first;
                    for (char ch : id) { if (std::isspace((unsigned char)ch)) break; first += ch; }
                    if (!first.empty()) currentVoice = first;
                    musicLine = lt.substr(close + 1);
                }
            }
        }

        tune.voice(currentVoice) += musicLine;
        tune.voice(currentVoice) += "\n";
    }
    tune.emit(out, filepath);

    return out;
}

std::vector<Song> LoadABCFile(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return {};
    std::stringstream ss;
    ss << f.rdbuf();

    auto songs = ParseABC(ss.str(), filepath);

    std::filesystem::path p(filepath);
    std::string stem = p.stem().string();
    std::replace(stem.begin(), stem.end(), '_', ' ');
    for (auto& s : songs)
        if (s.title.empty()) s.title = stem;
    return songs;
}

} // namespace Serenade
