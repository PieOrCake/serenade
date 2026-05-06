#include <windows.h>
#include <imgui.h>
#include <string>
#include <filesystem>
#include <timeapi.h>
#include "nexus/Nexus.h"
#include "MusicPlayer.h"
#include "SongParser.h"
#include "PlaylistEditor.h"
#include "Addon.h"
#include "ui/GW2Theme.h"
#include "icons.h"

#define V_MAJOR    0
#define V_MINOR    9
#define V_BUILD    4
#define V_REVISION 0

#define QA_ID           "QA_SERENADE"
#define TEX_ICON        "TEX_SERENADE_ICON"
#define TEX_ICON_HOVER  "TEX_SERENADE_ICON_HOVER"

// --- Globals (declared extern in Addon.h) ---
HMODULE          hSelf;
AddonDefinition_t AddonDef{};
AddonAPI_t*      APIDefs            = nullptr;
bool             g_PlayerWindowVisible = false;
ImFont*          g_TitleFont         = nullptr;

Serenade::MusicPlayer    g_Player;
Serenade::PlaylistEditor g_PlaylistEditor;
std::string g_SongsDirectory;
std::string g_KeyConfigPath;
std::string g_PlaylistPath;

static bool g_SavedPlayerVisible  = true;
static bool g_SavedPlaylistVisible = false;
static bool g_SavedDownloadVisible = false;

// Forward declarations of functions defined in ui/ files
void AddonRender();
void AddonOptions();
void ProcessKeybind(const char* aIdentifier, bool aIsRelease);

void ApplyQASetting(bool enabled) {
    if (enabled)
        APIDefs->QuickAccess_Add(QA_ID, TEX_ICON, TEX_ICON_HOVER, "KB_SERENADE_TOGGLE", "Serenade");
    else
        APIDefs->QuickAccess_Remove(QA_ID);
}

static HWND FindGW2Window() {
    HWND hwnd = FindWindowA("ArenaNet_Dx_Window_Class", nullptr);
    if (!hwnd) hwnd = FindWindowA("ArenaNet_Gr_Window_Class", nullptr);
    return hwnd;
}

void RefreshSongLibrary() {
    auto songs = Serenade::ScanMusicDirectory(g_SongsDirectory);
    g_Player.SetSongLibrary(std::move(songs));
    if (APIDefs)
        APIDefs->Log(LOGL_INFO, "Serenade",
            (std::string("Loaded ") + std::to_string(g_Player.GetLibrarySize()) + " songs").c_str());
}

static void EnsureMusicDirs() {
    try { std::filesystem::create_directories(g_SongsDirectory); } catch (...) {}
}

static void OnTitleFontReceived(const char* aIdentifier, void* aFont) {
    g_TitleFont = (ImFont*)aFont;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: hSelf = hModule; break;
    default: break;
    }
    return TRUE;
}

void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc,
                                 (void(*)(void*, void*))APIDefs->ImguiFree);

    BuildGW2Theme();
    timeBeginPeriod(1);

    const char* addonDir = APIDefs->Paths_GetAddonDirectory("Serenade");
    if (addonDir) {
        g_SongsDirectory = std::string(addonDir) + "/music";
        g_KeyConfigPath  = std::string(addonDir) + "/keybinds.cfg";
        g_Player.SetDebugLogPath(std::string(addonDir) + "/debug.log");
    }

    if (!g_KeyConfigPath.empty()) g_Player.LoadKeyConfig(g_KeyConfigPath);
    EnsureMusicDirs();
    RefreshSongLibrary();

    g_PlaylistEditor.SetRefreshCallback(RefreshSongLibrary);
    g_PlaylistEditor.SetSongsDirectory(g_SongsDirectory);

    if (addonDir) {
        g_PlaylistPath = std::string(addonDir) + "/playlist.txt";
        g_Player.LoadPlaylist(g_PlaylistPath);
    }

    HWND hwnd = FindGW2Window();
    if (hwnd) {
        g_Player.SetGameWindow(hwnd);
        APIDefs->Log(LOGL_INFO, "Serenade", "GW2 window found");
    } else {
        APIDefs->Log(LOGL_WARNING, "Serenade", "GW2 window not found - playback will not send keys");
    }

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    APIDefs->InputBinds_RegisterWithString("KB_SERENADE_TOGGLE",      ProcessKeybind, "CTRL+SHIFT+M");
    APIDefs->InputBinds_RegisterWithString("KB_SERENADE_PLAY_PAUSE",  ProcessKeybind, "");
    APIDefs->InputBinds_RegisterWithString("KB_SERENADE_NEXT",        ProcessKeybind, "");
    APIDefs->InputBinds_RegisterWithString("KB_SERENADE_PREV",        ProcessKeybind, "");

    APIDefs->Textures_LoadFromMemory(TEX_ICON,       (void*)ICON_NOTE,       ICON_NOTE_size,       nullptr);
    APIDefs->Textures_LoadFromMemory(TEX_ICON_HOVER, (void*)ICON_NOTE_HOVER, ICON_NOTE_HOVER_size, nullptr);
    ApplyQASetting(g_Player.GetQAEnabled());

    APIDefs->GUI_RegisterCloseOnEscape("Serenade", &g_PlayerWindowVisible);
    APIDefs->GUI_RegisterCloseOnEscape("Serenade - Playlist Editor", g_PlaylistEditor.GetVisiblePtr());
    APIDefs->GUI_RegisterCloseOnEscape("Download Music", g_PlaylistEditor.GetDownloadWindowVisiblePtr());

    APIDefs->Fonts_AddFromFile("SERENADE_TITLE", ImGui::GetFontSize() * 2.0f,
        "C:\\Windows\\Fonts\\segoeui.ttf", OnTitleFontReceived, nullptr);

    APIDefs->Log(LOGL_INFO, "Serenade", "Addon loaded successfully");
}

