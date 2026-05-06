#include "PlaylistEditor.h"
#include <imgui.h>
#include <cmath>

namespace Serenade {

void PlaylistEditor::RenderPlaylistPane(MusicPlayer& player) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("Playlist (%d tracks)", (int)player.GetPlaylistSize());
    ImGui::PopStyleColor();
    ImGui::Separator();

    const auto& playlist    = player.GetPlaylist();
    const auto& library     = player.GetSongLibrary();
    int currentTrack        = player.GetCurrentTrackIndex();

    if (currentTrack >= 0 && currentTrack != m_LastCurrentTrack) {
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
            m_ScrollTargetPending = true;
        m_LastCurrentTrack = currentTrack;
    } else if (currentTrack < 0) {
        m_LastCurrentTrack = -1;
    }

    if (ImGui::BeginTable("##PlTable", 5,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",          ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Title",      ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Artist",     ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Length",     ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 dropLineCol       = IM_COL32(255, 215, 50, 255);
        const float dropLineThickness = 3.0f;

        for (int i = 0; i < (int)playlist.size(); i++) {
            int libIdx = playlist[i];
            if (libIdx < 0 || libIdx >= (int)library.size()) continue;

            const auto& song    = library[libIdx];
            bool selected       = (m_SelectedPlaylistItem == i);
            bool isCurrent      = (i == currentTrack);
            bool playing        = isCurrent && player.IsPlaying();
            bool isDragSource   = (m_Dragging && m_DragSourceIdx == i);

            if (playing)      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            if (isDragSource) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            char buf[64];
            snprintf(buf, sizeof(buf), "##pl_%d", i);
            if (ImGui::Selectable(buf, selected, ImGuiSelectableFlags_SpanAllColumns))
                m_SelectedPlaylistItem = i;

            // Capture scroll target
            if (isCurrent && m_ScrollTargetPending) {
                float windowH    = ImGui::GetWindowHeight();
                float targetScroll = ImGui::GetCursorPosY() - windowH * 0.3f;
                if (targetScroll < 0.0f)  targetScroll = 0.0f;
                float maxScroll  = ImGui::GetScrollMaxY();
                if (targetScroll > maxScroll) targetScroll = maxScroll;
                m_ScrollTargetY       = targetScroll;
                m_ScrollTargetPending = false;
            }

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                m_DragSourceIdx = i;
                m_Dragging      = true;
                ImGui::SetDragDropPayload("PL_REORDER", &i, sizeof(int));
                ImGui::Text("Moving: %s", song.title.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target
            if (ImGui::BeginDragDropTarget()) {
                ImVec2 rowMin = ImGui::GetItemRectMin();
                ImVec2 rowMax = ImGui::GetItemRectMax();
                float rowMidY = (rowMin.y + rowMax.y) * 0.5f;
                bool dropBelow = ImGui::GetMousePos().y > rowMidY;
                float lineY = dropBelow ? rowMax.y : rowMin.y;
                m_DragTargetIdx = dropBelow ? i + 1 : i;

                drawList->AddLine(ImVec2(rowMin.x, lineY), ImVec2(rowMax.x, lineY),
                                  dropLineCol, dropLineThickness);
                float triSize = 5.0f;
                drawList->AddTriangleFilled(
                    ImVec2(rowMin.x, lineY - triSize), ImVec2(rowMin.x, lineY + triSize),
                    ImVec2(rowMin.x + triSize * 1.5f, lineY), dropLineCol);
                drawList->AddTriangleFilled(
                    ImVec2(rowMax.x, lineY - triSize), ImVec2(rowMax.x, lineY + triSize),
                    ImVec2(rowMax.x - triSize * 1.5f, lineY), dropLineCol);

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PL_REORDER")) {
                    int srcIdx = *(const int*)payload->Data;
                    int dstIdx = m_DragTargetIdx;
                    if (srcIdx < dstIdx) dstIdx--;
                    if (srcIdx != dstIdx && srcIdx >= 0 && dstIdx >= 0) {
                        player.MoveInPlaylist(srcIdx, dstIdx);
                        m_SelectedPlaylistItem = dstIdx;
                    }
                    m_Dragging      = false;
                    m_DragSourceIdx = -1;
                    m_DragTargetIdx = -1;
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                player.JumpToTrack(i);
                if (!player.IsPlaying()) player.Play();
                m_LastCurrentTrack    = i;
                m_ScrollTargetY       = -1.0f;
                m_ScrollTargetPending = false;
            }

            if (ImGui::BeginPopupContextItem()) {
                m_SelectedPlaylistItem = i;
                if (ImGui::MenuItem("Play")) {
                    player.JumpToTrack(i);
                    if (!player.IsPlaying()) player.Play();
                    m_LastCurrentTrack    = i;
                    m_ScrollTargetY       = -1.0f;
                    m_ScrollTargetPending = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Edit Metadata...")) {
                    m_EditSongLibIdx = libIdx;
                    snprintf(m_EditTitle,      sizeof(m_EditTitle),      "%s", song.title.c_str());
                    snprintf(m_EditAuthor,     sizeof(m_EditAuthor),     "%s", song.author.c_str());
                    snprintf(m_EditInstrument, sizeof(m_EditInstrument), "%s", song.instrument.c_str());
                    snprintf(m_EditPart,       sizeof(m_EditPart),       "%s", song.part.c_str());
                    m_ShowEditPopup = true;
                }
                if (ImGui::MenuItem("Remove from Playlist")) {
                    player.RemoveFromPlaylist(i);
                    if (m_SelectedPlaylistItem >= (int)player.GetPlaylistSize())
                        m_SelectedPlaylistItem = (int)player.GetPlaylistSize() - 1;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Song...")) {
                    m_EditSongLibIdx    = libIdx;
                    m_ShowDeleteConfirm = true;
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::Text("%s%d", isCurrent ? "> " : "  ", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", song.title.c_str());

            ImGui::TableSetColumnIndex(2);
            if (!playing && !isDragSource) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Text("%s", song.author.c_str());
            if (!playing && !isDragSource) ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(3);
            if (!playing && !isDragSource) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Text("%s", song.instrument.c_str());
            if (!playing && !isDragSource) ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(4);
            float dur = song.GetTotalDurationSeconds();
            if (!playing && !isDragSource) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Text("%d:%02d", (int)(dur/60), (int)dur%60);
            if (!playing && !isDragSource) ImGui::PopStyleColor();

            if (isDragSource) ImGui::PopStyleColor();
            if (playing)      ImGui::PopStyleColor();
        }

        // Reset drag state if mouse released outside a drop target
        if (m_Dragging && !ImGui::IsMouseDown(0)) {
            m_Dragging      = false;
            m_DragSourceIdx = -1;
            m_DragTargetIdx = -1;
        }

        // Smooth scroll animation (cancel if window is hovered)
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            m_ScrollTargetY       = -1.0f;
            m_ScrollTargetPending = false;
        } else if (m_ScrollTargetY >= 0.0f) {
            float current = ImGui::GetScrollY();
            float diff    = m_ScrollTargetY - current;
            if (std::abs(diff) < 1.0f) {
                ImGui::SetScrollY(m_ScrollTargetY);
                m_ScrollTargetY = -1.0f;
            } else {
                ImGui::SetScrollY(current + diff * 0.15f);
            }
        }

        ImGui::EndTable();
    }
}

} // namespace Serenade
