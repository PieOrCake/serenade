#include "MusicPlayer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace Serenade {

// Static instrument profiles
static const std::vector<InstrumentProfile> s_Instruments = {
    {"Ornate Grand Piano",     50,   60, true,  3, 8},
};

MusicPlayer::MusicPlayer() {
    m_Instrument = s_Instruments[0]; // Default to piano
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

const std::vector<InstrumentProfile>& MusicPlayer::GetAvailableInstruments() {
    return s_Instruments;
}

void MusicPlayer::SetInstrument(const std::string& name) {
    for (const auto& inst : s_Instruments) {
        if (inst.name == name) {
            m_Instrument = inst;
            return;
        }
    }
}

int MusicPlayer::GetEffectiveBPM() const {
    if (m_BPMOverride > 0) return m_BPMOverride;
    const Song* song = GetCurrentSong();
    if (song) return song->bpm;
    return 120;
}

const Song* MusicPlayer::GetCurrentSong() const {
    if (m_CurrentTrack < 0 || m_CurrentTrack >= (int)m_Playlist.size()) return nullptr;
    int libIdx = m_Playlist[m_CurrentTrack];
    if (libIdx < 0 || libIdx >= (int)m_Library.size()) return nullptr;
    return &m_Library[libIdx];
}

float MusicPlayer::GetPlaybackProgress() const {
    const Song* song = GetCurrentSong();
    if (!song || song->events.empty()) return 0.0f;
    int cur = m_CurrentEvent.load();
    return static_cast<float>(cur) / static_cast<float>(song->events.size());
}

float MusicPlayer::GetElapsedSeconds() const {
    if (m_State.load() == PlaybackState::Stopped) return 0.0f;

    float elapsed = m_ElapsedBeforeLastPause;
    if (m_State.load() == PlaybackState::Playing) {
        auto now = std::chrono::steady_clock::now();
        elapsed += std::chrono::duration<float>(now - m_PlaybackStart).count();
    }
    return elapsed;
}

float MusicPlayer::GetTotalSeconds() const {
    const Song* song = GetCurrentSong();
    if (!song) return 0.0f;
    // Recalculate with effective BPM
    int bpm = GetEffectiveBPM();
    if (bpm <= 0) return 0.0f;
    float totalBeats = 0.0f;
    for (const auto& ev : song->events) {
        totalBeats += ev.durationBeats;
    }
    return totalBeats * 60.0f / static_cast<float>(bpm);
}

void MusicPlayer::Play() {
    if (m_Playlist.empty()) return;

    PlaybackState expected = PlaybackState::Stopped;
    if (m_State.compare_exchange_strong(expected, PlaybackState::Playing)) {
        // Join any previously finished thread before starting a new one
        if (m_Thread.joinable()) {
            m_Thread.join();
        }

        // Start fresh
        if (m_CurrentTrack < 0) m_CurrentTrack = 0;
        m_CurrentEvent.store(0);
        m_CurrentOctave = Octave::Mid;
        m_ElapsedBeforeLastPause = 0.0f;
        m_PlaybackStart = std::chrono::steady_clock::now();

        // Launch playback thread
        m_ThreadStop.store(false);
        m_ThreadRunning.store(true);
        m_Thread = std::thread(&MusicPlayer::PlaybackThread, this);
        return;
    }

    expected = PlaybackState::Paused;
    if (m_State.compare_exchange_strong(expected, PlaybackState::Playing)) {
        // Resume
        m_PlaybackStart = std::chrono::steady_clock::now();
        return;
    }
}

void MusicPlayer::Pause() {
    PlaybackState expected = PlaybackState::Playing;
    if (m_State.compare_exchange_strong(expected, PlaybackState::Paused)) {
        auto now = std::chrono::steady_clock::now();
        m_ElapsedBeforeLastPause += std::chrono::duration<float>(now - m_PlaybackStart).count();
    }
}

void MusicPlayer::Stop() {
    m_ThreadStop.store(true);
    m_State.store(PlaybackState::Stopped);

    if (m_Thread.joinable()) {
        m_Thread.join();
    }

    m_CurrentEvent.store(0);
    m_CurrentOctave = Octave::Mid;
    m_ElapsedBeforeLastPause = 0.0f;
    m_ThreadRunning.store(false);
}

void MusicPlayer::Next() {
    bool wasPlaying = IsPlaying();
    Stop();
    m_CurrentTrack = ResolveNextTrack();
    if (wasPlaying && m_CurrentTrack >= 0) {
        Play();
    }
}

void MusicPlayer::Previous() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - m_LastPrevPressTime).count();
    m_LastPrevPressTime = now;

    bool wasPlaying = IsPlaying();

    if (elapsed < 1.5) {
        // Second press within 1.5s — go to previous track
        Stop();
        m_CurrentTrack = ResolvePrevTrack();
    } else {
        // First press — restart current song
        Stop();
        // m_CurrentTrack stays the same
    }

    if (wasPlaying && m_CurrentTrack >= 0) {
        Play();
    }
}

