// ggui settings (settings.json) and imgui.ini, both in SDL_GetPrefPath("gg","ggui")
// (GGUI_PREF_PATH overrides the directory; tests use it for isolation). All file I/O runs on
// a background thread; the UI thread only applies results (§3.1).
#pragma once

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ggui {

// Runs small jobs off the UI thread; completions run on the UI thread in pump().
class AsyncIo {
public:
    AsyncIo();
    ~AsyncIo();
    AsyncIo(const AsyncIo&) = delete;
    AsyncIo& operator=(const AsyncIo&) = delete;

    // `work` runs on the I/O thread; its returned continuation runs on the UI thread.
    void post(std::function<std::function<void()>()> work);
    void pump();
    bool idle() const;
    // Blocks until all queued work is done (shutdown only).
    void flush();

private:
    void loop();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<std::function<void()>()>> m_jobs;
    std::deque<std::function<void()>> m_done;
    int m_running = 0;
    bool m_stop = false;
    std::thread m_thread;
};

enum class Theme { Dark, Light };
enum class NothingStaged { Ask, StageAll, StageSelected };
enum class RecentOrder { MostRecent, Alphabetical }; // order of the Recent lists (storage stays by recency)

struct SettingsData {
    float uiScale = 1.0f;          // 0.5 – 3.0
    Theme theme = Theme::Dark;
    std::vector<std::string> recent; // most recent first, unique (normalised paths)
    RecentOrder recentOrder = RecentOrder::MostRecent;
    std::map<std::string, bool> panels; // window name → visible
    NothingStaged nothingStaged = NothingStaged::Ask;
    bool expandConflictStages = false; // §4.10: index stages 1–3 for two-sided first-class conflicts on checkout
    bool expandStagesOnCheckout = false;
    // Main window placement (imgui.ini [GGUIWindow])
    int windowX = -1, windowY = -1, windowW = 0, windowH = 0;
    bool windowMaximized = false;
    // View state (imgui.ini [GGUIView]; kViewSettings in Settings.cpp lists what is stored)
    bool historyShowStashes = true;
    bool historyConflictedOnly = false;
    bool diffSideBySide = false;
    int diffContext = 3;
    int diffWhitespace = 0;
    bool infoWrapMessage = false; // Change information: word-wrap the message fields
    bool rebaseNewestFirst = false; // interactive rebase editor: list shown newest commit first
};

class Settings {
public:
    explicit Settings(AsyncIo& io);

    // Directory for settings.json and imgui.ini.
    static std::filesystem::path prefDir();

    // Starts loading from prefDir(); `onLoaded` runs on the UI thread with the ini text.
    void load(std::function<void(const std::string& iniText)> onLoaded);
    bool loaded() const { return m_loaded; }
    // Schedules an asynchronous save of settings.json.
    void save();
    // Schedules an asynchronous save of the given imgui.ini text.
    void saveIni(std::string text);

    // Registers the imgui.ini handler ("GGUIView") for the view state in SettingsData; the data
    // must outlive the ImGui context (the App owns both).
    void registerIniHandler();
    // A view-state field changed: imgui.ini is written soon (settings.json is not involved).
    static void markViewDirty();

    SettingsData& data() { return m_data; }
    const SettingsData& data() const { return m_data; }

    void addRecent(const std::string& path);
    void forgetRecent(const std::string& path);

private:
    AsyncIo& m_io;
    SettingsData m_data;
    bool m_loaded = false;
    std::filesystem::path m_dir;
};

nlohmann::json toJson(const SettingsData& d);
SettingsData fromJson(const nlohmann::json& j);

// A repository path as stored in Recent: absolute, symlinks resolved where the path exists, no
// trailing separator ("/a/b/" and "/a/b" are the same entry).
std::string normalizeRepoPath(const std::string& path);
// `paths` without repeated repositories (after normalisation), first occurrence kept.
std::vector<std::string> uniqueRepoPaths(const std::vector<std::string>& paths);
// Indices into `paths` in display order: as stored, or alphabetical by the unique display name
// (case-insensitive; base name first, then the parent prefix).
std::vector<size_t> recentDisplayOrder(const std::vector<std::string>& paths, RecentOrder order);

// Short display name of a recent repository: `base` is the last path component; `prefix` holds
// the parent folders ("work/") added to tell it apart from other entries with the same base.
struct RecentName {
    std::string prefix;
    std::string base;
    std::string text() const { return prefix + base; }
};
// One name per path (same order). Entries whose base names collide all gain one parent folder at
// a time until every name is unique; the others keep their base name. A path that runs out of
// parents is shown in full (identical paths stay identical).
std::vector<RecentName> uniqueRecentNames(const std::vector<std::string>& paths);

} // namespace ggui
