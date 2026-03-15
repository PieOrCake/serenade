#include "PlaylistEditor.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace Serenade {

PlaylistEditor::PlaylistEditor() {}

bool PlaylistEditor::Render(MusicPlayer& player) {
    if (!m_Visible) return false;

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Serenade - Playlist Editor", &m_Visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return m_Visible;
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float buttonColWidth = 40.0f;
    float paneWidth = (availWidth - buttonColWidth - ImGui::GetStyle().ItemSpacing.x * 2) * 0.5f;

    // Refresh library button
    if (ImGui::Button("Refresh")) {
        if (m_RefreshCb) m_RefreshCb();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Place .ahk or .txt files in music/ to add songs");

    float availHeight = ImGui::GetContentRegionAvail().y;

    // Left pane: Song Library
    ImGui::BeginChild("##LibraryPane", ImVec2(paneWidth, availHeight), true);
    RenderLibraryPane(player);
    ImGui::EndChild();

    ImGui::SameLine();

    // Center: Action buttons
    ImGui::BeginChild("##ActionButtons", ImVec2(buttonColWidth, availHeight), false);
    RenderActionButtons(player);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: Current Playlist
    ImGui::BeginChild("##PlaylistPane", ImVec2(paneWidth, availHeight), true);
    RenderPlaylistPane(player);
    ImGui::EndChild();

    ImGui::End();
    return m_Visible;
}

void PlaylistEditor::RenderLibraryPane(MusicPlayer& player) {
    ImGui::Text("Song Library");
    ImGui::Separator();

    const auto& library = player.GetSongLibrary();

    // Collect unique instrument names for tabs
    std::set<std::string> instrumentSet;
    for (const auto& song : library) {
        instrumentSet.insert(song.instrument.empty() ? "Unknown" : song.instrument);
    }
    std::vector<std::string> instruments(instrumentSet.begin(), instrumentSet.end());

    // Instrument tabs
    if (ImGui::BeginTabBar("##InstrumentTabs")) {
        // "All" tab
        if (ImGui::BeginTabItem("All")) {
            m_SelectedInstrumentTab = 0;
            ImGui::EndTabItem();
        }
        for (int t = 0; t < (int)instruments.size(); t++) {
            if (ImGui::BeginTabItem(instruments[t].c_str())) {
                m_SelectedInstrumentTab = t + 1;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    // Determine which instrument is selected (empty = show all)
    std::string activeInstrument;
    if (m_SelectedInstrumentTab > 0 && m_SelectedInstrumentTab <= (int)instruments.size()) {
        activeInstrument = instruments[m_SelectedInstrumentTab - 1];
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##LibFilter", "Filter...", m_LibraryFilter, sizeof(m_LibraryFilter));

    std::string filterStr(m_LibraryFilter);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    ImGui::BeginChild("##LibList", ImVec2(0, 0), false);
    for (int i = 0; i < (int)library.size(); i++) {
        const auto& song = library[i];

        // Filter by instrument tab
        if (!activeInstrument.empty()) {
            std::string songInst = song.instrument.empty() ? "Unknown" : song.instrument;
            if (songInst != activeInstrument) continue;
        }

        // Apply text filter
        if (!filterStr.empty()) {
            std::string titleLower = song.title;
            std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
            std::string authorLower = song.author;
            std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
            if (titleLower.find(filterStr) == std::string::npos &&
                authorLower.find(filterStr) == std::string::npos) {
                continue;
            }
        }

        bool selected = (m_SelectedLibraryItem == i);

        // Build display label: title [instrument - part]
        std::string label = song.title;
        std::string meta;
        if (!song.instrument.empty()) meta = song.instrument;
        if (!song.part.empty()) {
            if (!meta.empty()) meta += " - ";
            meta += song.part;
        }
        if (!meta.empty()) label += "  [" + meta + "]";

        char buf[512];
        snprintf(buf, sizeof(buf), "%s##lib_%d", label.c_str(), i);

        if (ImGui::Selectable(buf, selected)) {
            m_SelectedLibraryItem = i;
        }

        // Double-click to add
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            player.AddToPlaylist(i);
        }

        // Tooltip with details
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Title: %s", song.title.c_str());
            if (!song.author.empty()) ImGui::Text("Author: %s", song.author.c_str());
            if (!song.instrument.empty()) ImGui::Text("Instrument: %s", song.instrument.c_str());
            if (!song.part.empty()) ImGui::Text("Part: %s", song.part.c_str());
            ImGui::Text("Events: %d", (int)song.events.size());
            float dur = song.GetTotalDurationSeconds();
            ImGui::Text("Duration: %d:%02d", (int)(dur / 60), (int)dur % 60);
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
}

void PlaylistEditor::RenderActionButtons(MusicPlayer& player) {
    float totalHeight = ImGui::GetContentRegionAvail().y;
    float buttonHeight = ImGui::GetFrameHeight();
    float totalButtonsHeight = buttonHeight * 5 + ImGui::GetStyle().ItemSpacing.y * 4;
    float startY = (totalHeight - totalButtonsHeight) * 0.5f;
    if (startY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + startY);

    // Add selected to playlist
    if (ImGui::Button(">", ImVec2(-1, 0))) {
        if (m_SelectedLibraryItem >= 0 && m_SelectedLibraryItem < (int)player.GetLibrarySize()) {
            player.AddToPlaylist(m_SelectedLibraryItem);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add to playlist");

    // Add all
    if (ImGui::Button(">>", ImVec2(-1, 0))) {
        for (int i = 0; i < (int)player.GetLibrarySize(); i++) {
            player.AddToPlaylist(i);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add all");

    // Remove selected from playlist
    if (ImGui::Button("<", ImVec2(-1, 0))) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize()) {
            player.RemoveFromPlaylist(m_SelectedPlaylistItem);
            if (m_SelectedPlaylistItem >= (int)player.GetPlaylistSize()) {
                m_SelectedPlaylistItem = (int)player.GetPlaylistSize() - 1;
            }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from playlist");

    // Remove all
    if (ImGui::Button("<<", ImVec2(-1, 0))) {
        player.ClearPlaylist();
        m_SelectedPlaylistItem = -1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear playlist");

    ImGui::Separator();

    // Move up
    if (ImGui::Button("^", ImVec2(-1, 0))) {
        if (m_SelectedPlaylistItem > 0) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem - 1);
            m_SelectedPlaylistItem--;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");

    // Move down
    if (ImGui::Button("v", ImVec2(-1, 0))) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize() - 1) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem + 1);
            m_SelectedPlaylistItem++;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");
}

void PlaylistEditor::RenderPlaylistPane(MusicPlayer& player) {
    ImGui::Text("Playlist (%d tracks)", (int)player.GetPlaylistSize());
    ImGui::Separator();

    const auto& playlist = player.GetPlaylist();
    const auto& library = player.GetSongLibrary();
    int currentTrack = player.GetCurrentTrackIndex();

    ImGui::BeginChild("##PlaylistList", ImVec2(0, 0), false);
    for (int i = 0; i < (int)playlist.size(); i++) {
        int libIdx = playlist[i];
        if (libIdx < 0 || libIdx >= (int)library.size()) continue;

        const auto& song = library[libIdx];
        bool selected = (m_SelectedPlaylistItem == i);
        bool isCurrent = (i == currentTrack);

        // Highlight currently playing track
        bool pushed = isCurrent && player.IsPlaying();
        if (pushed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        }

        // Build display label: title [instrument - part]
        std::string plLabel = song.title;
        std::string plMeta;
        if (!song.instrument.empty()) plMeta = song.instrument;
        if (!song.part.empty()) {
            if (!plMeta.empty()) plMeta += " - ";
            plMeta += song.part;
        }
        if (!plMeta.empty()) plLabel += "  [" + plMeta + "]";

        char buf[512];
        snprintf(buf, sizeof(buf), "%s%d. %s##pl_%d",
                 isCurrent ? "> " : "  ",
                 i + 1, plLabel.c_str(), i);

        if (ImGui::Selectable(buf, selected)) {
            m_SelectedPlaylistItem = i;
        }

        // Double-click to play
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            player.JumpToTrack(i);
            if (!player.IsPlaying()) player.Play();
        }

        if (pushed) {
            ImGui::PopStyleColor();
        }

        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Title: %s", song.title.c_str());
            if (!song.author.empty()) ImGui::Text("Author: %s", song.author.c_str());
            if (!song.instrument.empty()) ImGui::Text("Instrument: %s", song.instrument.c_str());
            if (!song.part.empty()) ImGui::Text("Part: %s", song.part.c_str());
            float dur = song.GetTotalDurationSeconds();
            ImGui::Text("Duration: %d:%02d", (int)(dur / 60), (int)dur % 60);
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
}

} // namespace Serenade