void AddonUnload() {
    timeEndPeriod(1);
    g_Player.Stop();

    if (!g_PlaylistPath.empty()) g_Player.SavePlaylist(g_PlaylistPath);

    APIDefs->GUI_DeregisterCloseOnEscape("Serenade");
    APIDefs->GUI_DeregisterCloseOnEscape("Serenade - Playlist Editor");
    APIDefs->GUI_DeregisterCloseOnEscape("Download Music");

    if (!g_KeyConfigPath.empty()) g_Player.SaveKeyConfig(g_KeyConfigPath);

    APIDefs->InputBinds_Deregister("KB_SERENADE_TOGGLE");
    APIDefs->InputBinds_Deregister("KB_SERENADE_PLAY_PAUSE");
    APIDefs->InputBinds_Deregister("KB_SERENADE_NEXT");
    APIDefs->InputBinds_Deregister("KB_SERENADE_PREV");
    APIDefs->QuickAccess_Remove(QA_ID);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    APIDefs->Fonts_Release("SERENADE_TITLE", OnTitleFontReceived);
    g_TitleFont = nullptr;

    APIDefs = nullptr;
}

void ProcessKeybind(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_SERENADE_PLAY_PAUSE") == 0) {
        if (!g_Player.GetPlaylist().empty()) {
            if (g_Player.IsPlaying()) g_Player.Pause();
            else                      g_Player.Play();
        }
        return;
    }

    if (strcmp(aIdentifier, "KB_SERENADE_NEXT") == 0) {
        if (!g_Player.GetPlaylist().empty()) g_Player.Next();
        return;
    }

    if (strcmp(aIdentifier, "KB_SERENADE_PREV") == 0) {
        if (!g_Player.GetPlaylist().empty()) g_Player.Previous();
        return;
    }

    if (strcmp(aIdentifier, "KB_SERENADE_TOGGLE") == 0) {
        bool anyVisible = g_PlayerWindowVisible ||
                          g_PlaylistEditor.IsVisible() ||
                          *g_PlaylistEditor.GetDownloadWindowVisiblePtr();
        if (anyVisible) {
            g_SavedPlayerVisible   = g_PlayerWindowVisible;
            g_SavedPlaylistVisible = g_PlaylistEditor.IsVisible();
            g_SavedDownloadVisible = *g_PlaylistEditor.GetDownloadWindowVisiblePtr();
            g_PlayerWindowVisible  = false;
            g_PlaylistEditor.Hide();
            *g_PlaylistEditor.GetDownloadWindowVisiblePtr() = false;
        } else {
            g_PlayerWindowVisible = true;
            if (g_SavedPlaylistVisible) g_PlaylistEditor.Show();
            *g_PlaylistEditor.GetDownloadWindowVisiblePtr() = g_SavedDownloadVisible;
        }
    }
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature   = 0x1ffb30f0;
    AddonDef.APIVersion  = NEXUS_API_VERSION;
    AddonDef.Name        = "Serenade";
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author      = "PieOrCake.7635";
    AddonDef.Description = "Music player - automate instrument playback";
    AddonDef.Load        = AddonLoad;
    AddonDef.Unload      = AddonUnload;
    AddonDef.Flags       = AF_None;
    AddonDef.Provider    = UP_GitHub;
    AddonDef.UpdateLink  = "https://github.com/PieOrCake/serenade";
    return &AddonDef;
}
