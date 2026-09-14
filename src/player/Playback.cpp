#include "MusicPlayer.h"
#include <algorithm>
#include "nexus/Nexus.h"

// Nexus API table, owned by dllmain.cpp. Null until AddonLoad runs.
extern AddonAPI_t* APIDefs;

namespace Serenade {

// Builds the lParam a real WM_KEYDOWN/WM_KEYUP carries: scan code plus the
// transition and previous-state bits. The message-based input modes need this;
// SendInput fills it in itself.
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

// Delivers one key transition to the game via the OS input queue.
// Foreground-only — this is what the message modes fall back to.
static void SendKeyViaInput(WORD vk, bool down) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

// The single point every playback key passes through.
//
// Both message modes address the game window directly, so they keep working
// while the user is in another window. Each falls back to SendInput rather than
// dropping the key if its prerequisites are missing — a silently swallowed key
// is a wrong note, which is worse than a note sent the old way.
void MusicPlayer::SendKeyDown(WORD vk) {
    LPARAM lp = MakeLParam(vk, true);

    switch (m_InputMode.load()) {
    case InputMode::NexusWndProc:
        if (m_GameWindow && APIDefs && APIDefs->WndProc_SendToGameOnly) {
            APIDefs->WndProc_SendToGameOnly(m_GameWindow, WM_KEYDOWN, (WPARAM)vk, lp);
            return;
        }
        break;
    case InputMode::PostMessage:
        if (m_GameWindow) {
            PostMessageA(m_GameWindow, WM_KEYDOWN, (WPARAM)vk, lp);
            return;
        }
        break;
    case InputMode::SendInput:
        break;
    }

    SendKeyViaInput(vk, true);
}

void MusicPlayer::SendKeyUp(WORD vk) {
    LPARAM lp = MakeLParam(vk, false);

    switch (m_InputMode.load()) {
    case InputMode::NexusWndProc:
        if (m_GameWindow && APIDefs && APIDefs->WndProc_SendToGameOnly) {
            APIDefs->WndProc_SendToGameOnly(m_GameWindow, WM_KEYUP, (WPARAM)vk, lp);
            return;
        }
        break;
    case InputMode::PostMessage:
        if (m_GameWindow) {
            PostMessageA(m_GameWindow, WM_KEYUP, (WPARAM)vk, lp);
            return;
        }
        break;
    case InputMode::SendInput:
        break;
    }

    SendKeyViaInput(vk, false);
}

// GW2 in front, or no window handle at all. With no handle only SendInput can
// run, so the focus-sensitive guards must stay armed exactly as before.
bool MusicPlayer::GameIsForeground() const {
    if (!m_GameWindow) return true;
    return GetForegroundWindow() == m_GameWindow;
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

    int upPresses = static_cast<int>(target);

    // These runs used to go out as batched SendInput arrays. The message modes
    // cannot batch, so they are sent one key at a time instead. Ordering still
    // holds: WndProc_SendToGameOnly is synchronous, and PostMessage preserves
    // order within a thread's message queue.

    // Run 1: reset to Low (3x octave-down, clamps harmlessly)
    for (int i = 0; i < 3; i++) {
        SendKeyDown(downVk);
        SendKeyUp(downVk);
    }

    if (upPresses > 0) {
        Sleep(20);

        // Run 2: go up to target (1-2x octave-up)
        for (int i = 0; i < upPresses; i++) {
            SendKeyDown(upVk);
            SendKeyUp(upVk);
        }
    }

    Sleep(10);
    m_CurrentOctave = target;
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

static const char* ChannelPrefix(AnnounceChannel ch) {
    switch (ch) {
        case AnnounceChannel::Say:    return "/say ";
        case AnnounceChannel::Party:  return "/party ";
        case AnnounceChannel::Squad:  return "/squad ";
        case AnnounceChannel::Map:    return "/map ";
        case AnnounceChannel::Guild1: return "/g1 ";
        case AnnounceChannel::Guild2: return "/g2 ";
        case AnnounceChannel::Guild3: return "/g3 ";
        case AnnounceChannel::Guild4: return "/g4 ";
        case AnnounceChannel::Guild5: return "/g5 ";
        case AnnounceChannel::Guild6: return "/g6 ";
        default:                      return "";
    }
}

void MusicPlayer::SendChatMessage(const std::string& message) {
    if (message.empty() || m_Unloading) return;

    // Skipped when GW2 is not in front. This function holds Ctrl down with
    // SendInput (which lands in the foreground window) while sending the paste
    // to the game as a message. Run unfocused, it would press and hold Ctrl in
    // whatever app the user is actually using. Announcing unfocused would need
    // the modifier moved onto the message path too; that is a separate change.
    if (!GameIsForeground()) {
        DebugLog("Chat announce skipped: GW2 is not the foreground window");
        return;
    }

    DebugLog("Chat announce: " + message);

    HWND game = m_GameWindow;
    if (!game) {
        game = FindWindowA("ArenaNet_Dx_Window_Class", nullptr);
        if (!game) game = FindWindowA("ArenaNet_Gr_Window_Class", nullptr);
        if (!game) return;
        m_GameWindow = game;
    }

    if (!CopyToOsClipboard(game, message)) return;

    constexpr int kDelay = 50;

    bool focused = m_MumbleLink && m_MumbleLink->Context.IsTextboxFocused;
    if (!focused) {
        SendMessage(game, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
        SendMessage(game, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
        Sleep(kDelay);
    }

    if (m_Unloading) return;

    INPUT ctrlDown{};
    ctrlDown.type     = INPUT_KEYBOARD;
    ctrlDown.ki.wVk   = VK_CONTROL;
    ctrlDown.ki.wScan = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);

    INPUT ctrlUp{};
    ctrlUp.type       = INPUT_KEYBOARD;
    ctrlUp.ki.wVk     = VK_CONTROL;
    ctrlUp.ki.wScan   = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(1, &ctrlDown, sizeof(INPUT));
    Sleep(kDelay);

    if (m_Unloading) {
        SendInput(1, &ctrlUp, sizeof(INPUT));
        return;
    }

    SendMessage(game, WM_KEYDOWN, 'V', MakeLParam('V', true));
    SendMessage(game, WM_KEYUP,   'V', MakeLParam('V', false));
    Sleep(kDelay);

    SendInput(1, &ctrlUp, sizeof(INPUT));
    Sleep(kDelay);

    if (m_Unloading) return;

    SendMessage(game, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(game, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
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

    SendChatMessage(std::string(ChannelPrefix(m_AnnounceChannel)) + msg);
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

    // Chat protection (the GetAsyncKeyState(VK_RETURN) checks below) is gated on
    // GameIsForeground(). GetAsyncKeyState is a global keyboard read that ignores
    // focus; now that playback survives alt-tab, an ungated check would pause the
    // song when the user pressed Enter in a browser or terminal, with no visible
    // cause. The guard still does its real job of keeping notes out of game chat.
    //
    // The combat check needs no such gate: it reads the game's own MumbleLink
    // state, not the keyboard, so nothing outside GW2 can trigger it.
    auto inCombat = [&]() -> bool {
        return m_MumbleLink && m_MumbleLink->Context.IsInCombat;
    };

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
                    if (GameIsForeground() && (GetAsyncKeyState(VK_RETURN) & 0x8000)) {
                        DebugLog("Enter key detected — pausing playback (chat protection)");
                        Pause();
                        return false;
                    }
                    if (inCombat()) {
                        DebugLog("Combat detected — stopping playback");
                        Stop();
                        return false;
                    }
                    int chunk = std::min(10, sleepMs - slept);
                    Sleep(chunk);
                    slept += chunk;
                }
            }
            while (!m_ThreadStop.load() && m_State.load() == PlaybackState::Playing) {
                if (GameIsForeground() && (GetAsyncKeyState(VK_RETURN) & 0x8000)) {
                    DebugLog("Enter key detected — pausing playback (chat protection)");
                    Pause();
                    return false;
                }
                if (inCombat()) {
                    DebugLog("Combat detected — stopping playback");
                    Stop();
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
                        if (GameIsForeground() && (GetAsyncKeyState(VK_RETURN) & 0x8000)) {
                            DebugLog("Enter key detected — pausing playback (chat protection)");
                            Pause();
                            break;
                        }
                        if (inCombat()) {
                            DebugLog("Combat detected — stopping playback");
                            Stop();
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
            if (GameIsForeground() && (GetAsyncKeyState(VK_RETURN) & 0x8000)) {
                DebugLog("Enter key detected — pausing playback (chat protection)");
                Pause();
                break;
            }
            if (inCombat()) {
                DebugLog("Combat detected — stopping playback");
                Stop();
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
