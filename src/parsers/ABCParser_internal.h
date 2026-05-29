#ifndef SERENADE_ABCPARSER_INTERNAL_H
#define SERENADE_ABCPARSER_INTERNAL_H

#include "SongParser.h"
#include <string>
#include <map>

namespace Serenade { namespace abc {

struct KeyPos {
    Octave octave;  // Low / Mid / High
    int key;        // 1-8 natural, 9-13 sharp
};

// Map an ABC absolute semitone (uppercase C with no octave mark == 0) onto
// GW2's 3-octave keyboard, folding out-of-range notes into range by octaves.
KeyPos SemitoneToKey(int abcSemitone);

// Pitch class for a note letter (C=0, D=2, E=4, F=5, G=7, A=9, B=11);
// -1 if the letter is not A-G/a-g.
int LetterPitchClass(char letter);

// Parse a K: field into a per-letter accidental map (semitone offsets, e.g.
// {F:+1}). Empty/unknown field -> empty map (C major / all natural).
std::map<char, int> ParseKeySignature(const std::string& keyField);

// Parse a Q: tempo field to quarter-note BPM. 120 on failure.
int ParseTempoToQuarterBPM(const std::string& tempoField);

// Parse an L: field (e.g. "1/8") to its fractional value (0.125). 0 on failure.
double ParseNoteLength(const std::string& lengthField);

}} // namespace Serenade::abc

#endif
