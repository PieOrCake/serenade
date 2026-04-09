#ifndef SERENADE_MUSICPLAYER_H
#define SERENADE_MUSICPLAYER_H

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <windows.h>
#include "SongParser.h"

namespace Serenade {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

enum class RepeatMode {
    Off,
    All,
    One
};

// Key configuration for instrument playback
struct KeyConfig {
    WORD noteKeys[8];       // VK codes for notes 1-8 (C, D, E, F, G, A, B, C)
    WORD sharpKeys[5];      // VK codes for F1-F5 (C#, D#, F#, G#, A#)
    WORD octaveUpKey;       // VK code for octave up (default '0' = GW2 "Next Scale")
    WORD octaveDownKey;     // VK code for octave down (default '9' = GW2 "Previous Scale")

    KeyConfig() {
        for (int i = 0; i < 8; i++) noteKeys[i] = (WORD)('1' + i);
        for (int i = 0; i < 5; i++) sharpKeys[i] = (WORD)(VK_F1 + i);
        octaveUpKey = '0';
        octaveDownKey = '9';
    }
};

// Get display name for a virtual key code
std::string VKToDisplayName(WORD vk);

// Instrument timing profiles (minimum ms between notes)
struct InstrumentProfile {
    std::string name;
    int minNoteDelayMs;         // Minimum delay between notes
    int octaveSwapDelayMs;      // Delay after octave change (server roundtrip)
    bool supportsChords;
    int octaveCount;            // 2 or 3
    int notesPerOctave;         // 7 for piano (C-B), 8 for bell (C-C')
};

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();

    // Playback controls
    void Play();
    void Pause();
    void Stop();
    void Next();
    void Previous();
    void SeekTo(float progress);  // Seek to position (0.0-1.0)

    // State
    PlaybackState GetState() const { return m_State.load(); }
    bool IsPlaying() const { return m_State.load() == PlaybackState::Playing; }
    bool IsPaused() const { return m_State.load() == PlaybackState::Paused; }
    bool IsStopped() const { return m_State.load() == PlaybackState::Stopped; }

    // Shuffle & Repeat
    void SetShuffle(bool enabled) { m_Shuffle = enabled; }
    bool GetShuffle() const { return m_Shuffle; }
    void SetRepeatMode(RepeatMode mode) { m_RepeatMode = mode; }
    RepeatMode GetRepeatMode() const { return m_RepeatMode; }
    void CycleRepeatMode();

    // Current track info
    const Song* GetCurrentSong() const;
    int GetCurrentTrackIndex() const { return m_CurrentTrack; }
    float GetPlaybackProgress() const;     // 0.0 to 1.0
    float GetElapsedSeconds() const;
    float GetTotalSeconds() const;
    int GetCurrentEventIndex() const { return m_CurrentEvent.load(); }

    // Song library (all available songs)
    void SetSongLibrary(std::vector<Song> songs);
    const std::vector<Song>& GetSongLibrary() const { return m_Library; }
    size_t GetLibrarySize() const { return m_Library.size(); }

    // Playlist (curated subset)
    void SetPlaylist(std::vector<int> indices);
    const std::vector<int>& GetPlaylist() const { return m_Playlist; }
    void AddToPlaylist(int libraryIndex);
    void RemoveFromPlaylist(int playlistPosition);
    void MoveInPlaylist(int from, int to);
    void ClearPlaylist();
    size_t GetPlaylistSize() const { return m_Playlist.size(); }

    // Jump to a specific track in the playlist
    void JumpToTrack(int playlistIndex);

    // Play a song directly from the library without modifying the playlist
    void PlayDirectFromLibrary(int libraryIndex);
    bool IsDirectPlay() const { return m_DirectPlayLibIdx >= 0; }

    // Instrument selection
    void SetInstrument(const std::string& name);
    const InstrumentProfile& GetInstrument() const { return m_Instrument; }
    static const std::vector<InstrumentProfile>& GetAvailableInstruments();

    // BPM override (0 = use song default)
    void SetBPMOverride(int bpm) { m_BPMOverride = bpm; }
    int GetBPMOverride() const { return m_BPMOverride; }
    int GetEffectiveBPM() const;

    // Persistence
    void SavePlaylist(const std::string& filepath) const;
    void LoadPlaylist(const std::string& filepath);

    // Key configuration
    void SetKeyConfig(const KeyConfig& config) { m_KeyConfig = config; }
    const KeyConfig& GetKeyConfig() const { return m_KeyConfig; }
    void SaveKeyConfig(const std::string& filepath) const;
    void LoadKeyConfig(const std::string& filepath);

    // Chat announcement
    void SetAnnounceEnabled(bool enabled) { m_AnnounceEnabled = enabled; }
    bool GetAnnounceEnabled() const { return m_AnnounceEnabled; }
    void SetAnnounceFormat(const std::string& fmt) { m_AnnounceFormat = fmt; }
    const std::string& GetAnnounceFormat() const { return m_AnnounceFormat; }

    // WndProc handle for sending keys
    void SetGameWindow(HWND hwnd) { m_GameWindow = hwnd; }

    // Debug log (writes to file)
    void SetDebugLogPath(const std::string& path);
    void DebugLog(const std::string& msg);

private:
    void PlaybackThread();
    void SendNoteKeys(const std::vector<int>& keys);
    void SendOctaveChange(Octave target);
    void SendChatMessage(const std::string& message);
    void AnnounceCurrentSong();
    void AdvanceTrack();
    int ResolveNextTrack() const;
    int ResolvePrevTrack() const;

    // State
    std::atomic<PlaybackState> m_State{PlaybackState::Stopped};
    bool m_Shuffle = false;
    RepeatMode m_RepeatMode = RepeatMode::Off;

    // Track position
    int m_CurrentTrack = -1;            // Index into m_Playlist
    int m_DirectPlayLibIdx = -1;         // Direct play from library (-1 = off)
    std::atomic<int> m_CurrentEvent{0}; // Index into current song's events
    Octave m_CurrentOctave = Octave::Mid;
    int m_OctaveChangeCount = 0;           // For periodic re-sync

    // Timing
    int m_BPMOverride = 0;
    std::chrono::steady_clock::time_point m_PlaybackStart;
    float m_ElapsedBeforeLastPause = 0.0f;
    std::chrono::steady_clock::time_point m_LastPrevPressTime{};

    // Seek support
    std::atomic<double> m_SeekOffsetMs{0.0};
    std::atomic<bool> m_SkipOctaveReset{false};
    Octave m_SeekTargetOctave = Octave::Mid;

    // Song data
    std::vector<Song> m_Library;
    std::vector<int> m_Playlist;    // Indices into m_Library

    // Instrument
    InstrumentProfile m_Instrument;

    // Key config
    KeyConfig m_KeyConfig;

    // Playback thread
    std::thread m_Thread;
    std::atomic<bool> m_ThreadRunning{false};
    std::atomic<bool> m_ThreadStop{false};
    std::mutex m_Mutex;

    // Shuffle history (recently played playlist indices, most recent at back)
    std::deque<int> m_ShuffleHistory;

    // Game window handle
    HWND m_GameWindow = nullptr;

    // Chat announcement
    bool m_AnnounceEnabled = false;
    std::string m_AnnounceFormat = "Now serenading: %s by %a %l";

    // Debug log (file-based)
    std::string m_DebugLogPath;
    std::mutex m_DebugMutex;
};

} // namespace Serenade

#endif
