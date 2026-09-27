// One open repository: its engine, the latest snapshot/status, the shared selection and the
// panels. Panels own their view models; the session routes engine events to them and offers
// the dialogs shared by several panels and the toolbar.
#pragma once

#include "shell/Actions.hpp"

#include <core/Engine.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ggui {

class App;
class HistoryPanel;
class ChangesPanel;
class InfoPanel;
class DiffPanel;
class BlamePanel;
class BranchesPanel;
class TagsPanel;
class WorktreesPanel;
class RemotesPanel;
class StashesPanel;
class ReflogPanel;
class OperationsPanel;
class RebasePanel;

enum class SelKind { None, WorkingTree, Index, Commit, Stash };

struct Selection {
    SelKind kind = SelKind::None;
    core::Oid id;          // commit, or stash commit
    int stashIndex = -1;
    bool operator==(const Selection& o) const
    {
        return kind == o.kind && id == o.id && stashIndex == o.stashIndex;
    }
};

// Names of the dockable panel windows (stable ImGui window names, see docs/spec/ui-spec.md).
namespace panel {
inline constexpr const char* History = "History";
inline constexpr const char* Changes = "Changes";
inline constexpr const char* Info = "Change information";
inline constexpr const char* Diff = "Diff";
inline constexpr const char* Blame = "Blame";
inline constexpr const char* Branches = "Branches";
inline constexpr const char* Tags = "Tags";
inline constexpr const char* Worktrees = "Worktrees";
inline constexpr const char* Remotes = "Remotes";
inline constexpr const char* Stashes = "Stashes";
inline constexpr const char* Reflog = "Reflog";
inline constexpr const char* Operations = "Operations";
inline constexpr const char* All[] = {History, Changes, Info, Diff, Blame, Branches, Tags, Worktrees, Remotes,
    Stashes, Reflog, Operations};
// Reflog, Operations and Blame start hidden (View menu, or opened on demand).
inline bool defaultVisible(const std::string& name) { return name != Blame && name != Reflog && name != Operations; }
} // namespace panel

using ConflictList = std::vector<std::pair<std::string, int>>; // path, sides

class Session {
public:
    Session(App& app, std::filesystem::path path);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- lifecycle ------------------------------------------------------------------------
    bool opening() const { return !m_opened && !m_failed; }
    bool opened() const { return m_opened; }
    bool failed() const { return m_failed; }
    void cancelOpen();
    const std::filesystem::path& path() const { return m_path; }
    std::string displayName() const;

    // Applies engine events, bounded per frame (§3.1).
    void pump();
    void draw();
    bool idle() const;

    // ---- shared state ---------------------------------------------------------------------
    App& app() { return m_app; }
    core::Engine& engine() { return *m_engine; }
    Actions& actions() { return *m_actions; }
    const core::SnapshotPtr& snapshot() const { return m_snapshot; }
    const core::StatusPtr& status() const { return m_status; }
    const Selection& selection() const { return m_selection; }
    void select(const Selection& sel);
    void selectCommit(const core::Oid& id);
    void revealCommit(const core::Oid& id);
    void refresh();
    std::vector<core::Activity> activities() const;
    void cancel(core::RequestId id) { m_engine->cancel(id); }
    void cancelAll() { m_engine->cancelAll(); }
    void nextChangedFile(int direction);
    void blameFile(const std::string& path, const core::Oid& commit);
    // Shows the panel and focuses it (after it has been drawn, if it was hidden).
    void focusPanel(const char* name);

    // Conflicted files of a commit (first-class, from the conflict scan), or null.
    const ConflictList* conflictsOf(const core::Oid& id) const;
    const std::unordered_map<core::Oid, ConflictList, core::OidHash>& conflicts() const { return m_conflicts; }
    const std::vector<gg::journal::Operation>& operations() const { return m_operations; }
    bool hooksInstalled() const { return m_hooksInstalled; }
    const std::optional<gg::hooks::Status>& hooksStatus() const { return m_hooksStatus; }
    void requestHooksStatus();
    const std::map<std::string, std::map<std::string, std::string>>& config() const { return m_config; }
    void requestConfig();

