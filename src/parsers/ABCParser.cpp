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

} // namespace abc
} // namespace Serenade
