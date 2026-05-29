// <climits>/<cmath>/<filesystem> etc. are used by parsing logic added in later tasks
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
} // namespace Serenade