    // The short (unique) form of a commit ID, as History shows it.
    std::string shortId(const core::Oid& id) const;
    // Length of History's abbreviations (git's grows with the repository).
    size_t shortIdLength() const;
    // HEAD's child in History (for "Move HEAD to child"), null when none is loaded.
    core::Oid headChild() const;
    // Pull: available with an upstream on an attached HEAD; `reason` explains otherwise.
    bool pullAvailable(std::string* reason) const;
    int incoming() const;
    int outgoing() const;
    // Paths selected in the Changes panel (working tree / index selection).
    std::vector<std::string> selectedPaths() const;

    // ---- dialogs shared by panels, menus and the toolbar (SessionDialogs.cpp) ---------------
    void showCommitDialog(bool amend);
    void showStashDialog(std::vector<std::string> paths = {});
    void showPushToDialog(const std::string& branch = {});
    void showCreateBranchDialog(const std::string& at);
    void showCreateTagDialog(const std::string& at);
    void showAddRemoteDialog();
    void showEditRemoteDialog(const std::string& remote);
    void showDiscardDialog(std::vector<std::string> tracked, std::vector<std::string> untracked);
    void showDiscardAllDialog();
    void showApplyPatchDialog();
    void showRenameBranchDialog(const std::string& branch);
    void showDeleteBranchDialog(const std::string& branch, int mode); // 0 local, 1 remote, 2 all
    void showMoveBranchDialog(const std::string& branch, const std::string& to);
    void showSetUpstreamDialog(const std::string& branch);
    void showBranchFromStashDialog(int index);
    void showDropStashDialog(int index);
    void showClearStashesDialog();
    void showDeleteFilesDialog(std::vector<std::string> paths);
    void showBranchFromCommitDialog(const std::string& commit);
    void newCommitOn(const std::vector<core::Oid>& parents, bool detach);
    void checkoutCommit(const core::Oid& id);
    void pushCurrent();
    void popStash();
    void maybePromptHooks();
    void maybePromptOldGgRefs();

    HistoryPanel& history() { return *m_history; }
    ChangesPanel& changes() { return *m_changes; }
    InfoPanel& info() { return *m_info; }
    DiffPanel& diff() { return *m_diff; }
    BlamePanel& blame() { return *m_blame; }
    BranchesPanel& branches() { return *m_branches; }
    TagsPanel& tags() { return *m_tags; }
    ReflogPanel& reflog() { return *m_reflog; }
    OperationsPanel& operationsPanel() { return *m_operationsPanel; }
    // The interactive rebase todo editor (shown while a todo is open).
    RebasePanel& rebase() { return *m_rebase; }

private:
    void handle(core::Event& event);
    void onSnapshot(core::SnapshotPtr snap, bool first);

    App& m_app;
    std::filesystem::path m_path;
    std::unique_ptr<core::Engine> m_engine;
    std::unique_ptr<Actions> m_actions;
    core::RequestId m_openRequest = 0;
    bool m_opened = false;
    bool m_failed = false;
    core::SnapshotPtr m_snapshot;
    core::StatusPtr m_status;
    Selection m_selection;
    std::unordered_map<core::Oid, ConflictList, core::OidHash> m_conflicts;
    std::vector<gg::journal::Operation> m_operations;
    bool m_hooksInstalled = false;
    std::optional<gg::hooks::Status> m_hooksStatus;
    bool m_hooksPromptChecked = false;
    bool m_ggRefsPromptChecked = false;
    std::map<std::string, std::map<std::string, std::string>> m_config;

    std::unique_ptr<HistoryPanel> m_history;
    std::unique_ptr<ChangesPanel> m_changes;
    std::unique_ptr<InfoPanel> m_info;
    std::unique_ptr<DiffPanel> m_diff;
    std::unique_ptr<BlamePanel> m_blame;
    std::unique_ptr<BranchesPanel> m_branches;
    std::unique_ptr<TagsPanel> m_tags;
    std::unique_ptr<WorktreesPanel> m_worktrees;
    std::unique_ptr<RemotesPanel> m_remotes;
    std::unique_ptr<StashesPanel> m_stashes;
    std::unique_ptr<ReflogPanel> m_reflog;
    std::unique_ptr<OperationsPanel> m_operationsPanel;
    std::unique_ptr<RebasePanel> m_rebase;
    std::string m_pendingFocus;
};

} // namespace ggui