void MusicPlayer::JumpToTrack(int playlistIndex) {
    if (playlistIndex < 0 || playlistIndex >= (int)m_Playlist.size()) return;
    bool wasPlaying = IsPlaying();
    Stop();
    m_CurrentTrack = playlistIndex;
    if (wasPlaying) {
        Play();
    }
}

void MusicPlayer::CycleRepeatMode() {
    switch (m_RepeatMode) {
        case RepeatMode::Off: m_RepeatMode = RepeatMode::All; break;
        case RepeatMode::All: m_RepeatMode = RepeatMode::One; break;
        case RepeatMode::One: m_RepeatMode = RepeatMode::Off; break;
    }
}

int MusicPlayer::ResolveNextTrack() const {
    if (m_Playlist.empty()) return -1;

    if (m_RepeatMode == RepeatMode::One) {
        return m_CurrentTrack;
    }

    if (m_Shuffle) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, (int)m_Playlist.size() - 1);
        int next = dist(rng);
        // Avoid same track if possible
        if (m_Playlist.size() > 1) {
            while (next == m_CurrentTrack) next = dist(rng);
        }
        return next;
    }

    int next = m_CurrentTrack + 1;
    if (next >= (int)m_Playlist.size()) {
        if (m_RepeatMode == RepeatMode::All) return 0;
        return -1; // End of playlist
    }
    return next;
}

int MusicPlayer::ResolvePrevTrack() const {
    if (m_Playlist.empty()) return -1;

    if (m_RepeatMode == RepeatMode::One) {
        return m_CurrentTrack;
    }

    int prev = m_CurrentTrack - 1;
    if (prev < 0) {
        if (m_RepeatMode == RepeatMode::All) return (int)m_Playlist.size() - 1;
        return 0;
    }
    return prev;
}

void MusicPlayer::AdvanceTrack() {
    int next = ResolveNextTrack();
    if (next < 0) {
        m_State.store(PlaybackState::Stopped);
        m_ThreadStop.store(true);
        return;
    }
    m_CurrentTrack = next;
    m_CurrentEvent.store(0);
    m_CurrentOctave = Octave::Mid;
    m_ElapsedBeforeLastPause = 0.0f;
    m_PlaybackStart = std::chrono::steady_clock::now();
}

