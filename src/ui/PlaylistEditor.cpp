#include "PlaylistEditor.h"
#include "GW2Theme.h"
#include <imgui.h>
#include <filesystem>
#include <shellapi.h>

namespace Serenade {

PlaylistEditor::PlaylistEditor() {}

bool PlaylistEditor::Render(MusicPlayer& player) {
    // Pick up completed downloads even if this window is hidden
    if (m_PendingRefresh.exchange(false) && m_RefreshCb)
        m_RefreshCb();

    if (!m_Visible) return false;

    ThemeGuard themeGuard;

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Serenade - Playlist Editor", &m_Visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return m_Visible;
    }

    float availWidth    = ImGui::GetContentRegionAvail().x;
    float buttonColWidth = 48.0f;
    float paneWidth     = (availWidth - buttonColWidth - ImGui::GetStyle().ItemSpacing.x * 2) * 0.5f;

    // Top bar
    if (ImGui::Button("Refresh")) {
        if (m_RefreshCb) m_RefreshCb();
    }
    ImGui::SameLine();
    if (ImGui::Button("Download Songs")) {
        m_ShowOnlinePane = true;
        if (!m_OnlineFetching) FetchOnlineSongs();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create your own music")) {
        ShellExecuteA(NULL, "open", "https://pie.rocks.cc/projects/serenade-converter/",
                      NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
    ImGui::Text("Place .ahk files in music/");
    ImGui::PopStyleColor();

    float availHeight = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("##LibraryPane", ImVec2(paneWidth, availHeight), true);
    RenderLibraryPane(player);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##ActionButtons", ImVec2(buttonColWidth, availHeight), false);
    RenderActionButtons(player);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##PlaylistPane", ImVec2(paneWidth, availHeight), true);
    RenderPlaylistPane(player);
    ImGui::EndChild();

    // Online downloader window
    if (m_ShowOnlinePane) {
        ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Download Music", &m_ShowOnlinePane, ImGuiWindowFlags_None))
            RenderOnlinePane(player);
        ImGui::End();
    }

    // Edit metadata popup
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

            ImGui::Text("Artist");
            ImGui::SetNextItemWidth(350);
            ImGui::InputText("##edit_author", m_EditAuthor, sizeof(m_EditAuthor));

            ImGui::Text("Instrument");
            ImGui::SetNextItemWidth(350);
            {
                static const char* kInstruments[] = {
                    "Piano", "Harp", "Lute", "Minstrel", "Horn",
                    "Bell", "Verdarach", "Flute", "Bass", "Drums"
                };
                static const int kInstrumentCount = sizeof(kInstruments) / sizeof(kInstruments[0]);
                if (ImGui::BeginCombo("##edit_instrument", m_EditInstrument)) {
                    for (int n = 0; n < kInstrumentCount; n++) {
                        bool isSelected = (strcmp(m_EditInstrument, kInstruments[n]) == 0);
                        if (ImGui::Selectable(kInstruments[n], isSelected))
                            snprintf(m_EditInstrument, sizeof(m_EditInstrument), "%s", kInstruments[n]);
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

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
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Invalid song selection.");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete confirmation popup
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
            ImGui::Text("File: %s",  s.filepath.c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.9f, 0.10f, 0.10f, 1.0f));
            if (ImGui::Button("Delete", ImVec2(120, 0))) {
                if (!s.filepath.empty()) {
                    try { std::filesystem::remove(s.filepath); } catch (...) {}
                    if (m_RefreshCb) m_RefreshCb();
                    m_SelectedLibraryItem  = -1;
                    m_SelectedPlaylistItem = -1;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Invalid song selection.");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    return m_Visible;
}

} // namespace Serenade
