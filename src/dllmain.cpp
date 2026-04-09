#include <windows.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include "../include/nexus/Nexus.h"
#include "MusicPlayer.h"
#include "SongParser.h"
#include "PlaylistEditor.h"
#include <shellapi.h>

// Version constants
#define V_MAJOR 0
#define V_MINOR 9
#define V_BUILD 3
#define V_REVISION 1

// Quick Access icon identifiers
#define QA_ID "QA_SERENADE"
#define TEX_ICON "TEX_SERENADE_ICON"
#define TEX_ICON_HOVER "TEX_SERENADE_ICON_HOVER"

// Embedded 32x32 beamed eighth note icon (normal - light silver, thin stroke)
static const unsigned char ICON_NOTE[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a, 0xf4, 0x00, 0x00, 0x00,
    0xba, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0xed, 0x96, 0x31, 0x0e, 0xc3,
    0x20, 0x0c, 0x45, 0x8d, 0xe5, 0x63, 0xb1, 0xf4, 0x60, 0x4c, 0x1c, 0xac,
    0x4b, 0xee, 0xd5, 0xca, 0x03, 0x52, 0x54, 0xc5, 0xc6, 0xa6, 0x26, 0x1d,
    0xea, 0x27, 0x65, 0x09, 0x89, 0xff, 0xff, 0x21, 0x18, 0x00, 0x92, 0xe4,
    0x26, 0x6a, 0x7d, 0xbc, 0xae, 0xee, 0xd3, 0x1d, 0x42, 0xad, 0x35, 0xe8,
    0xbd, 0x43, 0x98, 0x81, 0x2a, 0xa4, 0x19, 0x62, 0x1e, 0x68, 0xc5, 0x80,
    0x26, 0x24, 0x25, 0x0d, 0x37, 0x20, 0x89, 0x1d, 0xc7, 0xb3, 0x9c, 0xbf,
    0xd4, 0x30, 0x2a, 0x19, 0x43, 0xf8, 0x82, 0x51, 0x9c, 0x45, 0xc7, 0xe5,
    0xad, 0x81, 0xf0, 0x63, 0x30, 0x0d, 0x40, 0x4e, 0xc1, 0xbf, 0x4f, 0x01,
    0x45, 0x16, 0xd3, 0x5a, 0xf4, 0x76, 0x03, 0xf5, 0xd4, 0xf5, 0x3c, 0x6d,
    0x99, 0xac, 0xc5, 0xbd, 0xe2, 0x56, 0x28, 0x22, 0xd9, 0xaa, 0x38, 0x83,
    0xbb, 0x92, 0x31, 0x96, 0x77, 0x71, 0x97, 0xf8, 0xd6, 0x65, 0xd8, 0x9c,
    0x3f, 0x1b, 0x8f, 0x4b, 0x3b, 0x25, 0x42, 0x00, 0x5c, 0x5c, 0x32, 0xa1,
    0x89, 0x33, 0xea, 0xfe, 0x3d, 0x9b, 0x86, 0xcf, 0xe2, 0x57, 0xab, 0x65,
    0x76, 0x46, 0x28, 0xda, 0xa0, 0x66, 0x62, 0x96, 0xcc, 0x4a, 0xb1, 0x3c,
    0xb4, 0x92, 0x2c, 0x49, 0xc0, 0xc8, 0x1b, 0x29, 0x61, 0x5b, 0x81, 0x57,
    0x59, 0x04, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82
};
static const unsigned int ICON_NOTE_size = sizeof(ICON_NOTE);

// Embedded 32x32 beamed eighth note icon (hover - brighter white)
static const unsigned char ICON_NOTE_HOVER[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a, 0xf4, 0x00, 0x00, 0x00,
    0xbb, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0xed, 0x96, 0x41, 0x0e, 0x84,
    0x20, 0x0c, 0x45, 0x4b, 0xd3, 0x03, 0xb2, 0x99, 0x23, 0x70, 0x2a, 0x8e,
    0x30, 0x1b, 0xef, 0xe5, 0x66, 0x6e, 0x30, 0x86, 0x18, 0x0d, 0x71, 0x00,
    0x29, 0xb6, 0xce, 0xc2, 0xbe, 0xc4, 0x8d, 0xd1, 0xff, 0x7f, 0xad, 0x14,
    0x00, 0x0c, 0xe3, 0x26, 0xbc, 0x7f, 0x7d, 0x4b, 0xf7, 0xe9, 0x0e, 0xa3,
    0x18, 0x23, 0x84, 0x10, 0x40, 0x2c, 0x80, 0xaf, 0x54, 0xb3, 0x99, 0x71,
    0xa0, 0x91, 0x00, 0x2d, 0xa3, 0x5a, 0xa5, 0xe2, 0x01, 0x6a, 0x66, 0xd3,
    0xf4, 0x76, 0xf9, 0x97, 0xda, 0x82, 0x8a, 0xb6, 0xe0, 0xd8, 0xdb, 0xdc,
    0x94, 0x0b, 0xc2, 0x9f, 0x41, 0x0b, 0x00, 0xd6, 0x82, 0xa7, 0xb7, 0x80,
    0x24, 0xc5, 0x5a, 0x23, 0x5a, 0x3d, 0x80, 0xcf, 0xa6, 0x1e, 0x67, 0x2c,
    0x53, 0xaf, 0x38, 0xd7, 0xbc, 0x17, 0x92, 0xa8, 0x6c, 0xd4, 0x3c, 0x81,
    0x5a, 0x95, 0x25, 0x7a, 0xde, 0x45, 0x2d, 0x73, 0xd5, 0x65, 0x18, 0x99,
    0x3f, 0x5b, 0x6b, 0xc7, 0x44, 0x10, 0x20, 0x89, 0xd7, 0x42, 0x9c, 0x6d,
    0xd7, 0xae, 0x25, 0x7c, 0xd6, 0x86, 0xa3, 0x78, 0x69, 0xb5, 0x5c, 0x39,
    0x2b, 0xec, 0xa2, 0xf3, 0xfc, 0xf9, 0xb9, 0x46, 0x86, 0x4e, 0x09, 0xd7,
    0xf3, 0x90, 0x4a, 0x65, 0x86, 0x01, 0x2b, 0x0b, 0x34, 0x00, 0x6a, 0x58,
    0xe8, 0x08, 0x0d, 0xb2, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82
};
static const unsigned int ICON_NOTE_HOVER_size = sizeof(ICON_NOTE_HOVER);