std::string VKToDisplayName(WORD vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_SPACE:   return "Space";
        case VK_TAB:     return "Tab";
        case VK_RETURN:  return "Enter";
        case VK_ESCAPE:  return "Esc";
        case VK_BACK:    return "Backspace";
        case VK_DELETE:  return "Delete";
        case VK_INSERT:  return "Insert";
        case VK_HOME:    return "Home";
        case VK_END:     return "End";
        case VK_PRIOR:   return "PgUp";
        case VK_NEXT:    return "PgDn";
        case VK_UP:      return "Up";
        case VK_DOWN:    return "Down";
        case VK_LEFT:    return "Left";
        case VK_RIGHT:   return "Right";
        case VK_OEM_MINUS:   return "-";
        case VK_OEM_PLUS:    return "=";
        case VK_OEM_4:       return "[";
        case VK_OEM_6:       return "]";
        case VK_OEM_1:       return ";";
        case VK_OEM_7:       return "'";
        case VK_OEM_COMMA:   return ",";
        case VK_OEM_PERIOD:  return ".";
        case VK_OEM_2:       return "/";
        case VK_OEM_3:       return "`";
        case VK_OEM_5:       return "\\";
        case VK_MULTIPLY:    return "Num*";
        case VK_ADD:         return "Num+";
        case VK_SUBTRACT:    return "Num-";
        case VK_DECIMAL:     return "Num.";
        case VK_DIVIDE:      return "Num/";
        default: break;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

static void SendKeyDown(WORD vk) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));
}

static void SendKeyUp(WORD vk) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void MusicPlayer::SendNoteKeys(const std::vector<int>& keys) {
    if (keys.empty()) return;

    for (int key : keys) {
        WORD vk = 0;
        if (key >= 1 && key <= 8) {
            vk = m_KeyConfig.noteKeys[key - 1];         // Natural notes
        } else if (key >= 9 && key <= 13) {
            vk = m_KeyConfig.sharpKeys[key - 9];        // Sharps/flats (F1-F5)
        }
        if (vk == 0) continue;
        DebugLog("  KEY DOWN: key=" + std::to_string(key) + " vk=" + VKToDisplayName(vk));
        SendKeyDown(vk);
    }

    // Brief delay then release
    Sleep(30);

    for (int key : keys) {
        WORD vk = 0;
        if (key >= 1 && key <= 8) {
            vk = m_KeyConfig.noteKeys[key - 1];
        } else if (key >= 9 && key <= 13) {
            vk = m_KeyConfig.sharpKeys[key - 9];
        }
        if (vk == 0) continue;
        SendKeyUp(vk);
    }

    // Small gap after release so GW2 can process before next keypress
    Sleep(15);
}

void MusicPlayer::SendOctaveChange(Octave target) {
    if (target == m_CurrentOctave) return;

    const char* octNames[] = {"Low", "Mid", "High"};
    DebugLog("  OCTAVE: " + std::string(octNames[(int)m_CurrentOctave]) + " -> " + octNames[(int)target]);

    int keyHoldMs = 40;     // hold key long enough for GW2 to register
    int interPressMs = 80;  // wait between presses for GW2 to process

    // Use relative positioning: just press up/down the needed number of steps.
    // This is much faster than resetting to Low every time.
    int diff = static_cast<int>(target) - static_cast<int>(m_CurrentOctave);
    WORD vk = (diff > 0) ? m_KeyConfig.octaveUpKey : m_KeyConfig.octaveDownKey;
    int presses = (diff > 0) ? diff : -diff;

    for (int i = 0; i < presses; i++) {
        if (m_ThreadStop.load()) return;
        DebugLog("  KEY: vk=" + VKToDisplayName(vk));
        SendKeyDown(vk);
        Sleep(keyHoldMs);
        SendKeyUp(vk);
        Sleep(interPressMs);
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            DebugLog("Enter key detected in octave change — stopping");
            m_ThreadStop.store(true);
            m_State.store(PlaybackState::Stopped);
            return;
        }
    }

    m_CurrentOctave = target;
}

