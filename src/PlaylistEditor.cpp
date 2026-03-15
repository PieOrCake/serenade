#include "PlaylistEditor.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <fstream>
#include <filesystem>
#include <wininet.h>
#include "../lib/nlohmann_json.hpp"

namespace Serenade {

// --- Icon drawing helpers for playlist action buttons ---
typedef void (*PlIconDrawFn)(ImDrawList*, float, float, float, ImU32);

static bool PlIconButton(const char* id, float w, float h, PlIconDrawFn drawFn,
                         ImVec4 activeCol = ImVec4(0,0,0,0)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    ImU32 bgCol;
    if (held)        bgCol = IM_COL32(90, 90, 100, 200);
    else if (hovered) bgCol = IM_COL32(70, 70, 80, 200);
    else              bgCol = IM_COL32(46, 46, 56, 200);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bgCol, 6.0f);

    ImU32 iconCol = hovered ? IM_COL32(220, 220, 230, 255) : IM_COL32(160, 160, 170, 255);

    float cx = pos.x + w * 0.5f;
    float cy = pos.y + h * 0.5f;
    float s  = (w < h ? w : h);
    drawFn(dl, cx, cy, s, iconCol);

    return pressed;
}

// Single right chevron: >
static void DrawIconChevronRight(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f;
    float w = s * 0.18f;
    dl->AddTriangleFilled(
        ImVec2(cx - w, cy - h), ImVec2(cx - w, cy + h),
        ImVec2(cx + w, cy), col);
}

// Double right chevron: >>
static void DrawIconChevronRightDouble(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f;
    float w = s * 0.15f;
    float off = s * 0.12f;
    dl->AddTriangleFilled(
        ImVec2(cx - off - w, cy - h), ImVec2(cx - off - w, cy + h),
        ImVec2(cx - off + w, cy), col);
    dl->AddTriangleFilled(
        ImVec2(cx + off - w, cy - h), ImVec2(cx + off - w, cy + h),
        ImVec2(cx + off + w, cy), col);
}

// Single left chevron: <
static void DrawIconChevronLeft(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f;
    float w = s * 0.18f;
    dl->AddTriangleFilled(
        ImVec2(cx + w, cy - h), ImVec2(cx + w, cy + h),
        ImVec2(cx - w, cy), col);
}

// Double left chevron: <<
static void DrawIconChevronLeftDouble(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f;
    float w = s * 0.15f;
    float off = s * 0.12f;
    dl->AddTriangleFilled(
        ImVec2(cx + off + w, cy - h), ImVec2(cx + off + w, cy + h),
        ImVec2(cx + off - w, cy), col);
    dl->AddTriangleFilled(
        ImVec2(cx - off + w, cy - h), ImVec2(cx - off + w, cy + h),
        ImVec2(cx - off - w, cy), col);
}

// Up chevron: ^
static void DrawIconChevronUp(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f;
    float h = s * 0.18f;
    dl->AddTriangleFilled(
        ImVec2(cx - w, cy + h), ImVec2(cx + w, cy + h),
        ImVec2(cx, cy - h), col);
}

// Down chevron: v
static void DrawIconChevronDown(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f;
    float h = s * 0.18f;
    dl->AddTriangleFilled(
        ImVec2(cx - w, cy - h), ImVec2(cx + w, cy - h),
        ImVec2(cx, cy + h), col);
}

PlaylistEditor::PlaylistEditor() {}

