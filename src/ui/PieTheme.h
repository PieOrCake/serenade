#pragma once
#include <cstdint>
#include <imgui.h>

// ── Pie UI theme-broadcast consumer ────────────────────────────────────────
// Pie UI is an optional peer addon that broadcasts its active ImGui palette
// over the Nexus event bus. When present (and the user hasn't opted out),
// Serenade tints its windows to match. When absent, Serenade looks unchanged.

#define PIEUI_THEME_VERSION    1
#define PIEUI_THEME_MAX_COLORS 96      // fixed cap; struct stays a flat POD

typedef struct PieUiTheme {
    uint32_t version;                        // == PIEUI_THEME_VERSION; ignore if unknown
    uint32_t accent;                         // signature highlight (trim-aware); NOT an ImGuiCol
    uint32_t count;                          // valid entries in colors[]
    uint32_t colors[PIEUI_THEME_MAX_COLORS]; // IM_COL32, indexed by ImGuiCol_
} PieUiTheme;

namespace PieTheme {
    void Init();      // subscribe + request a broadcast (uses global APIDefs)
    void Shutdown();  // unsubscribe

    bool HasPalette();                       // a valid palette has been received

    // Overwrite style.Colors[] with Pie's palette (only the received prefix).
    // No-op if no palette has arrived. Leaves geometry/spacing untouched.
    void ApplyToStyle(ImGuiStyle& style);

    // True when Pie colours should be used right now (palette received AND the
    // "Use Pie UI theme" option is ON). Gates the custom-draw accent branches.
    bool Active();

    // Pie's signature accent, for custom-drawn highlights (valid if HasPalette).
    ImU32 Accent();
    // Accent with its RGB brightness scaled (alpha preserved, channels clamped).
    ImU32 AccentScaled(float mul);
}