void MusicPlayer::PlaybackThread() {
    // Absolute timing: we track when each event SHOULD fire based on
    // accumulated beat durations, preventing drift from key-send delays
    // and octave-change overhead.
    //
    // Key design: 0-duration events (octave changes) are executed EARLY,
    // during the previous note's sustain, so the following note fires
    // exactly on schedule. This prevents octave change delays (~140ms)
    // from pushing notes late.
    // Reset piano to known state: press 9 (decrease) 5 times to reach Low Major
    // from any state (9 clamps at Low), then press 0 (increase) once to reach Mid.
    // Use generous timing so GW2 reliably processes every key press.
    DebugLog("=== RESET: pressing 9 x5, then 0 x1 to reach Mid octave ===");
    int resetHoldMs = 50;     // key hold time
    int resetDelayMs = 200;   // generous inter-press delay for reset
    for (int i = 0; i < 5; i++) {
        SendKeyDown(m_KeyConfig.octaveDownKey);
        Sleep(resetHoldMs);
        SendKeyUp(m_KeyConfig.octaveDownKey);
        Sleep(resetDelayMs);
    }
    SendKeyDown(m_KeyConfig.octaveUpKey);
    Sleep(resetHoldMs);
    SendKeyUp(m_KeyConfig.octaveUpKey);
    Sleep(resetDelayMs);
    m_CurrentOctave = Octave::Mid;
    Sleep(500);  // extra settle time after reset before playing
    DebugLog("=== RESET complete: now at Mid octave ===");

    auto timelineStart = std::chrono::steady_clock::now();
    double accumulatedMs = 0.0;
    double pauseOffset = 0.0;

    // Helper: wait until a target time on the timeline, staying responsive.
    // Also monitors for Enter key (opens GW2 chat) — stops playback immediately.
    auto waitUntil = [&](double targetMs) -> bool {
        auto targetTime = timelineStart + std::chrono::microseconds(
            static_cast<long long>((targetMs + pauseOffset) * 1000.0));
        auto now = std::chrono::steady_clock::now();
        if (now < targetTime) {
            auto remainMs = std::chrono::duration<double, std::milli>(targetTime - now).count();
            if (remainMs > 15.0) {
                int sleepMs = static_cast<int>(remainMs) - 10;
                int slept = 0;
                while (slept < sleepMs && !m_ThreadStop.load() &&
                       m_State.load() == PlaybackState::Playing) {
                    if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                        DebugLog("Enter key detected — stopping playback (chat protection)");
                        m_ThreadStop.store(true);
                        m_State.store(PlaybackState::Stopped);
                        return false;
                    }
                    int chunk = std::min(10, sleepMs - slept);
                    Sleep(chunk);
                    slept += chunk;
                }
            }
            while (!m_ThreadStop.load() && m_State.load() == PlaybackState::Playing) {
                if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                    DebugLog("Enter key detected — stopping playback (chat protection)");
                    m_ThreadStop.store(true);
                    m_State.store(PlaybackState::Stopped);
                    return false;
                }
                if (std::chrono::steady_clock::now() >= targetTime) break;
            }
        }
        return !m_ThreadStop.load() && m_State.load() == PlaybackState::Playing;
    };

    // Helper: execute a single event (send keys / change octave)
    auto executeEvent = [&](const NoteEvent& ev, int idx) {
        std::string keysStr;
        for (int k : ev.keys) {
            if (!keysStr.empty()) keysStr += ",";
            keysStr += std::to_string(k);
        }
        const char* typeNames[] = {"Note", "Chord", "OctUp", "OctDn", "Rest", "OctSet"};
        int ti = static_cast<int>(ev.type);
        DebugLog("[" + std::to_string(idx) + "] " +
                 (ti >= 0 && ti < 6 ? typeNames[ti] : "?") +
                 " keys=[" + keysStr + "] dur=" +
                 std::to_string(ev.durationBeats) + "b");

        switch (ev.type) {
            case EventType::Note:
            case EventType::Chord:
                // Translate note 8 for instruments with 7 notes per octave:
                // note 8 = high C = octave up + note 1
                if (m_Instrument.notesPerOctave == 7) {
                    bool hasNote8 = false;
                    std::vector<int> normalKeys;
                    for (int k : ev.keys) {
                        if (k == 8) hasNote8 = true;
                        else normalKeys.push_back(k);
                    }
                    if (hasNote8) {
                        // Play any non-8 notes first in current octave
                        if (!normalKeys.empty()) {
                            SendNoteKeys(normalKeys);
                        }
                        // Octave up, play note 1, octave back
                        Octave prevOctave = m_CurrentOctave;
                        if (static_cast<int>(m_CurrentOctave) < 2) {
                            SendOctaveChange(static_cast<Octave>(
                                static_cast<int>(m_CurrentOctave) + 1));
                        }
                        SendNoteKeys({1});
                        SendOctaveChange(prevOctave);
                        break;
                    }
                }
                if (ev.type == EventType::Chord && !m_Instrument.supportsChords) {
                    for (int k : ev.keys) {
                        SendNoteKeys({k});
                        Sleep(std::max(20, m_Instrument.minNoteDelayMs / 2));
                    }
                } else {
                    SendNoteKeys(ev.keys);
                }
                break;

            case EventType::OctaveUp:
                SendOctaveChange(static_cast<Octave>(
                    std::min(2, static_cast<int>(m_CurrentOctave) + 1)));
                break;

            case EventType::OctaveDown:
                SendOctaveChange(static_cast<Octave>(
                    std::max(0, static_cast<int>(m_CurrentOctave) - 1)));
                break;

            case EventType::OctaveSet:
                SendOctaveChange(ev.targetOctave);
                break;

            case EventType::Rest:
                break;
        }
    };

    while (!m_ThreadStop.load()) {
        // Handle pause
        if (m_State.load() == PlaybackState::Paused) {
            auto pauseStart = std::chrono::steady_clock::now();
            while (m_State.load() == PlaybackState::Paused && !m_ThreadStop.load()) {
                Sleep(10);
            }
            auto pauseEnd = std::chrono::steady_clock::now();
            pauseOffset += std::chrono::duration<double, std::milli>(pauseEnd - pauseStart).count();
            continue;
        }

        if (m_State.load() != PlaybackState::Playing) break;

        const Song* song = GetCurrentSong();
        if (!song || song->events.empty()) {
            AdvanceTrack();
            if (m_ThreadStop.load()) break;
            timelineStart = std::chrono::steady_clock::now();
            accumulatedMs = 0.0;
            pauseOffset = 0.0;
            continue;
        }

        int eventIdx = m_CurrentEvent.load();
        if (eventIdx >= (int)song->events.size()) {
            AdvanceTrack();
            if (m_ThreadStop.load()) break;
            // 3-second gap between songs
            DebugLog("=== Inter-song gap: waiting 3 seconds ===");
            for (int waited = 0; waited < 3000 && !m_ThreadStop.load(); waited += 10) {
                if (m_State.load() != PlaybackState::Playing) break;
                if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                    DebugLog("Enter key detected — stopping playback (chat protection)");
                    m_ThreadStop.store(true);
                    m_State.store(PlaybackState::Stopped);
                    break;
                }
                Sleep(10);
            }
            if (m_ThreadStop.load()) break;
            timelineStart = std::chrono::steady_clock::now();
            accumulatedMs = 0.0;
            pauseOffset = 0.0;
            continue;
        }

        int bpm = GetEffectiveBPM();
        double beatDurationMs = 60000.0 / static_cast<double>(bpm);

        // Execute all consecutive 0-duration events immediately (octave
        // changes, etc.) — these run BEFORE we wait for the next timed
        // event, so they complete during the previous note's sustain.
        auto zeroStart = std::chrono::steady_clock::now();
        while (eventIdx < (int)song->events.size() &&
               song->events[eventIdx].durationBeats == 0.0f &&
               !m_ThreadStop.load()) {
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                DebugLog("Enter key detected — stopping playback (chat protection)");
                m_ThreadStop.store(true);
                m_State.store(PlaybackState::Stopped);
                break;
            }
            executeEvent(song->events[eventIdx], eventIdx);
            eventIdx++;
            m_CurrentEvent.store(eventIdx);
        }
        if (m_ThreadStop.load()) break;
        if (eventIdx >= (int)song->events.size()) continue;

        // Compensate timeline for real time consumed by 0-duration events
        // (octave changes). Without this, notes after octave changes would
        // rapid-fire to "catch up" and GW2 would drop them.
        auto zeroElapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - zeroStart).count();
        if (zeroElapsed > 5.0) {
            pauseOffset += zeroElapsed;
            DebugLog("  Timeline shift: +" + std::to_string((int)zeroElapsed) + "ms for octave change");
        }

        // Wait until the next timed event's scheduled time
        if (!waitUntil(accumulatedMs)) continue;

        // Execute the timed event (note, chord, or rest)
        const NoteEvent& ev = song->events[eventIdx];
        executeEvent(ev, eventIdx);

        // Advance timeline by this event's beat duration
        accumulatedMs += ev.durationBeats * beatDurationMs;
        m_CurrentEvent.store(eventIdx + 1);
    }

    m_ThreadRunning.store(false);
}

