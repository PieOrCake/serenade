#include "PlaylistEditor.h"
#include <imgui.h>
#include <algorithm>
#include <set>

namespace Serenade {

// --- Icon drawing helpers for action buttons ---
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

static void DrawIconChevronRight(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f, w = s * 0.18f;
    dl->AddTriangleFilled(ImVec2(cx-w,cy-h), ImVec2(cx-w,cy+h), ImVec2(cx+w,cy), col);
}

static void DrawIconChevronRightDouble(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f, w = s * 0.15f, off = s * 0.12f;
    dl->AddTriangleFilled(ImVec2(cx-off-w,cy-h),ImVec2(cx-off-w,cy+h),ImVec2(cx-off+w,cy),col);
    dl->AddTriangleFilled(ImVec2(cx+off-w,cy-h),ImVec2(cx+off-w,cy+h),ImVec2(cx+off+w,cy),col);
}

static void DrawIconChevronLeft(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f, w = s * 0.18f;
    dl->AddTriangleFilled(ImVec2(cx+w,cy-h), ImVec2(cx+w,cy+h), ImVec2(cx-w,cy), col);
}

static void DrawIconChevronLeftDouble(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f, w = s * 0.15f, off = s * 0.12f;
    dl->AddTriangleFilled(ImVec2(cx+off+w,cy-h),ImVec2(cx+off+w,cy+h),ImVec2(cx+off-w,cy),col);
    dl->AddTriangleFilled(ImVec2(cx-off+w,cy-h),ImVec2(cx-off+w,cy+h),ImVec2(cx-off-w,cy),col);
}

static void DrawIconChevronUp(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f, h = s * 0.18f;
    dl->AddTriangleFilled(ImVec2(cx-w,cy+h), ImVec2(cx+w,cy+h), ImVec2(cx,cy-h), col);
}

static void DrawIconChevronDown(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f, h = s * 0.18f;
    dl->AddTriangleFilled(ImVec2(cx-w,cy-h), ImVec2(cx+w,cy-h), ImVec2(cx,cy+h), col);
}

// ---

void PlaylistEditor::RenderLibraryPane(MusicPlayer& player) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("Song Library");
    ImGui::PopStyleColor();
    ImGui::Separator();

    const auto& library = player.GetSongLibrary();

    if (library.size() != m_CachedLibSize) {
        m_CachedLibSize = library.size();
        m_CachedInstCounts.clear();
        for (const auto& song : library) {
            std::string inst = song.instrument.empty() ? "Unknown" : song.instrument;
            m_CachedInstCounts[inst]++;
        }
        m_CachedInstruments.clear();
        for (const auto& pair : m_CachedInstCounts)
            m_CachedInstruments.push_back(pair.first);
        m_CachedInstTab = -1;
    }
    const auto& instrumentCounts = m_CachedInstCounts;
    const auto& instruments      = m_CachedInstruments;

