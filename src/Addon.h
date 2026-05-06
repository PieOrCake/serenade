#pragma once
#include <string>
#include <imgui.h>
#include "nexus/Nexus.h"
#include "MusicPlayer.h"
#include "PlaylistEditor.h"

// Globals defined in dllmain.cpp, shared with UI files
extern AddonAPI_t* APIDefs;
extern bool        g_PlayerWindowVisible;
extern Serenade::MusicPlayer    g_Player;
extern Serenade::PlaylistEditor g_PlaylistEditor;
extern std::string g_SongsDirectory;
extern std::string g_KeyConfigPath;
extern std::string g_PlaylistPath;
extern ImFont*     g_TitleFont;

void RefreshSongLibrary();
void ApplyQASetting(bool enabled);