void MusicPlayer::SetSongLibrary(std::vector<Song> songs) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    // Remap existing playlist indices by title so the playlist is stable
    // across library refreshes (e.g. when new songs are added).
    if (!m_Playlist.empty()) {
        // Collect titles of current playlist entries
        std::vector<std::string> playlistTitles;
        playlistTitles.reserve(m_Playlist.size());
        for (int idx : m_Playlist) {
            if (idx >= 0 && idx < (int)m_Library.size())
                playlistTitles.push_back(m_Library[idx].title);
            else
                playlistTitles.push_back("");
        }

        // Build title → index map for new library
        std::unordered_map<std::string, int> titleToIdx;
        for (int i = 0; i < (int)songs.size(); i++) {
            // First occurrence wins (matches LoadPlaylist behavior)
            if (titleToIdx.find(songs[i].title) == titleToIdx.end())
                titleToIdx[songs[i].title] = i;
        }

        // Remap playlist, dropping entries whose songs were deleted
        std::vector<int> newPlaylist;
        newPlaylist.reserve(playlistTitles.size());
        int newCurrentTrack = -1;
        for (int i = 0; i < (int)playlistTitles.size(); i++) {
            auto it = titleToIdx.find(playlistTitles[i]);
            if (it != titleToIdx.end()) {
                if (i == m_CurrentTrack)
                    newCurrentTrack = (int)newPlaylist.size();
                newPlaylist.push_back(it->second);
            }
        }

        m_Playlist = std::move(newPlaylist);
        m_CurrentTrack = newCurrentTrack >= 0 ? newCurrentTrack :
                         (m_Playlist.empty() ? -1 : 0);
    }

    m_Library = std::move(songs);
}

