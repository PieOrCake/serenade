#include "PieTheme.h"
#include "../Addon.h"
#include <mutex>

// Nexus named events (contract with Pie UI)
static const char* EV_PIEUI_THEME         = "EV_PIEUI_THEME";
static const char* EV_PIEUI_REQUEST_THEME = "EV_PIEUI_REQUEST_THEME";

namespace {
    std::mutex g_Mutex;
    PieUiTheme g_Palette{};
    bool       g_Has = false;

    // Raised by Pie with a pointer to a PieUiTheme. Delivery is synchronous and
    // may be off the render thread: copy immediately, never store the pointer.
    void OnPieTheme(void* aEventArgs) {
        if (!aEventArgs) return;
        const PieUiTheme* t = (const PieUiTheme*)aEventArgs;
        if (t->version != PIEUI_THEME_VERSION) return;   // unknown schema — ignore

        std::lock_guard<std::mutex> lk(g_Mutex);
        g_Palette = *t;
        g_Has = true;
    }
}

namespace PieTheme {

void Init() {
    if (!APIDefs) return;
    APIDefs->Events_Subscribe(EV_PIEUI_THEME, OnPieTheme);
    // Ask Pie to (re-)broadcast, in case it loaded before us. No-op if absent.
    APIDefs->Events_Raise(EV_PIEUI_REQUEST_THEME, nullptr);
}

void Shutdown() {
    if (!APIDefs) return;
    APIDefs->Events_Unsubscribe(EV_PIEUI_THEME, OnPieTheme);
}

bool HasPalette() {
    std::lock_guard<std::mutex> lk(g_Mutex);
    return g_Has;
}

void ApplyToStyle(ImGuiStyle& style) {
    std::lock_guard<std::mutex> lk(g_Mutex);
    if (!g_Has) return;
    int n = (int)g_Palette.count;
    if (n > ImGuiCol_COUNT)        n = ImGuiCol_COUNT;
    if (n > PIEUI_THEME_MAX_COLORS) n = PIEUI_THEME_MAX_COLORS;
    for (int i = 0; i < n; ++i)
        style.Colors[i] = ImGui::ColorConvertU32ToFloat4(g_Palette.colors[i]);
}

bool Active() {
    return g_Player.GetUsePieTheme() && HasPalette();
}

ImU32 Accent() {
    std::lock_guard<std::mutex> lk(g_Mutex);
    return g_Palette.accent;   // packed 0xAABBGGRR — matches IM_COL32 layout
}

ImU32 AccentScaled(float mul) {
    ImU32 c;
    { std::lock_guard<std::mutex> lk(g_Mutex); c = g_Palette.accent; }
    int r = (c >>  0) & 0xFF;
    int g = (c >>  8) & 0xFF;
    int b = (c >> 16) & 0xFF;
    int a = (c >> 24) & 0xFF;
    auto sc = [mul](int v) { int x = (int)(v * mul); return x < 0 ? 0 : (x > 255 ? 255 : x); };
    return IM_COL32(sc(r), sc(g), sc(b), a);
}

} // namespace PieTheme
