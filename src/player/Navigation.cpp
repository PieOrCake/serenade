#include "MusicPlayer.h"
#include <algorithm>
#include <random>

namespace Serenade {

void MusicPlayer::Play() {
    if (m_Playlist.empty()) return;

    PlaybackState expected = PlaybackState::Stopped;
    if (m_State.compare_exchange_strong(expected, PlaybackState::Playing)) {
        if (m_Thread.joinable()) m_Thread.join();

        if (m_CurrentTrack < 0) m_CurrentTrack = 0;
        if (!m_SkipOctaveReset.load()) {
            m_CurrentEvent.store(0);
            m_CurrentOctave = Octave::Mid;
            m_ElapsedBeforeLastPause = 0.0f;
        }
        m_PlaybackStart = std::chrono::steady_clock::now();
        m_ThreadStop.store(false);
        m_ThreadRunning.store(true);
        m_Thread = std::thread(&MusicPlayer::PlaybackThread, this);
        SetThreadPriority((HANDLE)m_Thread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
        return;
    }

    expected = PlaybackState::Paused;
    if (m_State.compare_exchange_strong(expected, PlaybackState::Playing)) {
        m_PlaybackStart = std::chrono::steady_clock::now();
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
    m_StopAfterCurrent.store(false);
    m_ThreadStop.store(true);
    m_State.store(PlaybackState::Stopped);

    if (m_Thread.joinable() && m_Thread.get_id() != std::this_thread::get_id())
        m_Thread.join();

    m_CurrentEvent.store(0);
    m_CurrentOctave = Octave::Mid;
    m_ElapsedBeforeLastPause = 0.0f;
    m_ThreadRunning.store(false);
    m_SkipOctaveReset.store(false);
    m_SeekOffsetMs.store(0.0);
}

void MusicPlayer::StopAfterCurrent() {
    if (m_Playlist.empty()) return;
    m_StopAfterCurrent.store(true);
}

void MusicPlayer::Next() {
    m_StopAfterCurrent.store(false);
    bool wasPlaying = IsPlaying();
    if (m_Shuffle && m_CurrentTrack >= 0) {
        m_ShuffleHistory.push_back(m_CurrentTrack);
        int maxHist = std::max(2, std::min(50, (int)m_Playlist.size() / 2));
        while ((int)m_ShuffleHistory.size() > maxHist) m_ShuffleHistory.pop_front();
    }
    Stop();
    m_DirectPlayLibIdx = -1;
    m_CurrentTrack = ResolveNextTrack();
    if (wasPlaying && m_CurrentTrack >= 0) Play();
}

void MusicPlayer::Previous() {
    m_StopAfterCurrent.store(false);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - m_LastPrevPressTime).count();
    m_LastPrevPressTime = now;

    bool wasPlaying = IsPlaying();
    m_DirectPlayLibIdx = -1;

    if (elapsed < 1.5) {
        Stop();
        m_CurrentTrack = ResolvePrevTrack();
    } else {
        Stop();
    }

    if (wasPlaying && m_CurrentTrack >= 0) Play();
}

void MusicPlayer::SeekTo(float progress) {
    const Song* song = GetCurrentSong();
    if (!song || song->events.empty()) return;

    progress = std::max(0.0f, std::min(1.0f, progress));
    bool wasPlaying = IsPlaying();

    m_ThreadStop.store(true);
    if (wasPlaying || IsPaused()) m_State.store(PlaybackState::Stopped);
    if (m_Thread.joinable()) m_Thread.join();

    int bpm = GetEffectiveBPM();
    double beatMs = 60000.0 / static_cast<double>(bpm);
    double totalMs = 0.0;
    for (const auto& ev : song->events)
        totalMs += ev.durationBeats * beatMs;
    double targetMs = progress * totalMs;

    double accMs = 0.0;
    int targetEvent = 0;
    for (int i = 0; i < (int)song->events.size(); i++) {
        if (accMs >= targetMs) { targetEvent = i; break; }
        accMs += song->events[i].durationBeats * beatMs;
        targetEvent = i + 1;
    }
    targetEvent = std::min(targetEvent, (int)song->events.size() - 1);

    while (targetEvent > 0 && song->events[targetEvent - 1].durationBeats == 0.0f)
        targetEvent--;

    Octave targetOct = Octave::Mid;
    for (int i = 0; i < targetEvent; i++) {
        const auto& ev = song->events[i];
        switch (ev.type) {
            case EventType::OctaveUp:
                targetOct = static_cast<Octave>(std::min(2, (int)targetOct + 1)); break;
            case EventType::OctaveDown:
                targetOct = static_cast<Octave>(std::max(0, (int)targetOct - 1)); break;
            case EventType::OctaveSet:
                targetOct = ev.targetOctave; break;
            default: break;
        }
    }

    double seekMs = 0.0;
    for (int i = 0; i < targetEvent; i++)
        seekMs += song->events[i].durationBeats * beatMs;

    m_SeekTargetOctave = targetOct;
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
        m_State.store(PlaybackState::Stopped);
        Play();
    }
}

void MusicPlayer::JumpToTrack(int playlistIndex) {
    if (playlistIndex < 0 || playlistIndex >= (int)m_Playlist.size()) return;
    bool wasPlaying = IsPlaying();
    Stop();
    m_DirectPlayLibIdx = -1;
    m_CurrentTrack = playlistIndex;
    if (wasPlaying) Play();
}

void MusicPlayer::PlayDirectFromLibrary(int libraryIndex) {
    if (libraryIndex < 0 || libraryIndex >= (int)m_Library.size()) return;
    Stop();
    m_DirectPlayLibIdx = libraryIndex;
    m_State.store(PlaybackState::Stopped);
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
    if (m_RepeatMode == RepeatMode::One) return m_CurrentTrack;

    if (m_Shuffle) {
        static std::mt19937 rng(std::random_device{}());
        int n = (int)m_Playlist.size();
        if (n == 1) return 0;

        std::vector<double> weights(n, 1.0);
        int histSize = (int)m_ShuffleHistory.size();
        for (int h = 0; h < histSize; h++) {
            int idx = m_ShuffleHistory[h];
            if (idx >= 0 && idx < n) {
                int recency = histSize - h;
                double w = 1.0 / (1.0 + recency * 2.0);
                if (weights[idx] > w) weights[idx] = w;
            }
        }
        if (m_CurrentTrack >= 0 && m_CurrentTrack < n)
            weights[m_CurrentTrack] = 0.0;

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
        return -1;
    }
    return next;
}

int MusicPlayer::ResolvePrevTrack() const {
    if (m_Playlist.empty()) return -1;
    if (m_RepeatMode == RepeatMode::One) return m_CurrentTrack;

    int prev = m_CurrentTrack - 1;
    if (prev < 0) {
        if (m_RepeatMode == RepeatMode::All) return (int)m_Playlist.size() - 1;
        return 0;
    }
    return prev;
}

void MusicPlayer::AdvanceTrack() {
    m_DirectPlayLibIdx = -1;

    if (m_StopAfterCurrent.exchange(false)) {
        m_State.store(PlaybackState::Stopped);
        m_ThreadStop.store(true);
        return;
    }

    if (m_Shuffle && m_CurrentTrack >= 0) {
        m_ShuffleHistory.push_back(m_CurrentTrack);
        int maxHist = std::max(2, std::min(50, (int)m_Playlist.size() / 2));
        while ((int)m_ShuffleHistory.size() > maxHist)
            m_ShuffleHistory.pop_front();
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

} // namespace Serenade