void MusicPlayer::SetPlaylist(std::vector<int> indices) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist = std::move(indices);
    if (m_CurrentTrack >= (int)m_Playlist.size()) {
        m_CurrentTrack = m_Playlist.empty() ? -1 : 0;
    }
}

void MusicPlayer::AddToPlaylist(int libraryIndex) {
    if (libraryIndex < 0 || libraryIndex >= (int)m_Library.size()) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist.push_back(libraryIndex);
}

void MusicPlayer::RemoveFromPlaylist(int playlistPosition) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (playlistPosition < 0 || playlistPosition >= (int)m_Playlist.size()) return;
    m_Playlist.erase(m_Playlist.begin() + playlistPosition);
    if (m_CurrentTrack >= (int)m_Playlist.size()) {
        m_CurrentTrack = m_Playlist.empty() ? -1 : (int)m_Playlist.size() - 1;
    }
}

void MusicPlayer::MoveInPlaylist(int from, int to) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (from < 0 || from >= (int)m_Playlist.size()) return;
    if (to < 0 || to >= (int)m_Playlist.size()) return;
    int val = m_Playlist[from];
    m_Playlist.erase(m_Playlist.begin() + from);
    m_Playlist.insert(m_Playlist.begin() + to, val);
    // Adjust current track pointer
    if (m_CurrentTrack == from) {
        m_CurrentTrack = to;
    } else if (from < m_CurrentTrack && to >= m_CurrentTrack) {
        m_CurrentTrack--;
    } else if (from > m_CurrentTrack && to <= m_CurrentTrack) {
        m_CurrentTrack++;
    }
}

