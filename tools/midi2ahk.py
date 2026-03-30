#!/usr/bin/env python3
"""
midi2ahk — MIDI to GW2 AHK converter for Serenade.

GUI tool (PyQt6) that converts MIDI files to AHK scripts compatible with the
Serenade addon for Guild Wars 2.

GW2 Piano mapping (C Major instruments):
    Key 1-7: C D E F G A B  (natural notes)
    F1-F5:   C# D# F# G# A# (sharps/flats)
    Key 9:   Octave up
    Key 0:   Octave down
    GW2 piano has 3 octaves (low/mid/high).
"""

import sys
import webbrowser
import os
import re
import json
import urllib.request
import urllib.parse

# Add venv to path
VENV_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.venv')
VENV_SITE = os.path.join(VENV_DIR, 'lib')
for d in os.listdir(VENV_SITE) if os.path.isdir(VENV_SITE) else []:
    sp = os.path.join(VENV_SITE, d, 'site-packages')
    if os.path.isdir(sp) and sp not in sys.path:
        sys.path.insert(0, sp)

import mido
import xml.etree.ElementTree as ET
import zipfile
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QFileDialog, QComboBox, QLineEdit, QTextEdit,
    QGroupBox, QProgressBar, QMessageBox, QListWidget, QListWidgetItem,
    QSizePolicy, QTableWidget, QTableWidgetItem, QHeaderView, QAbstractItemView,
    QTabWidget, QCheckBox, QSplitter, QScrollBar, QMenu
)
from PyQt6.QtCore import Qt, QSettings, QTimer, QThread, pyqtSignal, QRectF, QPointF
from PyQt6.QtGui import QFont, QPainter, QColor, QPen, QBrush, QCursor, QPolygonF

# ── GW2 Note Mapping ──────────────────────────────────────────────────────────

NOTE_MAP = {
    0:  ('note', 1),    # C
    1:  ('sharp', 1),   # C# → F1
    2:  ('note', 2),    # D
    3:  ('sharp', 2),   # D# → F2
    4:  ('note', 3),    # E
    5:  ('note', 4),    # F
    6:  ('sharp', 3),   # F# → F3
    7:  ('note', 5),    # G
    8:  ('sharp', 4),   # G# → F4
    9:  ('note', 6),    # A
    10: ('sharp', 5),   # A# → F5
    11: ('note', 7),    # B
}


# Reverse lookup for sorting chord notes by pitch (low to high)
_GW2_KEY_SEMITONE = {}
for _semi, (_kt, _kn) in NOTE_MAP.items():
    _GW2_KEY_SEMITONE[(_kt, _kn)] = _semi
GW2_MID = 1

# Semitones that land on white keys (C major natural notes)
WHITE_KEY_SEMITONES = {0, 2, 4, 5, 7, 9, 11}

SEMITONE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def search_itunes(query, limit=5):
    """Search iTunes for song metadata. Returns list of (title, artist, source_label)."""
    results = []
    try:
        encoded = urllib.parse.urlencode({'term': query, 'entity': 'song', 'limit': limit})
        url = f'https://itunes.apple.com/search?{encoded}'
        req = urllib.request.Request(url, headers={'User-Agent': 'Serenade-MIDI-Converter/1.0'})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode('utf-8'))
        seen = set()
        for item in data.get('results', []):
            title = item.get('trackName', '')
            artist = item.get('artistName', '')
            if title and (title.lower(), artist.lower()) not in seen:
                seen.add((title.lower(), artist.lower()))
                results.append((title, artist, 'iTunes'))
    except Exception:
        pass  # silently fail — suggestions are best-effort
    return results


def generate_metadata_suggestions(filepath, mid=None):
    """Generate multiple (title, artist) suggestions from filename and MIDI metadata.

    Returns a list of (title, artist, source_label) tuples, deduplicated.
    """
    suggestions = []
    seen = set()

    def add(title, artist, source):
        key = (title.lower(), artist.lower())
        if key not in seen:
            seen.add(key)
            suggestions.append((title, artist, source))

    stem = os.path.splitext(os.path.basename(filepath))[0]
    cleaned = stem.replace('_', ' ').strip()
    cleaned = re.sub(r'^\d+[\.\s\-]+\s*', '', cleaned)

    # Online lookup via iTunes Search API
    itunes_results = search_itunes(cleaned)
    for title, artist, source in itunes_results:
        add(title, artist, source)

    # Pattern: "A - B" → suggest both orderings
    if ' - ' in cleaned:
        parts = cleaned.split(' - ', 1)
        a, b = parts[0].strip(), parts[1].strip()
        add(b, a, f'"{a}" as artist')
        add(a, b, f'"{b}" as artist')

    # Pattern: "Title (Artist)" or "Title [Artist]"
    if m := re.match(r'^(.+?)\s*[\(\[]\s*(.+?)\s*[\)\]]$', cleaned):
        add(m.group(1).strip(), m.group(2).strip(), 'parentheses')

    # Pattern: "Title by Artist"
    if m := re.search(r'^(.+?)\s+by\s+(.+)$', cleaned, re.IGNORECASE):
        add(m.group(1).strip(), m.group(2).strip(), '"by" keyword')

    # Raw filename as title, no artist
    add(cleaned, '', 'filename only')

    # MIDI metadata suggestions
    if mid:
        meta = extract_midi_metadata(mid)
        if meta['title']:
            add(meta['title'], meta['artist'], 'MIDI metadata')
        if meta['artist'] and not meta['title']:
            add(cleaned, meta['artist'], 'MIDI artist + filename')
        if meta['copyright']:
            add(cleaned, meta['copyright'], 'MIDI copyright')

    return suggestions


def extract_midi_metadata(mid):
    """Extract metadata from MIDI file (track names, copyright, text events).

    Returns dict with keys: title, artist, copyright, texts.
    """
    meta = {'title': '', 'artist': '', 'copyright': '', 'texts': []}

    # Track/instrument-like names to skip when looking for a real title
    _skip_words = {'piano', 'guitar', 'bass', 'drum', 'drums', 'vocal', 'vocals',
                   'voice', 'melody', 'lead', 'rhythm', 'strings', 'synth', 'organ',
                   'flute', 'harp', 'violin', 'cello', 'brass', 'choir', 'tempo',
                   'rh', 'lh', 'left', 'right'}

    for track in mid.tracks:
        for msg in track:
            if msg.type == 'track_name' and msg.name.strip():
                name = msg.name.strip()
                # Skip names that are just instrument/part labels
                words = set(name.lower().replace('-', ' ').split())
                if not meta['title'] and not words.issubset(_skip_words):
                    meta['title'] = name
            elif msg.type == 'copyright' and hasattr(msg, 'text') and msg.text.strip():
                meta['copyright'] = msg.text.strip()
            elif msg.type == 'text' and hasattr(msg, 'text') and msg.text.strip():
                meta['texts'].append(msg.text.strip())
            elif msg.type == 'instrument_name' and hasattr(msg, 'name'):
                pass  # skip instrument names

    # Try to find artist in text events
    for text in meta['texts']:
        lower = text.lower()
        if any(kw in lower for kw in ['composed by', 'artist:', 'performer:', 'arranged by']):
            # Extract the name after the keyword
            for kw in ['composed by ', 'artist: ', 'performer: ', 'arranged by ']:
                if kw in lower:
                    idx = lower.index(kw) + len(kw)
                    meta['artist'] = text[idx:].strip()
                    break

    return meta


def find_best_transpose(notes_with_times):
    """Find the transposition (0-11 semitones) that maximizes white-key usage.

    Returns (best_shift, white_pct) where best_shift is semitones to add.
    """
    if not notes_with_times:
        return 0, 100.0

    midi_notes = [n for _, n, _ in notes_with_times]
    total = len(midi_notes)
    best_shift = 0
    best_white = 0

    for shift in range(12):
        white = sum(1 for n in midi_notes if (n + shift) % 12 in WHITE_KEY_SEMITONES)
        if white > best_white:
            best_white = white
            best_shift = shift

    return best_shift, (best_white / total) * 100.0


def midi_note_to_gw2(note, base_octave_midi=48):
    offset = note - base_octave_midi
    if offset < 0 or offset >= 36:
        return None
    octave = offset // 12
    semitone = offset % 12
    key_type, key_num = NOTE_MAP[semitone]
    return (octave, key_type, key_num)


def gw2_key_name(key_type, key_num):
    if key_type == 'note':
        return f'Numpad{key_num}'
    else:
        return f'F{key_num}'


# GW2 mode system: Numpad0 increases, Numpad9 decreases
GW2_MODE_LOW = 0
GW2_MODE_MID = 1
GW2_MODE_HIGH = 2
GW2_MODE_MINOR = 3
GW2_MODE_MAJOR = 4


def detect_triad(midi_pitches):
    """Check if a set of MIDI pitches contains a major or minor triad.
    Returns (root_semitone, 'major'|'minor') or None.
    A major triad = root, root+4, root+7 semitones.
    A minor triad = root, root+3, root+7 semitones."""
    if len(midi_pitches) < 3:
        return None
    pcs = set(p % 12 for p in midi_pitches)
    if len(pcs) < 3:
        return None
    # Check each pitch class as potential root
    for root in pcs:
        major_3rd = (root + 4) % 12
        minor_3rd = (root + 3) % 12
        fifth = (root + 7) % 12
        if major_3rd in pcs and fifth in pcs:
            return (root, 'major')
        if minor_3rd in pcs and fifth in pcs:
            return (root, 'minor')
    return None


def get_track_info(midi_file):
    """Return list of (index, name, note_count, note_range, is_duplicate) for each track.

    Also returns the recommended track index (best single track for GW2 piano).
    """
    mid = mido.MidiFile(midi_file)
    tracks = []
    seen_fingerprints = {}  # fingerprint → first track index

    for i, track in enumerate(mid.tracks):
        notes = [msg.note for msg in track if msg.type == 'note_on' and msg.velocity > 0]
        note_count = len(notes)
        name = track.name if track.name else "(unnamed)"
        note_range = (min(notes), max(notes)) if notes else (0, 0)

        # Fingerprint: note count + sorted note tuple (detects exact duplicates)
        fingerprint = (note_count, tuple(sorted(notes))) if notes else None
        is_duplicate = False
        if fingerprint and fingerprint in seen_fingerprints:
            is_duplicate = True
        elif fingerprint:
            seen_fingerprints[fingerprint] = i

        tracks.append((i, name, note_count, note_range, is_duplicate))

    # Recommend best single track: most notes, fits in 3 octaves, not a duplicate
    best_idx = None
    best_score = -1
    for i, name, count, (lo, hi), is_dup in tracks:
        if count == 0 or is_dup:
            continue
        span = hi - lo
        # Score: prefer more notes, penalize range > 36 semitones (3 octaves)
        range_penalty = max(0, span - 36) * 10
        score = count - range_penalty
        if score > best_score:
            best_score = score
            best_idx = i

    return tracks, mid, best_idx


