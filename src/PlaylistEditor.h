#ifndef SERENADE_PLAYLIST_EDITOR_H
#define SERENADE_PLAYLIST_EDITOR_H

#include <imgui.h>
#include <string>
#include <functional>
#include "MusicPlayer.h"

namespace Serenade {

class PlaylistEditor {
public:
    PlaylistEditor();

    // Render the playlist editor window. Returns false if window was closed.
    bool Render(MusicPlayer& player);

    // Set callback to refresh library after scanning music directory
    void SetRefreshCallback(std::function<void()> cb) { m_RefreshCb = cb; }

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

    bool m_Visible = false;
    char m_LibraryFilter[128] = "";
    int m_SelectedLibraryItem = -1;
    int m_SelectedPlaylistItem = -1;
    int m_SelectedInstrumentTab = 0;    // 0 = All, 1+ = specific instrument
    std::function<void()> m_RefreshCb;
};

} // namespace Serenade

#endif