// Global state
HMODULE hSelf;
AddonDefinition_t AddonDef{};
AddonAPI_t* APIDefs = nullptr;
bool g_PlayerWindowVisible = false;
static std::string g_DebugLogPath;
static ImFont* g_TitleFont = nullptr;

// Player and editor
static Serenade::MusicPlayer g_Player;
static Serenade::PlaylistEditor g_PlaylistEditor;
static std::string g_SongsDirectory;
static std::string g_KeyConfigPath;
static std::string g_PlaylistPath;
static int g_SelectedInstrument = 0;

// Keybind rebind state: -1 = not rebinding, 0-7 = note key, 8 = octave up, 9 = octave down
static int g_RebindingSlot = -1;

// Saved window visibility for QA toggle restore
static bool g_SavedPlayerVisible = true;
static bool g_SavedPlaylistVisible = false;
static bool g_SavedDownloadVisible = false;

// --- GW2-Themed UI Style (shared with PlaylistEditor) ---
static ImGuiStyle g_GW2Style;
static std::vector<ImGuiStyle> g_StyleStack;
static bool g_GW2StyleBuilt = false;

void PushGW2Theme() {
    g_StyleStack.push_back(ImGui::GetStyle());
    ImGui::GetStyle() = g_GW2Style;
}

void PopGW2Theme() {
    if (!g_StyleStack.empty()) {
        ImGui::GetStyle() = g_StyleStack.back();
        g_StyleStack.pop_back();
    }
}

static void BuildGW2Theme() {
    g_GW2Style = ImGui::GetStyle();
    ImGuiStyle& s = g_GW2Style;

    // Rounding
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;

    // Spacing & padding
    s.WindowPadding     = ImVec2(10, 10);
    s.FramePadding      = ImVec2(6, 4);
    s.ItemSpacing       = ImVec2(8, 5);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 8.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    // Colors — dark slate base with warm gold accents
    ImVec4* c = s.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.07f, 0.09f, 0.80f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);

    // Borders
    c[ImGuiCol_Border]               = ImVec4(0.28f, 0.25f, 0.18f, 0.50f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames (input boxes, combos)
    c[ImGuiCol_FrameBg]              = ImVec4(0.14f, 0.13f, 0.11f, 0.80f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.20f, 0.14f, 0.80f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.25f, 0.16f, 0.90f);

    // Title bar
    c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.09f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.14f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.07f, 0.05f, 0.75f);

    // Menu bar
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.11f, 0.09f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.06f, 0.07f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.27f, 0.18f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.22f, 0.90f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.44f, 0.26f, 1.00f);

    // Checkmark, slider
    c[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.75f, 0.25f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.85f, 0.70f, 0.25f, 1.00f);

    // Buttons — warm gold
    c[ImGuiCol_Button]               = ImVec4(0.22f, 0.20f, 0.12f, 0.80f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.45f, 0.38f, 0.16f, 1.00f);

    // Headers (selectables, collapsing headers)
    c[ImGuiCol_Header]               = ImVec4(0.18f, 0.16f, 0.10f, 0.70f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.24f, 0.12f, 0.80f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);

    // Separator
    c[ImGuiCol_Separator]            = ImVec4(0.28f, 0.25f, 0.18f, 0.40f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.50f, 0.42f, 0.20f, 0.70f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.65f, 0.55f, 0.25f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.27f, 0.18f, 0.30f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.50f, 0.44f, 0.26f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.65f, 0.55f, 0.25f, 0.90f);

    // Tabs — gold accent for active
    c[ImGuiCol_Tab]                  = ImVec4(0.14f, 0.13f, 0.10f, 0.86f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_TabActive]            = ImVec4(0.28f, 0.24f, 0.10f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.09f, 0.07f, 0.97f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);

    // Text
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.87f, 0.78f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.47f, 0.40f, 1.00f);

    // Modal dim background
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

    // Nav highlight
    c[ImGuiCol_NavHighlight]         = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);

    // Table
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.13f, 0.10f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.28f, 0.25f, 0.18f, 0.60f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.22f, 0.20f, 0.15f, 0.40f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.08f, 0.30f);

    // Plot (progress bars)
    c[ImGuiCol_PlotHistogram]        = ImVec4(0.65f, 0.55f, 0.15f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.68f, 0.20f, 1.00f);

    g_GW2StyleBuilt = true;
}

// Forward declarations
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);
void AddonRender();
void AddonOptions();

// Helper: find GW2 game window
static HWND FindGW2Window() {
    HWND hwnd = FindWindowA("ArenaNet_Dx_Window_Class", nullptr);
    if (!hwnd) hwnd = FindWindowA("ArenaNet_Gr_Window_Class", nullptr);
    return hwnd;
}