bool PlaylistEditor::Render(MusicPlayer& player) {
    if (!m_Visible) return false;

    // Match player window styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.13f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.25f, 0.15f, 0.5f));
    // Child pane backgrounds
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.07f, 0.8f));
    // Buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.26f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.30f, 0.18f, 1.0f));
    // Tabs
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.30f, 0.25f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.35f, 0.28f, 0.12f, 1.0f));
    // Selectables
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.18f, 0.12f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.25f, 0.15f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.32f, 0.18f, 1.0f));
    // Frame (input fields)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
    // Separator
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.22f, 0.15f, 0.5f));

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Serenade - Playlist Editor", &m_Visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor(17);
        ImGui::PopStyleVar(4);
        return m_Visible;
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float buttonColWidth = 48.0f;
    float paneWidth = (availWidth - buttonColWidth - ImGui::GetStyle().ItemSpacing.x * 2) * 0.5f;

    // Top bar: Refresh + Download Songs toggle
    if (ImGui::Button("Refresh")) {
        if (m_RefreshCb) m_RefreshCb();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ShowOnlinePane ? "Back to Library" : "Download Songs")) {
        m_ShowOnlinePane = !m_ShowOnlinePane;
        if (m_ShowOnlinePane && !m_OnlineFetched && !m_OnlineFetching) {
            FetchOnlineSongs();
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
    ImGui::Text(m_ShowOnlinePane ? "Songs available on GitHub" : "Place .ahk files in music/");
    ImGui::PopStyleColor();

    float availHeight = ImGui::GetContentRegionAvail().y;

    if (m_ShowOnlinePane) {
        // Online song browser (full width)
        ImGui::BeginChild("##OnlinePane", ImVec2(0, availHeight), true);
        RenderOnlinePane(player);
        ImGui::EndChild();
    } else {
        // Normal dual-pane layout
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
    }

    // --- Edit Metadata Popup ---
    if (m_ShowEditPopup) {
        ImGui::OpenPopup("Edit Song Metadata");
        m_ShowEditPopup = false;
    }
    if (ImGui::BeginPopupModal("Edit Song Metadata", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& library = player.GetSongLibrary();
        if (m_EditSongLibIdx >= 0 && m_EditSongLibIdx < (int)library.size()) {
            ImGui::Text("Editing: %s", library[m_EditSongLibIdx].filepath.c_str());
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));

            ImGui::Text("Title");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##edit_title", m_EditTitle, sizeof(m_EditTitle));

            ImGui::Text("Author");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##edit_author", m_EditAuthor, sizeof(m_EditAuthor));

            ImGui::Text("Instrument");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##edit_instrument", m_EditInstrument, sizeof(m_EditInstrument));

            ImGui::Text("Part");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##edit_part", m_EditPart, sizeof(m_EditPart));

            ImGui::Dummy(ImVec2(0, 4));
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0))) {
                const auto& s = library[m_EditSongLibIdx];
                if (!s.filepath.empty()) {
                    UpdateSongMetadata(s.filepath, m_EditTitle, m_EditAuthor,
                                       m_EditInstrument, m_EditPart);
                    if (m_RefreshCb) m_RefreshCb();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Text("Invalid song selection.");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Delete Confirmation Popup ---
    if (m_ShowDeleteConfirm) {
        ImGui::OpenPopup("Delete Song?");
        m_ShowDeleteConfirm = false;
    }
    if (ImGui::BeginPopupModal("Delete Song?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& library = player.GetSongLibrary();
        if (m_EditSongLibIdx >= 0 && m_EditSongLibIdx < (int)library.size()) {
            const auto& s = library[m_EditSongLibIdx];
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::Text("Are you sure you want to delete this song?");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::Text("Title: %s", s.title.c_str());
            ImGui::Text("File: %s", s.filepath.c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Delete", ImVec2(120, 0))) {
                if (!s.filepath.empty()) {
                    try {
                        std::filesystem::remove(s.filepath);
                    } catch (...) {}
                    if (m_RefreshCb) m_RefreshCb();
                    m_SelectedLibraryItem = -1;
                    m_SelectedPlaylistItem = -1;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Text("Invalid song selection.");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    ImGui::PopStyleColor(17);
    ImGui::PopStyleVar(4);
    return m_Visible;
}

void PlaylistEditor::RenderLibraryPane(MusicPlayer& player) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("Song Library");
    ImGui::PopStyleColor();
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

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            m_SelectedLibraryItem = i;
            if (ImGui::MenuItem("Add to Playlist")) {
                player.AddToPlaylist(i);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Metadata...")) {
                m_EditSongLibIdx = i;
                snprintf(m_EditTitle, sizeof(m_EditTitle), "%s", song.title.c_str());
                snprintf(m_EditAuthor, sizeof(m_EditAuthor), "%s", song.author.c_str());
                snprintf(m_EditInstrument, sizeof(m_EditInstrument), "%s", song.instrument.c_str());
                snprintf(m_EditPart, sizeof(m_EditPart), "%s", song.part.c_str());
                m_ShowEditPopup = true;
            }
            if (ImGui::MenuItem("Delete Song...")) {
                m_EditSongLibIdx = i;
                m_ShowDeleteConfirm = true;
            }
            ImGui::EndPopup();
        }

        // Tooltip with details
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

void PlaylistEditor::RenderActionButtons(MusicPlayer& player) {
    float totalHeight = ImGui::GetContentRegionAvail().y;
    float btnW = ImGui::GetContentRegionAvail().x;
    float btnH = 36.0f;
    float sepH = 12.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.y;
    float totalButtonsHeight = btnH * 6 + spacing * 5 + sepH;
    float startY = (totalHeight - totalButtonsHeight) * 0.5f;
    if (startY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + startY);

    // Add selected to playlist
    if (PlIconButton("##pl_add", btnW, btnH, DrawIconChevronRight)) {
        if (m_SelectedLibraryItem >= 0 && m_SelectedLibraryItem < (int)player.GetLibrarySize()) {
            player.AddToPlaylist(m_SelectedLibraryItem);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add to playlist");

    // Add all
    if (PlIconButton("##pl_addall", btnW, btnH, DrawIconChevronRightDouble)) {
        for (int i = 0; i < (int)player.GetLibrarySize(); i++) {
            player.AddToPlaylist(i);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add all");

    // Remove selected from playlist
    if (PlIconButton("##pl_rem", btnW, btnH, DrawIconChevronLeft)) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize()) {
            player.RemoveFromPlaylist(m_SelectedPlaylistItem);
            if (m_SelectedPlaylistItem >= (int)player.GetPlaylistSize()) {
                m_SelectedPlaylistItem = (int)player.GetPlaylistSize() - 1;
            }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from playlist");

    // Remove all
    if (PlIconButton("##pl_remall", btnW, btnH, DrawIconChevronLeftDouble)) {
        player.ClearPlaylist();
        m_SelectedPlaylistItem = -1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear playlist");

    ImGui::Dummy(ImVec2(0, sepH * 0.5f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, sepH * 0.5f));

    // Move up
    if (PlIconButton("##pl_up", btnW, btnH, DrawIconChevronUp)) {
        if (m_SelectedPlaylistItem > 0) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem - 1);
            m_SelectedPlaylistItem--;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");

    // Move down
    if (PlIconButton("##pl_down", btnW, btnH, DrawIconChevronDown)) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize() - 1) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem + 1);
            m_SelectedPlaylistItem++;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");
}

void PlaylistEditor::RenderPlaylistPane(MusicPlayer& player) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("Playlist (%d tracks)", (int)player.GetPlaylistSize());
    ImGui::PopStyleColor();
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

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            m_SelectedPlaylistItem = i;
            if (ImGui::MenuItem("Play")) {
                player.JumpToTrack(i);
                if (!player.IsPlaying()) player.Play();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Metadata...")) {
                m_EditSongLibIdx = libIdx;
                snprintf(m_EditTitle, sizeof(m_EditTitle), "%s", song.title.c_str());
                snprintf(m_EditAuthor, sizeof(m_EditAuthor), "%s", song.author.c_str());
                snprintf(m_EditInstrument, sizeof(m_EditInstrument), "%s", song.instrument.c_str());
                snprintf(m_EditPart, sizeof(m_EditPart), "%s", song.part.c_str());
                m_ShowEditPopup = true;
            }
            if (ImGui::MenuItem("Remove from Playlist")) {
                player.RemoveFromPlaylist(i);
                if (m_SelectedPlaylistItem >= (int)player.GetPlaylistSize()) {
                    m_SelectedPlaylistItem = (int)player.GetPlaylistSize() - 1;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Song...")) {
                m_EditSongLibIdx = libIdx;
                m_ShowDeleteConfirm = true;
            }
            ImGui::EndPopup();
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

// --- HTTP helper: fetch a URL and return the response body as a string ---
static std::string HttpGet(const std::string& url) {
    std::string result;
    HINTERNET hInternet = InternetOpenA("Serenade/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return result;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return result;
    }

    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        result.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return result;
}

// --- Download a file from URL and save to disk ---
static bool HttpDownloadFile(const std::string& url, const std::string& destPath) {
    std::string data = HttpGet(url);
    if (data.empty()) return false;

    std::ofstream file(destPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(data.data(), data.size());
    return file.good();
}

static const char* kGitHubRawBase = "https://raw.githubusercontent.com/PieOrCake/serenade/main/songs/";

void PlaylistEditor::FetchOnlineSongs() {
    m_OnlineFetching = true;
    m_OnlineError.clear();
    m_OnlineSongs.clear();

    std::string indexUrl = std::string(kGitHubRawBase) + "index.json";
    std::string json = HttpGet(indexUrl);
    m_OnlineFetching = false;

    if (json.empty()) {
        m_OnlineError = "Failed to connect to GitHub. Check your internet connection.";
        return;
    }

    try {
        auto parsed = nlohmann::json::parse(json);

        if (!parsed.is_array()) {
            m_OnlineError = "Unexpected index.json format.";
            return;
        }

        for (const auto& item : parsed) {
            if (!item.is_object()) continue;
            std::string file = item.value("file", "");
            if (file.empty()) continue;

            OnlineSong song;
            song.name = file;
            song.title = item.value("title", "");
            song.author = item.value("author", "");
            song.instrument = item.value("instrument", "");
            song.part = item.value("part", "");
            song.downloadUrl = std::string(kGitHubRawBase) + file;
            song.size = item.value("size", 0);
            m_OnlineSongs.push_back(song);
        }

        // Sort by title (fall back to filename)
        std::sort(m_OnlineSongs.begin(), m_OnlineSongs.end(),
            [](const OnlineSong& a, const OnlineSong& b) {
                const std::string& ak = a.title.empty() ? a.name : a.title;
                const std::string& bk = b.title.empty() ? b.name : b.title;
                return ak < bk;
            });

        UpdateLocalStatus();
        m_OnlineFetched = true;

    } catch (const std::exception& e) {
        m_OnlineError = std::string("JSON parse error: ") + e.what();
    }
}

void PlaylistEditor::DownloadSong(int index) {
    if (index < 0 || index >= (int)m_OnlineSongs.size()) return;
    if (m_SongsDirectory.empty()) return;

    OnlineSong& song = m_OnlineSongs[index];
    if (song.downloadUrl.empty() || song.downloaded) return;

    song.downloading = true;

    std::string destPath = m_SongsDirectory + "/" + song.name;
    bool ok = HttpDownloadFile(song.downloadUrl, destPath);

    song.downloading = false;
    if (ok) {
        song.downloaded = true;
        // Refresh the song library so it picks up the new file
        if (m_RefreshCb) m_RefreshCb();
    }
}

void PlaylistEditor::UpdateLocalStatus() {
    if (m_SongsDirectory.empty()) return;
    for (auto& song : m_OnlineSongs) {
        std::string localPath = m_SongsDirectory + "/" + song.name;
        song.downloaded = std::filesystem::exists(localPath);
    }
}

void PlaylistEditor::RenderOnlinePane(MusicPlayer& player) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("Download Songs");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Refresh button
    if (ImGui::Button("Refresh List")) {
        m_OnlineFetched = false;
        FetchOnlineSongs();
    }
    ImGui::SameLine();
    if (!m_OnlineSongs.empty()) {
        // Download All button
        int notDownloaded = 0;
        for (const auto& s : m_OnlineSongs) {
            if (!s.downloaded && !s.downloading) notDownloaded++;
        }
        if (notDownloaded > 0) {
            char label[64];
            snprintf(label, sizeof(label), "Download All (%d)", notDownloaded);
            if (ImGui::Button(label)) {
                for (int i = 0; i < (int)m_OnlineSongs.size(); i++) {
                    if (!m_OnlineSongs[i].downloaded && !m_OnlineSongs[i].downloading) {
                        DownloadSong(i);
                    }
                }
            }
        }
    }

    // Status messages
    if (m_OnlineFetching) {
        ImGui::TextDisabled("Fetching song list from GitHub...");
        return;
    }
    if (!m_OnlineError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", m_OnlineError.c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (m_OnlineSongs.empty()) {
        ImGui::TextDisabled("No songs available on GitHub yet.");
        return;
    }

    // Filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##OnlineFilter", "Filter...", m_OnlineFilter, sizeof(m_OnlineFilter));

    std::string filterStr(m_OnlineFilter);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    // Song list
    ImGui::BeginChild("##OnlineList", ImVec2(0, 0), false);

    for (int i = 0; i < (int)m_OnlineSongs.size(); i++) {
        const auto& song = m_OnlineSongs[i];

        // Apply filter across title, author, instrument, filename
        if (!filterStr.empty()) {
            auto toLower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                return s;
            };
            bool match = toLower(song.name).find(filterStr) != std::string::npos
                      || toLower(song.title).find(filterStr) != std::string::npos
                      || toLower(song.author).find(filterStr) != std::string::npos
                      || toLower(song.instrument).find(filterStr) != std::string::npos;
            if (!match) continue;
        }

        ImGui::PushID(i);

        // Use title from metadata, fall back to filename without extension
        std::string displayName = song.title;
        if (displayName.empty()) {
            displayName = song.name;
            if (displayName.size() > 4 && displayName.substr(displayName.size() - 4) == ".ahk")
                displayName = displayName.substr(0, displayName.size() - 4);
        }

        // Build metadata tag: [instrument - part] or [instrument] or [part]
        std::string metaTag;
        if (!song.instrument.empty()) metaTag = song.instrument;
        if (!song.part.empty()) {
            if (!metaTag.empty()) metaTag += " - ";
            metaTag += song.part;
        }

        // Size label
        char sizeLabel[32] = "";
        if (song.size > 0) {
            if (song.size >= 1024 * 1024)
                snprintf(sizeLabel, sizeof(sizeLabel), "%.1f MB", song.size / (1024.0f * 1024.0f));
            else if (song.size >= 1024)
                snprintf(sizeLabel, sizeof(sizeLabel), "%.1f KB", song.size / 1024.0f);
            else
                snprintf(sizeLabel, sizeof(sizeLabel), "%d B", song.size);
        }

        // Status indicator + download button
        if (song.downloaded) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("[OK]");
            ImGui::PopStyleColor();
            ImGui::SameLine();
        } else if (song.downloading) {
            ImGui::TextDisabled("[...]");
            ImGui::SameLine();
        } else {
            if (ImGui::SmallButton("Download")) {
                DownloadSong(i);
            }
            ImGui::SameLine();
        }

        // Title
        ImGui::Text("%s", displayName.c_str());

        // Author + metadata on same line, dimmed
        if (!song.author.empty() || !metaTag.empty() || sizeLabel[0]) {
            ImGui::SameLine();
            ImGui::TextDisabled(" -");
            if (!song.author.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", song.author.c_str());
            }
            if (!metaTag.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", metaTag.c_str());
            }
            if (sizeLabel[0]) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", sizeLabel);
            }
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

} // namespace Serenade
