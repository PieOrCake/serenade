#include "MusicPlayer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

namespace Serenade {

static const std::vector<InstrumentProfile> s_Instruments = {
    {"Ornate Grand Piano", 50, 60, true, 3, 8},
};

MusicPlayer::MusicPlayer() {
    m_Instrument = s_Instruments[0];
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

const std::vector<InstrumentProfile>& MusicPlayer::GetAvailableInstruments() {
    return s_Instruments;
}

void MusicPlayer::SetInstrument(const std::string& name) {
    for (const auto& inst : s_Instruments) {
        if (inst.name == name) { m_Instrument = inst; return; }
    }
}

int MusicPlayer::GetEffectiveBPM() const {
    if (m_BPMOverride > 0) return m_BPMOverride;
    const Song* song = GetCurrentSong();
    if (song) return song->bpm;
    return 120;
}

const Song* MusicPlayer::GetCurrentSong() const {
    if (m_DirectPlayLibIdx >= 0 && m_DirectPlayLibIdx < (int)m_Library.size())
        return &m_Library[m_DirectPlayLibIdx];
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
    int bpm = GetEffectiveBPM();
    if (bpm <= 0) return 0.0f;
    float totalBeats = 0.0f;
    for (const auto& ev : song->events)
        totalBeats += ev.durationBeats;
    return totalBeats * 60.0f / static_cast<float>(bpm);
}

// --- Library ---

void MusicPlayer::SetSongLibrary(std::vector<Song> songs) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_Playlist.empty()) {
        std::vector<std::string> playlistTitles;
        playlistTitles.reserve(m_Playlist.size());
        for (int idx : m_Playlist) {
            if (idx >= 0 && idx < (int)m_Library.size())
                playlistTitles.push_back(m_Library[idx].title);
            else
                playlistTitles.push_back("");
        }

        std::unordered_map<std::string, int> titleToIdx;
        for (int i = 0; i < (int)songs.size(); i++) {
            if (titleToIdx.find(songs[i].title) == titleToIdx.end())
                titleToIdx[songs[i].title] = i;
        }

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

// --- Playlist ---

void MusicPlayer::SetPlaylist(std::vector<int> indices) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist = std::move(indices);
    if (m_CurrentTrack >= (int)m_Playlist.size())
        m_CurrentTrack = m_Playlist.empty() ? -1 : 0;
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
    if (m_CurrentTrack >= (int)m_Playlist.size())
        m_CurrentTrack = m_Playlist.empty() ? -1 : (int)m_Playlist.size() - 1;
}

void MusicPlayer::MoveInPlaylist(int from, int to) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (from < 0 || from >= (int)m_Playlist.size()) return;
    if (to   < 0 || to   >= (int)m_Playlist.size()) return;
    int val = m_Playlist[from];
    m_Playlist.erase(m_Playlist.begin() + from);
    m_Playlist.insert(m_Playlist.begin() + to, val);
    if (m_CurrentTrack == from)
        m_CurrentTrack = to;
    else if (from < m_CurrentTrack && to >= m_CurrentTrack)
        m_CurrentTrack--;
    else if (from > m_CurrentTrack && to <= m_CurrentTrack)
        m_CurrentTrack++;
}

void MusicPlayer::ClearPlaylist() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist.clear();
    m_CurrentTrack = -1;
}

// --- Persistence ---

void MusicPlayer::SavePlaylist(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return;

    if (m_CurrentTrack >= 0 && m_CurrentTrack < (int)m_Playlist.size()) {
        int libIdx = m_Playlist[m_CurrentTrack];
        if (libIdx >= 0 && libIdx < (int)m_Library.size())
            file << "#current=" << m_Library[libIdx].title << "\n";
    }

    for (int idx : m_Playlist) {
        if (idx >= 0 && idx < (int)m_Library.size())
            file << m_Library[idx].title << "\n";
    }
}

void MusicPlayer::LoadPlaylist(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::vector<int> indices;
    std::string currentTitle;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        if (line.size() > 9 && line.substr(0, 9) == "#current=") {
            currentTitle = line.substr(9);
            continue;
        }
        if (!line.empty() && line[0] == '#') continue;

        for (int i = 0; i < (int)m_Library.size(); i++) {
            if (m_Library[i].title == line) {
                indices.push_back(i);
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Playlist = std::move(indices);
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

// --- Key config ---

std::string VKToDisplayName(WORD vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_SPACE:      return "Space";
        case VK_TAB:        return "Tab";
        case VK_RETURN:     return "Enter";
        case VK_ESCAPE:     return "Esc";
        case VK_BACK:       return "Backspace";
        case VK_DELETE:     return "Delete";
        case VK_INSERT:     return "Insert";
        case VK_HOME:       return "Home";
        case VK_END:        return "End";
        case VK_PRIOR:      return "PgUp";
        case VK_NEXT:       return "PgDn";
        case VK_UP:         return "Up";
        case VK_DOWN:       return "Down";
        case VK_LEFT:       return "Left";
        case VK_RIGHT:      return "Right";
        case VK_OEM_MINUS:  return "-";
        case VK_OEM_PLUS:   return "=";
        case VK_OEM_4:      return "[";
        case VK_OEM_6:      return "]";
        case VK_OEM_1:      return ";";
        case VK_OEM_7:      return "'";
        case VK_OEM_COMMA:  return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2:      return "/";
        case VK_OEM_3:      return "`";
        case VK_OEM_5:      return "\\";
        case VK_MULTIPLY:   return "Num*";
        case VK_ADD:        return "Num+";
        case VK_SUBTRACT:   return "Num-";
        case VK_DECIMAL:    return "Num.";
        case VK_DIVIDE:     return "Num/";
        default: break;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

void MusicPlayer::SaveKeyConfig(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return;
    for (int i = 0; i < 8; i++)
        file << "note" << (i + 1) << "=" << m_KeyConfig.noteKeys[i] << "\n";
    for (int i = 0; i < 5; i++)
        file << "sharp" << (i + 1) << "=" << m_KeyConfig.sharpKeys[i] << "\n";
    file << "octave_up="         << m_KeyConfig.octaveUpKey   << "\n";
    file << "octave_down="       << m_KeyConfig.octaveDownKey << "\n";
    file << "announce_enabled="  << (m_AnnounceEnabled ? 1 : 0) << "\n";
    file << "announce_format="   << m_AnnounceFormat << "\n";
    file << "qa_enabled="        << (m_QAEnabled ? 1 : 0) << "\n";
}

void MusicPlayer::LoadKeyConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key    = line.substr(0, eq);
        std::string valStr = line.substr(eq + 1);

        if (key == "announce_format") { m_AnnounceFormat = valStr; continue; }

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
        } else if (key == "qa_enabled") {
            m_QAEnabled = (val != 0);
        }
    }
}

// --- Debug log ---

void MusicPlayer::SetDebugLogPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_DebugMutex);
    m_DebugLogPath = path;
    m_DebugFile.close();
    if (!path.empty())
        m_DebugFile.open(path, std::ios::trunc);
}

void MusicPlayer::DebugLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_DebugMutex);
    if (!m_DebugFile.is_open()) return;
    m_DebugFile << msg << "\n";
    m_DebugFile.flush();
}

} // namespace Serenade