// Helper: format time as M:SS
static void FormatTime(float seconds, char* buf, size_t bufSize) {
    int total = (int)seconds;
    if (total < 0) total = 0;
    int min = total / 60;
    int sec = total % 60;
    snprintf(buf, bufSize, "%d:%02d", min, sec);
}

// Font callback from Nexus
static void OnTitleFontReceived(const char* aIdentifier, void* aFont) {
    g_TitleFont = (ImFont*)aFont;
}

// Helper: scan and load songs from music/ directory
static void RefreshSongLibrary() {
    auto songs = Serenade::ScanMusicDirectory(g_SongsDirectory);
    g_Player.SetSongLibrary(std::move(songs));
    APIDefs->Log(LOGL_INFO, "Serenade", 
        (std::string("Loaded ") + std::to_string(g_Player.GetLibrarySize()) + " songs").c_str());
}

// Helper: ensure music/ directory exists
static void EnsureMusicDirs() {
    try {
        std::filesystem::create_directories(g_SongsDirectory);
    } catch (...) {}
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: hSelf = hModule; break;
    case DLL_PROCESS_DETACH: break;
    case DLL_THREAD_ATTACH: break;
    case DLL_THREAD_DETACH: break;
    }
    return TRUE;
}

void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc, 
                                 (void(*)(void*, void*))APIDefs->ImguiFree);

    BuildGW2Theme();

    // Set up directories
    const char* addonDir = APIDefs->Paths_GetAddonDirectory("Serenade");
    if (addonDir) {
        g_SongsDirectory = std::string(addonDir) + "/music";
        g_KeyConfigPath = std::string(addonDir) + "/keybinds.cfg";
    }

    // Set up debug log
    if (addonDir) {
        g_DebugLogPath = std::string(addonDir) + "/debug.log";
        g_Player.SetDebugLogPath(g_DebugLogPath);
    }

    // Load key config and music
    if (!g_KeyConfigPath.empty()) g_Player.LoadKeyConfig(g_KeyConfigPath);
    EnsureMusicDirs();
    RefreshSongLibrary();

    // Configure playlist editor
    g_PlaylistEditor.SetRefreshCallback(RefreshSongLibrary);
    g_PlaylistEditor.SetSongsDirectory(g_SongsDirectory);

    // Load saved playlist
    if (addonDir) {
        g_PlaylistPath = std::string(addonDir) + "/playlist.txt";
        g_Player.LoadPlaylist(g_PlaylistPath);
    }

    // Find GW2 window for key injection
    HWND hwnd = FindGW2Window();
    if (hwnd) {
        g_Player.SetGameWindow(hwnd);
        APIDefs->Log(LOGL_INFO, "Serenade", "GW2 window found");
    } else {
        APIDefs->Log(LOGL_WARNING, "Serenade", "GW2 window not found - playback will not send keys");
    }

    // Register render functions
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    // Register keybinds
    APIDefs->InputBinds_RegisterWithString("KB_SERENADE_TOGGLE", ProcessKeybind, "CTRL+SHIFT+M");

    // Load icon textures
    APIDefs->Textures_LoadFromMemory(TEX_ICON, (void*)ICON_NOTE, ICON_NOTE_size, nullptr);
    APIDefs->Textures_LoadFromMemory(TEX_ICON_HOVER, (void*)ICON_NOTE_HOVER, ICON_NOTE_HOVER_size, nullptr);

    // Register quick access shortcut
    APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON_HOVER, "KB_SERENADE_TOGGLE", "Serenade");

    // Register escape-to-close for both windows
    APIDefs->GUI_RegisterCloseOnEscape("Serenade", &g_PlayerWindowVisible);
    APIDefs->GUI_RegisterCloseOnEscape("Serenade - Playlist Editor", g_PlaylistEditor.GetVisiblePtr());
    APIDefs->GUI_RegisterCloseOnEscape("Download Music", g_PlaylistEditor.GetDownloadWindowVisiblePtr());

    // Load a crisp title font at 2x default size
    APIDefs->Fonts_AddFromFile("SERENADE_TITLE", ImGui::GetFontSize() * 2.0f,
        "C:\\Windows\\Fonts\\segoeui.ttf", OnTitleFontReceived, nullptr);

    APIDefs->Log(LOGL_INFO, "Serenade", "Addon loaded successfully");
}

void AddonUnload() {
    // Stop playback
    g_Player.Stop();

    // Save playlist
    if (!g_PlaylistPath.empty()) {
        g_Player.SavePlaylist(g_PlaylistPath);
    }

    // Deregister escape-to-close
    APIDefs->GUI_DeregisterCloseOnEscape("Serenade");
    APIDefs->GUI_DeregisterCloseOnEscape("Serenade - Playlist Editor");
    APIDefs->GUI_DeregisterCloseOnEscape("Download Music");

    // Save key config
    if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);

    // Deregister keybinds
    APIDefs->InputBinds_Deregister("KB_SERENADE_TOGGLE");

    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    // Release font to prevent crash on unload
    APIDefs->Fonts_Release("SERENADE_TITLE", OnTitleFontReceived);
    g_TitleFont = nullptr;

    APIDefs = nullptr;
}

