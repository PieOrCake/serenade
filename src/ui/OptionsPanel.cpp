#include "Addon.h"
#include <shellapi.h>

// Rebind state: -1 = idle, 0-7 = note key, 8-12 = sharp key, 13-14 = octave keys
static int g_RebindingSlot = -1;

void AddonOptions() {
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Serenade");
    if (ImGui::SmallButton("Homepage")) {
        ShellExecuteA(NULL, "open", "https://pie.rocks.cc/", NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Buy me a coffee!")) {
        ShellExecuteA(NULL, "open", "https://ko-fi.com/pieorcake", NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::Separator();

    const auto& inst = g_Player.GetInstrument();
    ImGui::Text("Instrument: %s", inst.name.c_str());
    ImGui::Text("  Octaves: %d | Chords: %s | Min note delay: %dms",
                inst.octaveCount,
                inst.supportsChords ? "Yes" : "No",
                inst.minNoteDelayMs);

    ImGui::Spacing();

    int bpmOverride = g_Player.GetBPMOverride();
    if (ImGui::SliderInt("BPM Override", &bpmOverride, 0, 300, bpmOverride == 0 ? "Auto" : "%d"))
        g_Player.SetBPMOverride(bpmOverride);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = use song's default BPM");

    ImGui::Spacing();

    {
        bool announce = g_Player.GetAnnounceEnabled();
        if (ImGui::Checkbox("Announce in chat", &announce)) {
            g_Player.SetAnnounceEnabled(announce);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Send a message to the game chatbox when a song starts playing");

        if (announce) {
            static char fmtBuf[256] = {};
            static bool fmtInit = false;
            if (!fmtInit) {
                snprintf(fmtBuf, sizeof(fmtBuf), "%s", g_Player.GetAnnounceFormat().c_str());
                fmtInit = true;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##AnnounceFormat", fmtBuf, sizeof(fmtBuf))) {
                g_Player.SetAnnounceFormat(fmtBuf);
                if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
            }
            ImGui::TextDisabled("%%s = title, %%a = artist, %%l = length");

            ImGui::Spacing();
            ImGui::TextDisabled("Chat Channel");

            static const char* kChannelNames[] = {
                "Currently Active Channel",
                "Say", "Party", "Squad", "Map",
                "Guild 1", "Guild 2", "Guild 3", "Guild 4", "Guild 5", "Guild 6"
            };
            int ch = static_cast<int>(g_Player.GetAnnounceChannel());
            ImGui::SetNextItemWidth(220);
            if (ImGui::Combo("##announce_channel", &ch, kChannelNames, IM_ARRAYSIZE(kChannelNames))) {
                g_Player.SetAnnounceChannel(static_cast<Serenade::AnnounceChannel>(ch));
                if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Which chat channel to post the Now Playing message to");
        }
    }

    ImGui::Spacing();

    {
        bool qaEnabled = g_Player.GetQAEnabled();
        if (ImGui::Checkbox("Show Quick Access icon", &qaEnabled)) {
            g_Player.SetQAEnabled(qaEnabled);
            ApplyQASetting(qaEnabled);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }
    }

    ImGui::Spacing();

    ImGui::Text("Music directory:");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextWrapped("%s", g_SongsDirectory.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Place .ahk (recommended) or .txt files in music/");

    if (ImGui::Button("Refresh Song Library"))
        RefreshSongLibrary();

    ImGui::Text("Library: %d songs | Playlist: %d tracks",
                (int)g_Player.GetLibrarySize(), (int)g_Player.GetPlaylistSize());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Piano Keybindings");
    ImGui::TextDisabled("Match these to your GW2 keybinds. Click a button then press a key to rebind.");
    ImGui::Spacing();

    {
        Serenade::KeyConfig config = g_Player.GetKeyConfig();
        bool changed = false;

        const char* labels[15] = {
            "C  (1)",  "D  (2)",  "E  (3)",  "F  (4)",
            "G  (5)",  "A  (6)",  "B  (7)",  "C' (8)",
            "C# (F1)", "D# (F2)", "F# (F3)", "G# (F4)", "A# (F5)",
            "Octave Up (0)", "Octave Down (9)"
        };

        ImGui::TextDisabled("Natural Notes");
        for (int i = 0; i < 8; i++) {
            ImGui::PushID(i);
            ImGui::Text("%-14s", labels[i]);
            ImGui::SameLine(130);

            if (g_RebindingSlot == i) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                ImGui::Button("Press a key...", ImVec2(120, 0));
                ImGui::PopStyleColor();
                for (int vk = 0x08; vk <= 0xFE; vk++) {
                    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                        vk == VK_LSHIFT || vk == VK_RSHIFT ||
                        vk == VK_LCONTROL || vk == VK_RCONTROL ||
                        vk == VK_LMENU || vk == VK_RMENU ||
                        vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)
                        continue;
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        config.noteKeys[i] = (WORD)vk;
                        g_RebindingSlot = -1;
                        changed = true;
                        break;
                    }
                }
            } else {
                std::string name = Serenade::VKToDisplayName(config.noteKeys[i]);
                if (ImGui::Button(name.c_str(), ImVec2(120, 0)))
                    g_RebindingSlot = i;
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Octave Controls");
        for (int i = 1; i >= 0; i--) {
            int slot = 13 + i;
            WORD* vkPtr = (i == 0) ? &config.octaveUpKey : &config.octaveDownKey;
            ImGui::PushID(slot);
            ImGui::Text("%-14s", labels[slot]);
            ImGui::SameLine(130);

            if (g_RebindingSlot == slot) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                ImGui::Button("Press a key...", ImVec2(120, 0));
                ImGui::PopStyleColor();
                for (int vk = 0x08; vk <= 0xFE; vk++) {
                    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                        vk == VK_LSHIFT || vk == VK_RSHIFT ||
                        vk == VK_LCONTROL || vk == VK_RCONTROL ||
                        vk == VK_LMENU || vk == VK_RMENU ||
                        vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)
                        continue;
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        *vkPtr = (WORD)vk;
                        g_RebindingSlot = -1;
                        changed = true;
                        break;
                    }
                }
            } else {
                std::string name = Serenade::VKToDisplayName(*vkPtr);
                if (ImGui::Button(name.c_str(), ImVec2(120, 0)))
                    g_RebindingSlot = slot;
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Sharps / Flats");
        for (int i = 0; i < 5; i++) {
            int slot = 8 + i;
            ImGui::PushID(slot);
            ImGui::Text("%-14s", labels[slot]);
            ImGui::SameLine(130);

            if (g_RebindingSlot == slot) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                ImGui::Button("Press a key...", ImVec2(120, 0));
                ImGui::PopStyleColor();
                for (int vk = 0x08; vk <= 0xFE; vk++) {
                    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                        vk == VK_LSHIFT || vk == VK_RSHIFT ||
                        vk == VK_LCONTROL || vk == VK_RCONTROL ||
                        vk == VK_LMENU || vk == VK_RMENU ||
                        vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)
                        continue;
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        config.sharpKeys[i] = (WORD)vk;
                        g_RebindingSlot = -1;
                        changed = true;
                        break;
                    }
                }
            } else {
                std::string name = Serenade::VKToDisplayName(config.sharpKeys[i]);
                if (ImGui::Button(name.c_str(), ImVec2(120, 0)))
                    g_RebindingSlot = slot;
            }
            ImGui::PopID();
        }

        if (changed) {
            g_Player.SetKeyConfig(config);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to Defaults")) {
            Serenade::KeyConfig defaults;
            g_Player.SetKeyConfig(defaults);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }
    }
}