    if (ImGui::BeginTabBar("##InstrumentTabs")) {
        char allLabel[64];
        snprintf(allLabel, sizeof(allLabel), "All (%d)", (int)library.size());
        if (ImGui::BeginTabItem(allLabel)) {
            if (m_SelectedInstrumentTab != 0) { m_SelectedInstrumentTab = 0; m_LibraryArtistFilter = 0; }
            ImGui::EndTabItem();
        }
        for (int t = 0; t < (int)instruments.size(); t++) {
            char tabLabel[64];
            auto it = instrumentCounts.find(instruments[t]);
            int cnt = (it != instrumentCounts.end()) ? it->second : 0;
            snprintf(tabLabel, sizeof(tabLabel), "%s (%d)", instruments[t].c_str(), cnt);
            if (ImGui::BeginTabItem(tabLabel)) {
                if (m_SelectedInstrumentTab != t + 1) { m_SelectedInstrumentTab = t + 1; m_LibraryArtistFilter = 0; }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    std::string activeInstrument;
    if (m_SelectedInstrumentTab > 0 && m_SelectedInstrumentTab <= (int)instruments.size())
        activeInstrument = instruments[m_SelectedInstrumentTab - 1];

    bool artistListRebuilt = false;
    if (m_CachedInstTab != m_SelectedInstrumentTab) {
        m_CachedInstTab = m_SelectedInstrumentTab;
        artistListRebuilt = true;
        std::set<std::string> libArtistSet;
        for (const auto& song : library) {
            if (!activeInstrument.empty()) {
                std::string inst = song.instrument.empty() ? "Unknown" : song.instrument;
                if (inst != activeInstrument) continue;
            }
            if (!song.author.empty()) libArtistSet.insert(song.author);
        }
        m_CachedLibArtists.assign(libArtistSet.begin(), libArtistSet.end());
    }
    const auto& libArtists = m_CachedLibArtists;

    ImGui::SetNextItemWidth(160);
    const char* artistLabel = m_LibraryArtistFilter == 0 ? "All Artists" :
        (m_LibraryArtistFilter <= (int)libArtists.size() ? libArtists[m_LibraryArtistFilter - 1].c_str() : "All Artists");
    if (ImGui::BeginCombo("##LibArtistFilter", artistLabel)) {
        if (ImGui::Selectable("All Artists", m_LibraryArtistFilter == 0)) m_LibraryArtistFilter = 0;
        for (int a = 0; a < (int)libArtists.size(); a++) {
            bool sel = (m_LibraryArtistFilter == a + 1);
            if (ImGui::Selectable(libArtists[a].c_str(), sel)) m_LibraryArtistFilter = a + 1;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##LibFilter", "Search...", m_LibraryFilter, sizeof(m_LibraryFilter));

    std::string filterStr(m_LibraryFilter);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    std::string activeArtist;
    if (m_LibraryArtistFilter > 0 && m_LibraryArtistFilter <= (int)libArtists.size())
        activeArtist = libArtists[m_LibraryArtistFilter - 1];

    if (ImGui::BeginTable("##LibTable", 4,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Title",      ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Artist",     ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 70.0f);
        ImGui::TableSetupColumn("Length",     ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        int sortCol = -1, sortDir = 0;
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsCount > 0) {
                sortCol = specs->Specs[0].ColumnIndex;
                sortDir = specs->Specs[0].SortDirection;
            }
        }

        bool filterDirty = (filterStr != m_CachedLibFilter)
                        || (m_LibraryArtistFilter  != m_CachedArtistFilter)
                        || artistListRebuilt
                        || (library.size()          != m_CachedLibSize)
                        || (sortCol != m_CachedSortCol)
                        || (sortDir != m_CachedSortDir);
        if (filterDirty) {
            m_CachedLibFilter    = filterStr;
            m_CachedArtistFilter = m_LibraryArtistFilter;
            m_CachedSortCol      = sortCol;
            m_CachedSortDir      = sortDir;

            m_FilteredLibrary.clear();
            for (int i = 0; i < (int)library.size(); i++) {
                const auto& song = library[i];
                if (!activeInstrument.empty()) {
                    std::string songInst = song.instrument.empty() ? "Unknown" : song.instrument;
                    if (songInst != activeInstrument) continue;
                }
                if (!activeArtist.empty() && song.author != activeArtist) continue;
                if (!filterStr.empty()) {
                    std::string tl = song.title;  std::transform(tl.begin(),tl.end(),tl.begin(),::tolower);
                    std::string al = song.author; std::transform(al.begin(),al.end(),al.begin(),::tolower);
                    if (tl.find(filterStr) == std::string::npos &&
                        al.find(filterStr) == std::string::npos) continue;
                }
                m_FilteredLibrary.push_back(i);
            }

            if (sortCol >= 0) {
                bool asc = (sortDir == ImGuiSortDirection_Ascending);
                std::sort(m_FilteredLibrary.begin(), m_FilteredLibrary.end(), [&](int a, int b) {
                    int cmp = 0;
                    if      (sortCol == 0) cmp = _stricmp(library[a].title.c_str(),  library[b].title.c_str());
                    else if (sortCol == 1) cmp = _stricmp(library[a].author.c_str(), library[b].author.c_str());
                    else if (sortCol == 3) cmp = (library[a].GetTotalDurationSeconds() < library[b].GetTotalDurationSeconds()) ? -1 : 1;
                    return asc ? (cmp < 0) : (cmp > 0);
                });
            }
        }

        for (int idx : m_FilteredLibrary) {
            const auto& song = library[idx];
            bool selected = (m_SelectedLibraryItem == idx);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            char buf[64];
            snprintf(buf, sizeof(buf), "##lib_%d", idx);
            if (ImGui::Selectable(buf, selected, ImGuiSelectableFlags_SpanAllColumns))
                m_SelectedLibraryItem = idx;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                player.AddToPlaylist(idx);

            if (ImGui::BeginPopupContextItem()) {
                m_SelectedLibraryItem = idx;
                if (ImGui::MenuItem("Play"))             player.PlayDirectFromLibrary(idx);
                if (ImGui::MenuItem("Add to Playlist"))  player.AddToPlaylist(idx);
                ImGui::Separator();
                if (ImGui::MenuItem("Edit Metadata...")) {
                    m_EditSongLibIdx = idx;
                    snprintf(m_EditTitle,      sizeof(m_EditTitle),      "%s", song.title.c_str());
                    snprintf(m_EditAuthor,     sizeof(m_EditAuthor),     "%s", song.author.c_str());
                    snprintf(m_EditInstrument, sizeof(m_EditInstrument), "%s", song.instrument.c_str());
                    snprintf(m_EditPart,       sizeof(m_EditPart),       "%s", song.part.c_str());
                    m_ShowEditPopup = true;
                }
                if (ImGui::MenuItem("Delete Song...")) { m_EditSongLibIdx = idx; m_ShowDeleteConfirm = true; }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::Text("%s", song.title.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", song.author.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%s", song.instrument.c_str());
            ImGui::TableSetColumnIndex(3);
            float dur = song.GetTotalDurationSeconds();
            ImGui::TextDisabled("%d:%02d", (int)(dur/60), (int)dur%60);
        }
        ImGui::EndTable();
    }
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

    if (PlIconButton("##pl_add", btnW, btnH, DrawIconChevronRight)) {
        if (m_SelectedLibraryItem >= 0 && m_SelectedLibraryItem < (int)player.GetLibrarySize())
            player.AddToPlaylist(m_SelectedLibraryItem);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add to playlist");

    if (PlIconButton("##pl_addall", btnW, btnH, DrawIconChevronRightDouble)) {
        for (int idx : m_FilteredLibrary) player.AddToPlaylist(idx);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add all visible");

    if (PlIconButton("##pl_rem", btnW, btnH, DrawIconChevronLeft)) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize()) {
            player.RemoveFromPlaylist(m_SelectedPlaylistItem);
            if (m_SelectedPlaylistItem >= (int)player.GetPlaylistSize())
                m_SelectedPlaylistItem = (int)player.GetPlaylistSize() - 1;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from playlist");

    if (PlIconButton("##pl_remall", btnW, btnH, DrawIconChevronLeftDouble)) {
        player.ClearPlaylist();
        m_SelectedPlaylistItem = -1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear playlist");

    ImGui::Dummy(ImVec2(0, sepH * 0.5f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, sepH * 0.5f));

    if (PlIconButton("##pl_up", btnW, btnH, DrawIconChevronUp)) {
        if (m_SelectedPlaylistItem > 0) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem - 1);
            m_SelectedPlaylistItem--;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");

    if (PlIconButton("##pl_down", btnW, btnH, DrawIconChevronDown)) {
        if (m_SelectedPlaylistItem >= 0 && m_SelectedPlaylistItem < (int)player.GetPlaylistSize() - 1) {
            player.MoveInPlaylist(m_SelectedPlaylistItem, m_SelectedPlaylistItem + 1);
            m_SelectedPlaylistItem++;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");
}

} // namespace Serenade