void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_SERENADE_TOGGLE") == 0) {
        bool anyVisible = g_PlayerWindowVisible ||
                          g_PlaylistEditor.IsVisible() ||
                          *g_PlaylistEditor.GetDownloadWindowVisiblePtr();
        if (anyVisible) {
            // Save which windows are open, then close all
            g_SavedPlayerVisible = g_PlayerWindowVisible;
            g_SavedPlaylistVisible = g_PlaylistEditor.IsVisible();
            g_SavedDownloadVisible = *g_PlaylistEditor.GetDownloadWindowVisiblePtr();
            g_PlayerWindowVisible = false;
            g_PlaylistEditor.Hide();
            *g_PlaylistEditor.GetDownloadWindowVisiblePtr() = false;
        } else {
            // Always show player, restore playlist/download state
            g_PlayerWindowVisible = true;
            if (g_SavedPlaylistVisible) g_PlaylistEditor.Show();
            *g_PlaylistEditor.GetDownloadWindowVisiblePtr() = g_SavedDownloadVisible;
        }
    }
}

// --- Icon drawing helpers ---
// All draw into a square region of size `s` centered at `cx, cy`

static void DrawIconPlay(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.45f;
    float w = s * 0.4f;
    dl->AddTriangleFilled(
        ImVec2(cx - w * 0.4f, cy - h), ImVec2(cx - w * 0.4f, cy + h),
        ImVec2(cx + w * 0.8f, cy), col);
}

static void DrawIconPause(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.38f;
    float bw = s * 0.12f;
    float gap = s * 0.1f;
    dl->AddRectFilled(ImVec2(cx - gap - bw, cy - h), ImVec2(cx - gap + bw, cy + h), col, bw * 0.4f);
    dl->AddRectFilled(ImVec2(cx + gap - bw, cy - h), ImVec2(cx + gap + bw, cy + h), col, bw * 0.4f);
}

static void DrawIconStop(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.3f;
    dl->AddRectFilled(ImVec2(cx - h, cy - h), ImVec2(cx + h, cy + h), col, h * 0.2f);
}

static void DrawIconPrev(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.35f;
    float w = s * 0.28f;
    float barW = s * 0.08f;
    // Bar
    dl->AddRectFilled(ImVec2(cx - w - barW, cy - h), ImVec2(cx - w, cy + h), col);
    // Triangle
    dl->AddTriangleFilled(
        ImVec2(cx + w, cy - h), ImVec2(cx + w, cy + h),
        ImVec2(cx - w + barW, cy), col);
}

static void DrawIconNext(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.35f;
    float w = s * 0.28f;
    float barW = s * 0.08f;
    // Triangle
    dl->AddTriangleFilled(
        ImVec2(cx - w, cy - h), ImVec2(cx - w, cy + h),
        ImVec2(cx + w - barW, cy), col);
    // Bar
    dl->AddRectFilled(ImVec2(cx + w, cy - h), ImVec2(cx + w + barW, cy + h), col);
}

static void DrawIconShuffle(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.32f;
    float h = s * 0.2f;
    float t = 1.4f;
    // Two crossing lines
    dl->AddLine(ImVec2(cx - w, cy - h), ImVec2(cx + w, cy + h), col, t);
    dl->AddLine(ImVec2(cx - w, cy + h), ImVec2(cx + w, cy - h), col, t);
    // Arrow heads on right ends
    float arrowS = s * 0.12f;
    dl->AddTriangleFilled(
        ImVec2(cx + w + arrowS, cy - h),
        ImVec2(cx + w - arrowS, cy - h - arrowS),
        ImVec2(cx + w - arrowS, cy - h + arrowS), col);
    dl->AddTriangleFilled(
        ImVec2(cx + w + arrowS, cy + h),
        ImVec2(cx + w - arrowS, cy + h - arrowS),
        ImVec2(cx + w - arrowS, cy + h + arrowS), col);
}

static void DrawIconRepeat(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.32f;
    float h = s * 0.18f;
    float t = 1.4f;
    float arrowS = s * 0.1f;
    // Top line (left to right)
    dl->AddLine(ImVec2(cx - w, cy - h), ImVec2(cx + w, cy - h), col, t);
    // Right side down
    dl->AddLine(ImVec2(cx + w, cy - h), ImVec2(cx + w, cy + h), col, t);
    // Bottom line (right to left)
    dl->AddLine(ImVec2(cx + w, cy + h), ImVec2(cx - w, cy + h), col, t);
    // Left side up
    dl->AddLine(ImVec2(cx - w, cy + h), ImVec2(cx - w, cy - h), col, t);
    // Arrow on top-right
    dl->AddTriangleFilled(
        ImVec2(cx + w + arrowS, cy - h),
        ImVec2(cx + w - arrowS * 0.5f, cy - h - arrowS),
        ImVec2(cx + w - arrowS * 0.5f, cy - h + arrowS), col);
}

static void DrawIconPlaylist(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f;
    float t = 1.4f;
    float gap = s * 0.2f;
    // Three horizontal lines
    dl->AddLine(ImVec2(cx - w, cy - gap), ImVec2(cx + w, cy - gap), col, t);
    dl->AddLine(ImVec2(cx - w, cy),       ImVec2(cx + w, cy),       col, t);
    dl->AddLine(ImVec2(cx - w, cy + gap), ImVec2(cx + w, cy + gap), col, t);
}

static void DrawMusicNote(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    // Stem
    float stemX = cx + s * 0.15f;
    dl->AddLine(ImVec2(stemX, cy - s * 0.4f), ImVec2(stemX, cy + s * 0.2f), col, 1.5f);
    // Note head (filled ellipse via circle)
    dl->AddCircleFilled(ImVec2(cx, cy + s * 0.2f), s * 0.18f, col);
    // Flag
    dl->AddBezierQuadratic(
        ImVec2(stemX, cy - s * 0.4f),
        ImVec2(stemX + s * 0.3f, cy - s * 0.15f),
        ImVec2(stemX, cy), col, 1.5f);
}

