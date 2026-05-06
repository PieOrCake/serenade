#include "MusicPlayer.h"
#include <algorithm>

namespace Serenade {

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
        if (key >= 1 && key <= 8)        vk = m_KeyConfig.noteKeys[key - 1];
        else if (key >= 9 && key <= 13)  vk = m_KeyConfig.sharpKeys[key - 9];
        if (vk == 0) continue;
        DebugLog("  KEY DOWN: key=" + std::to_string(key) + " vk=" + VKToDisplayName(vk));
        SendKeyDown(vk);
    }

    Sleep(30);

    for (int key : keys) {
        WORD vk = 0;
        if (key >= 1 && key <= 8)        vk = m_KeyConfig.noteKeys[key - 1];
        else if (key >= 9 && key <= 13)  vk = m_KeyConfig.sharpKeys[key - 9];
        if (vk == 0) continue;
        SendKeyUp(vk);
    }
}

void MusicPlayer::SendOctaveChange(Octave target) {
    if (target == m_CurrentOctave) return;

    const char* octNames[] = {"Low", "Mid", "High"};
    DebugLog("  OCTAVE: " + std::string(octNames[(int)m_CurrentOctave]) + " -> " + octNames[(int)target]);

    WORD downVk   = m_KeyConfig.octaveDownKey;
    WORD upVk     = m_KeyConfig.octaveUpKey;
    WORD downScan = (WORD)MapVirtualKeyA(downVk, MAPVK_VK_TO_VSC);
    WORD upScan   = (WORD)MapVirtualKeyA(upVk,   MAPVK_VK_TO_VSC);

    int upPresses = static_cast<int>(target);

    // Batch 1: reset to Low (3x key 9, clamps harmlessly)
    INPUT downInputs[6] = {};
    for (int i = 0; i < 3; i++) {
        downInputs[i * 2].type             = INPUT_KEYBOARD;
        downInputs[i * 2].ki.wVk           = downVk;
        downInputs[i * 2].ki.wScan         = downScan;
        downInputs[i * 2].ki.dwFlags       = 0;
        downInputs[i * 2 + 1].type         = INPUT_KEYBOARD;
        downInputs[i * 2 + 1].ki.wVk       = downVk;
        downInputs[i * 2 + 1].ki.wScan     = downScan;
        downInputs[i * 2 + 1].ki.dwFlags   = KEYEVENTF_KEYUP;
    }
    SendInput(6, downInputs, sizeof(INPUT));

    if (upPresses > 0) {
        Sleep(20);

        // Batch 2: go up to target (1-2x key 0)
        std::vector<INPUT> upInputs(upPresses * 2);
        for (int i = 0; i < upPresses; i++) {
            upInputs[i * 2] = {};
            upInputs[i * 2].type         = INPUT_KEYBOARD;
            upInputs[i * 2].ki.wVk       = upVk;
            upInputs[i * 2].ki.wScan     = upScan;
            upInputs[i * 2].ki.dwFlags   = 0;
            upInputs[i * 2 + 1] = {};
            upInputs[i * 2 + 1].type       = INPUT_KEYBOARD;
            upInputs[i * 2 + 1].ki.wVk     = upVk;
            upInputs[i * 2 + 1].ki.wScan   = upScan;
            upInputs[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        SendInput((UINT)upInputs.size(), upInputs.data(), sizeof(INPUT));
    }

    Sleep(10);
    m_CurrentOctave = target;
}

static LPARAM MakeLParam(uint32_t vk, bool down) {
    int64_t lp = !down;
    lp = lp << 1;
    lp += !down;
    lp = lp << 1;
    lp += 0;
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

    SendMessage(m_GameWindow, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(m_GameWindow, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
    Sleep(delayMs);

    INPUT ctrlDown = {};
    ctrlDown.type      = INPUT_KEYBOARD;
    ctrlDown.ki.wVk    = VK_CONTROL;
    ctrlDown.ki.wScan  = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    SendInput(1, &ctrlDown, sizeof(INPUT));
    Sleep(delayMs);

    SendMessage(m_GameWindow, WM_KEYDOWN, 'V', MakeLParam('V', true));
    SendMessage(m_GameWindow, WM_KEYUP,   'V', MakeLParam('V', false));
    Sleep(delayMs);

    INPUT ctrlUp = {};
    ctrlUp.type         = INPUT_KEYBOARD;
    ctrlUp.ki.wVk       = VK_CONTROL;
    ctrlUp.ki.wScan     = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    ctrlUp.ki.dwFlags   = KEYEVENTF_KEYUP;
    SendInput(1, &ctrlUp, sizeof(INPUT));
    Sleep(delayMs);

    SendMessage(m_GameWindow, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(m_GameWindow, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
    Sleep(delayMs);
}

void MusicPlayer::AnnounceCurrentSong() {
    if (!m_AnnounceEnabled) return;

    const Song* song = GetCurrentSong();
    if (!song) return;

    float totalSec = song->GetTotalDurationSeconds();
    int mins = (int)totalSec / 60;
    int secs = (int)totalSec % 60;
    char lengthBuf[16];
    snprintf(lengthBuf, sizeof(lengthBuf), "(%d:%02d)", mins, secs);

    std::string msg = m_AnnounceFormat;
    for (size_t pos = msg.find("%s"); pos != std::string::npos; pos = msg.find("%s", pos)) {
        msg.replace(pos, 2, song->title);
        pos += song->title.size();
    }
    for (size_t pos = msg.find("%a"); pos != std::string::npos; pos = msg.find("%a", pos)) {
        msg.replace(pos, 2, song->author);
        pos += song->author.size();
    }
    std::string lenStr(lengthBuf);
    for (size_t pos = msg.find("%l"); pos != std::string::npos; pos = msg.find("%l", pos)) {
        msg.replace(pos, 2, lenStr);
        pos += lenStr.size();
    }

    SendChatMessage(msg);
}

void MusicPlayer::PlaybackThread() {
    AnnounceCurrentSong();

    bool seekMode = m_SkipOctaveReset.exchange(false);
    if (seekMode) {
        const char* octNames[] = {"Low", "Mid", "High"};
        Octave seekTarget = m_SeekTargetOctave;
        DebugLog("=== SEEK RESUME: setting octave to " +
                 std::string(octNames[(int)seekTarget]) + " ===");
        m_CurrentOctave = static_cast<Octave>((static_cast<int>(seekTarget) + 1) % 3);
        SendOctaveChange(seekTarget);
        m_OctaveChangeCount = 0;
        Sleep(200);
        DebugLog("=== SEEK RESUME: ready ===");
    } else {
        const Song* startSong = GetCurrentSong();
        bool isDrum = startSong && startSong->instrument == "Drums";
        if (isDrum) {
            DebugLog("=== RESET skipped: drum song has no octaves ===");
        } else {
            DebugLog("=== RESET: pressing 9 x5, then 0 x1 to reach Mid octave ===");
            int resetHoldMs  = 50;
            int resetDelayMs = 200;
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
            Sleep(500);
            DebugLog("=== RESET complete: now at Mid octave ===");
        }
    }

    auto timelineStart = std::chrono::steady_clock::now();
    double accumulatedMs = 0.0;
    if (seekMode) {
        double seekMs = m_SeekOffsetMs.load();
        accumulatedMs = seekMs;
        timelineStart -= std::chrono::microseconds(
            static_cast<long long>(seekMs * 1000.0));
    }
    double pauseOffset = 0.0;

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
                if (m_Instrument.notesPerOctave == 7) {
                    bool hasNote8 = false;
                    std::vector<int> normalKeys;
                    for (int k : ev.keys) {
                        if (k == 8) hasNote8 = true;
                        else normalKeys.push_back(k);
                    }
                    if (hasNote8) {
                        if (!normalKeys.empty()) SendNoteKeys(normalKeys);
                        Octave prevOctave = m_CurrentOctave;
                        if (static_cast<int>(m_CurrentOctave) < 2)
                            SendOctaveChange(static_cast<Octave>(static_cast<int>(m_CurrentOctave) + 1));
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
        if (m_State.load() == PlaybackState::Paused) {
            auto pauseStart = std::chrono::steady_clock::now();
            while (m_State.load() == PlaybackState::Paused && !m_ThreadStop.load())
                Sleep(10);
            auto pauseEnd = std::chrono::steady_clock::now();
            pauseOffset += std::chrono::duration<double, std::milli>(pauseEnd - pauseStart).count();
            continue;
        }

        if (m_State.load() != PlaybackState::Playing) break;

        const Song* song = GetCurrentSong();
        if (!song || song->events.empty()) {
            AdvanceTrack();
            if (m_ThreadStop.load()) break;
            timelineStart  = std::chrono::steady_clock::now();
            accumulatedMs  = 0.0;
            pauseOffset    = 0.0;
            continue;
        }

        int eventIdx = m_CurrentEvent.load();
        if (eventIdx >= (int)song->events.size()) {
            AdvanceTrack();
            if (m_ThreadStop.load()) break;
            {
                const Song* nextSong = GetCurrentSong();
                bool nextIsDrum = nextSong && nextSong->instrument == "Drums";
                if (nextIsDrum) {
                    DebugLog("=== Inter-song gap skipped: drum song has no octaves ===");
                } else {
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
                }
            }
            if (m_ThreadStop.load()) break;
            timelineStart  = std::chrono::steady_clock::now();
            accumulatedMs  = 0.0;
            pauseOffset    = 0.0;
            continue;
        }

        int bpm = GetEffectiveBPM();
        double beatDurationMs = 60000.0 / static_cast<double>(bpm);

        // Execute 0-duration events (octave changes) early, during previous note's sustain
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

        if (!waitUntil(accumulatedMs)) continue;

        const NoteEvent& ev = song->events[eventIdx];
        executeEvent(ev, eventIdx);

        accumulatedMs += ev.durationBeats * beatDurationMs;
        m_CurrentEvent.store(eventIdx + 1);
    }

    m_ThreadRunning.store(false);
}

} // namespace Serenade
