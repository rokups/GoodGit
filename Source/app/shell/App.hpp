// The application shell (REBUILD_PLAN §3): docking, menus, toolbar, error banner, Welcome
// screen and dialogs. Repository state and the panels live in Session (one per open repo);
// each panel has its own view model.
#pragma once

#include "shell/Dialogs.hpp"
#include "shell/Settings.hpp"

#include <core/Engine.hpp>
#include <core/Services.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ggui {

class Platform;
class Session;

struct AppOptions {
    std::string initialPath;
    bool autoOpen = true;
    std::string argv0;
};

// Result of `git --version` (G2: git >= 2.36 is required).
struct GitCheck {
    bool done = false;
    bool running = false;
    bool found = false;
    bool supported = false;
    std::string version;
};

class App {
public:
    App(Platform& platform, AppOptions options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void frame();
    void shutdown();

    bool idle() const;
    bool quitRequested() const { return m_quit; }
    bool confirmQuit() { return true; }
    void requestQuit()
    {
        m_quit = true;
        ++m_quitRequests;
    }
    // Test mode keeps running after Quit; the scenario checks the request count instead.
    void clearQuit() { m_quit = false; }
    int quitRequests() const { return m_quitRequests; }
    // Text of recent row `i` as shown on the Welcome screen (path + summary).
    std::string recentRowText(size_t i) const;

    // Test isolation (P0-08): closes the repository and reloads settings from the (new)
    // preferences directory, as on a fresh start.
    void resetForTest();

    // ---- actions (menus, toolbar, Welcome) --------------------------------------------------
    // Checks git first (G2), then opens.
    void openRepository(const std::filesystem::path& path);
    void closeRepository();
    void pickAndOpenRepository();
    void initializeRepository();
    void showCloneDialog();
    // Important errors: a modal error popup (OK / Copy message).
    void showError(const std::string& title, const std::string& message);
    // Low-importance information and warnings: a toast in the bottom-right corner that fades out.
    enum class Notice { Info, Warning };
    void notify(Notice level, const std::string& title, const std::string& message);
    void clearError();
    struct Toast {
        std::uint64_t id = 0;
        Notice level = Notice::Info;
        std::string title;
        std::string message;
        float age = 0.0f; // seconds shown (not counted while hovered)
    };
    const std::vector<Toast>& toasts() const { return m_toasts; }
    void resetLayout();

    Settings& settings() { return m_settings; }
    AsyncIo& io() { return m_io; }
    Dialogs& dialogs() { return m_dialogs; }
    Session* session() { return m_session.get(); }
    Platform& platform() { return m_platform; }
    core::CloneService& clone() { return m_clone; }
    const GitCheck& gitCheck() const { return m_git; }
    // The todo editor shows a list a waiting `git rebase -i` gets back (sequence.editor).
    bool editingForGit() const { return m_sequenceOpen != 0; }
    bool settingsOpen() const { return m_showSettings; }
    // Opens Settings on its General tab (unless already open).
    void openSettings()
    {
        if (!m_showSettings)
            m_settingsFreshOpen = true;
        m_showSettings = true;
    }

    // Picks a folder with the native dialog (off the UI thread); `done` runs on the UI thread.
    void pickFolder(const std::string& title, std::function<void(std::string)> done);
    // Picks a file to save to; `done` receives "" when cancelled.
    void pickSaveFile(const std::string& title, const std::string& defaultName, std::function<void(std::string)> done);

    // Runs `fn` on the UI thread at the start of the next frame.
    void post(std::function<void()> fn) { m_posted.push_back(std::move(fn)); }

    // Last error or warning reported (also logged), until clearError().
    const std::string& errorTitle() const { return m_errorTitle; }
    const std::string& errorMessage() const { return m_errorMessage; }

private:
    void onSettingsLoaded(const std::string& iniText);
    void applyTheme();
    void handleShortcuts();
    void drawMenuBar();
    void drawRecentMenu();
    void drawToolbar();
    void drawRepositoryButtons();
    void drawStateBadge();
    void drawRebaseProgress(Session& s, const core::RebaseProgress& rebase);
    void drawToasts();
    void drawWelcome();
    void drawDockHost();
    void drawSettingsWindow();
    void pumpSummaries();
    void pumpAskpass();
    // Hands lists from `git gg sequence-editor` (plain git rebase -i) to the todo editor.
    void pumpSequenceEditor();
    void pumpClone();
    void saveIniIfNeeded();
    void updateTitle();
    // Runs `git --version` off the UI thread, then `done(ok)`. When git is missing or too old a
    // blocking prompt offers Retry (calls `retry`) and Quit.
    void checkGit(std::function<void(bool ok)> done, std::function<void()> retry);
    void openNow(const std::filesystem::path& path);

    Platform& m_platform;
    AppOptions m_options;
    AsyncIo m_io;
    Settings m_settings;
    Dialogs m_dialogs;
    core::SummaryService m_summaries;
    core::AskpassServer m_askpass;
    core::SequenceEditorServer m_sequenceEditor;
    core::CloneService m_clone;
    GitCheck m_git;
    std::vector<core::RepoSummary> m_recentInfo;
    std::unique_ptr<Session> m_session;
    std::vector<std::unique_ptr<Session>> m_closing; // sessions whose engines shut down later
    std::vector<std::function<void()>> m_posted;
    std::uint64_t m_askpassShown = 0;
    std::uint64_t m_sequenceOpen = 0;    // the request the todo editor shows (0 = none)
    std::uint64_t m_sequenceNoticed = 0; // the request told to wait for the open todo editor

    bool m_quit = false;
    bool m_showSettings = false;
    bool m_settingsFreshOpen = false;
    bool m_autoOpenDone = false;
    bool m_layoutPending = false;
    bool m_iniApplied = false;
    int m_pendingOpens = 0;
    std::string m_welcomePath;
    std::string m_recentFilter;
    std::string m_errorTitle;
    std::string m_errorMessage;
    std::vector<Toast> m_toasts;
    std::uint64_t m_nextToast = 1;
    std::string m_title;
    int m_pickersRunning = 0;
    int m_recentFocus = -1;
    int m_quitRequests = 0;
    // Settings window: editable git config values (key "scope|name") and the value they were
    // last synchronised from (a change made elsewhere replaces the text unless it is being edited).
    std::map<std::string, std::string> m_configEdit;
    std::map<std::string, std::string> m_configSeen;
    bool m_configLoaded = false;
    void drawGitConfigSettings(Session& s);
};

// Draws a toolbar button: `icon` (and label) as its text, `id` ("##name") as its stable ID, `tooltip` on hover.
bool iconButton(const char* icon, const char* id, const char* tooltip, bool enabled = true);

} // namespace ggui