// --- Invisible button with icon overlay ---
// Returns true if clicked. Draws icon centered on the button area.
typedef void (*IconDrawFn)(ImDrawList*, float, float, float, ImU32);

static bool IconButton(const char* id, float size, IconDrawFn drawFn, bool active, ImVec4 activeCol) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float btnH = ImGui::GetFrameHeight();

    // Invisible button for hit testing
    ImGui::InvisibleButton(id, ImVec2(size, btnH));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    // Button background
    ImU32 bgCol;
    if (held) {
        bgCol = IM_COL32(90, 90, 100, 200);
    } else if (hovered) {
        if (active) {
            bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(activeCol.x + 0.1f, activeCol.y + 0.1f, activeCol.z + 0.1f, 1.0f));
        } else {
            bgCol = IM_COL32(70, 70, 80, 200);
        }
    } else if (active) {
        bgCol = ImGui::ColorConvertFloat4ToU32(activeCol);
    } else {
        bgCol = IM_COL32(46, 46, 56, 200);
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + btnH), bgCol, 6.0f);

    // Icon color: brighter on hover
    ImU32 iconCol;
    if (active) {
        iconCol = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 230, 255);
    } else {
        iconCol = hovered ? IM_COL32(220, 220, 230, 255) : IM_COL32(160, 160, 170, 255);
    }

    // Draw icon centered
    float cx = pos.x + size * 0.5f;
    float cy = pos.y + btnH * 0.5f;
    drawFn(dl, cx, cy, size, iconCol);

    return pressed;
}

// Circular icon button (for prominent play/pause)
static bool CircleIconButton(const char* id, float radius, IconDrawFn drawFn, bool active,
                             ImU32 bgNorm, ImU32 bgHov, ImU32 bgHeld) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float diameter = radius * 2.0f;

    ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    float cx = pos.x + radius;
    float cy = pos.y + radius;

    ImU32 bg = held ? bgHeld : (hovered ? bgHov : bgNorm);
    dl->AddCircleFilled(ImVec2(cx, cy), radius, bg, 32);

    ImU32 iconCol = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 240, 255);
    drawFn(dl, cx, cy, diameter * 0.6f, iconCol);

    return pressed;
}

