// One open repository: its engine, the latest snapshot/status, the shared selection and the
// panels. Panels own their view models; the session routes engine events to them and offers
// the dialogs shared by several panels and the toolbar.
#pragma once

#include "shell/Actions.hpp"

#include <core/Engine.hpp>
#include <libgg/EditSession.hpp>
#include <libgg/Outgoing.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
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
class RepositoriesPanel;
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
inline constexpr const char* Repositories = "Repositories";
inline constexpr const char* Worktrees = "Worktrees";
inline constexpr const char* Remotes = "Remotes";
inline constexpr const char* Stashes = "Stashes";
inline constexpr const char* Reflog = "Reflog";
inline constexpr const char* Operations = "Operations";
inline constexpr const char* All[] = {History, Changes, Info, Diff, Blame, Branches, Tags, Repositories,
    Worktrees, Remotes, Stashes, Reflog, Operations};
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
    // Reads the full messages of `ids` on the worker, then calls `done` (on the UI thread) with
    // them in the same order. Not called when the request is superseded or fails.
    void commitMessages(const std::vector<core::Oid>& ids, std::function<void(const std::vector<std::string>&)> done);
    Actions& actions() { return *m_actions; }
    // Set from the first OpenedEvent on (opened()). Panels, menus and actions run only for an
    // opened session, so they use it without a null check.
    const core::SnapshotPtr& snapshot() const { return m_snapshot; }
    // Null until the first status arrives (panels draw before that).
    const core::StatusPtr& status() const { return m_status; }
    const Selection& selection() const { return m_selection; }
    // Change information shows this instead of the history selection while it is set (the Blame panel's
    // selected line); the selection itself, Changes and Diff stay as they are.
    void setInfoOverride(const std::optional<Selection>& sel);
    const std::optional<Selection>& infoOverride() const;
    // Clears the Blame panel's line selection (which drops the override on its next frame).
    void clearBlameSelection();
    void select(const Selection& sel);
    // While a stash message is being rewritten its commit id changes: the selected stash is kept
    // (not reset to the working tree when a snapshot arrives without it), then re-pointed at the
    // new commit, same index.
    void stashRewordPending(bool pending) { m_stashRewordPending = pending; }
    void stashRewordDone(const std::string& oldCommit, const std::string& newCommit, int index);
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
    // The Edit commit session (§4.3), read with each snapshot; a stale one (HEAD no longer
    // detached, or its branch gone) is cleared.
    const std::optional<gg::edit::Session>& editSession() const { return m_editSession; }
    void stopEditing();
    // What committing the staged files would make or leave conflicted (read off the UI thread on every
    // status change and when the commit dialog opens); never blocks.
    const std::vector<gg::outgoing::StagedWarning>& commitWarnings() const { return m_commitWarnings; }
    // The warning as shown by the commit dialog and the Info panel's Commit button: one line per
    // affected file (the first few), or "" for none.
    std::string commitWarningText() const;
    const std::map<std::string, std::map<std::string, std::string>>& config() const { return m_config; }
    void requestConfig();

    // Tags on each remote (git ls-remote), read while the Tags panel is shown and again after the
    // remotes change or a network operation (fetch, pull, push) finishes. A remote missing from
    // the map has not been read yet; `ok` false: it could not be read.
    struct RemoteTags {
        bool ok = false;
        std::set<std::string> tags;
        std::string error;
    };
    const std::map<std::string, RemoteTags>& remoteTags() const { return m_remoteTags; }
    void requestRemoteTagsIfStale();
    void markRemoteTagsStale() { m_remoteTagsStale = true; }

    // The short form of a commit ID (its first kShortIdLength characters), as History shows it.
    std::string shortId(const core::Oid& id) const;
    // Length of the short IDs the UI shows (kShortIdLength).
    size_t shortIdLength() const;
    // Pull: available with an upstream on an attached HEAD; `reason` explains otherwise.
    bool pullAvailable(std::string* reason) const;
    int incoming() const;
    int outgoing() const;
    // Paths selected in the Changes panel (working tree / index selection).
    std::vector<std::string> selectedPaths() const;

    // ---- dialogs shared by panels, menus and the toolbar (SessionDialogs.cpp) ---------------
    // What the Commit dialog held, to reopen it as it was (an Amend declined at the published-history
    // question).
    struct CommitDialogState {
        std::string commitText;
        std::string amendText;
        bool skipHooks = false;
        bool messageOnly = false;
    };
    void showCommitDialog(const CommitDialogState* restore = nullptr);
    void showStashDialog(std::vector<std::string> paths = {});
    void showPushToDialog(const std::string& branch = {});
    void showCreateBranchDialog(const std::string& at, const std::string& name = {});
    void showCreateTagDialog(const std::string& at);
    void showAddRemoteDialog();
    void showEditRemoteDialog(const std::string& remote);
    void showDiscardDialog(std::vector<std::string> tracked, std::vector<std::string> untracked,
        std::vector<StagedDiscard> staged = {});
    void showDiscardAllDialog();
    void showApplyPatchDialog();
    void showRenameBranchDialog(const std::string& branch);
    void showDeleteBranchDialog(const std::string& branch, int mode); // 0 local, 1 remote, 2 all
    void showDeleteRemoteBranchDialog(const std::string& remoteBranch); // "origin/x": git push origin --delete x
    void showMoveBranchDialog(const std::string& branch, const std::string& to);
    void showSetUpstreamDialog(const std::string& branch);
    void showBranchFromStashDialog(int index);
    void showDropStashDialog(int index);
    void showClearStashesDialog();
    void showDeleteFilesDialog(std::vector<std::string> paths);
    void showBranchFromCommitDialog(const std::string& commit);
    // Worktrees (WorktreeDialogs.cpp). Add…: `mode` 0 new branch, 1 existing branch `preset`,
    // 2 detached at `preset`.
    void showAddWorktreeDialog(int mode = 0, const std::string& preset = {});
    void showRemoveWorktreeDialog(const core::WorktreeInfo& w);
    void showLockWorktreeDialog(const core::WorktreeInfo& w);
    void showPruneWorktreesDialog();
    void showRepairWorktreeDialog(const core::WorktreeInfo& w);
    // New: an empty commit on `parent` (null = HEAD). Attached, the branch at `parent` advances (see
    // newCommitBranch) and HEAD follows it; `detach`, or no such branch, leaves branches alone.
    void newCommitOn(const core::Oid& parent, bool detach);
    // The branch a non-detached New on `at` (null = HEAD) advances: HEAD's branch when `at` is HEAD,
    // else the only local branch pointing at `at`. Empty when New there can only be detached.
    std::string newCommitBranch(const core::Oid& at) const;
    void pushCurrent();
    void popStash();

    HistoryPanel& history() { return *m_history; }
    ChangesPanel& changes() { return *m_changes; }
    InfoPanel& info() { return *m_info; }
    DiffPanel& diff() { return *m_diff; }
    BlamePanel& blame() { return *m_blame; }
    BranchesPanel& branches() { return *m_branches; }
    TagsPanel& tags() { return *m_tags; }
    RepositoriesPanel& repositories() { return *m_repositories; }
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
    std::map<core::RequestId, std::function<void(const std::vector<std::string>&)>> m_messageWaiters;
    bool m_opened = false;
    bool m_failed = false;
    core::SnapshotPtr m_snapshot;
    core::StatusPtr m_status;
    Selection m_selection;
    bool m_stashRewordPending = false;
    std::unordered_map<core::Oid, ConflictList, core::OidHash> m_conflicts;
    std::vector<gg::journal::Operation> m_operations;
    std::optional<gg::edit::Session> m_editSession;
    std::vector<gg::outgoing::StagedWarning> m_commitWarnings;
    std::map<std::string, std::map<std::string, std::string>> m_config;
    std::map<std::string, RemoteTags> m_remoteTags;
    std::string m_journalError; // the undo journal's problem last reported ("" = none)
    std::vector<std::string> m_remoteTagsFor; // the remotes last asked about
    core::RequestId m_remoteTagsRequest = 0;
    bool m_remoteTagsStale = true;

    std::unique_ptr<HistoryPanel> m_history;
    std::unique_ptr<ChangesPanel> m_changes;
    std::unique_ptr<InfoPanel> m_info;
    std::unique_ptr<DiffPanel> m_diff;
    std::unique_ptr<BlamePanel> m_blame;
    std::unique_ptr<BranchesPanel> m_branches;
    std::unique_ptr<TagsPanel> m_tags;
    std::unique_ptr<RepositoriesPanel> m_repositories;
    std::unique_ptr<WorktreesPanel> m_worktrees;
    std::unique_ptr<RemotesPanel> m_remotes;
    std::unique_ptr<StashesPanel> m_stashes;
    std::unique_ptr<ReflogPanel> m_reflog;
    std::unique_ptr<OperationsPanel> m_operationsPanel;
    std::unique_ptr<RebasePanel> m_rebase;
    std::string m_pendingFocus;
};

} // namespace ggui
