#include "Addon.h"
#include "GW2Theme.h"
#include <cstdio>
#include <cmath>

// --- Helper: format time as M:SS ---
static void FormatTime(float seconds, char* buf, size_t bufSize) {
    int total = (int)seconds;
    if (total < 0) total = 0;
    int min = total / 60;
    int sec = total % 60;
    snprintf(buf, bufSize, "%d:%02d", min, sec);
}

// --- Icon drawing helpers ---

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
    dl->AddRectFilled(ImVec2(cx - w - barW, cy - h), ImVec2(cx - w, cy + h), col);
    dl->AddTriangleFilled(
        ImVec2(cx + w, cy - h), ImVec2(cx + w, cy + h),
        ImVec2(cx - w + barW, cy), col);
}

static void DrawIconNext(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float h = s * 0.35f;
    float w = s * 0.28f;
    float barW = s * 0.08f;
    dl->AddTriangleFilled(
        ImVec2(cx - w, cy - h), ImVec2(cx - w, cy + h),
        ImVec2(cx + w - barW, cy), col);
    dl->AddRectFilled(ImVec2(cx + w, cy - h), ImVec2(cx + w + barW, cy + h), col);
}

static void DrawIconShuffle(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.32f;
    float h = s * 0.2f;
    float t = 1.4f;
    dl->AddLine(ImVec2(cx - w, cy - h), ImVec2(cx + w, cy + h), col, t);
    dl->AddLine(ImVec2(cx - w, cy + h), ImVec2(cx + w, cy - h), col, t);
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
    dl->AddLine(ImVec2(cx - w, cy - h), ImVec2(cx + w, cy - h), col, t);
    dl->AddLine(ImVec2(cx + w, cy - h), ImVec2(cx + w, cy + h), col, t);
    dl->AddLine(ImVec2(cx + w, cy + h), ImVec2(cx - w, cy + h), col, t);
    dl->AddLine(ImVec2(cx - w, cy + h), ImVec2(cx - w, cy - h), col, t);
    dl->AddTriangleFilled(
        ImVec2(cx + w + arrowS, cy - h),
        ImVec2(cx + w - arrowS * 0.5f, cy - h - arrowS),
        ImVec2(cx + w - arrowS * 0.5f, cy - h + arrowS), col);
}

static void DrawIconPlaylist(ImDrawList* dl, float cx, float cy, float s, ImU32 col) {
    float w = s * 0.3f;
    float t = 1.4f;
    float gap = s * 0.2f;
    dl->AddLine(ImVec2(cx - w, cy - gap), ImVec2(cx + w, cy - gap), col, t);
    dl->AddLine(ImVec2(cx - w, cy),       ImVec2(cx + w, cy),       col, t);
    dl->AddLine(ImVec2(cx - w, cy + gap), ImVec2(cx + w, cy + gap), col, t);
}

// --- Invisible button with icon overlay ---
typedef void (*IconDrawFn)(ImDrawList*, float, float, float, ImU32);

static bool IconButton(const char* id, float size, IconDrawFn drawFn, bool active, ImVec4 activeCol) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float btnH = ImGui::GetFrameHeight();

    ImGui::InvisibleButton(id, ImVec2(size, btnH));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    ImU32 bgCol;
    if (held) {
        bgCol = IM_COL32(90, 90, 100, 200);
    } else if (hovered) {
        if (active)
            bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(activeCol.x + 0.1f, activeCol.y + 0.1f, activeCol.z + 0.1f, 1.0f));
        else
            bgCol = IM_COL32(70, 70, 80, 200);
    } else if (active) {
        bgCol = ImGui::ColorConvertFloat4ToU32(activeCol);
    } else {
        bgCol = IM_COL32(46, 46, 56, 200);
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + btnH), bgCol, 6.0f);

    ImU32 iconCol;
    if (active)
        iconCol = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 230, 255);
    else
        iconCol = hovered ? IM_COL32(220, 220, 230, 255) : IM_COL32(160, 160, 170, 255);

    float cx = pos.x + size * 0.5f;
    float cy = pos.y + btnH * 0.5f;
    drawFn(dl, cx, cy, size, iconCol);

    return pressed;
}

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