void MusicPlayer::ClearPlaylist() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist.clear();
    m_CurrentTrack = -1;
}

void MusicPlayer::SavePlaylist(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return;

    // Save current track as a header so we can restore it next session
    if (m_CurrentTrack >= 0 && m_CurrentTrack < (int)m_Playlist.size()) {
        int libIdx = m_Playlist[m_CurrentTrack];
        if (libIdx >= 0 && libIdx < (int)m_Library.size()) {
            file << "#current=" << m_Library[libIdx].title << "\n";
        }
    }

    for (int idx : m_Playlist) {
        if (idx >= 0 && idx < (int)m_Library.size()) {
            file << m_Library[idx].title << "\n";
        }
    }
}

void MusicPlayer::LoadPlaylist(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::vector<int> indices;
    std::string currentTitle;
    std::string line;
    while (std::getline(file, line)) {
        // Trim
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        // Parse #current= header
        if (line.size() > 9 && line.substr(0, 9) == "#current=") {
            currentTitle = line.substr(9);
            continue;
        }

        // Skip other comment lines
        if (!line.empty() && line[0] == '#') continue;

        // Find matching song in library
        for (int i = 0; i < (int)m_Library.size(); i++) {
            if (m_Library[i].title == line) {
                indices.push_back(i);
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist = std::move(indices);

    // Restore current track from saved title
    m_CurrentTrack = m_Playlist.empty() ? -1 : 0;
    if (!currentTitle.empty()) {
        for (int i = 0; i < (int)m_Playlist.size(); i++) {
            int libIdx = m_Playlist[i];
            if (libIdx >= 0 && libIdx < (int)m_Library.size() &&
                m_Library[libIdx].title == currentTitle) {
                m_CurrentTrack = i;
                break;
            }
        }
    }
}

void MusicPlayer::SaveKeyConfig(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return;
    for (int i = 0; i < 8; i++) {
        file << "note" << (i + 1) << "=" << m_KeyConfig.noteKeys[i] << "\n";
    }
    for (int i = 0; i < 5; i++) {
        file << "sharp" << (i + 1) << "=" << m_KeyConfig.sharpKeys[i] << "\n";
    }
    file << "octave_up=" << m_KeyConfig.octaveUpKey << "\n";
    file << "octave_down=" << m_KeyConfig.octaveDownKey << "\n";
}

void MusicPlayer::LoadKeyConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        int val = 0;
        try { val = std::stoi(line.substr(eq + 1)); } catch (...) { continue; }

        if (key.substr(0, 4) == "note" && key.size() == 5) {
            int idx = key[4] - '1';
            if (idx >= 0 && idx < 8) m_KeyConfig.noteKeys[idx] = (WORD)val;
        } else if (key.substr(0, 5) == "sharp" && key.size() == 6) {
            int idx = key[5] - '1';
            if (idx >= 0 && idx < 5) m_KeyConfig.sharpKeys[idx] = (WORD)val;
        } else if (key == "octave_up") {
            m_KeyConfig.octaveUpKey = (WORD)val;
        } else if (key == "octave_down") {
            m_KeyConfig.octaveDownKey = (WORD)val;
        }
    }
}

void MusicPlayer::SetDebugLogPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_DebugMutex);
    m_DebugLogPath = path;
    // Clear previous log
    std::ofstream f(path, std::ios::trunc);
}

void MusicPlayer::DebugLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_DebugMutex);
    if (m_DebugLogPath.empty()) return;
    std::ofstream f(m_DebugLogPath, std::ios::app);
    if (f.is_open()) f << msg << "\n";
}

} // namespace Serenade
