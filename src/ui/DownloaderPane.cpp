#include "PlaylistEditor.h"
#include <imgui.h>
#include <wininet.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <thread>
#include "nlohmann_json.hpp"

namespace Serenade {

static const char* kGitHubRawBase =
    "https://raw.githubusercontent.com/PieOrCake/serenade/main/music/";

static std::string HttpGet(const std::string& url) {
    std::string result;
    HINTERNET hInternet = InternetOpenA("Serenade/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                        NULL, NULL, 0);
    if (!hInternet) return result;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return result; }

    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        result.append(buffer, bytesRead);

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return result;
}

static bool HttpDownloadFile(const std::string& url, const std::string& destPath) {
    std::string data = HttpGet(url);
    if (data.empty()) return false;
    std::ofstream file(destPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(data.data(), data.size());
    return file.good();
}

void PlaylistEditor::FetchOnlineSongs() {
    // Bail if already fetching
    if (m_OnlineFetching.exchange(true)) return;

    m_OnlineError.clear();

    std::thread([this]() {
        std::vector<OnlineSong> songs;
        std::string error;

        std::string json = HttpGet(std::string(kGitHubRawBase) + "index.json");

        if (json.empty()) {
            error = "Failed to connect to GitHub. Check your internet connection.";
        } else {
            try {
                auto parsed = nlohmann::json::parse(json);
                if (!parsed.is_array()) {
                    error = "Unexpected index.json format.";
                } else {
                    for (const auto& item : parsed) {
                        if (!item.is_object()) continue;
                        std::string file = item.value("file", "");
                        if (file.empty()) continue;

                        OnlineSong song;
                        song.name        = file;
                        song.title       = item.value("title",      "");
                        song.author      = item.value("author",     "");
                        song.instrument  = item.value("instrument", "");
                        song.part        = item.value("part",       "");
                        song.downloadUrl = std::string(kGitHubRawBase) + file;
                        song.size        = item.value("size", 0);
                        songs.push_back(song);
                    }

                    std::sort(songs.begin(), songs.end(),
                        [](const OnlineSong& a, const OnlineSong& b) {
                            const std::string& ak = a.title.empty() ? a.name : a.title;
                            const std::string& bk = b.title.empty() ? b.name : b.title;
                            return ak < bk;
                        });
                }
            } catch (const std::exception& e) {
                error = std::string("JSON parse error: ") + e.what();
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_OnlineMutex);
            m_OnlineSongs   = std::move(songs);
            m_OnlineError   = error;
            m_OnlineFetched = error.empty();

            // Mark which songs already exist locally
            for (auto& s : m_OnlineSongs)
                s.downloaded = std::filesystem::exists(m_SongsDirectory + "/" + s.name);
        }

        m_OnlineFetching.store(false);
    }).detach();
}

void PlaylistEditor::DownloadSong(int index) {
    std::string url, destPath;
    {
        std::lock_guard<std::mutex> lock(m_OnlineMutex);
        if (index < 0 || index >= (int)m_OnlineSongs.size()) return;
        if (m_SongsDirectory.empty()) return;
        auto& song = m_OnlineSongs[index];
        if (song.downloadUrl.empty() || song.downloaded || song.downloading) return;
        song.downloading  = true;
        song.downloadError.clear();
        url      = song.downloadUrl;
        destPath = m_SongsDirectory + "/" + song.name;
    }

    std::thread([this, index, url, destPath]() {
        bool ok = HttpDownloadFile(url, destPath);

        {
            std::lock_guard<std::mutex> lock(m_OnlineMutex);
            auto& song       = m_OnlineSongs[index];
            song.downloading = false;
            song.downloaded  = ok;
            if (!ok) song.downloadError = "Download failed — check your connection";
        }

        if (ok) m_PendingRefresh.store(true);
    }).detach();
}

void PlaylistEditor::UpdateLocalStatus() {
    std::lock_guard<std::mutex> lock(m_OnlineMutex);
    for (auto& song : m_OnlineSongs)
        song.downloaded = std::filesystem::exists(m_SongsDirectory + "/" + song.name);
}

void PlaylistEditor::RenderOnlinePane(MusicPlayer& player) {
    ImGui::Checkbox("Show music I already have", &m_ShowDownloaded);
    ImGui::Separator();

    if (ImGui::Button("Refresh List")) {
        m_OnlineFetched = false;
        FetchOnlineSongs();
    }
    ImGui::SameLine();

    // Build "Download All New" state and song count under a brief lock, render after
    std::vector<int> toDownloadAll;
    int totalOnline = 0;
    {
        std::lock_guard<std::mutex> lock(m_OnlineMutex);
        totalOnline = (int)m_OnlineSongs.size();
        for (int i = 0; i < (int)m_OnlineSongs.size(); i++)
            if (!m_OnlineSongs[i].downloaded && !m_OnlineSongs[i].downloading)
                toDownloadAll.push_back(i);
    }

    if (totalOnline > 0) {
        char label[64];
        snprintf(label, sizeof(label), "Download All New (%d)", (int)toDownloadAll.size());
        if (toDownloadAll.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::Button(label);
            ImGui::PopStyleVar();
        } else {
            if (ImGui::Button(label))
                for (int i : toDownloadAll) DownloadSong(i); // each call acquires its own lock
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d songs available)", totalOnline);
    }

    if (m_OnlineFetching.load()) {
        ImGui::TextDisabled("Fetching song list from GitHub...");
        return;
    }
    if (!m_OnlineError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", m_OnlineError.c_str());
        ImGui::PopStyleColor();
        return;
    }

    // Take a snapshot of the data under lock to avoid holding it during ImGui rendering
    struct SongView {
        std::string name, title, author, instrument;
        int size;
        bool downloaded, downloading;
        std::string downloadError;
    };

    std::vector<SongView> snapshot;
    std::map<std::string, int> instCounts;
    std::vector<std::string> instruments;
    {
        std::lock_guard<std::mutex> lock(m_OnlineMutex);
        if (m_OnlineSongs.empty()) {
            ImGui::TextDisabled("No songs available on GitHub yet.");
            return;
        }

        snapshot.reserve(m_OnlineSongs.size());
        for (const auto& s : m_OnlineSongs) {
            snapshot.push_back({s.name, s.title, s.author, s.instrument,
                                 s.size, s.downloaded, s.downloading, s.downloadError});
        }

        // Rebuild instrument counts if list size changed
        if (m_OnlineSongs.size() != m_CachedOnlineSize) {
            m_CachedOnlineSize = m_OnlineSongs.size();
            m_CachedOnlineInstCounts.clear();
            for (const auto& s : m_OnlineSongs) {
                std::string inst = s.instrument.empty() ? "Unknown" : s.instrument;
                m_CachedOnlineInstCounts[inst]++;
            }
            m_CachedOnlineInstruments.clear();
            for (const auto& pair : m_CachedOnlineInstCounts)
                m_CachedOnlineInstruments.push_back(pair.first);
            m_CachedOnlineInstTab = -1;
        }
        instCounts  = m_CachedOnlineInstCounts;
        instruments = m_CachedOnlineInstruments;
    }

    // Instrument tabs
    if (ImGui::BeginTabBar("##OnlineInstrumentTabs")) {
        char allLabel[64];
        snprintf(allLabel, sizeof(allLabel), "All (%d)", (int)snapshot.size());
        if (ImGui::BeginTabItem(allLabel)) {
            if (m_OnlineInstrumentTab != 0) { m_OnlineInstrumentTab = 0; m_OnlineArtistFilter = 0; }
            ImGui::EndTabItem();
        }
        for (int t = 0; t < (int)instruments.size(); t++) {
            char tabLabel[64];
            auto it  = instCounts.find(instruments[t]);
            int  cnt = (it != instCounts.end()) ? it->second : 0;
            snprintf(tabLabel, sizeof(tabLabel), "%s (%d)", instruments[t].c_str(), cnt);
            if (ImGui::BeginTabItem(tabLabel)) {
                if (m_OnlineInstrumentTab != t + 1) { m_OnlineInstrumentTab = t + 1; m_OnlineArtistFilter = 0; }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    std::string selectedInstrument;
    if (m_OnlineInstrumentTab > 0 && m_OnlineInstrumentTab <= (int)instruments.size())
        selectedInstrument = instruments[m_OnlineInstrumentTab - 1];

    bool onlineArtistListRebuilt = false;
    if (m_CachedOnlineInstTab != m_OnlineInstrumentTab) {
        m_CachedOnlineInstTab = m_OnlineInstrumentTab;
        onlineArtistListRebuilt = true;
        std::set<std::string> artistSet;
        for (const auto& s : snapshot) {
            if (!selectedInstrument.empty()) {
                std::string inst = s.instrument.empty() ? "Unknown" : s.instrument;
                if (inst != selectedInstrument) continue;
            }
            if (!s.author.empty()) artistSet.insert(s.author);
        }
        m_CachedOnlineArtists.assign(artistSet.begin(), artistSet.end());
    }
    const auto& artists = m_CachedOnlineArtists;

    ImGui::SetNextItemWidth(160);
    const char* artistLabel = m_OnlineArtistFilter == 0 ? "All Artists" :
        (m_OnlineArtistFilter <= (int)artists.size() ? artists[m_OnlineArtistFilter - 1].c_str() : "All Artists");
    if (ImGui::BeginCombo("##ArtistFilter", artistLabel)) {
        if (ImGui::Selectable("All Artists", m_OnlineArtistFilter == 0)) m_OnlineArtistFilter = 0;
        for (int a = 0; a < (int)artists.size(); a++) {
            bool sel = (m_OnlineArtistFilter == a + 1);
            if (ImGui::Selectable(artists[a].c_str(), sel)) m_OnlineArtistFilter = a + 1;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##OnlineFilter", "Search...", m_OnlineFilter, sizeof(m_OnlineFilter));

    std::string filterStr(m_OnlineFilter);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    std::string selectedArtist;
    if (m_OnlineArtistFilter > 0 && m_OnlineArtistFilter <= (int)artists.size())
        selectedArtist = artists[m_OnlineArtistFilter - 1];

    if (ImGui::BeginTable("##OnlineTable", 5,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Status",     ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 60.0f);
        ImGui::TableSetupColumn("Title",      ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Artist",     ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Instrument", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 70.0f);
        ImGui::TableSetupColumn("Size",       ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableHeadersRow();

        int sortCol = -1, sortDir = 0;
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsCount > 0) {
                sortCol = specs->Specs[0].ColumnIndex;
                sortDir = specs->Specs[0].SortDirection;
            }
        }

        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };

        bool onlineDirty = (filterStr             != m_CachedOnlineFilter)
                        || (m_OnlineArtistFilter  != m_CachedOnlineArtistIdx)
                        || onlineArtistListRebuilt
                        || (m_ShowDownloaded      != m_CachedShowDownloaded)
                        || (snapshot.size()       != m_CachedOnlineSize)
                        || (sortCol != m_CachedOnlineSortCol)
                        || (sortDir != m_CachedOnlineSortDir);
        if (onlineDirty) {
            m_CachedOnlineFilter    = filterStr;
            m_CachedOnlineArtistIdx = m_OnlineArtistFilter;
            m_CachedShowDownloaded  = m_ShowDownloaded;
            m_CachedOnlineSortCol   = sortCol;
            m_CachedOnlineSortDir   = sortDir;

            m_CachedOnlineFiltered.clear();
            for (int i = 0; i < (int)snapshot.size(); i++) {
                const auto& s = snapshot[i];
                if (!m_ShowDownloaded && s.downloaded) continue;
                if (!selectedInstrument.empty()) {
                    std::string inst = s.instrument.empty() ? "Unknown" : s.instrument;
                    if (inst != selectedInstrument) continue;
                }
                if (!selectedArtist.empty() && s.author != selectedArtist) continue;
                if (!filterStr.empty()) {
                    bool match = toLower(s.name).find(filterStr)       != std::string::npos
                              || toLower(s.title).find(filterStr)      != std::string::npos
                              || toLower(s.author).find(filterStr)     != std::string::npos
                              || toLower(s.instrument).find(filterStr) != std::string::npos;
                    if (!match) continue;
                }
                m_CachedOnlineFiltered.push_back(i);
            }

            if (sortCol >= 0) {
                bool asc = (sortDir == ImGuiSortDirection_Ascending);
                std::sort(m_CachedOnlineFiltered.begin(), m_CachedOnlineFiltered.end(),
                    [&](int a, int b) {
                        int cmp = 0;
                        if (sortCol == 1) {
                            auto dispName = [&](const SongView& s) {
                                if (!s.title.empty()) return s.title;
                                std::string n = s.name;
                                if (n.size() > 4 && n.substr(n.size() - 4) == ".ahk")
                                    n = n.substr(0, n.size() - 4);
                                return n;
                            };
                            cmp = _stricmp(dispName(snapshot[a]).c_str(), dispName(snapshot[b]).c_str());
                        } else if (sortCol == 2) {
                            cmp = _stricmp(snapshot[a].author.c_str(), snapshot[b].author.c_str());
                        } else if (sortCol == 4) {
                            cmp = (snapshot[a].size < snapshot[b].size) ? -1 :
                                  (snapshot[a].size > snapshot[b].size) ? 1 : 0;
                        }
                        return asc ? (cmp < 0) : (cmp > 0);
                    });
            }
        }

        for (int i : m_CachedOnlineFiltered) {
            const auto& s = snapshot[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap);
            ImGui::SameLine();

            if (s.downloaded) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                ImGui::Text("Downloaded");
                ImGui::PopStyleColor();
            } else if (s.downloading) {
                ImGui::TextDisabled("...");
            } else if (!s.downloadError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                ImGui::Text("Failed");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", s.downloadError.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Retry")) DownloadSong(i);
            } else {
                if (ImGui::SmallButton("Download")) DownloadSong(i);
            }

            ImGui::TableSetColumnIndex(1);
            std::string displayName = s.title;
            if (displayName.empty()) {
                displayName = s.name;
                if (displayName.size() > 4 && displayName.substr(displayName.size() - 4) == ".ahk")
                    displayName = displayName.substr(0, displayName.size() - 4);
            }
            ImGui::Text("%s", displayName.c_str());

            ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%s", s.author.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("%s", s.instrument.c_str());
            ImGui::TableSetColumnIndex(4);
            if (s.size > 0) {
                if      (s.size >= 1024*1024) ImGui::TextDisabled("%.1f MB", s.size / (1024.0f*1024.0f));
                else if (s.size >= 1024)      ImGui::TextDisabled("%.1f KB", s.size / 1024.0f);
                else                          ImGui::TextDisabled("%d B",    s.size);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace Serenade
