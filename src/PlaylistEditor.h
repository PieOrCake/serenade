#ifndef SERENADE_PLAYLIST_EDITOR_H
#define SERENADE_PLAYLIST_EDITOR_H

#include <imgui.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>
#include "MusicPlayer.h"

namespace Serenade {

// Info about a song available for download from GitHub
struct OnlineSong {
    std::string name;         // filename
    std::string title;        // display title from metadata
    std::string author;       // song author
    std::string instrument;   // recommended instrument
    std::string part;         // part name
    std::string downloadUrl;  // raw download URL
    int size = 0;             // file size in bytes
    bool downloaded = false;  // already exists locally
    bool downloading = false; // currently downloading
    std::string downloadError;
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
    bool* GetDownloadWindowVisiblePtr() { return &m_ShowOnlinePane; }

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
    int m_LibraryArtistFilter = 0;      // 0 = All Artists, 1+ = specific artist
    std::function<void()> m_RefreshCb;

    // Online songs state
    std::string m_SongsDirectory;
    std::vector<OnlineSong> m_OnlineSongs;
    std::atomic<bool> m_OnlineFetching{false};
    std::atomic<bool> m_PendingRefresh{false};
    bool m_OnlineFetched = false;
    std::string m_OnlineError;
    std::mutex m_OnlineMutex;
    char m_OnlineFilter[128] = "";
    bool m_ShowOnlinePane = false;
    bool m_ShowDownloaded = true;     // "show music I already have" checkbox
    int m_OnlineInstrumentTab = 0;    // 0 = All, 1+ = specific instrument
    int m_OnlineArtistFilter = 0;     // 0 = All Artists, 1+ = specific artist

    // Context menu / edit state
    int m_ContextMenuLibIdx = -1;     // library index for right-click menu
    int m_ContextMenuPlIdx = -1;      // playlist index for right-click menu
    bool m_ShowEditPopup = false;
    bool m_ShowDeleteConfirm = false;
    int m_EditSongLibIdx = -1;        // library index of song being edited/deleted
    char m_EditTitle[256] = "";
    char m_EditAuthor[256] = "";
    char m_EditInstrument[256] = "";
    char m_EditPart[256] = "";

    // Filtered library indices (built by RenderLibraryPane, used by RenderActionButtons)
    std::vector<int> m_FilteredLibrary;

    // Cached library metadata (recomputed only when library changes or filters change)
    size_t m_CachedLibSize = 0;                          // detect library refresh
    std::map<std::string, int> m_CachedInstCounts;       // instrument -> count
    std::vector<std::string> m_CachedInstruments;        // sorted instrument names
    int m_CachedInstTab = -1;                            // last instrument tab
    int m_CachedArtistFilter = -1;                       // last artist filter index
    std::string m_CachedLibFilter;                       // last text filter
    std::vector<std::string> m_CachedLibArtists;         // artist list for current tab
    int m_CachedSortCol = -1;                            // last sort column
    int m_CachedSortDir = 0;                             // last sort direction

    // Cached online pane metadata
    size_t m_CachedOnlineSize = 0;
    std::map<std::string, int> m_CachedOnlineInstCounts;
    std::vector<std::string> m_CachedOnlineInstruments;
    int m_CachedOnlineInstTab = -1;
    int m_CachedOnlineArtistIdx = -1;
    std::string m_CachedOnlineFilter;
    bool m_CachedShowDownloaded = true;
    std::vector<std::string> m_CachedOnlineArtists;
    std::vector<int> m_CachedOnlineFiltered;
    int m_CachedOnlineSortCol = -1;
    int m_CachedOnlineSortDir = 0;

    // Auto-scroll to current track (smooth)
    int m_LastCurrentTrack = -1;
    float m_ScrollTargetY = -1.0f;     // -1 = no active scroll animation
    bool m_ScrollTargetPending = false; // waiting to capture row position

    // Drag-and-drop reorder state
    int m_DragSourceIdx = -1;         // playlist index being dragged
    int m_DragTargetIdx = -1;         // current drop target index
    bool m_Dragging = false;
};

} // namespace Serenade

#endif
