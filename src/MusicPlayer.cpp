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
    // Direct play mode: use library index directly
    if (m_DirectPlayLibIdx >= 0 && m_DirectPlayLibIdx < (int)m_Library.size()) {
        return &m_Library[m_DirectPlayLibIdx];
    }
    if (m_CurrentTrack < 0 || m_CurrentTrack >= (int)m_Playlist.size()) return nullptr;
    int libIdx = m_Playlist[m_CurrentTrack];
    if (libIdx < 0 || libIdx >= (int)m_Library.size()) return nullptr;
    return &m_Library[libIdx];
}

float MusicPlayer::GetPlaybackProgress() const {
    float total = GetTotalSeconds();
    if (total <= 0.0f) return 0.0f;
    float p = GetElapsedSeconds() / total;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
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

        // Start fresh (unless seeking — SeekTo already set position)
        if (m_CurrentTrack < 0) m_CurrentTrack = 0;
        if (!m_SkipOctaveReset.load()) {
            m_CurrentEvent.store(0);
            m_CurrentOctave = Octave::Mid;
            m_ElapsedBeforeLastPause = 0.0f;
        }
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
    m_SkipOctaveReset.store(false);
    m_SeekOffsetMs.store(0.0);
}

void MusicPlayer::Next() {
    bool wasPlaying = IsPlaying();
    // Record current track in shuffle history before skipping
    if (m_Shuffle && m_CurrentTrack >= 0) {
        m_ShuffleHistory.push_back(m_CurrentTrack);
        int maxHist = std::max(2, std::min(50, (int)m_Playlist.size() / 2));
        while ((int)m_ShuffleHistory.size() > maxHist) m_ShuffleHistory.pop_front();
    }
    Stop();
    m_DirectPlayLibIdx = -1;
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
    m_DirectPlayLibIdx = -1;

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

void MusicPlayer::SeekTo(float progress) {
    const Song* song = GetCurrentSong();
    if (!song || song->events.empty()) return;

    progress = std::max(0.0f, std::min(1.0f, progress));
    bool wasPlaying = IsPlaying();

    // Stop the playback thread cleanly
    m_ThreadStop.store(true);
    if (wasPlaying || IsPaused()) {
        m_State.store(PlaybackState::Stopped);
    }
    if (m_Thread.joinable()) {
        m_Thread.join();
    }

    // Find target event from progress (by accumulated time)
    int bpm = GetEffectiveBPM();
    double beatMs = 60000.0 / static_cast<double>(bpm);
    double totalMs = 0.0;
    for (const auto& ev : song->events) {
        totalMs += ev.durationBeats * beatMs;
    }
    double targetMs = progress * totalMs;

    // Find the event at or just after targetMs
    double accMs = 0.0;
    int targetEvent = 0;
    for (int i = 0; i < (int)song->events.size(); i++) {
        if (accMs >= targetMs) {
            targetEvent = i;
            break;
        }
        accMs += song->events[i].durationBeats * beatMs;
        targetEvent = i + 1;
    }
    targetEvent = std::min(targetEvent, (int)song->events.size() - 1);

    // Walk back to include any 0-duration events (octave changes) before
    // the target note, so the thread's "execute 0-duration" loop handles them
    while (targetEvent > 0 && song->events[targetEvent - 1].durationBeats == 0.0f) {
        targetEvent--;
    }

    // Compute octave state at this position by scanning events [0, targetEvent)
    Octave targetOct = Octave::Mid;
    for (int i = 0; i < targetEvent; i++) {
        const auto& ev = song->events[i];
        switch (ev.type) {
            case EventType::OctaveUp:
                targetOct = static_cast<Octave>(std::min(2, (int)targetOct + 1));
                break;
            case EventType::OctaveDown:
                targetOct = static_cast<Octave>(std::max(0, (int)targetOct - 1));
                break;
            case EventType::OctaveSet:
                targetOct = ev.targetOctave;
                break;
            default: break;
        }
    }

    // Compute the accumulated time at targetEvent for the timeline offset
    double seekMs = 0.0;
    for (int i = 0; i < targetEvent; i++) {
        seekMs += song->events[i].durationBeats * beatMs;
    }

    // Store target octave for the playback thread to set physically
    // (don't set m_CurrentOctave here — SendOctaveChange needs to see a difference)
    m_SeekTargetOctave = targetOct;

    // Set up state for the playback thread
    m_CurrentEvent.store(targetEvent);
    m_SeekOffsetMs.store(seekMs);
    m_SkipOctaveReset.store(true);
    m_ElapsedBeforeLastPause = static_cast<float>(seekMs / 1000.0);

    const char* octNames[] = {"Low", "Mid", "High"};
    DebugLog("=== SEEK: progress=" + std::to_string(progress) +
             " event=" + std::to_string(targetEvent) +
             " time=" + std::to_string(seekMs) + "ms" +
             " octave=" + octNames[(int)targetOct] + " ===");

    m_ThreadRunning.store(false);

    if (wasPlaying) {
        // Restart playback from new position
        m_State.store(PlaybackState::Stopped);
        Play();
    }
}

void MusicPlayer::JumpToTrack(int playlistIndex) {
    if (playlistIndex < 0 || playlistIndex >= (int)m_Playlist.size()) return;
    bool wasPlaying = IsPlaying();
    Stop();
    m_DirectPlayLibIdx = -1;  // Exit direct play mode
    m_CurrentTrack = playlistIndex;
    if (wasPlaying) {
        Play();
    }
}

void MusicPlayer::PlayDirectFromLibrary(int libraryIndex) {
    if (libraryIndex < 0 || libraryIndex >= (int)m_Library.size()) return;
    Stop();
    m_DirectPlayLibIdx = libraryIndex;
    m_State.store(PlaybackState::Stopped);  // Ensure clean state
    // Start playback
    if (m_Thread.joinable()) m_Thread.join();
    m_CurrentEvent.store(0);
    m_CurrentOctave = Octave::Mid;
    m_ElapsedBeforeLastPause = 0.0f;
    m_PlaybackStart = std::chrono::steady_clock::now();
    m_ThreadStop.store(false);
    m_ThreadRunning.store(true);
    m_State.store(PlaybackState::Playing);
    m_Thread = std::thread(&MusicPlayer::PlaybackThread, this);
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
        int n = (int)m_Playlist.size();
        if (n == 1) return 0;

        // Build weights: songs not in history get weight 1.0,
        // songs in history get lower weight based on recency
        // (most recent = lowest weight)
        std::vector<double> weights(n, 1.0);
        int histSize = (int)m_ShuffleHistory.size();
        for (int h = 0; h < histSize; h++) {
            int idx = m_ShuffleHistory[h];
            if (idx >= 0 && idx < n) {
                // h=0 is oldest, h=histSize-1 is most recent
                // Most recent gets near-zero weight, oldest in history gets partial weight
                int recency = histSize - h; // 1 = oldest in history, histSize = most recent
                double w = 1.0 / (1.0 + recency * 2.0);
                if (weights[idx] > w) weights[idx] = w;
            }
        }
        // Current track gets zero weight
        if (m_CurrentTrack >= 0 && m_CurrentTrack < n) {
            weights[m_CurrentTrack] = 0.0;
        }

        // If all weights are zero (tiny playlist, all in history), reset to uniform
        double totalWeight = 0.0;
        for (double w : weights) totalWeight += w;
        if (totalWeight <= 0.0) {
            for (int i = 0; i < n; i++) weights[i] = (i == m_CurrentTrack) ? 0.0 : 1.0;
            totalWeight = 0.0;
            for (double w : weights) totalWeight += w;
            if (totalWeight <= 0.0) return (m_CurrentTrack + 1) % n;
        }

        std::discrete_distribution<int> dist(weights.begin(), weights.end());
        return dist(rng);
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
    m_DirectPlayLibIdx = -1;  // Exit direct play on track advance

    // Record current track in shuffle history before advancing
    if (m_Shuffle && m_CurrentTrack >= 0) {
        m_ShuffleHistory.push_back(m_CurrentTrack);
        // Cap history at half the playlist size (min 2, max 50)
        int maxHist = std::max(2, std::min(50, (int)m_Playlist.size() / 2));
        while ((int)m_ShuffleHistory.size() > maxHist) {
            m_ShuffleHistory.pop_front();
        }
    }

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
    AnnounceCurrentSong();
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
}

void MusicPlayer::SendOctaveChange(Octave target) {
    if (target == m_CurrentOctave) return;

    const char* octNames[] = {"Low", "Mid", "High"};
    DebugLog("  OCTAVE: " + std::string(octNames[(int)m_CurrentOctave]) + " -> " + octNames[(int)target]);

    WORD downVk = m_KeyConfig.octaveDownKey;
    WORD upVk = m_KeyConfig.octaveUpKey;
    WORD downScan = (WORD)MapVirtualKeyA(downVk, MAPVK_VK_TO_VSC);
    WORD upScan = (WORD)MapVirtualKeyA(upVk, MAPVK_VK_TO_VSC);

    // Split-batch absolute positioning:
    // Batch 1: 3x key 9 (clamps at Low from any state, max 3 presses)
    // Sleep:   let GW2 process the down presses
    // Batch 2: target x key 0 (Low→target, max 2 presses)
    //
    // Each batch is ≤3 presses — within GW2's reliable processing limit.
    // Always self-correcting: key 9 clamps at Low, so even if we were
    // already at Low, the extra presses are harmless.

    int upPresses = static_cast<int>(target);  // 0=Low, 1=Mid, 2=High

    // Batch 1: reset to Low (3x key 9, always — clamps harmlessly)
    INPUT downInputs[6] = {};
    for (int i = 0; i < 3; i++) {
        downInputs[i * 2].type = INPUT_KEYBOARD;
        downInputs[i * 2].ki.wVk = downVk;
        downInputs[i * 2].ki.wScan = downScan;
        downInputs[i * 2].ki.dwFlags = 0;
        downInputs[i * 2 + 1].type = INPUT_KEYBOARD;
        downInputs[i * 2 + 1].ki.wVk = downVk;
        downInputs[i * 2 + 1].ki.wScan = downScan;
        downInputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    SendInput(6, downInputs, sizeof(INPUT));

    if (upPresses > 0) {
        // Brief pause so GW2 processes the down batch before the up batch
        Sleep(20);

        // Batch 2: go up to target (1-2x key 0)
        std::vector<INPUT> upInputs(upPresses * 2);
        for (int i = 0; i < upPresses; i++) {
            upInputs[i * 2] = {};
            upInputs[i * 2].type = INPUT_KEYBOARD;
            upInputs[i * 2].ki.wVk = upVk;
            upInputs[i * 2].ki.wScan = upScan;
            upInputs[i * 2].ki.dwFlags = 0;
            upInputs[i * 2 + 1] = {};
            upInputs[i * 2 + 1].type = INPUT_KEYBOARD;
            upInputs[i * 2 + 1].ki.wVk = upVk;
            upInputs[i * 2 + 1].ki.wScan = upScan;
            upInputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        SendInput((UINT)upInputs.size(), upInputs.data(), sizeof(INPUT));
    }

    Sleep(10);  // settle time before next note
    m_CurrentOctave = target;
}

void MusicPlayer::PlaybackThread() {
    // Announce song in chat before playing
    AnnounceCurrentSong();

    // Absolute timing: we track when each event SHOULD fire based on
    // accumulated beat durations, preventing drift from key-send delays
    // and octave-change overhead.
    //
    // Key design: 0-duration events (octave changes) are executed EARLY,
    // during the previous note's sustain, so the following note fires
    // exactly on schedule. This prevents octave change delays (~140ms)
    // from pushing notes late.
    bool seekMode = m_SkipOctaveReset.exchange(false);
    if (seekMode) {
        // After a seek: set octave to the target computed by SeekTo.
        // Reset m_CurrentOctave to force SendOctaveChange to send keys.
        const char* octNames[] = {"Low", "Mid", "High"};
        Octave seekTarget = m_SeekTargetOctave;
        DebugLog("=== SEEK RESUME: setting octave to " +
                 std::string(octNames[(int)seekTarget]) + " ===");
        m_CurrentOctave = static_cast<Octave>((static_cast<int>(seekTarget) + 1) % 3);  // force different
        SendOctaveChange(seekTarget);
        m_OctaveChangeCount = 0;
        Sleep(200);  // settle time before playing
        DebugLog("=== SEEK RESUME: ready ===");
    } else {
        // Normal start: reset piano to known state
        // Press 9 (decrease) 5 times to reach Low from any state (9 clamps at Low),
        // then press 0 (increase) once to reach Mid.
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
        m_OctaveChangeCount = 0;
        Sleep(500);  // extra settle time after reset before playing
        DebugLog("=== RESET complete: now at Mid octave ===");
    }

    auto timelineStart = std::chrono::steady_clock::now();
    double accumulatedMs = 0.0;
    if (seekMode) {
        double seekMs = m_SeekOffsetMs.load();
        accumulatedMs = seekMs;
        // Shift timeline start backwards so waitUntil(seekMs) resolves to "now"
        timelineStart -= std::chrono::microseconds(
            static_cast<long long>(seekMs * 1000.0));
    }
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
                        DebugLog("Enter key detected — pausing playback (chat protection)");
                        Pause();
                        return false;
                    }
                    int chunk = std::min(10, sleepMs - slept);
                    Sleep(chunk);
                    slept += chunk;
                }
            }
            while (!m_ThreadStop.load() && m_State.load() == PlaybackState::Playing) {
                if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                    DebugLog("Enter key detected — pausing playback (chat protection)");
                    Pause();
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
                    DebugLog("Enter key detected — pausing playback (chat protection)");
                    Pause();
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
        while (eventIdx < (int)song->events.size() &&
               song->events[eventIdx].durationBeats == 0.0f &&
               !m_ThreadStop.load()) {
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                DebugLog("Enter key detected — pausing playback (chat protection)");
                Pause();
                break;
            }
            executeEvent(song->events[eventIdx], eventIdx);
            eventIdx++;
            m_CurrentEvent.store(eventIdx);
        }
        if (m_ThreadStop.load()) break;
        if (eventIdx >= (int)song->events.size()) continue;

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

static LPARAM MakeLParam(uint32_t vk, bool down) {
    int64_t lp = !down;        // transition state
    lp = lp << 1;
    lp += !down;               // previous key state
    lp = lp << 1;
    lp += 0;                   // context code
    lp = lp << 1;
    lp = lp << 4;
    lp = lp << 1;
    lp = lp << 8;
    lp += MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    lp = lp << 16;
    lp += 1;
    return (LPARAM)lp;
}

static bool CopyToOsClipboard(HWND hwnd, const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (wlen <= 0) return false;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * (wlen + 1));
    if (!hMem) return false;
    WCHAR* wBuf = (WCHAR*)GlobalLock(hMem);
    if (!wBuf) { GlobalFree(hMem); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wBuf, wlen);
    wBuf[wlen] = L'\0';
    GlobalUnlock(hMem);
    if (!OpenClipboard(hwnd)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

void MusicPlayer::SendChatMessage(const std::string& message) {
    if (!m_GameWindow || message.empty()) return;
    DebugLog("Chat announce: " + message);

    const int delayMs = 50;

    if (!CopyToOsClipboard(m_GameWindow, message)) return;

    // Open chat with Enter
    SendMessage(m_GameWindow, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(m_GameWindow, WM_KEYUP, VK_RETURN, MakeLParam(VK_RETURN, false));
    Sleep(delayMs);

    // Ctrl+V to paste
    INPUT ctrlDown = {};
    ctrlDown.type = INPUT_KEYBOARD;
    ctrlDown.ki.wVk = VK_CONTROL;
    ctrlDown.ki.wScan = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    SendInput(1, &ctrlDown, sizeof(INPUT));
    Sleep(delayMs);

    SendMessage(m_GameWindow, WM_KEYDOWN, 'V', MakeLParam('V', true));
    SendMessage(m_GameWindow, WM_KEYUP, 'V', MakeLParam('V', false));
    Sleep(delayMs);

    INPUT ctrlUp = {};
    ctrlUp.type = INPUT_KEYBOARD;
    ctrlUp.ki.wVk = VK_CONTROL;
    ctrlUp.ki.wScan = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ctrlUp, sizeof(INPUT));
    Sleep(delayMs);

    // Enter to send
    SendMessage(m_GameWindow, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(m_GameWindow, WM_KEYUP, VK_RETURN, MakeLParam(VK_RETURN, false));
    Sleep(delayMs);
}

void MusicPlayer::AnnounceCurrentSong() {
    if (!m_AnnounceEnabled) return;

    const Song* song = GetCurrentSong();
    if (!song) return;

    // Format the length string as (M:SS)
    float totalSec = song->GetTotalDurationSeconds();
    int mins = (int)totalSec / 60;
    int secs = (int)totalSec % 60;
    char lengthBuf[16];
    snprintf(lengthBuf, sizeof(lengthBuf), "(%d:%02d)", mins, secs);

    // Build message from format string
    std::string msg = m_AnnounceFormat;
    // Replace %s with title
    for (size_t pos = msg.find("%s"); pos != std::string::npos; pos = msg.find("%s", pos)) {
        msg.replace(pos, 2, song->title);
        pos += song->title.size();
    }
    // Replace %a with author
    for (size_t pos = msg.find("%a"); pos != std::string::npos; pos = msg.find("%a", pos)) {
        msg.replace(pos, 2, song->author);
        pos += song->author.size();
    }
    // Replace %l with length
    std::string lenStr(lengthBuf);
    for (size_t pos = msg.find("%l"); pos != std::string::npos; pos = msg.find("%l", pos)) {
        msg.replace(pos, 2, lenStr);
        pos += lenStr.size();
    }

    SendChatMessage(msg);
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
    file << "announce_enabled=" << (m_AnnounceEnabled ? 1 : 0) << "\n";
    file << "announce_format=" << m_AnnounceFormat << "\n";
}

void MusicPlayer::LoadKeyConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string valStr = line.substr(eq + 1);

        // String-valued keys
        if (key == "announce_format") {
            m_AnnounceFormat = valStr;
            continue;
        }

        // Numeric keys
        int val = 0;
        try { val = std::stoi(valStr); } catch (...) { continue; }

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
        } else if (key == "announce_enabled") {
            m_AnnounceEnabled = (val != 0);
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
