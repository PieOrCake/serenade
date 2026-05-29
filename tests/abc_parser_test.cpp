// Build & run: g++ -std=c++17 -I src tests/abc_parser_test.cpp src/parsers/ABCParser.cpp -o /tmp/abc_test && /tmp/abc_test
#include "SongParser.h"
#include "parsers/ABCParser_internal.h"
#include <cstdio>
#include <string>

using namespace Serenade;
using namespace Serenade::abc;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static void test_pitch_mapping() {
    CHECK(LetterPitchClass('C') == 0);
    CHECK(LetterPitchClass('D') == 2);
    CHECK(LetterPitchClass('E') == 4);
    CHECK(LetterPitchClass('F') == 5);
    CHECK(LetterPitchClass('G') == 7);
    CHECK(LetterPitchClass('A') == 9);
    CHECK(LetterPitchClass('B') == 11);
    CHECK(LetterPitchClass('z') == -1);
    // lowercase should return the same pitch class as uppercase
    CHECK(LetterPitchClass('c') == 0);

    KeyPos c   = SemitoneToKey(0);   CHECK(c.octave   == Octave::Mid  && c.key == 1);
    KeyPos d   = SemitoneToKey(2);   CHECK(d.octave   == Octave::Mid  && d.key == 2);
    KeyPos cs  = SemitoneToKey(1);   CHECK(cs.octave  == Octave::Mid  && cs.key == 9);
    KeyPos b   = SemitoneToKey(11);  CHECK(b.octave   == Octave::Mid  && b.key == 7);
    KeyPos lo  = SemitoneToKey(-12); CHECK(lo.octave  == Octave::Low  && lo.key == 1);
    KeyPos hi  = SemitoneToKey(12);  CHECK(hi.octave  == Octave::High && hi.key == 1);
    KeyPos top = SemitoneToKey(24);  CHECK(top.octave == Octave::High && top.key == 8);
    // fold: one above top C wraps down an octave
    KeyPos fold = SemitoneToKey(25); CHECK(fold.octave == Octave::High && fold.key == 9);
}

int main() {
    test_pitch_mapping();
    if (g_failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