def find_best_base_octave(notes_with_times):
    if not notes_with_times:
        return 48, 0, 0

    midi_notes = [n for _, n, _ in notes_with_times]
    min_note = min(midi_notes)
    max_note = max(midi_notes)

    best_base = 48
    best_count = 0

    start = (max(0, min_note - 35) // 12) * 12
    for base in range(start, min(128, max_note + 1), 12):
        count = sum(1 for n in midi_notes if base <= n < base + 36)
        if count > best_count:
            best_count = count
            best_base = base

    return best_base, best_count, len(midi_notes)


def extract_notes(mid, track_indices=None):
    """Extract note events from MIDI.

    track_indices: list of track indices, or None for all tracks merged.
    """
    if mid.type == 0 or track_indices is None:
        events_raw = []
        abs_time = 0.0
        for msg in mid:
            abs_time += msg.time
            if msg.type == 'note_on' and msg.velocity > 0:
                events_raw.append((abs_time * 1000, msg.note, msg.velocity))
        return events_raw
    else:
        tempo = 500000
        tpb = mid.ticks_per_beat

        tempo_map = []
        if mid.type == 1 and len(mid.tracks) > 0:
            tick = 0
            for msg in mid.tracks[0]:
                tick += msg.time
                if msg.type == 'set_tempo':
                    tempo_map.append((tick, msg.tempo))
        if not tempo_map:
            tempo_map = [(0, tempo)]

        def ticks_to_ms(target_tick):
            ms = 0.0
            current_tick = 0
            current_tempo = tempo_map[0][1]
            tempo_idx = 0
            while current_tick < target_tick:
                next_tempo_tick = tempo_map[tempo_idx + 1][0] if tempo_idx + 1 < len(tempo_map) else target_tick
                end_tick = min(next_tempo_tick, target_tick)
                delta_ticks = end_tick - current_tick
                ms += (delta_ticks / tpb) * (current_tempo / 1000.0)
                current_tick = end_tick
                if current_tick >= next_tempo_tick and tempo_idx + 1 < len(tempo_map):
                    tempo_idx += 1
                    current_tempo = tempo_map[tempo_idx][1]
            return ms

        events = []
        for tidx in track_indices:
            track = mid.tracks[tidx]
            abs_tick = 0
            for msg in track:
                abs_tick += msg.time
                if msg.type == 'note_on' and msg.velocity > 0:
                    events.append((ticks_to_ms(abs_tick), msg.note, msg.velocity))

        events.sort(key=lambda e: e[0])
        return events


# ── MusicXML Parsing ──────────────────────────────────────────────────────────

STEP_TO_SEMI = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def parse_musicxml_file(filepath):
    """Parse a MusicXML file (.musicxml, .xml, or .mxl compressed).
    Returns the XML root element."""
    import os
    ext = os.path.splitext(filepath)[1].lower()

    if ext == '.mxl':
        with zipfile.ZipFile(filepath, 'r') as z:
            # Try META-INF/container.xml for the rootfile path
            try:
                container = ET.parse(z.open('META-INF/container.xml')).getroot()
                # Handle namespace
                for rf in container.iter():
                    if rf.tag.endswith('rootfile') and rf.get('full-path'):
                        return ET.parse(z.open(rf.get('full-path'))).getroot()
            except (KeyError, ET.ParseError):
                pass
            # Fallback: find any .musicxml or .xml file
            for name in z.namelist():
                if 'META-INF' in name:
                    continue
                if name.endswith('.musicxml') or name.endswith('.xml'):
                    return ET.parse(z.open(name)).getroot()
            raise ValueError("No MusicXML file found in .mxl archive")
    else:
        return ET.parse(filepath).getroot()


def _xml_ns(root):
    """Extract namespace prefix from root element."""
    if root.tag.startswith('{'):
        return root.tag.split('}')[0] + '}'
    return ''


def get_musicxml_parts(filepath):
    """Get part info from a MusicXML file.
    Returns (parts, root, best_idx) matching get_track_info() format."""
    root = parse_musicxml_file(filepath)
    ns = _xml_ns(root)

    # Part names from part-list
    part_names = {}
    part_list = root.find(f'{ns}part-list')
    if part_list is not None:
        for sp in part_list.findall(f'{ns}score-part'):
            pid = sp.get('id')
            pn = sp.find(f'{ns}part-name')
            part_names[pid] = pn.text.strip() if pn is not None and pn.text else '(unnamed)'

    parts = []
    seen_fp = {}

    for idx, part in enumerate(root.findall(f'{ns}part')):
        pid = part.get('id')
        name = part_names.get(pid, f'Part {idx}')

        midi_notes = []
        for note_elem in part.iter(f'{ns}note'):
            if note_elem.find(f'{ns}rest') is not None:
                continue
            pitch = note_elem.find(f'{ns}pitch')
            if pitch is None:
                continue
            step = pitch.find(f'{ns}step').text
            octave = int(pitch.find(f'{ns}octave').text)
            alter_elem = pitch.find(f'{ns}alter')
            alter = int(float(alter_elem.text)) if alter_elem is not None else 0
            midi_notes.append((octave + 1) * 12 + STEP_TO_SEMI[step] + alter)

        note_count = len(midi_notes)
        note_range = (min(midi_notes), max(midi_notes)) if midi_notes else (0, 0)

        fp = (note_count, tuple(sorted(midi_notes))) if midi_notes else None
        is_dup = False
        if fp and fp in seen_fp:
            is_dup = True
        elif fp:
            seen_fp[fp] = idx

        parts.append((idx, name, note_count, note_range, is_dup))

    best_idx = None
    best_score = -1
    for i, name, count, (lo, hi), is_dup in parts:
        if count == 0 or is_dup:
            continue
        span = hi - lo
        score = count - max(0, span - 36) * 10
        if score > best_score:
            best_score = score
            best_idx = i

    return parts, root, best_idx


def extract_notes_musicxml(root, part_indices=None):
    """Extract note events from MusicXML.
    Returns list of (time_ms, midi_note, velocity)."""
    ns = _xml_ns(root)

    all_parts = root.findall(f'{ns}part')
    if part_indices is None:
        part_indices = list(range(len(all_parts)))

    events = []

    for pidx in part_indices:
        part = all_parts[pidx]

        divisions = 1
        tempo_bpm = 120.0
        current_time_ms = 0.0
        prev_note_start = 0.0

        for measure in part.findall(f'{ns}measure'):
            for elem in measure:
                tag = elem.tag.replace(ns, '')

                if tag == 'attributes':
                    div_elem = elem.find(f'{ns}divisions')
                    if div_elem is not None:
                        divisions = int(div_elem.text)

                elif tag == 'direction':
                    sound = elem.find(f'{ns}sound')
                    if sound is not None and sound.get('tempo'):
                        tempo_bpm = float(sound.get('tempo'))

                elif tag == 'note':
                    is_chord = elem.find(f'{ns}chord') is not None
                    is_rest = elem.find(f'{ns}rest') is not None
                    dur_elem = elem.find(f'{ns}duration')
                    duration = int(dur_elem.text) if dur_elem is not None else 0

                    ms_per_div = (60000.0 / tempo_bpm) / divisions

                    if is_chord:
                        note_start = prev_note_start
                    else:
                        note_start = current_time_ms
                        prev_note_start = note_start
                        current_time_ms += duration * ms_per_div

                    if not is_rest:
                        pitch = elem.find(f'{ns}pitch')
                        if pitch is not None:
                            step = pitch.find(f'{ns}step').text
                            octave = int(pitch.find(f'{ns}octave').text)
                            alter_elem = pitch.find(f'{ns}alter')
                            alter = int(float(alter_elem.text)) if alter_elem is not None else 0
                            midi_note = (octave + 1) * 12 + STEP_TO_SEMI[step] + alter
                            events.append((note_start, midi_note, 80))

                elif tag == 'forward':
                    dur_elem = elem.find(f'{ns}duration')
                    if dur_elem is not None:
                        ms_per_div = (60000.0 / tempo_bpm) / divisions
                        current_time_ms += int(dur_elem.text) * ms_per_div

                elif tag == 'backup':
                    dur_elem = elem.find(f'{ns}duration')
                    if dur_elem is not None:
                        ms_per_div = (60000.0 / tempo_bpm) / divisions
                        current_time_ms -= int(dur_elem.text) * ms_per_div

    events.sort(key=lambda e: e[0])
    return events


def extract_musicxml_metadata(root):
    """Extract title and artist from MusicXML."""
    ns = _xml_ns(root)
    meta = {'title': '', 'artist': '', 'copyright': '', 'texts': []}

    work = root.find(f'{ns}work')
    if work is not None:
        wt = work.find(f'{ns}work-title')
        if wt is not None and wt.text:
            meta['title'] = wt.text.strip()

    if not meta['title']:
        mt = root.find(f'{ns}movement-title')
        if mt is not None and mt.text:
            meta['title'] = mt.text.strip()

    ident = root.find(f'{ns}identification')
    if ident is not None:
        for creator in ident.findall(f'{ns}creator'):
            ctype = creator.get('type', '').lower()
            if ctype in ('composer', 'lyricist', 'arranger') and creator.text:
                if not meta['artist']:
                    meta['artist'] = creator.text.strip()
        for rights in ident.findall(f'{ns}rights'):
            if rights.text:
                meta['copyright'] = rights.text.strip()

    return meta


def convert(midi_file, output_file, track_indices=None, title=None, author=None, transpose=None, base_octave=None, chord_window_ms=5, notes_override=None, use_chords=False, instrument=None):
    """Convert a MIDI file to a GW2 AHK script. Returns (success, log_lines).

    track_indices: list of track indices, or None for all tracks merged.
    transpose: semitones to shift (0-11), or None for auto-detect.
    base_octave: MIDI note for Low octave base, or None for auto-detect.
    chord_window_ms: max gap (ms) between notes to group as a chord.
        Default 5 (nearly simultaneous). Higher values collapse arpeggios.
    use_chords: if True, substitute detected triads with GW2 chord mode keypresses.
    """
    log = []
    if notes_override is not None:
        notes = notes_override
        log.append(f"Source: MusicXML")
    else:
        try:
            mid = mido.MidiFile(midi_file)
        except Exception as e:
            return False, [f"ERROR: Failed to read MIDI: {e}"]
        log.append(f"Type: {mid.type}, Ticks/beat: {mid.ticks_per_beat}, Tracks: {len(mid.tracks)}")
        notes = extract_notes(mid, track_indices)

    if not notes:
        return False, log + ["ERROR: No notes found in selected track!"]

    log.append(f"Note events: {len(notes)}")

    # Auto-transpose to maximize white-key (natural note) usage
    if transpose is None:
        transpose, white_pct = find_best_transpose(notes)
    else:
        _, orig_pct = find_best_transpose([(t, n, v) for t, n, v in notes])
        white_pct = sum(1 for _, n, _ in notes if (n + transpose) % 12 in WHITE_KEY_SEMITONES) / len(notes) * 100

    if transpose != 0:
        log.append(f"Transpose: +{transpose} semitones (white keys: {white_pct:.0f}%)")
        notes = [(t, n + transpose, v) for t, n, v in notes]
    else:
        log.append(f"Transpose: none needed (white keys: {white_pct:.0f}%)")

    if base_octave is not None:
        base_midi = base_octave
        in_range = sum(1 for _, n, _ in notes if base_midi <= n < base_midi + 36)
        total = len(notes)
    else:
        base_midi, in_range, total = find_best_base_octave(notes)
    log.append(f"Base octave: MIDI {base_midi} (C{base_midi // 12 - 1})")
    log.append(f"Notes in range: {in_range}/{total}")
    if total > in_range:
        log.append(f"WARNING: {total - in_range} notes out of range (clamped)")

    # Group notes into chords. Each note joins the current group if it's
    # within chord_window_ms of the FIRST note in the group.
    chord_groups = []
    current_group = [notes[0]]
    for i in range(1, len(notes)):
        if notes[i][0] - current_group[0][0] <= chord_window_ms:
            current_group.append(notes[i])
        else:
            chord_groups.append(current_group)
            current_group = [notes[i]]
    chord_groups.append(current_group)

    log.append(f"Note groups (chords+singles): {len(chord_groups)}")

    # Generate AHK script
    if not title:
        title = os.path.splitext(os.path.basename(midi_file))[0]
    lines = []
    lines.append(f'; title: {title}')
    if author:
        lines.append(f'; author: {author}')
    lines.append(f'; instrument: {instrument or "Piano"}')
    lines.append('; Converted by Serenade Music Converter')
    lines.append('')

    # First pass: compute target octave for each group
    group_data = []
    for group in chord_groups:
        gw2_notes = []
        for _, midi_note, _ in group:
            result = midi_note_to_gw2(midi_note, base_midi)
            if result is None:
                continue  # drop out-of-range notes instead of clamping
            gw2_notes.append(result)

        if not gw2_notes:
            group_data.append((group, gw2_notes, GW2_MID))
            continue

        octave_counts = {}
        for oct, _, _ in gw2_notes:
            octave_counts[oct] = octave_counts.get(oct, 0) + 1
        target_octave = max(octave_counts, key=octave_counts.get)
        group_data.append((group, gw2_notes, target_octave))

    # Second pass: smooth octave sequence using hybrid time + run-length.
    # In fast passages (short gaps), only switch octave if we stay in the
    # new octave for MIN_RUN groups. In slow passages (long gaps), allow
    # octave changes freely since there's enough time for the switch.
    MIN_RUN = 4       # min consecutive groups in fast passages
    SLOW_GAP_MS = 250  # gaps >= this are "slow" — always allow changes
    octave_changes_saved = 0
    # Start smoothing from the first group's actual octave
    first_oct = GW2_MID
    for _, notes_check, oct_check in group_data:
        if notes_check:
            first_oct = oct_check
            break
    current_smooth_oct = first_oct
    i = 0
    while i < len(group_data):
        grp, notes, oct = group_data[i]
        if not notes:
            i += 1
            continue
        if oct == current_smooth_oct:
            i += 1
            continue

        # Check timing gap before this group
        gap_ms = 0
        if i > 0:
            gap_ms = grp[0][0] - group_data[i - 1][0][0][0]

        # Slow passage — allow octave change regardless of run length
        if gap_ms >= SLOW_GAP_MS:
            current_smooth_oct = oct
            i += 1
            continue

        # Fast passage — require minimum run length
        run_length = 0
        for j in range(i, len(group_data)):
            if group_data[j][1]:  # has notes
                if group_data[j][2] == oct:
                    run_length += 1
                else:
                    break
        if run_length >= MIN_RUN:
            current_smooth_oct = oct
            i += 1
        else:
            # Flatten this short run to current octave
            for j in range(i, len(group_data)):
                if group_data[j][1]:
                    if group_data[j][2] == oct:
                        group_data[j] = (group_data[j][0], group_data[j][1], current_smooth_oct)
                        octave_changes_saved += 1
                    else:
                        break
            i += 1

    if octave_changes_saved > 0:
        log.append(f"Octave smoothing: flattened {octave_changes_saved} short octave excursions")

    # Count actual octave changes
    octave_change_count = 0
    prev_oct = GW2_MID
    for _, notes, oct in group_data:
        if notes and oct != prev_oct:
            octave_change_count += abs(oct - prev_oct)
            prev_oct = oct
    log.append(f"Octave changes: {octave_change_count}")

    # current_mode: 0=low, 1=mid, 2=high, 3=minor chords, 4=major chords
    current_mode = GW2_MODE_MID
    last_time_ms = 0.0
    chord_subs = 0

    for group, gw2_notes, target_octave in group_data:
        group_time = group[0][0]
        gap_ms = group_time - last_time_ms

        if gap_ms > 1:
            lines.append(f'Sleep, {int(round(gap_ms))}')

        if not gw2_notes:
            last_time_ms = group_time
            continue

        # Check for triad substitution
        triad = None
        if use_chords and len(gw2_notes) >= 3:
            midi_pitches = [n for _, n, _ in group]
            triad = detect_triad(midi_pitches)

        if triad:
            root_semi, chord_type = triad
            target_mode = GW2_MODE_MAJOR if chord_type == 'major' else GW2_MODE_MINOR
            # Switch to chord mode
            if target_mode != current_mode:
                if target_mode > current_mode:
                    for _ in range(target_mode - current_mode):
                        lines.append('SendInput {Numpad0}')
                else:
                    for _ in range(current_mode - target_mode):
                        lines.append('SendInput {Numpad9}')
                current_mode = target_mode
            # Send root key
            key_type, key_num = NOTE_MAP[root_semi]
            lines.append(f'SendInput {{{gw2_key_name(key_type, key_num)}}}')
            chord_subs += 1
        else:
            # Regular note output — ensure we're in an octave mode (0-2)
            target_mode_oct = target_octave  # 0=low, 1=mid, 2=high
            if target_mode_oct != current_mode:
                if target_mode_oct > current_mode:
                    for _ in range(target_mode_oct - current_mode):
                        lines.append('SendInput {Numpad0}')
                else:
                    for _ in range(current_mode - target_mode_oct):
                        lines.append('SendInput {Numpad9}')
                current_mode = target_mode_oct

            # Sort notes low-to-high so melody (highest pitch) is sent last
            gw2_notes_sorted = sorted(gw2_notes, key=lambda n: n[0] * 12 + _GW2_KEY_SEMITONE.get((n[1], n[2]), 0))
            key_names = []
            for oct, key_type, key_num in gw2_notes_sorted:
                name = gw2_key_name(key_type, key_num)
                if name not in key_names:
                    key_names.append(name)
            key_names = key_names[:4]

            if key_names:
                for k in key_names:
                    lines.append(f'SendInput {{{k}}}')

        last_time_ms = group_time

    if chord_subs > 0:
        log.append(f"Chord substitutions: {chord_subs} triads replaced with GW2 chord mode")

    with open(output_file, 'w') as f:
        f.write('\r\n'.join(lines) + '\r\n')

    duration_s = last_time_ms / 1000.0
    log.append(f"Duration: {int(duration_s // 60)}:{int(duration_s % 60):02d}")
    log.append(f"Output lines: {len(lines)}")
    log.append(f"Saved: {output_file}")

    return True, log

# ── Piano Roll ────────────────────────────────────────────────────────────────

TRACK_COLORS = [
    (0x4F, 0xC3, 0xF7),  # Blue
    (0x81, 0xC7, 0x84),  # Green
    (0xFF, 0xB7, 0x4D),  # Orange
    (0xE5, 0x73, 0x73),  # Red
    (0xBA, 0x68, 0xC8),  # Purple
    (0xFF, 0xD5, 0x4F),  # Yellow
    (0x4D, 0xD0, 0xE1),  # Cyan
    (0xA1, 0x88, 0x7F),  # Brown
]



# ── Audio Synth Engine ────────────────────────────────────────────────────────

import numpy as np
import io
import wave as wave_mod

SYNTH_RATE = 44100

def _midi_to_freq(note):
    return 440.0 * (2.0 ** ((note - 69) / 12.0))

_synth_cache = {}  # pitch -> base waveform (1 second at vel=127)

def _build_piano_wave(pitch, sample_rate=SYNTH_RATE):
    """Pre-build a 2-second piano waveform for the given MIDI pitch."""
    freq = _midi_to_freq(pitch)
    dur = 2.0
    n = int(sample_rate * dur)
    t = np.linspace(0, dur, n, False).astype(np.float32)

    # Inharmonicity by register
    if pitch < 40:
        B = 0.0004
    elif pitch < 60:
        B = 0.00015
    else:
        B = 0.00005

    # Limit harmonics for speed (8 max — still sounds good)
    n_harm = max(3, min(8, int(8000 / max(1, freq))))
    amps =   [1.0, 0.6, 0.35, 0.18, 0.10, 0.06, 0.04, 0.03]
    decays = [1.0, 1.8, 2.6,  3.4,  4.2,  5.0,  5.8,  6.6]

    tone = np.zeros(n, dtype=np.float32)
    for h in range(n_harm):
        idx = h + 1
        f_h = freq * idx * np.sqrt(1 + B * idx * idx)
        if f_h > sample_rate * 0.45:
            break
        phase = (idx * 0.7) % (2 * np.pi)
        harmonic = amps[h] * np.sin((2 * np.pi * f_h) * t + phase, dtype=np.float32)
        harmonic *= np.exp(-decays[h] * t, dtype=np.float32)
        tone += harmonic

    # Hammer noise
    hd = min(int(0.003 * sample_rate), n)
    if hd > 0:
        tone[:hd] += (np.random.randn(hd).astype(np.float32) * 0.06 *
                       np.linspace(1, 0, hd, dtype=np.float32))

    # Double-decay envelope
    env = (0.35 * np.exp(-3.5 * t, dtype=np.float32) +
           0.65 * np.exp(-0.4 * t, dtype=np.float32))
    att = min(int(0.004 * sample_rate), n)
    if att > 0:
        env[:att] *= np.linspace(0, 1, att, dtype=np.float32)
    env[-min(int(0.06 * sample_rate), n):] *= np.linspace(1, 0,
        min(int(0.06 * sample_rate), n), dtype=np.float32)

    tone *= env * 0.30
    return tone

def synth_note(freq, duration_s, velocity=100, sample_rate=SYNTH_RATE):
    """Synthesize an acoustic grand piano tone (cached per pitch)."""
    pitch = int(round(12 * np.log2(freq / 440.0) + 69))
    pitch = max(0, min(127, pitch))

    if pitch not in _synth_cache:
        _synth_cache[pitch] = _build_piano_wave(pitch, sample_rate)

    base = _synth_cache[pitch]
    n_samples = int(sample_rate * duration_s)
    if n_samples == 0:
        return np.zeros(0, dtype=np.float32)

    vel_norm = velocity / 127.0

    if n_samples <= len(base):
        tone = base[:n_samples].copy() * vel_norm
    else:
        # For very long notes, pad with silence (piano decays by 2s anyway)
        tone = np.zeros(n_samples, dtype=np.float32)
        tone[:len(base)] = base * vel_norm

    # Fade out at end
    rel = min(int(0.08 * sample_rate), n_samples)
    if rel > 0 and n_samples > rel:
        tone[-rel:] *= np.linspace(1, 0, rel, dtype=np.float32)

    return tone

def simulate_gw2_playback(notes):
    """Transform notes to simulate GW2 instrument constraints.
    - Flatten velocity (GW2 has no dynamics)
    - All notes in a chord group play (SendInput fires near-instantly)
    - Only flatten velocity; preserve original timing
    Returns a new list of (start_ms, duration_ms, pitch, velocity) tuples."""
    if not notes:
        return notes
    GW2_VEL = 100
    return [(s, d, p, GW2_VEL) for s, d, p, v in notes]


def render_notes_to_audio(notes, sample_rate=SYNTH_RATE):
    """Render a list of (start_ms, duration_ms, pitch, velocity) to a numpy audio array."""
    if not notes:
        return np.zeros(0, dtype=np.int16)
    total_ms = max(n[0] + n[1] for n in notes) + 200
    total_samples = int(total_ms / 1000.0 * sample_rate)
    audio = np.zeros(total_samples, dtype=np.float32)
    for start_ms, dur_ms, pitch, vel in notes:
        freq = _midi_to_freq(pitch)
        dur_s = max(0.05, dur_ms / 1000.0)
        tone = synth_note(freq, dur_s, vel, sample_rate)
        start_sample = int(start_ms / 1000.0 * sample_rate)
        end_sample = start_sample + len(tone)
        if end_sample > len(audio):
            tone = tone[:len(audio) - start_sample]
            end_sample = len(audio)
        if start_sample < len(audio):
            audio[start_sample:end_sample] += tone
    # Normalize and convert to int16
    peak = np.max(np.abs(audio))
    if peak > 0:
        audio = audio / peak * 0.85
    return (audio * 32767).astype(np.int16)


class RenderWorker(QThread):
    """Background thread for rendering audio from notes."""
    finished = pyqtSignal(object)  # emits the int16 audio array

    def __init__(self, notes, gw2_preview=False, parent=None):
        super().__init__(parent)
        self._notes = notes
        self._gw2_preview = gw2_preview

    def run(self):
        notes = self._notes
        if self._gw2_preview:
            notes = simulate_gw2_playback(notes)
        audio = render_notes_to_audio(notes)
        self.finished.emit(audio)

def audio_to_wav_bytes(audio, sample_rate=SYNTH_RATE):
    """Convert int16 numpy array to WAV bytes."""
    buf = io.BytesIO()
    with wave_mod.open(buf, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(audio.tobytes())
    buf.seek(0)
    return buf

_pygame_inited = False
def _ensure_pygame():
    global _pygame_inited
    if not _pygame_inited:
        import pygame
        pygame.mixer.init(frequency=SYNTH_RATE, size=-16, channels=1, buffer=512)
        _pygame_inited = True

def play_preview_note(pitch, velocity=100, duration_s=0.4):
    """Play a single note preview (non-blocking)."""
    import pygame
    _ensure_pygame()
    freq = _midi_to_freq(pitch)
    tone = synth_note(freq, duration_s, velocity)
    audio = (tone * 32767 / max(0.01, np.max(np.abs(tone)))).astype(np.int16)
    wav = audio_to_wav_bytes(audio)
    sound = pygame.mixer.Sound(wav)
    sound.play()


# GW2 instrument definitions: (name, key, midi_low, midi_high)
# midi_low/high = the full playable range including the octave-repeat top note
GW2_INSTRUMENTS = [
    ("Piano",     "C Major", 48, 84),   # C3-C6, 3 octaves
    ("Harp",      "C Major", 48, 84),   # C3-C6, 3 octaves
    ("Lute",      "C Major", 48, 84),   # C3-C6, 3 octaves
    ("Minstrel",  "C Major", 48, 84),   # C3-C6, 3 octaves
    ("Horn",      "C Major", 48, 84),   # C3-C6, 3 octaves
    ("Bell",      "D Major", 50, 86),   # D3-D6, 3 octaves
    ("Verdarach", "D Major", 50, 86),   # D3-D6, 3 octaves
    ("Flute",     "E Major", 64, 88),   # E4-E6, 2 octaves
    ("Bass",      "C Major", 36, 60),   # C2-C4, 2 octaves
]


# GW2 playback timing constraints (from MusicPlayer.h InstrumentProfile)
GW2_MIN_NOTE_DELAY_MS = 50    # Minimum ms between consecutive notes
GW2_OCTAVE_SWAP_DELAY_MS = 60 # Additional delay when octave changes
# Octave boundaries: each octave spans 12 semitones; notes in different
# floor(pitch/12) groups require an octave swap command.

def _note_octave(pitch, base_pitch=60):
    """Return which GW2 octave a pitch falls in (0=low, 1=mid, 2=high)."""
    # Each GW2 octave is 12 semitones. Base pitch is the middle octave root.
    # For C-major instruments: low=C3(48), mid=C4(60), high=C5(72)
    offset = pitch - (base_pitch - 12)  # offset from low octave start
    return max(0, min(2, offset // 12))

def analyze_gw2_issues(notes):
    """Analyze notes for GW2 playback issues. Sets .warning on each note.
    Returns (overlap_count, too_close_count, octave_tight_count).
    """
    # Clear all warnings
    active = [n for n in notes if not n.deleted]
    for n in active:
        n.warning = ''
    if len(active) < 2:
        return (0, 0, 0)

    # Sort by start time, then pitch
    sorted_notes = sorted(active, key=lambda n: (n.start_ms, n.pitch))

    overlap_count = 0
    too_close_count = 0
    octave_tight_count = 0

    for i in range(len(sorted_notes)):
        note = sorted_notes[i]
        if note.warning:
            continue  # already flagged

        # Check against all subsequent notes for overlaps
        for j in range(i + 1, len(sorted_notes)):
            other = sorted_notes[j]
            if other.start_ms >= note.start_ms + note.duration_ms + GW2_MIN_NOTE_DELAY_MS * 2:
                break  # too far ahead

            # Overlap: two notes playing at the same time
            if other.start_ms < note.start_ms + note.duration_ms and abs(other.start_ms - note.start_ms) < 1:
                # Simultaneous notes (chord) - GW2 fires them as rapid keypresses
                if not note.warning:
                    note.warning = 'chord'
                    overlap_count += 1
                if not other.warning:
                    other.warning = 'chord'
                    overlap_count += 1

        # Check gap to next sequential note
        if i + 1 < len(sorted_notes):
            nxt = sorted_notes[i + 1]
            gap = nxt.start_ms - (note.start_ms + note.duration_ms)
            note_oct = _note_octave(note.pitch)
            next_oct = _note_octave(nxt.pitch)
            needs_octave_change = (note_oct != next_oct)

            if needs_octave_change:
                min_gap = GW2_MIN_NOTE_DELAY_MS + GW2_OCTAVE_SWAP_DELAY_MS
                if gap < min_gap and not nxt.warning:
                    nxt.warning = 'octave_tight'
                    octave_tight_count += 1
            else:
                if gap < GW2_MIN_NOTE_DELAY_MS and gap >= 0 and not nxt.warning:
                    nxt.warning = 'too_close'
                    too_close_count += 1

    return (overlap_count, too_close_count, octave_tight_count)

def fix_gw2_issues(notes):
    """Auto-fix GW2 playback issues. Returns number of fixes applied.
    - Trims note durations to ensure minimum gaps between sequential notes
    - Adds extra gap padding for octave changes
    - Does NOT touch simultaneous notes (chords) — converter handles those natively
    """
    active = [n for n in notes if not n.deleted]
    if len(active) < 2:
        return 0

    # Sort by start time, then pitch
    sorted_notes = sorted(active, key=lambda n: (n.start_ms, n.pitch))

    # Group simultaneous notes (within 1ms = same chord)
    groups = []
    i = 0
    while i < len(sorted_notes):
        group = [sorted_notes[i]]
        j = i + 1
        while j < len(sorted_notes) and abs(sorted_notes[j].start_ms - sorted_notes[i].start_ms) < 1:
            group.append(sorted_notes[j])
            j += 1
        groups.append(group)
        i = j

    fixes = 0

    for gi in range(len(groups) - 1):
        cur_group = groups[gi]
        nxt_group = groups[gi + 1]

        nxt_start = nxt_group[0].start_ms

        # Determine if octave change is needed between groups
        cur_pitches = [n.pitch for n in cur_group]
        nxt_pitches = [n.pitch for n in nxt_group]
        cur_octs = set(_note_octave(p) for p in cur_pitches)
        nxt_octs = set(_note_octave(p) for p in nxt_pitches)
        needs_octave_change = cur_octs != nxt_octs

        min_gap = GW2_MIN_NOTE_DELAY_MS
        if needs_octave_change:
            min_gap = GW2_MIN_NOTE_DELAY_MS + GW2_OCTAVE_SWAP_DELAY_MS

        # Trim each note in current group so it ends min_gap ms before next group
        for note in cur_group:
            note_end = note.start_ms + note.duration_ms
            gap = nxt_start - note_end
            if gap < min_gap:
                new_dur = nxt_start - note.start_ms - min_gap
                if new_dur < 10:
                    new_dur = 10  # floor: don't shrink below 10ms
                if new_dur != note.duration_ms:
                    note.duration_ms = new_dur
                    fixes += 1

        # If trimming to floor still leaves insufficient gap, shift next group forward
        latest_end = max(n.start_ms + n.duration_ms for n in cur_group)
        actual_gap = nxt_start - latest_end
        if actual_gap < min_gap:
            shift = min_gap - actual_gap
            for note in nxt_group:
                note.start_ms += shift
            fixes += 1

    return fixes

class PianoRollNote:
    __slots__ = ('start_ms', 'duration_ms', 'pitch', 'velocity', 'track', 'selected', 'deleted', 'warning')
    def __init__(self, start_ms, duration_ms, pitch, velocity, track):
        self.start_ms = start_ms
        self.duration_ms = duration_ms
        self.pitch = pitch
        self.velocity = velocity
        self.track = track
        self.selected = False
        self.deleted = False
        self.warning = ''  # '', 'overlap', 'too_close', 'octave_tight'


def extract_notes_with_duration(mid):
    """Extract notes with duration from MIDI. Returns (list of PianoRollNote, bpm)."""
    tpb = mid.ticks_per_beat

    # Build tempo map from all tracks
    tempo_map = []
    for track in mid.tracks:
        tick = 0
        for msg in track:
            tick += msg.time
            if msg.type == 'set_tempo':
                tempo_map.append((tick, msg.tempo))
    if not tempo_map:
        tempo_map = [(0, 500000)]
    tempo_map.sort()

    def ticks_to_ms(target_tick):
        ms = 0.0
        current_tick = 0
        current_tempo = tempo_map[0][1]
        tempo_idx = 0
        while current_tick < target_tick:
            next_tempo_tick = tempo_map[tempo_idx + 1][0] if tempo_idx + 1 < len(tempo_map) else target_tick
            end_tick = min(next_tempo_tick, target_tick)
            delta_ticks = end_tick - current_tick
            ms += (delta_ticks / tpb) * (current_tempo / 1000.0)
            current_tick = end_tick
            if current_tick >= next_tempo_tick and tempo_idx + 1 < len(tempo_map):
                tempo_idx += 1
                current_tempo = tempo_map[tempo_idx][1]
        return ms

    initial_bpm = round(60000000 / tempo_map[0][1])

    notes = []
    for track_idx, track in enumerate(mid.tracks):
        active = {}
        abs_tick = 0
        for msg in track:
            abs_tick += msg.time
            if msg.type == 'note_on' and msg.velocity > 0:
                key = (msg.note, getattr(msg, 'channel', 0))
                active[key] = (abs_tick, msg.velocity)
            elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
                key = (msg.note, getattr(msg, 'channel', 0))
                if key in active:
                    start_tick, vel = active.pop(key)
                    start_ms = ticks_to_ms(start_tick)
                    end_ms = ticks_to_ms(abs_tick)
                    dur = max(10, end_ms - start_ms)
                    notes.append(PianoRollNote(start_ms, dur, msg.note, vel, track_idx))
        # Flush lingering notes
        for (pitch, ch), (start_tick, vel) in active.items():
            start_ms = ticks_to_ms(start_tick)
            end_ms = ticks_to_ms(abs_tick)
            dur = max(10, end_ms - start_ms)
            notes.append(PianoRollNote(start_ms, dur, pitch, vel, track_idx))

    notes.sort(key=lambda n: n.start_ms)
    return notes, initial_bpm




def parse_ahk_to_notes(ahk_path):
    """Parse an AHK script and return (list of PianoRollNote, bpm, title, author).

    Resolves octave changes to absolute MIDI pitches.
    Key mapping (GW2 default):
      Numpad 1-8 -> C,D,E,F,G,A,B,C'  (semitone offsets: 0,2,4,5,7,9,11,12)
      F1-F5      -> C#,D#,F#,G#,A#     (semitone offsets: 1,3,6,8,10)
      Numpad 9   -> octave up
      Numpad 0   -> octave down
    Mid octave base = MIDI 60 (C4).
    """
    import re as _re

    KEY_SEMITONES = {
        1: 0, 2: 2, 3: 4, 4: 5, 5: 7, 6: 9, 7: 11, 8: 12,  # natural notes
        9: 1, 10: 3, 11: 6, 12: 8, 13: 10,  # sharps (F1-F5 mapped to 9-13)
    }
    OCTAVE_UP = 100
    OCTAVE_DOWN = 101
    MID_BASE = 60  # MIDI note for C4

    with open(ahk_path, 'r', errors='replace') as f:
        lines = f.readlines()

    title = os.path.splitext(os.path.basename(ahk_path))[0]
    title = title.replace('_', ' ')
    author = ''

    # Parse brace content to key code
    def parse_brace(content):
        c = content.strip().lower()
        # Skip "up" events
        if c.endswith(' up'):
            return -1
        # Strip " down"
        if c.endswith(' down'):
            c = c[:-5].strip()
        # Numpad keys
        m = _re.match(r'numpad(\d)', c)
        if m:
            digit = int(m.group(1))
            if digit == 9:
                return OCTAVE_UP
            if digit == 0:
                return OCTAVE_DOWN
            return digit  # 1-8
        # Bare digit
        if len(c) == 1 and c.isdigit():
            digit = int(c)
            if digit == 9:
                return OCTAVE_UP
            if digit == 0:
                return OCTAVE_DOWN
            return digit
        # F-keys (sharps)
        m = _re.match(r'f(\d+)', c)
        if m:
            fnum = int(m.group(1))
            if 1 <= fnum <= 5:
                return 8 + fnum  # F1->9, F2->10, F3->11, F4->12, F5->13
        return -1

    # First pass: collect events as (type, value) where type is 'key' or 'sleep'
    events = []
    for line in lines:
        trimmed = line.strip()
        if not trimmed:
            continue
        # Metadata comments
        if trimmed.startswith(';') or trimmed.startswith('#'):
            meta = trimmed.lstrip(';#').strip()
            if meta.lower().startswith('title:'):
                title = meta[6:].strip()
            elif meta.lower().startswith('author:'):
                author = meta[7:].strip()
            continue

        lower = trimmed.lower()

        # SendInput / Send lines
        if 'sendinput' in lower or ('send' in lower and 'sleep' not in lower):
            pos = 0
            while pos < len(trimmed):
                bstart = trimmed.find('{', pos)
                if bstart == -1:
                    break
                bend = trimmed.find('}', bstart + 1)
                if bend == -1:
                    break
                key = parse_brace(trimmed[bstart+1:bend])
                if key >= 0:
                    events.append(('key', key))
                pos = bend + 1
            continue

        # Sleep lines
        if 'sleep' in lower:
            m = _re.search(r'sleep[\s,]+(\d+)', lower)
            if m:
                events.append(('sleep', int(m.group(1))))
            continue

    # Second pass: resolve to PianoRollNote objects
    notes = []
    current_time_ms = 0.0
    octave_offset = 0  # 0 = Mid, +1 = High, -1 = Low
    pending_keys = []  # keys accumulated before a sleep

    def flush_pending(sleep_ms):
        nonlocal current_time_ms
        if not pending_keys:
            # Pure rest
            current_time_ms += sleep_ms
            return
        # All pending keys form a chord at current_time_ms
        # Duration = sleep_ms (or minimum 80ms for visual clarity)
        dur = max(80, sleep_ms) if sleep_ms > 0 else 150
        for key_code in pending_keys:
            semi = KEY_SEMITONES.get(key_code)
            if semi is not None:
                midi_pitch = MID_BASE + (octave_offset * 12) + semi
                midi_pitch = max(0, min(127, midi_pitch))
                notes.append(PianoRollNote(
                    start_ms=current_time_ms,
                    duration_ms=dur,
                    pitch=midi_pitch,
                    velocity=100,
                    track=0
                ))
        pending_keys.clear()
        current_time_ms += sleep_ms

    for etype, evalue in events:
        if etype == 'key':
            if evalue == OCTAVE_UP:
                octave_offset = min(1, octave_offset + 1)
            elif evalue == OCTAVE_DOWN:
                octave_offset = max(-1, octave_offset - 1)
            else:
                pending_keys.append(evalue)
        elif etype == 'sleep':
            flush_pending(evalue)

    # Flush any remaining
    flush_pending(0)

    # Estimate BPM from average sleep time
    sleep_times = [v for t, v in events if t == 'sleep' and v > 0]
    if sleep_times:
        avg_ms = sum(sleep_times) / len(sleep_times)
        bpm = max(30, min(300, 60000.0 / (avg_ms * 2)))  # rough estimate
    else:
        bpm = 120

    return notes, bpm, title, author

class PianoRollWidget(QWidget):
    NOTE_HEIGHT = 14
    PIANO_WIDTH = 50
    RULER_HEIGHT = 24
    SCROLLBAR_SIZE = 14
    GW2_PITCH_MIN = 48   # C3 (Low octave bottom)
    GW2_PITCH_MAX = 83   # B5 (High octave top)

    notesChanged = pyqtSignal()
    selectionChanged = pyqtSignal()  # emitted when note selection or range changes

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(400, 542)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self._notes = []
        self._tracks = []
        self._px_per_ms = 0.5
        self._scroll_x = 0.0
        self._scroll_y = 0
        self._pitch_min = 48
        self._pitch_max = 84
        self._total_ms = 0.0
        self._selecting = False
        self._sel_start = None
        self._sel_end = None
        self._bpm = 120
        self._cursor_ms = -1.0  # playback cursor position (-1 = hidden)
        self._range_start_ms = -1.0  # ruler range selection start (-1 = none)
        self._range_end_ms = -1.0    # ruler range selection end
        self._dragging_range = False

        # Editing state
        self._edit_mode = 'select'  # 'select' or 'draw'
        self._drawing = False       # currently drawing a new note
        self._draw_start_ms = 0.0
        self._draw_pitch = 60
        self._draw_end_ms = 0.0
        self._dragging_notes = False  # dragging selected notes
        self._drag_start_ms = 0.0
        self._drag_start_pitch = 0
        self._drag_offset_ms = 0.0
        self._drag_offset_pitch = 0
        self._resizing = False       # resizing a note's right edge
        self._resize_note = None
        self._resize_orig_dur = 0.0
        self._resize_start_x = 0.0
        self._snap_ms = 0.0  # snap grid in ms (0 = no snap)
        self._gw2_only = False  # True = clamp to GW2 range (New/AHK)

        # Scrollbars
        self._hscroll = QScrollBar(Qt.Orientation.Horizontal, self)
        self._hscroll.setFixedHeight(self.SCROLLBAR_SIZE)
        self._hscroll.valueChanged.connect(self._on_hscroll)
        self._vscroll = QScrollBar(Qt.Orientation.Vertical, self)
        self._vscroll.setFixedWidth(self.SCROLLBAR_SIZE)
        self._vscroll.valueChanged.connect(self._on_vscroll)
        self._updating_scrollbars = False

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._layout_scrollbars()
        self._update_scrollbars()

    def _layout_scrollbars(self):
        w, h = self.width(), self.height()
        self._hscroll.setGeometry(self.PIANO_WIDTH, h - self.SCROLLBAR_SIZE,
                                   w - self.PIANO_WIDTH - self.SCROLLBAR_SIZE, self.SCROLLBAR_SIZE)
        self._vscroll.setGeometry(w - self.SCROLLBAR_SIZE, self.RULER_HEIGHT,
                                   self.SCROLLBAR_SIZE, h - self.RULER_HEIGHT - self.SCROLLBAR_SIZE)

    def _update_scrollbars(self):
        self._updating_scrollbars = True
        # Horizontal
        view_w = max(1, self.width() - self.PIANO_WIDTH - self.SCROLLBAR_SIZE)
        total_px = self._total_ms * self._px_per_ms
        if total_px > view_w:
            self._hscroll.setRange(0, int(total_px - view_w))
            self._hscroll.setPageStep(int(view_w))
            self._hscroll.setValue(int(self._scroll_x * self._px_per_ms))
            self._hscroll.setVisible(True)
        else:
            self._hscroll.setRange(0, 0)
            self._hscroll.setVisible(False)
        # Vertical
        pitch_rows = self._pitch_max - self._pitch_min + 1
        view_h = self.height() - self.RULER_HEIGHT - self.SCROLLBAR_SIZE
        total_row_px = pitch_rows * self.NOTE_HEIGHT
        if total_row_px > view_h:
            max_scroll = pitch_rows - view_h / self.NOTE_HEIGHT
            self._vscroll.setRange(0, int(max_scroll * 100))
            self._vscroll.setPageStep(int(view_h / self.NOTE_HEIGHT * 100))
            self._vscroll.setValue(int(self._scroll_y * 100))
            self._vscroll.setVisible(True)
        else:
            self._vscroll.setRange(0, 0)
            self._vscroll.setVisible(False)
        self._updating_scrollbars = False

    def _on_hscroll(self, value):
        if self._updating_scrollbars:
            return
        self._scroll_x = value / self._px_per_ms
        self.update()

    def _on_vscroll(self, value):
        if self._updating_scrollbars:
            return
        self._scroll_y = value / 100.0
        self.update()

    def setCursorMs(self, ms):
        self._cursor_ms = ms
        self.update()

    def setNotes(self, notes, tracks, bpm=120):
        self._notes = notes
        self._tracks = tracks
        self._bpm = bpm
        # Snap to 1/4 beat (16th note)
        if bpm > 0:
            self._snap_ms = 60000.0 / bpm / 4
        else:
            self._snap_ms = 0
        if self._gw2_only:
            # In GW2 mode, keep the pitch range locked to the instrument
            self._pitch_min = self.GW2_PITCH_MIN
            self._pitch_max = self.GW2_PITCH_MAX
            if notes:
                self._total_ms = max(n.start_ms + n.duration_ms for n in notes) + 1000
            else:
                self._total_ms = 0
        elif notes:
            pitches = [n.pitch for n in notes]
            self._pitch_min = max(0, min(pitches) - 4)
            self._pitch_max = min(127, max(pitches) + 4)
            self._total_ms = max(n.start_ms + n.duration_ms for n in notes) + 1000
        else:
            self._pitch_min = 48
            self._pitch_max = 84
            self._total_ms = 0
        self._scroll_x = 0.0
        self._scroll_y = 0
        # Zoom to show ~8 beats at a readable scale
        if bpm > 0:
            beat_ms = 60000.0 / bpm
            target_beats_visible = 16
            view_w = max(1, self.width() - self.PIANO_WIDTH - self.SCROLLBAR_SIZE)
            self._px_per_ms = view_w / (target_beats_visible * beat_ms)
            self._px_per_ms = max(0.05, min(5.0, self._px_per_ms))
        # Center vertically on the note range
        pitch_rows = self._pitch_max - self._pitch_min + 1
        view_rows = (self.height() - self.RULER_HEIGHT - self.SCROLLBAR_SIZE) / self.NOTE_HEIGHT
        if pitch_rows > view_rows:
            self._scroll_y = max(0, (pitch_rows - view_rows) / 2)
        self._update_scrollbars()
        self.update()

    def getActiveNotes(self):
        """Return list of (time_ms, pitch, velocity) for non-deleted, visible-track notes."""
        visible = set()
        for i, t in enumerate(self._tracks):
            if t.get('visible', True):
                visible.add(i)
        result = []
        for n in self._notes:
            if not n.deleted and n.track in visible:
                result.append((n.start_ms, n.pitch, n.velocity))
        result.sort(key=lambda x: x[0])
        return result

    def getSelectedNotes(self):
        """Return list of (start_ms, duration_ms, pitch, velocity) for selected notes only."""
        result = []
        for n in self._notes:
            if n.selected and not n.deleted:
                if n.track < len(self._tracks) and not self._tracks[n.track].get('visible', True):
                    continue
                result.append((n.start_ms, n.duration_ms, n.pitch, n.velocity))
        result.sort(key=lambda x: x[0])
        return result

    def getRangeNotes(self):
        """Return visible notes within the ruler-selected time range."""
        if self._range_start_ms < 0 or self._range_end_ms < 0:
            return []
        lo = min(self._range_start_ms, self._range_end_ms)
        hi = max(self._range_start_ms, self._range_end_ms)
        visible = set()
        for i, t in enumerate(self._tracks):
            if t.get('visible', True):
                visible.add(i)
        result = []
        for n in self._notes:
            if n.deleted or n.track not in visible:
                continue
            # Include note if it overlaps the range
            if n.start_ms + n.duration_ms >= lo and n.start_ms <= hi:
                result.append((n.start_ms, n.duration_ms, n.pitch, n.velocity))
        result.sort(key=lambda x: x[0])
        return result

    def _filter_in_range(self, notes):
        """Remove notes outside the GW2 instrument pitch range."""
        lo, hi = self.GW2_PITCH_MIN, self.GW2_PITCH_MAX
        return [(s, d, p, v) for s, d, p, v in notes if lo <= p <= hi]

    def getPlaybackNotes(self):
        """Return the best set of notes for playback:
        1. Selected notes if any
        2. Range notes if ruler range is set
        3. All visible notes
        Returns (notes_list, offset_ms) where offset_ms is the start time offset.
        Notes outside the GW2 instrument range are excluded."""
        selected = self.getSelectedNotes()
        if selected:
            offset = selected[0][0] if selected else 0
            adjusted = [(s - offset, d, p, v) for s, d, p, v in selected]
            return self._filter_in_range(adjusted), offset

        ranged = self.getRangeNotes()
        if ranged:
            offset = min(self._range_start_ms, self._range_end_ms)
            adjusted = [(max(0, s - offset), d, p, v) for s, d, p, v in ranged]
            return self._filter_in_range(adjusted), offset

        # All visible
        active = self.getActiveNotes()
        all_notes = []
        visible = set()
        for i, t in enumerate(self._tracks):
            if t.get('visible', True):
                visible.add(i)
        for n in self._notes:
            if not n.deleted and n.track in visible:
                all_notes.append((n.start_ms, n.duration_ms, n.pitch, n.velocity))
        all_notes.sort(key=lambda x: x[0])
        return self._filter_in_range(all_notes), 0

    def getPlaybackMode(self):
        """Return 'selection', 'range', or 'all'."""
        selected = [n for n in self._notes if n.selected and not n.deleted]
        if selected:
            return 'selection'
        if self._range_start_ms >= 0 and self._range_end_ms >= 0:
            lo = min(self._range_start_ms, self._range_end_ms)
            hi = max(self._range_start_ms, self._range_end_ms)
            if hi - lo > 10:
                return 'range'
        return 'all'

    def clearRange(self):
        self._range_start_ms = -1.0
        self._range_end_ms = -1.0
        self.update()
        self.selectionChanged.emit()

    def setTrackVisible(self, track_idx, visible):
        if track_idx < len(self._tracks):
            self._tracks[track_idx]['visible'] = visible
            self.update()

    def _pitch_to_y(self, pitch):
        row = self._pitch_max - pitch
        return self.RULER_HEIGHT + (row - self._scroll_y) * self.NOTE_HEIGHT

    def _ms_to_x(self, ms):
        return self.PIANO_WIDTH + (ms - self._scroll_x) * self._px_per_ms

    def _x_to_ms(self, x):
        return (x - self.PIANO_WIDTH) / self._px_per_ms + self._scroll_x

    def _y_to_pitch(self, y):
        row = (y - self.RULER_HEIGHT) / self.NOTE_HEIGHT + self._scroll_y
        return int(self._pitch_max - row)

    def _snap(self, ms):
        if self._snap_ms > 0:
            return round(ms / self._snap_ms) * self._snap_ms
        return ms

    def setInstrumentRange(self, midi_lo, midi_hi):
        """Set pitch range and adjust widget minimum height to show all rows."""
        self._gw2_only = True
        self.GW2_PITCH_MIN = midi_lo
        self.GW2_PITCH_MAX = midi_hi
        self._pitch_min = midi_lo
        self._pitch_max = midi_hi
        pitch_rows = midi_hi - midi_lo + 1
        min_h = pitch_rows * self.NOTE_HEIGHT + self.RULER_HEIGHT + self.SCROLLBAR_SIZE
        self.setMinimumHeight(min_h)
        self._scroll_y = 0
        self._update_scrollbars()
        self.update()

    def setEditMode(self, mode):
        self._edit_mode = mode
        if mode == 'draw':
            self.setCursor(Qt.CursorShape.CrossCursor)
        else:
            self.setCursor(Qt.CursorShape.ArrowCursor)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        w, h = self.width(), self.height()

        p.fillRect(0, 0, w, h, QColor(30, 30, 30))

        na_x = self.PIANO_WIDTH
        na_y = self.RULER_HEIGHT
        black_semitones = {1, 3, 6, 8, 10}

        # Pitch rows + piano keys
        for pitch in range(self._pitch_min, self._pitch_max + 1):
            y = self._pitch_to_y(pitch)
            if y < na_y - self.NOTE_HEIGHT or y > h:
                continue
            semi = pitch % 12
            is_black = semi in black_semitones
            # Row background
            if is_black:
                p.fillRect(na_x, int(y), w - na_x, self.NOTE_HEIGHT, QColor(25, 25, 25))
            # Out-of-GW2-range shading
            if not self._gw2_only and (pitch < self.GW2_PITCH_MIN or pitch > self.GW2_PITCH_MAX):
                p.fillRect(na_x, int(y), w - na_x, self.NOTE_HEIGHT, QColor(80, 20, 20, 50))
                p.fillRect(0, int(y), self.PIANO_WIDTH - 2, self.NOTE_HEIGHT, QColor(80, 20, 20, 40))
            # Grid line
            if semi == 0:
                p.setPen(QPen(QColor(65, 65, 65), 1))
            else:
                p.setPen(QPen(QColor(42, 42, 42), 1))
            p.drawLine(na_x, int(y), w, int(y))
            # Piano key strip
            if is_black:
                p.fillRect(0, int(y), self.PIANO_WIDTH - 2, self.NOTE_HEIGHT, QColor(40, 40, 40))
            else:
                p.fillRect(0, int(y), self.PIANO_WIDTH - 2, self.NOTE_HEIGHT, QColor(70, 70, 70))
            # Note labels
            note_names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
            octave = pitch // 12 - 1
            name = f"{note_names[semi]}{octave}"
            if is_black:
                p.setPen(QColor(140, 140, 140))
            elif semi == 0:
                p.setPen(QColor(220, 220, 220))
            else:
                p.setPen(QColor(180, 180, 180))
            p.setFont(QFont("monospace", 7))
            p.drawText(2, int(y), self.PIANO_WIDTH - 6, self.NOTE_HEIGHT,
                       Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignRight,
                       name)

        # GW2 range boundary lines
        if not self._gw2_only:
            for boundary_pitch in (self.GW2_PITCH_MIN, self.GW2_PITCH_MAX + 1):
                by = self._pitch_to_y(boundary_pitch)
                if boundary_pitch == self.GW2_PITCH_MIN:
                    by += self.NOTE_HEIGHT  # bottom edge of lowest playable row
                if na_y <= by <= h:
                    p.setPen(QPen(QColor(255, 80, 80, 160), 2, Qt.PenStyle.DashLine))
                    p.drawLine(na_x, int(by), w, int(by))

        # Time grid
        if self._bpm > 0:
            beat_ms = 60000.0 / self._bpm
            first_beat = max(0, int(self._scroll_x / beat_ms))
            p.setFont(QFont("monospace", 7))
            ms = first_beat * beat_ms
            while True:
                x = self._ms_to_x(ms)
                if x > w:
                    break
                if x >= na_x:
                    beat_num = int(round(ms / beat_ms))
                    if beat_num % 4 == 0:
                        p.setPen(QPen(QColor(75, 75, 75), 1))
                        p.drawLine(int(x), na_y, int(x), h)
                    else:
                        p.setPen(QPen(QColor(42, 42, 42), 1))
                        p.drawLine(int(x), na_y, int(x), h)
                ms += beat_ms

        # Range highlight
        if self._range_start_ms >= 0 and self._range_end_ms >= 0:
            rx1 = self._ms_to_x(min(self._range_start_ms, self._range_end_ms))
            rx2 = self._ms_to_x(max(self._range_start_ms, self._range_end_ms))
            if rx2 > na_x and rx1 < w:
                rx1 = max(na_x, int(rx1))
                rx2 = min(w, int(rx2))
                p.fillRect(rx1, na_y, rx2 - rx1, h - na_y, QColor(80, 120, 200, 30))
                p.setPen(QPen(QColor(80, 120, 200, 120), 1))
                p.drawLine(rx1, na_y, rx1, h)
                p.drawLine(rx2, na_y, rx2, h)

        # Ruler
        p.fillRect(0, 0, w, self.RULER_HEIGHT, QColor(45, 45, 45))
        if self._bpm > 0:
            beat_ms = 60000.0 / self._bpm
            first_beat = max(0, int(self._scroll_x / beat_ms))
            ms = first_beat * beat_ms
            p.setFont(QFont("monospace", 8))
            while True:
                x = self._ms_to_x(ms)
                if x > w:
                    break
                if x >= na_x:
                    beat_num = int(round(ms / beat_ms))
                    if beat_num % 4 == 0:
                        bar_num = beat_num // 4 + 1
                        p.setPen(QColor(170, 170, 170))
                        p.drawText(int(x) + 3, 2, 50, self.RULER_HEIGHT - 4,
                                   Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                                   str(bar_num))
                        p.setPen(QPen(QColor(100, 100, 100), 1))
                        p.drawLine(int(x), 0, int(x), self.RULER_HEIGHT)
                ms += beat_ms
        # Range highlight on ruler
        if self._range_start_ms >= 0 and self._range_end_ms >= 0:
            rx1 = self._ms_to_x(min(self._range_start_ms, self._range_end_ms))
            rx2 = self._ms_to_x(max(self._range_start_ms, self._range_end_ms))
            if rx2 > na_x and rx1 < w:
                rx1 = max(na_x, int(rx1))
                rx2 = min(w, int(rx2))
                p.fillRect(rx1, 0, rx2 - rx1, self.RULER_HEIGHT, QColor(80, 120, 200, 80))

        p.setPen(QPen(QColor(80, 80, 80), 1))
        p.drawLine(0, self.RULER_HEIGHT, w, self.RULER_HEIGHT)

        # Notes
        for note in self._notes:
            if note.deleted:
                continue
            if note.track < len(self._tracks) and not self._tracks[note.track].get('visible', True):
                continue
            draw_ms = note.start_ms
            draw_pitch = note.pitch
            if self._dragging_notes and note.selected:
                snap = self._snap_ms if self._snap_ms > 0 else 0
                draw_ms += (self._snap(self._drag_offset_ms) if snap > 0 else self._drag_offset_ms)
                draw_pitch += self._drag_offset_pitch
                draw_ms = max(0, draw_ms)
                draw_pitch = max(self._pitch_min, min(self._pitch_max, draw_pitch))
            x = self._ms_to_x(draw_ms)
            y = self._pitch_to_y(draw_pitch)
            nw = max(2, note.duration_ms * self._px_per_ms)
            if x + nw < na_x or x > w or y + self.NOTE_HEIGHT < na_y or y > h:
                continue
            r, g, b = TRACK_COLORS[note.track % len(TRACK_COLORS)]
            color = QColor(r, g, b)
            out_of_range = not self._gw2_only and (draw_pitch < self.GW2_PITCH_MIN or draw_pitch > self.GW2_PITCH_MAX)
            if out_of_range:
                color = QColor(r // 2, g // 2, b // 2)  # dim out-of-range notes
            if note.selected:
                p.fillRect(int(x), int(y) + 1, int(nw), self.NOTE_HEIGHT - 1, color.lighter(150))
                p.setPen(QPen(QColor(255, 255, 255), 2))
                p.drawRect(int(x), int(y) + 1, int(nw), self.NOTE_HEIGHT - 2)
            else:
                p.fillRect(int(x), int(y) + 1, int(nw), self.NOTE_HEIGHT - 1, color)
                if out_of_range:
                    # Draw a subtle strikethrough to indicate unplayable
                    p.setPen(QPen(QColor(255, 60, 60, 100), 1))
                    p.drawLine(int(x), int(y) + self.NOTE_HEIGHT // 2, int(x) + int(nw), int(y) + self.NOTE_HEIGHT // 2)
            # GW2 timing warning overlay
            if note.warning:
                if note.warning == 'chord':
                    warn_color = QColor(100, 140, 255, 80)  # subtle blue for chords (informational)
                elif note.warning == 'octave_tight':
                    warn_color = QColor(255, 160, 0, 140)
                else:  # too_close
                    warn_color = QColor(255, 200, 0, 120)
                p.setPen(QPen(warn_color, 2))
                p.drawRect(int(x), int(y) + 1, int(nw), self.NOTE_HEIGHT - 2)
                # Small warning triangle at left edge
                p.setBrush(QBrush(warn_color))
                p.setPen(Qt.PenStyle.NoPen)
                tx = int(x) + 2
                ty = int(y) + 2
                tri = QPolygonF([QPointF(tx, ty + 8), QPointF(tx + 4, ty), QPointF(tx + 8, ty + 8)])
                p.drawPolygon(tri)
            # Draw note name on the bar
            _nn = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
            _lbl = f"{_nn[note.pitch % 12]}{note.pitch // 12 - 1}"
            p.setFont(QFont("monospace", 7))
            p.setPen(QColor(0, 0, 0))
            p.drawText(int(x) + 2, int(y) + 1, int(nw) - 2, self.NOTE_HEIGHT - 1,
                       Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, _lbl)

        # Selection rectangle
        if self._selecting and self._sel_start and self._sel_end:
            sx = min(self._sel_start.x(), self._sel_end.x())
            sy = min(self._sel_start.y(), self._sel_end.y())
            sw = abs(self._sel_end.x() - self._sel_start.x())
            sh = abs(self._sel_end.y() - self._sel_start.y())
            p.setPen(QPen(QColor(255, 255, 255, 150), 1))
            p.setBrush(QBrush(QColor(255, 255, 255, 30)))
            p.drawRect(int(sx), int(sy), int(sw), int(sh))

        # Ghost note while drawing
        if self._drawing:
            gx = self._ms_to_x(min(self._draw_start_ms, self._draw_end_ms))
            gy = self._pitch_to_y(self._draw_pitch)
            gw = abs(self._draw_end_ms - self._draw_start_ms) * self._px_per_ms
            gw = max(4, gw)
            p.fillRect(int(gx), int(gy) + 1, int(gw), self.NOTE_HEIGHT - 1,
                       QColor(100, 200, 100, 120))
            p.setPen(QPen(QColor(100, 200, 100, 200), 1))
            p.drawRect(int(gx), int(gy) + 1, int(gw), self.NOTE_HEIGHT - 2)

        # Playback cursor
        if self._cursor_ms >= 0:
            cx = self._ms_to_x(self._cursor_ms)
            if self.PIANO_WIDTH <= cx <= w:
                p.setPen(QPen(QColor(255, 60, 60), 2))
                p.drawLine(int(cx), na_y, int(cx), h)

        # Piano strip border
        p.setPen(QPen(QColor(80, 80, 80), 1))
        p.drawLine(self.PIANO_WIDTH, 0, self.PIANO_WIDTH, h)

        p.end()

    def _note_at(self, pos):
        for note in reversed(self._notes):
            if note.deleted:
                continue
            if note.track < len(self._tracks) and not self._tracks[note.track].get('visible', True):
                continue
            x = self._ms_to_x(note.start_ms)
            y = self._pitch_to_y(note.pitch)
            nw = max(2, note.duration_ms * self._px_per_ms)
            if x <= pos.x() <= x + nw and y <= pos.y() <= y + self.NOTE_HEIGHT:
                return note
        return None

    def _near_right_edge(self, note, pos, threshold=6):
        """Check if pos is near the right edge of a note (for resizing)."""
        x = self._ms_to_x(note.start_ms)
        nw = max(2, note.duration_ms * self._px_per_ms)
        right_x = x + nw
        return abs(pos.x() - right_x) < threshold

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            px, py = event.position().x(), event.position().y()

            # Ruler click = range selection (both modes)
            if py < self.RULER_HEIGHT:
                ms = self._x_to_ms(px)
                self._range_start_ms = max(0, ms)
                self._range_end_ms = self._range_start_ms
                self._dragging_range = True
                for n in self._notes:
                    n.selected = False
                self.update()
                self.selectionChanged.emit()
                return

            # Piano key area - ignore
            if px < self.PIANO_WIDTH:
                return

            note = self._note_at(event.position())
            ctrl = bool(event.modifiers() & Qt.KeyboardModifier.ControlModifier)

            if self._edit_mode == 'draw':
                if note:
                    # In draw mode, clicking existing note = select it for preview/delete
                    if ctrl:
                        note.selected = not note.selected
                    else:
                        for n in self._notes:
                            n.selected = False
                        note.selected = True
                    try:
                        play_preview_note(note.pitch, note.velocity)
                    except Exception:
                        pass
                    self.selectionChanged.emit()
                else:
                    # Draw a new note
                    ms = self._snap(max(0, self._x_to_ms(px)))
                    pitch = self._y_to_pitch(py)
                    if self._gw2_only:
                        pitch = max(self.GW2_PITCH_MIN, min(self.GW2_PITCH_MAX, pitch))
                    else:
                        pitch = max(self._pitch_min, min(self._pitch_max, pitch))
                    self._drawing = True
                    self._draw_start_ms = ms
                    self._draw_end_ms = ms
                    self._draw_pitch = pitch
                    # Clear selection
                    for n in self._notes:
                        n.selected = False
                    self._range_start_ms = -1.0
                    self._range_end_ms = -1.0
                    self.selectionChanged.emit()
                self.update()
                return

            # Select mode
            if note:
                self._range_start_ms = -1.0
                self._range_end_ms = -1.0

                # Check for right-edge resize
                if self._near_right_edge(note, event.position()):
                    if not note.selected:
                        for n in self._notes:
                            n.selected = False
                        note.selected = True
                    self._resizing = True
                    self._resize_note = note
                    self._resize_orig_dur = note.duration_ms
                    self._resize_start_x = px
                    self.selectionChanged.emit()
                    self.update()
                    return

                if ctrl:
                    note.selected = not note.selected
                else:
                    if not note.selected:
                        for n in self._notes:
                            n.selected = False
                        note.selected = True
                    # Start dragging selected notes
                    self._dragging_notes = True
                    self._drag_start_ms = self._x_to_ms(px)
                    self._drag_start_pitch = self._y_to_pitch(py)
                    self._drag_offset_ms = 0.0
                    self._drag_offset_pitch = 0

                try:
                    play_preview_note(note.pitch, note.velocity)
                except Exception:
                    pass
                self.selectionChanged.emit()
            else:
                self._range_start_ms = -1.0
                self._range_end_ms = -1.0
                if not ctrl:
                    for n in self._notes:
                        n.selected = False
                self._selecting = True
                self._sel_start = event.position()
                self._sel_end = event.position()
                self.selectionChanged.emit()
            self.update()

        elif event.button() == Qt.MouseButton.RightButton:
            px, py = event.position().x(), event.position().y()
            if py < self.RULER_HEIGHT:
                # Right-click on ruler: show trim menu
                ms = self._x_to_ms(px)
                self._show_trim_menu(event.globalPosition().toPoint(), ms)
                return
            # Right-click to delete a note (in any mode)
            note = self._note_at(event.position())
            if note:
                note.deleted = True
                note.selected = False
                self.notesChanged.emit()
                self.update()

    def mouseMoveEvent(self, event):
        px, py = event.position().x(), event.position().y()

        if self._drawing:
            ms = self._snap(max(0, self._x_to_ms(px)))
            self._draw_end_ms = ms
            self.update()
            return

        if self._dragging_range:
            ms = self._x_to_ms(px)
            self._range_end_ms = max(0, ms)
            self.update()
            return

        if self._resizing and self._resize_note:
            delta_px = px - self._resize_start_x
            delta_ms = delta_px / self._px_per_ms
            new_dur = max(self._snap_ms if self._snap_ms > 0 else 50,
                          self._resize_orig_dur + delta_ms)
            self._resize_note.duration_ms = self._snap(new_dur) if self._snap_ms > 0 else new_dur
            self.update()
            return

        if self._dragging_notes:
            current_ms = self._x_to_ms(px)
            current_pitch = self._y_to_pitch(py)
            self._drag_offset_ms = current_ms - self._drag_start_ms
            self._drag_offset_pitch = current_pitch - self._drag_start_pitch
            self.update()
            return

        if self._selecting:
            self._sel_end = event.position()
            self.update()
            return

        # Update cursor for resize hint (select mode)
        if self._edit_mode == 'select':
            note = self._note_at(event.position())
            if note and self._near_right_edge(note, event.position()):
                self.setCursor(Qt.CursorShape.SizeHorCursor)
            else:
                self.setCursor(Qt.CursorShape.ArrowCursor)

    def mouseReleaseEvent(self, event):
        if self._drawing:
            # Commit the drawn note
            start = min(self._draw_start_ms, self._draw_end_ms)
            end = max(self._draw_start_ms, self._draw_end_ms)
            dur = end - start
            if dur < (self._snap_ms if self._snap_ms > 0 else 50):
                # Click without drag = place a note with default duration (1 beat)
                dur = self._snap_ms * 4 if self._snap_ms > 0 else 250
            pitch = self._draw_pitch
            if self._gw2_only:
                pitch = max(self.GW2_PITCH_MIN, min(self.GW2_PITCH_MAX, pitch))
            else:
                pitch = max(self._pitch_min, min(self._pitch_max, pitch))
            note = PianoRollNote(start, dur, pitch, 100, 0)
            self._notes.append(note)
            # Extend total if needed
            if start + dur + 1000 > self._total_ms:
                self._total_ms = start + dur + 2000
                self._update_scrollbars()
            self._drawing = False
            self.notesChanged.emit()
            try:
                play_preview_note(pitch, 100)
            except Exception:
                pass
            self.update()
            return

        if self._resizing:
            self._resizing = False
            self._resize_note = None
            self.notesChanged.emit()
            self.update()
            return

        if self._dragging_notes:
            # Apply the drag offset to all selected notes
            snap = self._snap_ms if self._snap_ms > 0 else 0
            offset_ms = self._snap(self._drag_offset_ms) if snap > 0 else self._drag_offset_ms
            offset_pitch = self._drag_offset_pitch
            if abs(offset_ms) > 1 or abs(offset_pitch) > 0:
                for n in self._notes:
                    if n.selected and not n.deleted:
                        n.start_ms = max(0, n.start_ms + offset_ms)
                        pmin = self.GW2_PITCH_MIN if self._gw2_only else self._pitch_min
                        pmax = self.GW2_PITCH_MAX if self._gw2_only else self._pitch_max
                        n.pitch = max(pmin, min(pmax, n.pitch + offset_pitch))
                self.notesChanged.emit()
            self._dragging_notes = False
            self._drag_offset_ms = 0.0
            self._drag_offset_pitch = 0
            self.update()
            return

        if self._dragging_range:
            self._dragging_range = False
            lo = min(self._range_start_ms, self._range_end_ms)
            hi = max(self._range_start_ms, self._range_end_ms)
            if hi - lo < 50:
                self._range_start_ms = -1.0
                self._range_end_ms = -1.0
            self.update()
            self.selectionChanged.emit()
            return

        if self._selecting and self._sel_start and self._sel_end:
            sx = min(self._sel_start.x(), self._sel_end.x())
            sy = min(self._sel_start.y(), self._sel_end.y())
            ex = max(self._sel_start.x(), self._sel_end.x())
            ey = max(self._sel_start.y(), self._sel_end.y())
            if abs(ex - sx) > 3 or abs(ey - sy) > 3:
                for note in self._notes:
                    if note.deleted:
                        continue
                    if note.track < len(self._tracks) and not self._tracks[note.track].get('visible', True):
                        continue
                    nx = self._ms_to_x(note.start_ms)
                    ny = self._pitch_to_y(note.pitch)
                    nw = max(2, note.duration_ms * self._px_per_ms)
                    if nx + nw >= sx and nx <= ex and ny + self.NOTE_HEIGHT >= sy and ny <= ey:
                        note.selected = True
            self._selecting = False
            self._sel_start = None
            self._sel_end = None
            self.update()
            self.selectionChanged.emit()

    def wheelEvent(self, event):
        delta = event.angleDelta().y()
        mods = event.modifiers()
        if mods & Qt.KeyboardModifier.ControlModifier:
            factor = 1.25 if delta > 0 else 1 / 1.25
            mouse_ms = self._x_to_ms(event.position().x())
            self._px_per_ms = max(0.02, min(8.0, self._px_per_ms * factor))
            new_mouse_ms = self._x_to_ms(event.position().x())
            self._scroll_x += mouse_ms - new_mouse_ms
            self._scroll_x = max(0, self._scroll_x)
        elif mods & Qt.KeyboardModifier.ShiftModifier:
            scroll_amount = 300 / self._px_per_ms
            self._scroll_x -= delta / 120 * scroll_amount * 0.3
            self._scroll_x = max(0, min(self._total_ms, self._scroll_x))
        else:
            self._scroll_y -= delta / 120 * 3
            max_scroll = max(0, (self._pitch_max - self._pitch_min + 1) -
                             (self.height() - self.RULER_HEIGHT - self.SCROLLBAR_SIZE) / self.NOTE_HEIGHT)
            self._scroll_y = max(0, min(max_scroll, self._scroll_y))
        self._update_scrollbars()
        self.update()
        event.accept()

    def _compact_after_trim(self, cut_start_ms, cut_end_ms):
        """Shift notes after a deleted region left to close the gap and recalculate duration."""
        gap = cut_end_ms - cut_start_ms
        if gap <= 0:
            return
        for n in self._notes:
            if not n.deleted and n.start_ms >= cut_end_ms:
                n.start_ms -= gap
        active = [n for n in self._notes if not n.deleted]
        if active:
            self._total_ms = max(n.start_ms + n.duration_ms for n in active) + 1000
        else:
            self._total_ms = 0
        self._range_start_ms = -1.0
        self._range_end_ms = -1.0
        self._scroll_x = max(0, min(self._scroll_x, self._total_ms))
        self._update_scrollbars()

    def _show_trim_menu(self, global_pos, ms):
        """Show a context menu to delete/trim notes at a time point or selection range."""
        secs = ms / 1000.0
        menu = QMenu(self)
        before_action = menu.addAction(f"Delete all before {secs:.1f}s")
        after_action = menu.addAction(f"Delete all after {secs:.1f}s")

        # Add selection delete if a range is highlighted
        sel_action = None
        has_range = self._range_start_ms >= 0 and self._range_end_ms >= 0
        if has_range:
            lo = min(self._range_start_ms, self._range_end_ms)
            hi = max(self._range_start_ms, self._range_end_ms)
            if hi - lo > 1:
                sel_action = menu.addAction(f"Delete selection ({lo/1000:.1f}s – {hi/1000:.1f}s)")

        action = menu.exec(global_pos)
        if action is None:
            return
        count = 0
        if action == before_action:
            for n in self._notes:
                if not n.deleted and n.start_ms < ms:
                    n.deleted = True
                    n.selected = False
                    count += 1
            if count:
                self._compact_after_trim(0, ms)
        elif action == after_action:
            for n in self._notes:
                if not n.deleted and n.start_ms >= ms:
                    n.deleted = True
                    n.selected = False
                    count += 1
            if count:
                active = [n for n in self._notes if not n.deleted]
                if active:
                    self._total_ms = max(n.start_ms + n.duration_ms for n in active) + 1000
                else:
                    self._total_ms = 0
                self._update_scrollbars()
        elif action == sel_action and sel_action is not None:
            for n in self._notes:
                if not n.deleted and n.start_ms >= lo and n.start_ms < hi:
                    n.deleted = True
                    n.selected = False
                    count += 1
            if count:
                self._compact_after_trim(lo, hi)
        if count:
            self.notesChanged.emit()
            self.update()

    def ensureCursorVisible(self):
        """Smooth-scroll so the playback cursor stays at ~30% of the view width."""
        if self._cursor_ms < 0:
            return
        view_w = self.width() - self.PIANO_WIDTH - self.SCROLLBAR_SIZE
        target_x = self._cursor_ms - view_w * 0.3 / self._px_per_ms
        self._scroll_x = max(0, target_x)
        self._update_scrollbars()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Delete:
            changed = False
            for n in self._notes:
                if n.selected:
                    n.deleted = True
                    n.selected = False
                    changed = True
            if changed:
                self.notesChanged.emit()
                self.update()
        elif event.key() == Qt.Key.Key_A and event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            for n in self._notes:
                if not n.deleted:
                    n.selected = True
            self.update()
        elif event.key() == Qt.Key.Key_Escape:
            for n in self._notes:
                n.selected = False
            self._range_start_ms = -1.0
            self._range_end_ms = -1.0
            self.selectionChanged.emit()
            self.update()
        elif event.key() == Qt.Key.Key_Z and event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            # Simple undo: undelete all
            for n in self._notes:
                n.deleted = False
            self.notesChanged.emit()
            self.update()
        else:
            super().keyPressEvent(event)


# ── GUI ───────────────────────────────────────────────────────────────────────

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Serenade Music Converter")
        self.setMinimumSize(1100, 800)
        self.resize(1200, 900)
        self.midi_path = ""
        self.mid = None
        self.file_type = "midi"
        self.xml_root = None
        self.settings = QSettings('Serenade', 'MIDIConverter')
        self._pr_notes = []  # PianoRollNote list
        self._current_instrument = GW2_INSTRUMENTS[0]  # default instrument
        self._pr_tracks = []  # track info dicts

        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(6)
        main_layout.setContentsMargins(8, 8, 8, 8)

        # ── Top toolbar ───────────────────────────────────────────────
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        new_btn = QPushButton("New ▾")
        new_btn.setMinimumHeight(30)
        new_btn.setToolTip("Create a new blank song for a GW2 instrument")
        new_menu = QMenu(new_btn)
        for _inst_name, _inst_key, _inst_lo, _inst_hi in GW2_INSTRUMENTS:
            _oct = (_inst_hi - _inst_lo) // 12
            act = new_menu.addAction(f"{_inst_name}  ({_inst_key}, {_oct} oct)")
            _data = (_inst_name, _inst_key, _inst_lo, _inst_hi)
            act.triggered.connect(lambda checked, d=_data: self._new_song_instrument(d))
        new_btn.setMenu(new_menu)
        toolbar.addWidget(new_btn)

        import_btn = QPushButton("Import File...")
        import_btn.setMinimumHeight(30)
        import_btn.clicked.connect(self.browse_input)
        toolbar.addWidget(import_btn)

        self.input_label = QLabel("No file loaded")
        self.input_label.setStyleSheet("color: #888;")
        toolbar.addWidget(self.input_label, 1)

        toolbar.addWidget(QLabel("Title:"))
        self.title_edit = QLineEdit()
        self.title_edit.setPlaceholderText("Song title")
        self.title_edit.setFixedWidth(180)
        toolbar.addWidget(self.title_edit)

        toolbar.addWidget(QLabel("Artist:"))
        self.author_edit = QLineEdit()
        self.author_edit.setPlaceholderText("Artist")
        self.author_edit.setFixedWidth(140)
        toolbar.addWidget(self.author_edit)

        toolbar.addSpacing(12)
        self._analyze_btn = QPushButton("⚠ Analyze")
        self._analyze_btn.setMinimumHeight(30)
        self._analyze_btn.setToolTip("Check for notes that may be missed in GW2")
        self._analyze_btn.clicked.connect(self._analyze_gw2)
        toolbar.addWidget(self._analyze_btn)

        self._fix_btn = QPushButton("🔧 Fix")
        self._fix_btn.setMinimumHeight(30)
        self._fix_btn.setToolTip("Auto-fix timing issues for GW2 playback")
        self._fix_btn.clicked.connect(self._fix_gw2)
        toolbar.addWidget(self._fix_btn)

        toolbar.addSpacing(12)
        self._mode_btn = QPushButton("✏ Draw")
        self._mode_btn.setCheckable(True)
        self._mode_btn.setMinimumHeight(30)
        self._mode_btn.setFixedWidth(80)
        self._mode_btn.setToolTip("Toggle Draw mode to create notes (click grid to place)")
        self._mode_btn.toggled.connect(self._toggle_edit_mode)
        toolbar.addWidget(self._mode_btn)

        main_layout.addLayout(toolbar)

        # ── Main content: left panel + piano roll ─────────────────────
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Left panel
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 4, 0)
        left_layout.setSpacing(6)

        # Track controls
        self.tracks_group = QGroupBox("Tracks")
        tracks_layout = QVBoxLayout(self.tracks_group)
        tracks_layout.setSpacing(2)

        track_btn_row = QHBoxLayout()
        self.select_all_btn = QPushButton("All")
        self.select_all_btn.setFixedWidth(50)
        self.select_all_btn.clicked.connect(self._select_all_tracks)
        self.select_none_btn = QPushButton("None")
        self.select_none_btn.setFixedWidth(50)
        self.select_none_btn.clicked.connect(self._select_no_tracks)
        track_btn_row.addWidget(self.select_all_btn)
        track_btn_row.addWidget(self.select_none_btn)
        track_btn_row.addStretch()
        tracks_layout.addLayout(track_btn_row)

        self.track_list = QListWidget()
        self.track_list.setMinimumHeight(80)
        self.track_list.setMaximumHeight(160)
        tracks_layout.addWidget(self.track_list)

        oct_btn_row = QHBoxLayout()
        self.oct_down_btn = QPushButton("Octave −")
        self.oct_down_btn.setToolTip("Shift selected track down one octave")
        self.oct_down_btn.clicked.connect(lambda: self._shift_track_octave(-12))
        self.oct_up_btn = QPushButton("Octave +")
        self.oct_up_btn.setToolTip("Shift selected track up one octave")
        self.oct_up_btn.clicked.connect(lambda: self._shift_track_octave(12))
        oct_btn_row.addWidget(self.oct_down_btn)
        oct_btn_row.addWidget(self.oct_up_btn)
        oct_btn_row.addStretch()
        tracks_layout.addLayout(oct_btn_row)

        left_layout.addWidget(self.tracks_group)

        # Settings
        settings_group = QGroupBox("Settings")
        settings_layout = QVBoxLayout(settings_group)
        settings_layout.setSpacing(4)

        settings_layout.addWidget(QLabel("Transpose:"))
        self.transpose_combo = QComboBox()
        self.transpose_combo.addItem("Auto (maximize white keys)", None)
        for i in range(12):
            self.transpose_combo.addItem(f"+{i} semitones" if i else "None (keep original)", i)
        settings_layout.addWidget(self.transpose_combo)

        settings_layout.addWidget(QLabel("Chord window:"))
        self.chord_window_combo = QComboBox()
        self.chord_window_combo.addItem("Off (simultaneous only)", 5)
        self.chord_window_combo.addItem("Light (250ms)", 250)
        self.chord_window_combo.addItem("Medium (500ms)", 500)
        self.chord_window_combo.addItem("Heavy (850ms)", 850)
        settings_layout.addWidget(self.chord_window_combo)

        self.use_chords_cb = QCheckBox("Use GW2 chord mode")
        self.use_chords_cb.setToolTip("Substitute detected major/minor triads with\n"
                                       "GW2's built-in chord keypresses for better sound.")
        self.use_chords_cb.setChecked(False)
        settings_layout.addWidget(self.use_chords_cb)

        settings_layout.addWidget(QLabel("Instrument:"))
        self.instrument_combo = QComboBox()
        for _inst_name, _inst_key, _inst_lo, _inst_hi in GW2_INSTRUMENTS:
            _oct = (_inst_hi - _inst_lo) // 12
            self.instrument_combo.addItem(f"{_inst_name} ({_inst_key}, {_oct} oct)",
                                          (_inst_name, _inst_key, _inst_lo, _inst_hi))
        self.instrument_combo.setCurrentIndex(0)  # Default to Piano
        self.instrument_combo.currentIndexChanged.connect(self._on_instrument_changed)
        settings_layout.addWidget(self.instrument_combo)

        settings_layout.addStretch()
        left_layout.addWidget(settings_group)

        # Piano roll help
        help_label = QLabel("Scroll: wheel\nZoom: Ctrl+wheel\nH-scroll: Shift+wheel\nSelect: click/drag\nDelete: Del key\nUndo: Ctrl+Z")
        help_label.setStyleSheet("color: #666; font-size: 10px;")
        help_label.setWordWrap(True)
        left_layout.addWidget(help_label)

        left_widget.setFixedWidth(210)
        splitter.addWidget(left_widget)

        # Piano Roll
        self.piano_roll = PianoRollWidget()
        splitter.addWidget(self.piano_roll)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        main_layout.addWidget(splitter, 1)

        self.piano_roll.selectionChanged.connect(self._update_play_btn_text)
        self.piano_roll.notesChanged.connect(self._on_notes_changed)

        # ── Transport bar ─────────────────────────────────────────────
        transport = QHBoxLayout()
        transport.setSpacing(6)

        self.play_btn = QPushButton("▶ Play")
        self.play_btn.setMinimumHeight(30)
        self.play_btn.setFixedWidth(80)
        self.play_btn.setEnabled(False)
        self.play_btn.clicked.connect(self._playback_toggle)
        transport.addWidget(self.play_btn)

        self.stop_btn = QPushButton("■ Stop")
        self.stop_btn.setMinimumHeight(30)
        self.stop_btn.setFixedWidth(80)
        self.stop_btn.setEnabled(False)
        self.stop_btn.clicked.connect(self._playback_stop)
        transport.addWidget(self.stop_btn)

        self.gw2_preview_cb = QCheckBox("GW2 Preview")
        self.gw2_preview_cb.setToolTip("Simulate GW2 playback constraints:\n"
                                        "• Flat velocity (no dynamics)\n"
                                        "• Minimum note gaps (50ms)\n"
                                        "• Octave change delay (60ms)\n"
                                        "• Capped sustain (~800ms)")
        self.gw2_preview_cb.setChecked(False)
        self.gw2_preview_cb.toggled.connect(self._on_gw2_preview_toggled)
        transport.addWidget(self.gw2_preview_cb)

        self.playback_slider = QProgressBar()
        self.playback_slider.setRange(0, 1000)
        self.playback_slider.setValue(0)
        self.playback_slider.setTextVisible(False)
        self.playback_slider.setFixedHeight(16)
        transport.addWidget(self.playback_slider, 1)

        self.playback_time_label = QLabel("0:00 / 0:00")
        self.playback_time_label.setStyleSheet("color: #aaa; font-size: 11px;")
        transport.addWidget(self.playback_time_label)

        self.convert_btn = QPushButton("Save AHK")
        self.convert_btn.setEnabled(False)
        self.convert_btn.setMinimumHeight(30)
        font = self.convert_btn.font()
        font.setBold(True)
        self.convert_btn.setFont(font)
        self.convert_btn.setStyleSheet("QPushButton { background-color: #2d5a2d; } QPushButton:hover { background-color: #3a7a3a; }")
        self.convert_btn.clicked.connect(self.do_convert)
        transport.addWidget(self.convert_btn)

        self.export_xml_btn = QPushButton("Save MusicXML")
        self.export_xml_btn.setEnabled(False)
        self.export_xml_btn.setMinimumHeight(30)
        self.export_xml_btn.setToolTip("Convert loaded MIDI to MusicXML for editing in MuseScore")
        self.export_xml_btn.clicked.connect(self.do_export_musicxml)
        transport.addWidget(self.export_xml_btn)

        self.submit_btn = QPushButton("Submit Song")
        self.submit_btn.setEnabled(False)
        self.submit_btn.setMinimumHeight(30)
        self.submit_btn.setToolTip("Submit your song for inclusion in the Serenade music library")
        self.submit_btn.clicked.connect(self.do_submit_song)
        transport.addWidget(self.submit_btn)

        main_layout.addLayout(transport)

        # Playback state
        self._playback_sound = None
        self._playback_audio = None
        self._playback_total_ms = 0
        self._playback_start_time = 0
        self._playback_paused_at = 0
        self._playback_offset_ms = 0
        self._playback_playing = False
        self._playback_timer = QTimer()
        self._playback_timer.setInterval(30)
        self._playback_timer.timeout.connect(self._playback_tick)


        # ── Log ───────────────────────────────────────────────────────
        log_header = QHBoxLayout()
        log_header.addWidget(QLabel("Log"))
        log_header.addStretch()
        copy_log_btn = QPushButton("Copy")
        copy_log_btn.setFixedWidth(50)
        copy_log_btn.clicked.connect(lambda: QApplication.clipboard().setText(self.log_text.toPlainText()))
        log_header.addWidget(copy_log_btn)
        main_layout.addLayout(log_header)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("monospace", 9))
        self.log_text.setMaximumHeight(80)
        self.log_text.setPlaceholderText("Log...")
        main_layout.addWidget(self.log_text)


    def log(self, msg):
        self.log_text.append(msg)

    def _populate_piano_roll(self):
        """Populate the piano roll from the currently loaded MIDI."""
        if not self.mid:
            return
        self.piano_roll._gw2_only = False
        self.piano_roll.setMinimumHeight(400)  # Reset to flexible height
        self.transpose_combo.setEnabled(True)
        self.chord_window_combo.setEnabled(True)
        self.tracks_group.setEnabled(True)
        # Reset GW2 range to current instrument selection
        data = self.instrument_combo.currentData()
        if data:
            self.piano_roll.GW2_PITCH_MIN = data[2]
            self.piano_roll.GW2_PITCH_MAX = data[3]
        notes, bpm = extract_notes_with_duration(self.mid)
        self._pr_notes = notes
        # Build track info
        track_indices = set(n.track for n in notes)
        tracks_info = get_track_info(self.midi_path)
        tracks_map = {idx: name for idx, name, count, rng, dup in tracks_info[0]}
        self._pr_tracks = []
        for tidx in sorted(track_indices):
            name = tracks_map.get(tidx, f"Track {tidx}")
            note_count = sum(1 for n in notes if n.track == tidx)
            if note_count == 0:
                continue
            color_idx = len(self._pr_tracks) % len(TRACK_COLORS)
            self._pr_tracks.append({
                'index': tidx,
                'name': name,
                'visible': True,
                'color': TRACK_COLORS[color_idx],
                'notes': note_count,
            })
        # Map note track indices to sequential indices for the piano roll
        track_remap = {}
        for i, t in enumerate(self._pr_tracks):
            track_remap[t['index']] = i
        for n in self._pr_notes:
            n.track = track_remap.get(n.track, 0)

        self.piano_roll.setNotes(self._pr_notes, self._pr_tracks, bpm)
        self._populate_track_list()
        self.play_btn.setEnabled(bool(self._pr_notes))

    def _populate_track_list(self):
        """Populate the track checkbox list from piano roll track info."""
        self.track_list.clear()
        for i, t in enumerate(self._pr_tracks):
            r, g, b = TRACK_COLORS[i % len(TRACK_COLORS)]
            label = f"{t['name']} ({t['notes']})"
            item = QListWidgetItem(label)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked if t.get('visible', True) else Qt.CheckState.Unchecked)
            item.setData(Qt.ItemDataRole.UserRole, i)
            item.setForeground(QColor(r, g, b))
            self.track_list.addItem(item)
        # Connect itemChanged to toggle track visibility
        try:
            self.track_list.itemChanged.disconnect()
        except Exception:
            pass
        self.track_list.itemChanged.connect(self._on_track_toggled)

    def _on_track_toggled(self, item):
        idx = item.data(Qt.ItemDataRole.UserRole)
        visible = item.checkState() == Qt.CheckState.Checked
        if idx is not None and idx < len(self._pr_tracks):
            self._pr_tracks[idx]['visible'] = visible
            self.piano_roll.setTrackVisible(idx, visible)

    def _select_all_tracks(self):
        for i in range(self.track_list.count()):
            self.track_list.item(i).setCheckState(Qt.CheckState.Checked)

    def _select_no_tracks(self):
        for i in range(self.track_list.count()):
            self.track_list.item(i).setCheckState(Qt.CheckState.Unchecked)

    def _shift_track_octave(self, semitones):
        """Shift all notes in the selected track by the given number of semitones."""
        item = self.track_list.currentItem()
        if item is None:
            self.log("Select a track first.")
            return
        idx = item.data(Qt.ItemDataRole.UserRole)
        if idx is None:
            return
        count = 0
        for note in self._pr_notes:
            if note.track == idx and not note.deleted:
                note.pitch += semitones
                count += 1
        if count:
            direction = "up" if semitones > 0 else "down"
            self.log(f"Shifted {count} notes in '{self._pr_tracks[idx]['name']}' {direction} one octave")
            self.piano_roll.update()

    def _new_song_instrument(self, inst_data):
        """Create a new blank song for a specific GW2 instrument."""
        inst_name, inst_key, midi_lo, midi_hi = inst_data
        self.midi_path = ""
        self.mid = None
        self.xml_root = None
        self.file_type = "composed"
        self._current_instrument = inst_data
        self._pr_notes = []
        self._pr_tracks = [{
            'index': 0,
            'name': inst_name,
            'visible': True,
            'color': TRACK_COLORS[0],
            'notes': 0,
        }]
        bpm = 120
        self.piano_roll.setInstrumentRange(midi_lo, midi_hi)
        self.piano_roll.setNotes(self._pr_notes, self._pr_tracks, bpm)
        self.transpose_combo.setEnabled(False)
        self.chord_window_combo.setEnabled(False)
        self.tracks_group.setEnabled(False)
        self.piano_roll._total_ms = 32 * 4 * (60000.0 / bpm)  # 32 bars
        self.piano_roll._scroll_x = 0
        self.piano_roll._scroll_y = 0
        self.piano_roll._update_scrollbars()
        self.piano_roll.update()

        self._populate_track_list()
        self.play_btn.setEnabled(True)
        self.convert_btn.setEnabled(True)
        self.export_xml_btn.setEnabled(False)
        self.submit_btn.setEnabled(True)

        nn = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
        lo_name = f"{nn[midi_lo % 12]}{midi_lo // 12 - 1}"
        hi_name = f"{nn[midi_hi % 12]}{midi_hi // 12 - 1}"
        octaves = (midi_hi - midi_lo) // 12

        self.input_label.setText(f"New: {inst_name}")
        self.input_label.setStyleSheet("color: #8f8;")
        self.title_edit.clear()
        self.author_edit.clear()
        self.log_text.clear()
        self.log(f"New {inst_name} song created (120 BPM, 32 bars)")
        self.log(f"Key: {inst_key} | Range: {lo_name}-{hi_name} ({octaves} octaves)")
        self.log("Switch to Draw mode to start placing notes.")
        self.log("Right-click to delete notes. Ctrl+Z to undo.")

        # Set instrument combo to match
        for i in range(self.instrument_combo.count()):
            d = self.instrument_combo.itemData(i)
            if d and d[0] == inst_name:
                self.instrument_combo.blockSignals(True)
                self.instrument_combo.setCurrentIndex(i)
                self.instrument_combo.blockSignals(False)
                break

        # Auto-switch to draw mode
        self._mode_btn.setChecked(True)

    def _analyze_gw2(self):
        """Analyze notes for GW2 timing issues."""
        if not self._pr_notes:
            self.log("No notes to analyze.")
            return
        overlaps, too_close, octave_tight = analyze_gw2_issues(self._pr_notes)
        fixable = too_close + octave_tight
        self.piano_roll.update()
        if overlaps:
            self.log(f"ℹ {overlaps} chord notes (blue) — these are fine, sent as rapid keypresses")
        if fixable == 0:
            self.log("✓ No GW2 timing issues found!")
        else:
            self.log(f"⚠ Found {fixable} timing issues:")
            if too_close:
                self.log(f"  • {too_close} notes too close together (yellow) — need ≥{GW2_MIN_NOTE_DELAY_MS}ms gap")
            if octave_tight:
                self.log(f"  • {octave_tight} tight octave changes (orange) — need ≥{GW2_MIN_NOTE_DELAY_MS + GW2_OCTAVE_SWAP_DELAY_MS}ms gap")
            self.log("Click 🔧 Fix to auto-resolve these issues.")

    def _fix_gw2(self):
        """Auto-fix GW2 timing issues."""
        if not self._pr_notes:
            self.log("No notes to fix.")
            return
        # Save undo state
        if hasattr(self.piano_roll, '_undo_stack'):
            import copy
            self.piano_roll._undo_stack.append(copy.deepcopy(self._pr_notes))
        # Pass instrument range to fixer
        fix_gw2_issues._pitch_lo = self.piano_roll.GW2_PITCH_MIN
        fix_gw2_issues._pitch_hi = self.piano_roll.GW2_PITCH_MAX
        fixes = fix_gw2_issues(self._pr_notes)
        if fixes == 0:
            self.log("✓ No fixes needed.")
            return
        # Re-analyze to update warnings
        overlaps, too_close, octave_tight = analyze_gw2_issues(self._pr_notes)
        remaining = overlaps + too_close + octave_tight
        self.piano_roll.update()
        self._on_notes_changed()
        self.log(f"🔧 Applied {fixes} fixes.")
        fixable_remaining = too_close + octave_tight
        if fixable_remaining:
            self.log(f"  ⚠ {fixable_remaining} timing issues remain — run Fix again or adjust manually.")
        else:
            self.log("  ✓ All GW2 timing issues resolved!")
        if overlaps:
            self.log(f"  ℹ {overlaps} chord notes remain — these are fine.")

    def _on_instrument_changed(self, index):
        """Update GW2 range boundaries when instrument changes."""
        data = self.instrument_combo.currentData()
        if not data:
            return
        inst_name, inst_key, midi_lo, midi_hi = data
        self._current_instrument = data
        if self.file_type in ("composed", "ahk"):
            self.piano_roll.setInstrumentRange(midi_lo, midi_hi)
        else:
            self.piano_roll.GW2_PITCH_MIN = midi_lo
            self.piano_roll.GW2_PITCH_MAX = midi_hi
            self.piano_roll.update()

    def _on_notes_changed(self):
        """Called when notes are added/deleted/modified in the piano roll."""
        # Update track note counts
        for t in self._pr_tracks:
            tidx_seq = self._pr_tracks.index(t)
            t['notes'] = sum(1 for n in self._pr_notes if not n.deleted and n.track == tidx_seq)
        self._populate_track_list()

    def _toggle_edit_mode(self, checked):
        if checked:
            self._mode_btn.setText("✏ Draw")
            self._mode_btn.setStyleSheet("background-color: #365; color: #8f8;")
            self.piano_roll.setEditMode('draw')
        else:
            self._mode_btn.setText("⬚ Select")
            self._mode_btn.setStyleSheet("")
            self.piano_roll.setEditMode('select')

    def browse_input(self):
        last_input_dir = self.settings.value('last_input_dir', '')
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Music File", last_input_dir,
            "Music Files (*.mid *.midi *.musicxml *.mxl *.ahk);;MIDI Files (*.mid *.midi);;MusicXML Files (*.musicxml *.mxl);;AHK Scripts (*.ahk);;All Files (*)")
        if not path:
            return

        self.settings.setValue('last_input_dir', os.path.dirname(path))
        ext = os.path.splitext(path)[1].lower()

        self.midi_path = path
        self.input_label.setText(os.path.basename(path))
        self.input_label.setStyleSheet("color: #ccc;")

        if ext == '.ahk':
            self.file_type = "ahk"
            self.mid = None
            self.xml_root = None
            try:
                notes, bpm, title, author = parse_ahk_to_notes(path)
                self._pr_notes = notes
                self._pr_tracks = [{
                    'index': 0,
                    'name': 'AHK Script',
                    'visible': True,
                    'color': TRACK_COLORS[0],
                    'notes': len(notes),
                }]
                self.piano_roll.setInstrumentRange(48, 84)
                self.piano_roll.setNotes(self._pr_notes, self._pr_tracks, bpm)
                self.transpose_combo.setEnabled(False)
                self.chord_window_combo.setEnabled(False)
                self.tracks_group.setEnabled(False)
                self._populate_track_list()
                self.play_btn.setEnabled(bool(self._pr_notes))
                if title:
                    self.title_edit.setText(title)
                if author:
                    self.author_edit.setText(author)
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to parse AHK: {e}")
                return
        elif ext in ('.musicxml', '.mxl', '.xml'):
            self.file_type = "musicxml"
            try:
                self.xml_root = parse_musicxml_file(path)
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to parse MusicXML: {e}")
                return
            # For MusicXML, also try to load as MIDI via music21 for piano roll
            self.mid = None
            try:
                from music21 import converter as m21converter
                import tempfile
                score = m21converter.parse(path)
                tmp = tempfile.NamedTemporaryFile(suffix='.mid', delete=False)
                score.write('midi', fp=tmp.name)
                tmp.close()
                self.mid = mido.MidiFile(tmp.name)
                self._populate_piano_roll()
            except Exception:
                pass  # Piano roll won't show for MusicXML without music21
        else:
            self.file_type = "midi"
            self.xml_root = None
            try:
                self.mid = mido.MidiFile(path)
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to read MIDI: {e}")
                return
            try:
                self._populate_piano_roll()
            except Exception as e:
                self.log(f"ERROR loading MIDI: {e}")
                import traceback; traceback.print_exc()
                return

        # Auto-fill metadata
        basename = os.path.splitext(os.path.basename(path))[0]
        basename = re.sub(r'[\u2010-\u2015\u2212]', '-', basename)

        if self.file_type == "ahk":
            pass  # title/author already set from AHK metadata
        elif self.file_type == "musicxml" and self.xml_root is not None:
            meta = extract_musicxml_metadata(self.xml_root)
            title, artist = meta.get('title', ''), meta.get('artist', '')
            if title:
                self.title_edit.setText(title)
                self.author_edit.setText(artist)
            else:
                self.title_edit.setText(basename)
                self.author_edit.clear()
        else:
            suggestions = generate_metadata_suggestions(path, self.mid)
            if suggestions:
                self.title_edit.setText(suggestions[0][0])
                self.author_edit.setText(suggestions[0][1])
            else:
                self.title_edit.setText(basename)
                self.author_edit.clear()

        # Auto-fill output path
        last_output_dir = self.settings.value('last_output_dir', '')
        if last_output_dir:
            pass  # output path set at save time
        else:
            pass  # output path set at save time

        self.convert_btn.setEnabled(True)
        self.export_xml_btn.setEnabled(self.file_type == "midi")
        self.submit_btn.setEnabled(True)
        self.log_text.clear()
        self.log(f"Loaded: {os.path.basename(path)}")
        if self._pr_tracks:
            self.log(f"Tracks: {len(self._pr_tracks)}, Notes: {len([n for n in self._pr_notes if not n.deleted])}")

    def _flash_button(self, btn, text, color):
        original_text = btn.text()
        original_style = btn.styleSheet()
        btn.setText(text)
        btn.setStyleSheet(f"background-color: {color};")
        QTimer.singleShot(1500, lambda: (btn.setText(original_text), btn.setStyleSheet(original_style)))

    def do_submit_song(self):
        """Open a dialog explaining the submission process, then open the GitHub issue template."""
        title = self.title_edit.text().strip() or ""
        author = self.author_edit.text().strip() or ""
        inst_data = self.instrument_combo.currentData()
        instrument = inst_data[0] if inst_data else "Ornate Grand Piano"

        dlg = QMessageBox(self)
        dlg.setWindowTitle("Submit Song")
        dlg.setIcon(QMessageBox.Icon.Information)
        dlg.setText(
            "This will open a GitHub page where you can submit your song "
            "to the Serenade music library.\n\n"
            "What happens next:\n"
            "1. Your browser will open a pre-filled song submission form\n"
            "2. Drag and drop your saved .ahk file into the file field\n"
            "3. Review the details and click 'Submit new issue'\n\n"
            "A GitHub account is required. Your submission will be reviewed "
            "before being added to the library."
        )
        continue_btn = dlg.addButton("Continue", QMessageBox.ButtonRole.AcceptRole)
        dlg.addButton("Cancel", QMessageBox.ButtonRole.RejectRole)
        dlg.exec()

        if dlg.clickedButton() != continue_btn:
            return

        params = {
            "template": "song-submission.yml",
            "title": f"[Song] {title}" if title else "[Song] ",
        }
        if title:
            params["song-title"] = title
        if author:
            params["artist"] = author
        if instrument:
            params["instrument"] = instrument

        url = "https://github.com/PieOrCake/serenade/issues/new?" + urllib.parse.urlencode(params)
        webbrowser.open(url)
        self.log("Opened song submission page in browser.")

    def do_export_musicxml(self):
        """Convert loaded MIDI to MusicXML using music21."""
        if not self.midi_path:
            QMessageBox.warning(self, "Warning", "No MIDI file loaded.")
            return

        base = os.path.splitext(self.midi_path)[0]
        default_path = base + '.musicxml'
        last_output_dir = self.settings.value('last_output_dir', '')
        if last_output_dir:
            default_path = os.path.join(last_output_dir,
                os.path.splitext(os.path.basename(self.midi_path))[0] + '.musicxml')

        path, _ = QFileDialog.getSaveFileName(
            self, "Save MusicXML File", default_path,
            "MusicXML Files (*.musicxml);;All Files (*)")
        if not path:
            return

        self.log_text.clear()
        self.log("Converting MIDI to MusicXML...")
        self.export_xml_btn.setEnabled(False)
        QApplication.processEvents()

        try:
            from music21 import converter as m21converter
            from music21 import stream, note as m21note, meter
            score = m21converter.parse(self.midi_path)

            # Clean up notation: fix incomplete measures, pad shorter parts
            for part in score.parts:
                part.makeRests(fillGaps=True, inPlace=True)
                part.makeNotation(inPlace=True)

            maxMeasures = max(
                len(list(p.getElementsByClass('Measure'))) for p in score.parts
            )
            for part in score.parts:
                measures = list(part.getElementsByClass('Measure'))
                if len(measures) < maxMeasures:
                    ts = None
                    for m in reversed(measures):
                        tsList = m.getElementsByClass('TimeSignature')
                        if tsList:
                            ts = tsList[0]
                            break
                    if ts is None:
                        ts = meter.TimeSignature('4/4')
                    for mNum in range(len(measures) + 1, maxMeasures + 1):
                        restMeasure = stream.Measure(number=mNum)
                        r = m21note.Rest(quarterLength=ts.barDuration.quarterLength)
                        restMeasure.append(r)
                        part.append(restMeasure)

            title = self.title_edit.text().strip()
            artist = self.author_edit.text().strip()
            if title:
                score.metadata.title = title
            if artist:
                score.metadata.composer = artist

            score.write('musicxml', fp=path)
            self.log(f"MusicXML saved to: {path}")
            self.log(f"Parts: {len(score.parts)}")
            for i, part in enumerate(score.parts):
                name = part.partName or f"Part {i+1}"
                notes = len(part.flatten().notes)
                self.log(f"  Part {i+1}: {name} ({notes} notes)")
            self.settings.setValue('last_output_dir', os.path.dirname(path))
        except Exception as e:
            self.log(f"Error: {e}")
            QMessageBox.critical(self, "Export Error", f"Failed to convert MIDI to MusicXML:\n{e}")
        finally:
            self.export_xml_btn.setEnabled(self.file_type == "midi")

    def browse_output(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save AHK File", self.output_path.text(),
            "AHK Scripts (*.ahk);;All Files (*)")
        if path:
            self.output_path.setText(path)
            self.settings.setValue('last_output_dir', os.path.dirname(path))

    def do_convert(self):
        if not self.midi_path and not self._pr_notes:
            return

        # Suggest a filename: Title - Author.ahk (spaces → underscores)
        title_text = self.title_edit.text().strip()
        author_text = self.author_edit.text().strip()
        if title_text and author_text:
            suggested = f"{title_text.replace(' ', '_')}-{author_text.replace(' ', '_')}.ahk"
        elif title_text:
            suggested = title_text.replace(' ', '_') + '.ahk'
        elif self.midi_path:
            suggested = os.path.splitext(os.path.basename(self.midi_path))[0].replace(' ', '_') + '.ahk'
        else:
            suggested = 'composition.ahk'

        last_dir = self.settings.value('last_output_dir', '')
        if last_dir:
            suggested = os.path.join(last_dir, suggested)

        output, _ = QFileDialog.getSaveFileName(
            self, "Save AHK File", suggested, "AHK Files (*.ahk);;All Files (*)")
        if not output:
            return

        self.settings.setValue('last_output_dir', os.path.dirname(output))

        title = self.title_edit.text().strip() or None
        author = self.author_edit.text().strip() or None

        self.log_text.clear()
        src_name = os.path.basename(self.midi_path) if self.midi_path else "Composition"
        self.log(f"Converting ({self.file_type.upper()}): {src_name}")

        QApplication.processEvents()

        transpose_data = self.transpose_combo.currentData()
        chord_window = self.chord_window_combo.currentData()

        # Use piano roll notes if available (allows editing)
        if self._pr_notes:
            active_notes = self.piano_roll.getActiveNotes()
            if not active_notes:
                QMessageBox.warning(self, "Warning", "No visible notes to convert. Check track visibility.")
                return
            self.log(f"Notes from piano roll: {len(active_notes)} (after edits)")
            inst_data = self.instrument_combo.currentData()
            inst_name = inst_data[0] if inst_data else None
            success, log_lines = convert(self.midi_path or "composition", output, None, title, author,
                                         transpose_data, chord_window_ms=chord_window,
                                         notes_override=active_notes,
                                         use_chords=self.use_chords_cb.isChecked(),
                                         instrument=inst_name)
        elif self.file_type == "musicxml" and self.xml_root is not None:
            xml_notes = extract_notes_musicxml(self.xml_root, None)
            inst_data = self.instrument_combo.currentData()
            inst_name = inst_data[0] if inst_data else None
            success, log_lines = convert(self.midi_path, output, None, title, author,
                                         transpose_data, chord_window_ms=chord_window,
                                         notes_override=xml_notes,
                                         use_chords=self.use_chords_cb.isChecked(),
                                         instrument=inst_name)
        else:
            inst_data = self.instrument_combo.currentData()
            inst_name = inst_data[0] if inst_data else None
            success, log_lines = convert(self.midi_path, output, None, title, author,
                                         transpose_data, chord_window_ms=chord_window,
                                         use_chords=self.use_chords_cb.isChecked(),
                                         instrument=inst_name)

        for line in log_lines:
            self.log(line)

        if success:
            self.log("")
            self.log(f"Done! Output: {output}")
            self._flash_button(self.convert_btn, "Done!", "#4a7")
        else:
            self.log("")
            self.log("ERROR: Conversion failed. Check the log above.")
            self._flash_button(self.convert_btn, "Failed!", "#c44")

    def _update_play_btn_text(self):
        """Update play button text based on current selection state."""
        if self._playback_playing:
            return
        mode = self.piano_roll.getPlaybackMode()
        if mode == 'selection':
            count = sum(1 for n in self._pr_notes if n.selected and not n.deleted)
            self.play_btn.setText(f"▶ Selection ({count})")
        elif mode == 'range':
            lo = min(self.piano_roll._range_start_ms, self.piano_roll._range_end_ms)
            hi = max(self.piano_roll._range_start_ms, self.piano_roll._range_end_ms)
            dur = (hi - lo) / 1000
            self.play_btn.setText(f"▶ Range ({dur:.1f}s)")
        else:
            self.play_btn.setText("▶ Play")

    def _on_gw2_preview_toggled(self, checked):
        """Clear paused playback state so next Play does a fresh render."""
        if self._playback_paused_at > 0:
            self._playback_paused_at = 0
            self._playback_sound = None
            self.playback_slider.setValue(0)
            self.playback_time_label.setText("0:00 / 0:00")
            self.piano_roll.setCursorMs(-1)
            self._update_play_btn_text()

    def _playback_toggle(self):
        import pygame
        _ensure_pygame()
        if self._playback_playing:
            # Pause
            pygame.mixer.pause()
            self._playback_paused_at = self._playback_elapsed_ms()
            self._playback_playing = False
            self._playback_timer.stop()
            self.play_btn.setText("▶ Play")
            self.gw2_preview_cb.setEnabled(True)
            return

        if self._playback_paused_at > 0 and self._playback_sound:
            # Resume from pause
            pygame.mixer.unpause()
            self._playback_playing = True
            import time
            self._playback_start_time = time.time() * 1000 - self._playback_paused_at
            self._playback_timer.start()
            self.play_btn.setText("⏸ Pause")
            self.gw2_preview_cb.setEnabled(False)
            return

        # Start fresh playback
        if not self._pr_notes:
            return

        self.play_btn.setEnabled(False)
        self.play_btn.setText("Rendering...")

        # Get notes based on selection mode
        active, self._playback_offset_ms = self.piano_roll.getPlaybackNotes()

        if not active:
            self._update_play_btn_text()
            self.play_btn.setEnabled(True)
            return

        self._playback_total_ms = max(a[0] + a[1] for a in active) + 200

        # Render in background thread
        self._render_worker = RenderWorker(active, gw2_preview=self.gw2_preview_cb.isChecked())
        self._render_worker.finished.connect(self._on_render_done)
        self._render_worker.start()

    def _on_render_done(self, audio):
        import pygame
        _ensure_pygame()

        wav = audio_to_wav_bytes(audio)
        self._playback_sound = pygame.mixer.Sound(wav)
        self._playback_sound.play()
        self._playback_playing = True
        self._playback_paused_at = 0

        import time
        self._playback_start_time = time.time() * 1000

        self.play_btn.setText("⏸ Pause")
        self.play_btn.setEnabled(True)
        self.gw2_preview_cb.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self._playback_timer.start()

    def _playback_stop(self):
        import pygame
        _ensure_pygame()
        pygame.mixer.stop()
        self._playback_playing = False
        self._playback_paused_at = 0
        self._playback_offset_ms = 0
        self._playback_timer.stop()
        self._update_play_btn_text()
        self.stop_btn.setEnabled(False)
        self.gw2_preview_cb.setEnabled(True)
        self.playback_slider.setValue(0)
        self.playback_time_label.setText("0:00 / 0:00")
        self.piano_roll.setCursorMs(-1)

    def _playback_elapsed_ms(self):
        if not self._playback_playing:
            return self._playback_paused_at
        import time
        return time.time() * 1000 - self._playback_start_time

    def _playback_tick(self):
        elapsed = self._playback_elapsed_ms()
        if elapsed >= self._playback_total_ms:
            self._playback_stop()
            return
        # Update progress
        progress = int(elapsed / self._playback_total_ms * 1000)
        self.playback_slider.setValue(progress)
        # Update time label
        es = int(elapsed / 1000)
        ts = int(self._playback_total_ms / 1000)
        self.playback_time_label.setText(f"{es // 60}:{es % 60:02d} / {ts // 60}:{ts % 60:02d}")
        # Update piano roll cursor (offset to match original timeline position)
        self.piano_roll.setCursorMs(elapsed + self._playback_offset_ms)
        self.piano_roll.ensureCursorVisible()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
