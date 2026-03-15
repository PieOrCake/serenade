#ifndef SERENADE_PLAYLIST_EDITOR_H
#define SERENADE_PLAYLIST_EDITOR_H

#include <imgui.h>
#include <string>
#include <vector>
#include <functional>
#include "MusicPlayer.h"

namespace Serenade {

// Info about a song available for download from GitHub
struct OnlineSong {
    std::string name;         // filename
    std::string downloadUrl;  // raw download URL
    int size = 0;             // file size in bytes
    bool downloaded = false;  // already exists locally
    bool downloading = false; // currently downloading
};

class PlaylistEditor {
public:
    PlaylistEditor();

    // Render the playlist editor window. Returns false if window was closed.
    bool Render(MusicPlayer& player);

    // Set callback to refresh library after scanning music directory
    void SetRefreshCallback(std::function<void()> cb) { m_RefreshCb = cb; }

    // Set the local songs directory path (for downloading)
    void SetSongsDirectory(const std::string& dir) { m_SongsDirectory = dir; }

    // Visibility
    void Show() { m_Visible = true; }
    void Hide() { m_Visible = false; }
    void Toggle() { m_Visible = !m_Visible; }
    bool IsVisible() const { return m_Visible; }
    bool* GetVisiblePtr() { return &m_Visible; }

private:
    void RenderLibraryPane(MusicPlayer& player);
    void RenderActionButtons(MusicPlayer& player);
    void RenderPlaylistPane(MusicPlayer& player);
    void RenderOnlinePane(MusicPlayer& player);

    // Fetch the song listing from GitHub
    void FetchOnlineSongs();
    // Download a single song file
    void DownloadSong(int index);
    // Mark songs that already exist locally
    void UpdateLocalStatus();

    bool m_Visible = false;
    char m_LibraryFilter[128] = "";
    int m_SelectedLibraryItem = -1;
    int m_SelectedPlaylistItem = -1;
    int m_SelectedInstrumentTab = 0;    // 0 = All, 1+ = specific instrument
    std::function<void()> m_RefreshCb;

    // Online songs state
    std::string m_SongsDirectory;
    std::vector<OnlineSong> m_OnlineSongs;
    bool m_OnlineFetching = false;
    bool m_OnlineFetched = false;
    std::string m_OnlineError;
    char m_OnlineFilter[128] = "";
    bool m_ShowOnlinePane = false;
};

} // namespace Serenade

#endif