// --- Main Player Window ---
void AddonRender() {
    static bool g_WasWindowVisible = false;
    if (!g_PlayerWindowVisible) {
        if (g_WasWindowVisible) {
            g_Player.Stop();
            g_WasWindowVisible = false;
        }
        g_PlaylistEditor.Render(g_Player);
        return;
    }
    g_WasWindowVisible = true;

    ThemeGuard themeGuard;

    bool fontPushed = g_NexusLink && g_NexusLink->FontUI;
    if (fontPushed) ImGui::PushFont((ImFont*)g_NexusLink->FontUI);

    ImGui::SetNextWindowSize(ImVec2(340, 235), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("Serenade", &g_PlayerWindowVisible, flags)) {
        ImGui::End();
        if (fontPushed) ImGui::PopFont();
        g_PlaylistEditor.Render(g_Player);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float availW = ImGui::GetContentRegionAvail().x;

    // Auto-save playlist when the current track changes
    {
        static int s_LastSavedTrack = -2;
        int curTrack = g_Player.GetCurrentTrackIndex();
        if (s_LastSavedTrack == -2) {
            s_LastSavedTrack = curTrack;
        } else if (curTrack != s_LastSavedTrack) {
            s_LastSavedTrack = curTrack;
            if (!g_PlaylistPath.empty())
                g_Player.SavePlaylist(g_PlaylistPath);
        }
    }

    // ── Row 1: Song title (large font, scrolling if too wide) ──
    ImFont* titleFont = (g_NexusLink && g_NexusLink->FontBig) ? (ImFont*)g_NexusLink->FontBig : nullptr;
    float normalLineH = ImGui::GetTextLineHeightWithSpacing();
    float scaledLineH;

    if (titleFont) {
        ImGui::PushFont(titleFont);
        scaledLineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::PopFont();
    } else {
        scaledLineH = normalLineH;
    }

    float infoHeight = scaledLineH + normalLineH;
    ImVec2 infoStart = ImGui::GetCursorPos();

    #define PUSH_TITLE_FONT() do { if (titleFont) ImGui::PushFont(titleFont); } while(0)
    #define POP_TITLE_FONT()  do { if (titleFont) ImGui::PopFont(); } while(0)

    const Serenade::Song* song = g_Player.GetCurrentSong();

    if (song) {
        PUSH_TITLE_FONT();
        float titleW = ImGui::CalcTextSize(song->title.c_str()).x;
        POP_TITLE_FONT();

        float padX = ImGui::GetStyle().WindowPadding.x;
        float regionW = availW;

        if (titleW <= regionW) {
            PUSH_TITLE_FONT();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::SetCursorPosX((availW - titleW) * 0.5f + padX);
            ImGui::Text("%s", song->title.c_str());
            ImGui::PopStyleColor();
            POP_TITLE_FONT();
        } else {
            static double s_ScrollTimer = 0.0;
            static std::string s_LastTitle;
            if (s_LastTitle != song->title) {
                s_LastTitle = song->title;
                s_ScrollTimer = 0.0;
            }
            s_ScrollTimer += ImGui::GetIO().DeltaTime;

            float overflow = titleW - regionW;
            float scrollSpeed = 40.0f;
            float pauseDur = 1.5f;
            float scrollDur = overflow / scrollSpeed;
            float cycleDur = pauseDur + scrollDur + pauseDur + scrollDur;
            float t = fmodf((float)s_ScrollTimer, cycleDur);

            float offsetX = 0.0f;
            if (t < pauseDur)
                offsetX = 0.0f;
            else if (t < pauseDur + scrollDur)
                offsetX = ((t - pauseDur) / scrollDur) * overflow;
            else if (t < pauseDur + scrollDur + pauseDur)
                offsetX = overflow;
            else
                offsetX = overflow - ((t - pauseDur - scrollDur - pauseDur) / scrollDur) * overflow;

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
        bool barActive  = ImGui::IsItemActive();

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

        dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                          IM_COL32(35, 35, 40, 200), rounding);

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
    ImVec4 greenActive(0.22f, 0.48f, 0.28f, 1.0f);
    ImVec4 blueActive(0.22f, 0.28f, 0.52f, 1.0f);

    float smBtn = 26.0f;
    float mdBtn = 30.0f;
    float playR = 22.0f;
    float playD = playR * 2.0f;
    float gap   = 6.0f;

    float rowW = smBtn + gap + mdBtn + gap + mdBtn + gap + playD + gap + mdBtn + gap + smBtn + gap + smBtn;
    float startX = (availW - rowW) * 0.5f + ImGui::GetStyle().WindowPadding.x;
    if (startX < ImGui::GetStyle().WindowPadding.x) startX = ImGui::GetStyle().WindowPadding.x;

    float smBtnH = ImGui::GetFrameHeight();
    float yOffset = (playD - smBtnH) * 0.5f;
    float baseY = ImGui::GetCursorPosY();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));

    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(baseY + yOffset);
    bool shuffleOn = g_Player.GetShuffle();
    if (IconButton("##shf", smBtn, DrawIconShuffle, shuffleOn, greenActive))
        g_Player.SetShuffle(!shuffleOn);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shuffle %s", shuffleOn ? "ON" : "OFF");
    ImGui::SameLine();

    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##prev", mdBtn, DrawIconPrev, false, greenActive))
        g_Player.Previous();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous");
    ImGui::SameLine();

    ImGui::SetCursorPosY(baseY + yOffset);
    bool stopPending = g_Player.IsStopAfterCurrentPending();
    if (IconButton("##stop", mdBtn, DrawIconStop, stopPending, greenActive))
        g_Player.StopAfterCurrent();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(stopPending ? "Stop after this song (queued)" : "Stop after this song");
    ImGui::SameLine();

    ImGui::SetCursorPosY(baseY);
    bool isPlaying = g_Player.IsPlaying();
    ImU32 playBgN = IM_COL32(190, 145, 50, 255);
    ImU32 playBgH = IM_COL32(220, 175, 70, 255);
    ImU32 playBgA = IM_COL32(160, 120, 40, 255);
    if (isPlaying) {
        if (CircleIconButton("##pp", playR, DrawIconPause, true, playBgN, playBgH, playBgA))
            g_Player.Pause();
    } else {
        if (CircleIconButton("##pp", playR, DrawIconPlay, false, playBgN, playBgH, playBgA))
            g_Player.Play();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "Pause" : "Play");
    ImGui::SameLine();

    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##next", mdBtn, DrawIconNext, false, greenActive))
        g_Player.Next();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next");
    ImGui::SameLine();

    ImGui::SetCursorPosY(baseY + yOffset);
    Serenade::RepeatMode repeatMode = g_Player.GetRepeatMode();
    bool repeatActive = (repeatMode != Serenade::RepeatMode::Off);
    if (IconButton("##rpt", smBtn, DrawIconRepeat, repeatActive, blueActive))
        g_Player.CycleRepeatMode();
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

    ImGui::SetCursorPosY(baseY + yOffset);
    if (IconButton("##plist", smBtn, DrawIconPlaylist, false, greenActive))
        g_PlaylistEditor.Toggle();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playlist Editor");

    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(baseY + playD + 4);

    // ── Row 4: Up Next ──
    {
        const Serenade::Song* nextSong = nullptr;
        int curIdx  = g_Player.GetCurrentTrackIndex();
        int plSize  = (int)g_Player.GetPlaylistSize();
        const auto& playlist = g_Player.GetPlaylist();
        const auto& library  = g_Player.GetSongLibrary();

        if (plSize > 0 && curIdx >= 0) {
            int nextIdx = -1;
            if (g_Player.GetRepeatMode() == Serenade::RepeatMode::One) {
                nextIdx = curIdx;
            } else if (g_Player.GetShuffle()) {
                nextIdx = -1;
            } else {
                nextIdx = curIdx + 1;
                if (nextIdx >= plSize)
                    nextIdx = (g_Player.GetRepeatMode() == Serenade::RepeatMode::All) ? 0 : -1;
            }
            if (nextIdx >= 0 && nextIdx < (int)playlist.size()) {
                int libIdx = playlist[nextIdx];
                if (libIdx >= 0 && libIdx < (int)library.size())
                    nextSong = &library[libIdx];
            }
        }

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
    if (fontPushed) ImGui::PopFont();

    g_PlaylistEditor.Render(g_Player);
}
