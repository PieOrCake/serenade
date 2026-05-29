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

static void test_key_signature() {
    auto cmaj = ParseKeySignature("C");
    CHECK(cmaj.empty());

    auto gmaj = ParseKeySignature("G");      // 1 sharp: F
    CHECK(gmaj.size() == 1 && gmaj['F'] == 1);

    auto dmaj = ParseKeySignature("D");      // 2 sharps: F, C
    CHECK(dmaj['F'] == 1 && dmaj['C'] == 1 && dmaj.size() == 2);

    auto fmaj = ParseKeySignature("F");      // 1 flat: B
    CHECK(fmaj.size() == 1 && fmaj['B'] == -1);

    auto bbmaj = ParseKeySignature("Bb");    // 2 flats: B, E
    CHECK(bbmaj['B'] == -1 && bbmaj['E'] == -1 && bbmaj.size() == 2);

    auto amin = ParseKeySignature("Am");     // relative to C: no accidentals
    CHECK(amin.empty());

    auto emin = ParseKeySignature("Em");     // 1 sharp: F
    CHECK(emin.size() == 1 && emin['F'] == 1);

    auto unknown = ParseKeySignature("HP");  // exotic -> treat as none
    CHECK(unknown.empty());
}

static void test_tempo_and_length() {
    CHECK(ParseTempoToQuarterBPM("1/4=120") == 120);
    CHECK(ParseTempoToQuarterBPM("1/8=120") == 60);
    CHECK(ParseTempoToQuarterBPM("100")     == 100);
    CHECK(ParseTempoToQuarterBPM("\"Allegro\" 1/4=90") == 90);
    CHECK(ParseTempoToQuarterBPM("garbage")  == 120);  // fallback

    CHECK(ParseNoteLength("1/8")  == 0.125);
    CHECK(ParseNoteLength("1/16") == 0.0625);
    CHECK(ParseNoteLength("1/4")  == 0.25);
    CHECK(ParseNoteLength("")     == 0.0);
}

static void test_notes_basic() {
    // No L:, no M: -> default length 1/8 -> each note 0.5 beats.
    auto songs = ParseABC("X:1\nT:Scale\nK:C\nCDE\n");
    CHECK(songs.size() == 1);
    CHECK(songs[0].title == "Scale");
    CHECK(songs[0].events.size() == 3);
    CHECK(songs[0].events[0].type == EventType::Note && songs[0].events[0].keys.size() == 1
          && songs[0].events[0].keys[0] == 1);
    CHECK(songs[0].events[1].keys[0] == 2);
    CHECK(songs[0].events[2].keys[0] == 3);
    CHECK(songs[0].events[0].durationBeats == 0.5f);
}

static void test_octave_changes() {
    auto songs = ParseABC("X:1\nK:C\nCcC,\n");
    CHECK(songs.size() == 1);
    const auto& e = songs[0].events;
    // C (Mid, no octave set), c -> OctaveSet High + Note, C, -> OctaveSet Low + Note
    CHECK(e.size() == 5);
    CHECK(e[0].type == EventType::Note     && e[0].keys[0] == 1);
    CHECK(e[1].type == EventType::OctaveSet && e[1].targetOctave == Octave::High);
    CHECK(e[2].type == EventType::Note     && e[2].keys[0] == 1);
    CHECK(e[3].type == EventType::OctaveSet && e[3].targetOctave == Octave::Low);
    CHECK(e[4].type == EventType::Note     && e[4].keys[0] == 1);
}

static void test_key_and_accidentals() {
    auto g = ParseABC("X:1\nK:G\nF\n");
    CHECK(g[0].events[0].keys[0] == 11);   // F# -> key 11

    auto bar = ParseABC("X:1\nK:C\n^F F | F\n");
    CHECK(bar[0].events.size() == 3);
    CHECK(bar[0].events[0].keys[0] == 11); // ^F
    CHECK(bar[0].events[1].keys[0] == 11); // F still sharp in same bar
    CHECK(bar[0].events[2].keys[0] == 4);  // F natural after bar

    auto flat = ParseABC("X:1\nK:C\n_E\n");
    CHECK(flat[0].events[0].keys[0] == 10); // Eb -> D#
}

static void test_durations_rests() {
    auto s = ParseABC("X:1\nL:1/4\nK:C\nC2 D z E2\n");
    const auto& e = s[0].events;
    CHECK(e.size() == 4);
    CHECK(e[0].type == EventType::Note && e[0].durationBeats == 2.0f);
    CHECK(e[1].type == EventType::Note && e[1].durationBeats == 1.0f);
    CHECK(e[2].type == EventType::Rest && e[2].durationBeats == 1.0f);
    CHECK(e[3].type == EventType::Note && e[3].durationBeats == 2.0f);
}

static void test_tie_and_broken() {
    auto tie = ParseABC("X:1\nL:1/4\nK:C\nC- C D\n");
    CHECK(tie[0].events.size() == 2);
    CHECK(tie[0].events[0].keys[0] == 1 && tie[0].events[0].durationBeats == 2.0f);
    CHECK(tie[0].events[1].keys[0] == 2 && tie[0].events[1].durationBeats == 1.0f);

    auto br = ParseABC("X:1\nL:1/4\nK:C\nC>D\n");
    CHECK(br[0].events[0].durationBeats == 1.5f);
    CHECK(br[0].events[1].durationBeats == 0.5f);
}

static void test_readlength_via_durations() {
    // Lock in length-token parsing through ParseABC durations (L:1/4 -> quarter = 1.0 beat).
    auto s = ParseABC("X:1\nL:1/4\nK:C\nC C/2 C/ C// C3/2 C3/2/2\n");
    const auto& e = s[0].events;
    CHECK(e.size() == 6);
    CHECK(e[0].durationBeats == 1.0f);    // C    -> 1 * 1/4 * 4
    CHECK(e[1].durationBeats == 0.5f);    // C/2
    CHECK(e[2].durationBeats == 0.5f);    // C/
    CHECK(e[3].durationBeats == 0.25f);   // C//  -> 1/4
    CHECK(e[4].durationBeats == 1.5f);    // C3/2
    CHECK(e[5].durationBeats == 0.75f);   // C3/2/2 -> 3/4
}

static void test_first_note_high_octave() {
    // A voice that starts in the High octave must emit an OctaveSet before the first note.
    auto s = ParseABC("X:1\nK:C\nc\n");
    const auto& e = s[0].events;
    CHECK(e.size() == 2);
    CHECK(e[0].type == EventType::OctaveSet && e[0].targetOctave == Octave::High);
    CHECK(e[1].type == EventType::Note && e[1].keys[0] == 1);
}

static void test_no_key_field() {
    // A tune with no K: line still parses (defaults to C major / no accidentals).
    auto s = ParseABC("X:1\nT:NoKey\nK:C\nC\n");
    CHECK(s.size() == 1 && s[0].events.size() == 1 && s[0].events[0].keys[0] == 1);
}

int main() {
    test_pitch_mapping();
    test_key_signature();
    test_tempo_and_length();
    test_notes_basic();
    test_octave_changes();
    test_key_and_accidentals();
    test_durations_rests();
    test_tie_and_broken();
    test_readlength_via_durations();
    test_first_note_high_octave();
    test_no_key_field();
    if (g_failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
