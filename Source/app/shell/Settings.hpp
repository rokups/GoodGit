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
#include <set>
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

// A permanent repository entry: a normalised path and an optional alias ("work/app", see normalizeAlias).
struct RepoEntry {
    std::string path;
    std::string alias; // empty: the entry has no alias
};

// The state of one repository (settings.json "repoState", keyed by repoStateKey). Some parts are per worktree.
struct RepoState {
    // Legacy default of the hidden refs: the list of a worktree that has no entry in worktreeHidden.
    // Older versions stored the only list here. It is read, kept unchanged in the file and never written again.
    std::vector<std::string> hiddenRefs; // full ref names hidden in History; sorted, unique
    // Hidden refs of each worktree, keyed by the worktree id ("main" or WorktreeInfo::name). Each list is sorted
    // and unique. An entry with an empty list means "show all": it hides nothing, even if hiddenRefs is not empty.
    std::map<std::string, std::vector<std::string>> worktreeHidden;
    // Custom display names of worktrees, keyed by the worktree id (WorktreeInfo::name); no empty name or key.
    std::map<std::string, std::string> worktreeNames;
    bool empty() const { return hiddenRefs.empty() && worktreeHidden.empty() && worktreeNames.empty(); }
};

// The UI scale in percent: the limits of the Settings slider, of Ctrl+wheel and of a stored value.
constexpr int kUiScaleMinPercent = 50;
constexpr int kUiScaleMaxPercent = 300;

struct SettingsData {
    float uiScale = 1.0f;          // 1.0 = 100 %; kUiScaleMinPercent – kUiScaleMaxPercent as a fraction (0.5 – 3.0)
    Theme theme = Theme::Dark;
    std::vector<std::string> recent; // most recent first, unique (normalised paths)
    std::vector<RepoEntry> repositories; // in the order added, unique paths, no limit
    RecentOrder recentOrder = RecentOrder::MostRecent;
    std::map<std::string, bool> panels; // window name → visible
    std::map<std::string, RepoState> repoState; // repoStateKey → state; no empty entries
    NothingStaged nothingStaged = NothingStaged::Ask;
    bool expandConflictStages = false; // §4.10: index stages 1–3 for two-sided first-class conflicts on checkout
    bool expandStagesOnCheckout = false;
    bool submodulesAutoUpdate = true; // update the submodules after a checkout, pull, reset, ... (SubmoduleSync)
    bool submodulesAutoInit = false;  // ... and initialize the ones not yet initialized
    int historyBadgePrefix = 12; // History: characters kept at the start of an elided badge (1 – 100)
    int historyBadgeSuffix = 12; // ... and at its end
    bool historyGraphInDescription = false; // History: the graph is drawn in the Description column; each row's text follows its own lanes
    // Main window placement (imgui.ini [GGUIWindow])
    int windowX = -1, windowY = -1, windowW = 0, windowH = 0;
    bool windowMaximized = false;
    // View state (imgui.ini [GGUIView]; kViewSettings in Settings.cpp lists what is stored)
    bool historyShowStashes = true;
    bool historyConflictedOnly = false;
    bool branchesHideNew = false; // Branches: a new branch is hidden in History (top level, mixed groups); a group of hidden branches always hides it
    bool tagsHideNew = false; // Tags: a new tag is hidden in History
    bool diffSideBySide = false;
    int diffContext = 3;
    int diffWhitespace = 0;
    bool infoWrapMessage = false; // Change information: word-wrap the message fields
    int infoMessageLines = 6; // Change information: height of the message fields, in text lines
    bool rebaseNewestFirst = false; // interactive rebase editor: list shown newest commit first
    // Set by Settings::load (never stored): not empty when settings.json was partly unreadable. The App shows it.
    std::string loadWarning;
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

    // Permanent repositories. addRepository does nothing (no save) when the path is already listed.
    void addRepository(const std::string& path);
    void removeRepository(const std::string& path);
    // Stores normalizeAlias(alias) for the entry; an empty result removes the alias. An unknown path
    // changes nothing.
    void setAlias(const std::string& path, const std::string& alias);

    // The hidden refs of the worktree `wtId` in the repository `key` (repoStateKey). The worktree's own list
    // when it has one, else the legacy repository list; empty when none is stored.
    std::set<std::string> hiddenRefs(const std::string& key, const std::string& wtId) const;
    // Stores the hidden refs of the worktree `wtId` and saves settings.json when they changed.
    // The legacy repository list does not change.
    void setHiddenRefs(const std::string& key, const std::string& wtId, const std::set<std::string>& names);

    // The display name of the worktree `id` in the repository `key`; empty when it has no custom name.
    std::string worktreeName(const std::string& key, const std::string& id) const;
    // Stores a custom display name (trimmed) for the worktree `id`. An empty name removes it.
    // Saves settings.json when the name changed.
    void setWorktreeName(const std::string& key, const std::string& id, const std::string& name);

private:
    AsyncIo& m_io;
    SettingsData m_data;
    // Repositories whose state changed since the last save. A save writes only their state from memory;
    // the state of the others comes from the file, so another ggui process does not lose its changes.
    std::set<std::string> m_pendingRepoKeys;
    // Per repository key: the worktree ids whose hidden refs changed since the last save. A save takes the
    // other worktrees' lists from the file.
    std::map<std::string, std::set<std::string>> m_pendingHiddenIds;
    // Per repository key: the worktree ids whose name changed since the last save. Same rule as the hidden refs.
    std::map<std::string, std::set<std::string>> m_pendingNameIds;
    // Saves posted and not yet finished, per repository key. A finished save does not copy the file's state of
    // a key into memory while a later save of that key is still on its way: the file then holds an older value.
    std::map<std::string, int> m_savingRepoKeys;
    bool m_loaded = false;
    bool m_backupFailed = false; // settings.json was damaged and its backup failed: save() does not write
    std::filesystem::path m_dir;
};

nlohmann::json toJson(const SettingsData& d);
// `skipped` (optional) is set to true when the file has a value that is dropped: a key with the wrong type
// (a missing key is normal), a "recent" item that is not a string, or a repository item without a string "path".
SettingsData fromJson(const nlohmann::json& j, bool* skipped = nullptr);

// The key of a repository's shared state: its git common directory (one for all its worktrees), absolute,
// symlinks resolved, no trailing separator, lower case on Windows (as gg::seqlink::gitDirKey). "" for "".
std::string repoStateKey(const std::filesystem::path& commonDir);

// A repository path as stored in Recent: absolute, symlinks resolved where the path exists, no
// trailing separator ("/a/b/" and "/a/b" are the same entry).
std::string normalizeRepoPath(const std::string& path);
// `paths` without repeated repositories (after normalisation), first occurrence kept.
std::vector<std::string> uniqueRepoPaths(const std::vector<std::string>& paths);
// An alias as stored: white space trimmed from the whole text and from each "/" segment, empty
// segments removed, so no "/" at either end (" a//b/ " gives "a/b"; "/" gives "").
std::string normalizeAlias(const std::string& alias);
// Indices into `paths` in display order: as stored, or alphabetical by the unique display name
// (case-insensitive; the whole name as shown, parent prefix included).
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