// --- Main Player Window Rendering ---
void AddonRender() {
    static bool g_WasWindowVisible = false;
    if (!g_PlayerWindowVisible) {
        if (g_WasWindowVisible) {
            g_Player.Stop();
            g_WasWindowVisible = false;
        }
        // Still render playlist editor if open
        g_PlaylistEditor.Render(g_Player);
        return;
    }
    g_WasWindowVisible = true;

    // -- Window styling --
    PushGW2Theme();

    ImGui::SetNextWindowSize(ImVec2(340, 235), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("Serenade", &g_PlayerWindowVisible, flags)) {
        ImGui::End();
        PopGW2Theme();
        g_PlaylistEditor.Render(g_Player);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float availW = ImGui::GetContentRegionAvail().x;

    // Auto-save playlist when the current track changes (catches manual + auto advance)
    {
        static int s_LastSavedTrack = -2; // -2 = uninitialized
        int curTrack = g_Player.GetCurrentTrackIndex();
        if (s_LastSavedTrack == -2) {
            s_LastSavedTrack = curTrack; // first frame, just sync
        } else if (curTrack != s_LastSavedTrack) {
            s_LastSavedTrack = curTrack;
            if (!g_PlaylistPath.empty())
                g_Player.SavePlaylist(g_PlaylistPath);
        }
    }

    // ── Row 1: Centered song title (large font, scrolling if too wide) ──
    // Use crisp title font if loaded, otherwise fall back to scale
    bool hasTitleFont = (g_TitleFont != nullptr && g_TitleFont->IsLoaded());
    float normalLineH = ImGui::GetTextLineHeightWithSpacing();
    float scaledLineH;

    if (hasTitleFont) {
        ImGui::PushFont(g_TitleFont);
        scaledLineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PopFont();
    } else {
        scaledLineH = normalLineH * 2.0f;
    }

    float infoHeight = scaledLineH + normalLineH; // title + metadata
    ImVec2 infoStart = ImGui::GetCursorPos();

    // Helper macros to push/pop the title font or fall back to scale
    #define PUSH_TITLE_FONT() do { if (hasTitleFont) ImGui::PushFont(g_TitleFont); else ImGui::SetWindowFontScale(2.0f); } while(0)
    #define POP_TITLE_FONT()  do { if (hasTitleFont) ImGui::PopFont(); else ImGui::SetWindowFontScale(1.0f); } while(0)

    const Serenade::Song* song = g_Player.GetCurrentSong();

    if (song) {
        // Measure title at large size
        PUSH_TITLE_FONT();
        float titleW = ImGui::CalcTextSize(song->title.c_str()).x;
        POP_TITLE_FONT();

        float padX = ImGui::GetStyle().WindowPadding.x;
        float regionW = availW; // clipping width

        if (titleW <= regionW) {
            // Fits — center it
            PUSH_TITLE_FONT();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::SetCursorPosX((availW - titleW) * 0.5f + padX);
            ImGui::Text("%s", song->title.c_str());
            ImGui::PopStyleColor();
            POP_TITLE_FONT();
        } else {
            // Too wide — scroll back and forth
            static double s_ScrollTimer = 0.0;
            static std::string s_LastTitle;
            if (s_LastTitle != song->title) {
                s_LastTitle = song->title;
                s_ScrollTimer = 0.0; // reset on track change
            }
            s_ScrollTimer += ImGui::GetIO().DeltaTime;

            float overflow = titleW - regionW;
            // Ping-pong: 40px/sec scroll, pause 1.5s at each end
            float scrollSpeed = 40.0f;
            float pauseDur = 1.5f;
            float scrollDur = overflow / scrollSpeed;
            float cycleDur = pauseDur + scrollDur + pauseDur + scrollDur;
            float t = fmodf((float)s_ScrollTimer, cycleDur);

            float offsetX = 0.0f;
            if (t < pauseDur) {
                offsetX = 0.0f; // pause at start
            } else if (t < pauseDur + scrollDur) {
                offsetX = ((t - pauseDur) / scrollDur) * overflow; // scroll right
            } else if (t < pauseDur + scrollDur + pauseDur) {
                offsetX = overflow; // pause at end
            } else {
                offsetX = overflow - ((t - pauseDur - scrollDur - pauseDur) / scrollDur) * overflow; // scroll back
            }

            // Clip to content region
            ImVec2 clipMin = ImGui::GetCursorScreenPos();
            ImVec2 clipMax = ImVec2(clipMin.x + regionW, clipMin.y + scaledLineH);
            ImGui::PushClipRect(clipMin, clipMax, true);

            PUSH_TITLE_FONT();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::SetCursorPosX(padX - offsetX);
            ImGui::Text("%s", song->title.c_str());
            ImGui::PopStyleColor();
            POP_TITLE_FONT();

            ImGui::PopClipRect();
        }

        // Metadata — centered, soft grey (normal scale)
        std::string meta;
        if (!song->author.empty()) meta = song->author;
        if (!song->instrument.empty()) {
            if (!meta.empty()) meta += "  \xC2\xB7  ";
            meta += song->instrument;
        }
        if (!meta.empty()) {
            float metaW = ImGui::CalcTextSize(meta.c_str()).x;
            ImGui::SetCursorPosX((availW - metaW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
            ImGui::Text("%s", meta.c_str());
            ImGui::PopStyleColor();
        }
    } else {
        PUSH_TITLE_FONT();
        float noW = ImGui::CalcTextSize("No track loaded").x;
        ImGui::SetCursorPosX((availW - noW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
        ImGui::Text("No track loaded");
        ImGui::PopStyleColor();
        POP_TITLE_FONT();
    }

    #undef PUSH_TITLE_FONT
    #undef POP_TITLE_FONT

    // Reserve fixed height for info area so controls stay stable
    float usedH = ImGui::GetCursorPos().y - infoStart.y;
    if (usedH < infoHeight)
        ImGui::Dummy(ImVec2(0, infoHeight - usedH));

    ImGui::Dummy(ImVec2(0, 2));

    // ── Row 2: Progress bar with time labels ──
    float progress = g_Player.GetPlaybackProgress();
    float elapsed  = g_Player.GetElapsedSeconds();
    float total    = g_Player.GetTotalSeconds();

    char timeBufElapsed[16], timeBufTotal[16];
    FormatTime(elapsed, timeBufElapsed, sizeof(timeBufElapsed));
    FormatTime(total,   timeBufTotal,   sizeof(timeBufTotal));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::Text("%s", timeBufElapsed);
    ImGui::PopStyleColor();
    ImGui::SameLine();

    {
        float timeTextW = ImGui::CalcTextSize(timeBufTotal).x;
        float barW = ImGui::GetContentRegionAvail().x - timeTextW - ImGui::GetStyle().ItemSpacing.x;
        float textH = ImGui::GetTextLineHeight();
        ImVec2 barAreaPos = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##progress", ImVec2(barW, textH));
        bool barHovered = ImGui::IsItemHovered();
        bool barActive = ImGui::IsItemActive();

        // Click or drag to seek
        static bool s_Seeking = false;
        float displayProgress = progress;
        if (song && (barHovered || barActive)) {
            float mouseX = ImGui::GetIO().MousePos.x - barAreaPos.x;
            float seekProgress = mouseX / barW;
            seekProgress = seekProgress < 0.0f ? 0.0f : (seekProgress > 1.0f ? 1.0f : seekProgress);

            if (barActive) {
                s_Seeking = true;
                displayProgress = seekProgress;
            }
            if (s_Seeking && ImGui::IsMouseReleased(0)) {
                g_Player.SeekTo(seekProgress);
                s_Seeking = false;
            }

            // Hover tooltip: show time at mouse position
            float hoverSec = seekProgress * total;
            char hoverBuf[16];
            FormatTime(hoverSec, hoverBuf, sizeof(hoverBuf));
            ImGui::SetTooltip("%s", hoverBuf);
        } else {
            s_Seeking = false;
        }

        float barH = (barHovered || barActive) ? 8.0f : 4.0f;
        float rounding = barH * 0.5f;
        ImVec2 barPos = barAreaPos;
        barPos.y += (textH - barH) * 0.5f;

        // Track background
        dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                          IM_COL32(35, 35, 40, 200), rounding);

        // Filled portion
        if (displayProgress > 0.001f) {
            float fillW = barW * (displayProgress < 1.0f ? displayProgress : 1.0f);
            if (fillW < barH) fillW = barH;
            ImU32 colL = (barHovered || barActive) ? IM_COL32(210, 165, 60, 255) : IM_COL32(190, 145, 50, 255);
            ImU32 colR = (barHovered || barActive) ? IM_COL32(250, 200, 90, 255) : IM_COL32(230, 185, 70, 255);
            dl->AddRectFilledMultiColor(barPos, ImVec2(barPos.x + fillW, barPos.y + barH),
                                        colL, colR, colR, colL);
            dl->AddRectFilled(barPos, ImVec2(barPos.x + fillW, barPos.y + barH),
                              IM_COL32(0, 0, 0, 0), rounding);

            if (barHovered || barActive) {
                dl->AddCircleFilled(ImVec2(barPos.x + fillW, barPos.y + barH * 0.5f),
                                    barH * 0.8f, IM_COL32(255, 220, 120, 255));
            }
        }
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::Text("%s", timeBufTotal);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 4));

    // ── Row 3: Transport controls ──
    // Layout: [shuffle] [prev] [stop] [  ▶ PLAY  ] [next] [repeat] [playlist]
    ImVec4 greenActive(0.22f, 0.48f, 0.28f, 1.0f);
    ImVec4 blueActive(0.22f, 0.28f, 0.52f, 1.0f);

    float smBtn = 26.0f;    // small buttons (shuffle, repeat, playlist)
    float mdBtn = 30.0f;    // medium buttons (prev, stop, next)
    float playR = 22.0f;    // play button radius (44px diameter)
    float playD = playR * 2.0f;
    float gap   = 6.0f;

    // Total width of the transport row
    float rowW = smBtn + gap + mdBtn + gap + mdBtn + gap + playD + gap + mdBtn + gap + smBtn + gap + smBtn;
    float startX = (availW - rowW) * 0.5f + ImGui::GetStyle().WindowPadding.x;
    if (startX < ImGui::GetStyle().WindowPadding.x) startX = ImGui::GetStyle().WindowPadding.x;

    // Vertically align small/medium buttons with the taller circle button
    float smBtnH = ImGui::GetFrameHeight();
    float yOffset = (playD - smBtnH) * 0.5f;
    float baseY = ImGui::GetCursorPosY();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));

    // -- Shuffle --
    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(baseY + yOffset);
    bool shuffleOn = g_Player.GetShuffle();
    if (IconButton("##shf", smBtn, DrawIconShuffle, shuffleOn, greenActive)) {
        g_Player.SetShuffle(!shuffleOn);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shuffle %s", shuffleOn ? "ON" : "OFF");
    ImGui::SameLine();

    // -- Previous --
    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##prev", mdBtn, DrawIconPrev, false, greenActive)) {
        g_Player.Previous();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous");
    ImGui::SameLine();

    // -- Stop --
    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##stop", mdBtn, DrawIconStop, false, greenActive)) {
        g_Player.Stop();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");
    ImGui::SameLine();

    // -- Play / Pause (large circle) --
    ImGui::SetCursorPosY(baseY);
    bool isPlaying = g_Player.IsPlaying();
    ImU32 playBgN = IM_COL32(190, 145, 50, 255);
    ImU32 playBgH = IM_COL32(220, 175, 70, 255);
    ImU32 playBgA = IM_COL32(160, 120, 40, 255);
    if (isPlaying) {
        if (CircleIconButton("##pp", playR, DrawIconPause, true, playBgN, playBgH, playBgA)) {
            g_Player.Pause();
        }
    } else {
        if (CircleIconButton("##pp", playR, DrawIconPlay, false, playBgN, playBgH, playBgA)) {
            g_Player.Play();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "Pause" : "Play");
    ImGui::SameLine();

    // -- Next --
    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##next", mdBtn, DrawIconNext, false, greenActive)) {
        g_Player.Next();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next");
    ImGui::SameLine();

    // -- Repeat --
    ImGui::SetCursorPosY(baseY + yOffset);
    Serenade::RepeatMode repeatMode = g_Player.GetRepeatMode();
    bool repeatActive = (repeatMode != Serenade::RepeatMode::Off);
    if (IconButton("##rpt", smBtn, DrawIconRepeat, repeatActive, blueActive)) {
        g_Player.CycleRepeatMode();
    }
    if (ImGui::IsItemHovered()) {
        const char* modeStr = "OFF";
        if (repeatMode == Serenade::RepeatMode::All) modeStr = "ALL";
        else if (repeatMode == Serenade::RepeatMode::One) modeStr = "ONE";
        ImGui::SetTooltip("Repeat: %s", modeStr);
    }
    if (repeatMode == Serenade::RepeatMode::One) {
        ImVec2 rptPos = ImGui::GetItemRectMin();
        float rptW2 = ImGui::GetItemRectSize().x;
        float rptH2 = ImGui::GetItemRectSize().y;
        dl->AddText(ImVec2(rptPos.x + rptW2 * 0.5f - 3, rptPos.y + rptH2 * 0.5f - 5),
                    IM_COL32(220, 220, 230, 255), "1");
    }
    ImGui::SameLine();

    // -- Playlist Editor --
    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##plist", smBtn, DrawIconPlaylist, false, greenActive)) {
        g_PlaylistEditor.Toggle();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playlist Editor");

    ImGui::PopStyleVar(); // btnSpacing

    // Move cursor past the full play button height
    ImGui::SetCursorPosY(baseY + playD + 4);

    // ── Row 4: Up Next ──
    {
        const Serenade::Song* nextSong = nullptr;
        int curIdx = g_Player.GetCurrentTrackIndex();
        int plSize = (int)g_Player.GetPlaylistSize();
        const auto& playlist = g_Player.GetPlaylist();
        const auto& library  = g_Player.GetSongLibrary();

        if (plSize > 0 && curIdx >= 0) {
            int nextIdx = -1;
            if (g_Player.GetRepeatMode() == Serenade::RepeatMode::One) {
                nextIdx = curIdx;
            } else if (g_Player.GetShuffle()) {
                nextIdx = -1; // unpredictable
            } else {
                nextIdx = curIdx + 1;
                if (nextIdx >= plSize) {
                    nextIdx = (g_Player.GetRepeatMode() == Serenade::RepeatMode::All) ? 0 : -1;
                }
            }
            if (nextIdx >= 0 && nextIdx < (int)playlist.size()) {
                int libIdx = playlist[nextIdx];
                if (libIdx >= 0 && libIdx < (int)library.size())
                    nextSong = &library[libIdx];
            }
        }

        // Track counter + Up Next on same line
        char trackBuf[32];
        if (plSize > 0 && curIdx >= 0)
            snprintf(trackBuf, sizeof(trackBuf), "%d / %d", curIdx + 1, plSize);
        else
            snprintf(trackBuf, sizeof(trackBuf), "%d tracks", plSize);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.38f, 0.42f, 1.0f));

        if (nextSong) {
            char upNextBuf[256];
            snprintf(upNextBuf, sizeof(upNextBuf), "Up Next: %s", nextSong->title.c_str());
            float upNextW = ImGui::CalcTextSize(upNextBuf).x;
            float trackW  = ImGui::CalcTextSize(trackBuf).x;
            ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("%s", trackBuf);
            ImGui::SameLine();
            float rightEdge = ImGui::GetWindowContentRegionMax().x;
            ImGui::SetCursorPosX(rightEdge - upNextW);
            ImGui::Text("%s", upNextBuf);
        } else if (g_Player.GetShuffle() && plSize > 1) {
            ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("%s", trackBuf);
            ImGui::SameLine();
            float shuffleW = ImGui::CalcTextSize("Up Next: Shuffle").x;
            float rightEdge = ImGui::GetWindowContentRegionMax().x;
            ImGui::SetCursorPosX(rightEdge - shuffleW);
            ImGui::Text("Up Next: Shuffle");
        } else {
            float trackW = ImGui::CalcTextSize(trackBuf).x;
            ImGui::SetCursorPosX((availW - trackW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
            ImGui::Text("%s", trackBuf);
        }

        ImGui::PopStyleColor();
    }

    ImGui::End();
    PopGW2Theme();

    // Render playlist editor (separate window)
    g_PlaylistEditor.Render(g_Player);

}

// --- Nexus Options Panel ---
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

    // Instrument info
    const auto& inst = g_Player.GetInstrument();
    ImGui::Text("Instrument: %s", inst.name.c_str());
    ImGui::Text("  Octaves: %d | Chords: %s | Min note delay: %dms",
                inst.octaveCount,
                inst.supportsChords ? "Yes" : "No",
                inst.minNoteDelayMs);

    ImGui::Spacing();

    // BPM override
    int bpmOverride = g_Player.GetBPMOverride();
    if (ImGui::SliderInt("BPM Override", &bpmOverride, 0, 300, bpmOverride == 0 ? "Auto" : "%d")) {
        g_Player.SetBPMOverride(bpmOverride);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = use song's default BPM");

    ImGui::Spacing();

    // Chat announcement
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
        }
    }

    ImGui::Spacing();

    // Music directory
    ImGui::Text("Music directory:");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextWrapped("%s", g_SongsDirectory.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Place .ahk (recommended) or .txt files in music/");

    if (ImGui::Button("Refresh Song Library")) {
        RefreshSongLibrary();
    }

    ImGui::Text("Library: %d songs | Playlist: %d tracks",
                (int)g_Player.GetLibrarySize(), (int)g_Player.GetPlaylistSize());

    ImGui::Spacing();

    // --- Keybindings section ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Piano Keybindings");
    ImGui::TextDisabled("Match these to your GW2 keybinds. Click a button then press a key to rebind.");
    ImGui::Spacing();

    {
        Serenade::KeyConfig config = g_Player.GetKeyConfig();
        bool changed = false;

        // Total 15 slots: 8 notes + 5 sharps + octave up + octave down
        const char* labels[15] = {
            "C  (1)",  "D  (2)",  "E  (3)",  "F  (4)",
            "G  (5)",  "A  (6)",  "B  (7)",  "C' (8)",
            "C# (F1)", "D# (F2)", "F# (F3)", "G# (F4)", "A# (F5)",
            "Octave Up (0)", "Octave Down (9)"
        };

        // Natural notes header
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
                if (ImGui::Button(name.c_str(), ImVec2(120, 0))) {
                    g_RebindingSlot = i;
                }
            }
            ImGui::PopID();
        }

        // Octave controls (between naturals and sharps)
        ImGui::Spacing();
        ImGui::TextDisabled("Octave Controls");
        // Render Octave Down first, then Octave Up (down on top, up below)
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
                if (ImGui::Button(name.c_str(), ImVec2(120, 0))) {
                    g_RebindingSlot = slot;
                }
            }
            ImGui::PopID();
        }

        // Sharps/Flats header
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
                if (ImGui::Button(name.c_str(), ImVec2(120, 0))) {
                    g_RebindingSlot = slot;
                }
            }
            ImGui::PopID();
        }

        if (changed) {
            g_Player.SetKeyConfig(config);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }

        // Reset to defaults button
        ImGui::Spacing();
        if (ImGui::Button("Reset to Defaults")) {
            Serenade::KeyConfig defaults;
            g_Player.SetKeyConfig(defaults);
            if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);
        }
    }

}

// Export function
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature = 0x1ffb30f0;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "Serenade";
    AddonDef.Version.Major = V_MAJOR;
    AddonDef.Version.Minor = V_MINOR;
    AddonDef.Version.Build = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author = "PieOrCake.7635";
    AddonDef.Description = "GW2 in-game music player - automates instrument playback";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = AF_None;
    AddonDef.Provider = UP_GitHub;
    AddonDef.UpdateLink = "https://github.com/PieOrCake/serenade";

    return &AddonDef;
}
