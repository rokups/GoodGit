// Every mutation the UI can start (REBUILD_PLAN §4.3, §4.4, §4.7–§4.12). Each action is a
// MutationSpec run on the engine's mutation (or network) queue through plain git (G2) and
// recorded in the undo journal. Outcomes come back as MutationFinishedEvents; per-request
// callbacks let the caller react (e.g. offer "Stash and switch").
#pragma once

#include <core/Engine.hpp>
#include <libgg/Rewrite.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ggui {

class Session;

enum class PullMode { Config, Merge, Rebase, FastForwardOnly };
enum class CommitMode { Index, StageAllTracked, StageSelected };
enum class Side { Ours, Theirs };

class Actions {
public:
    using Callback = std::function<void(const core::MutationFinishedEvent&)>;

    explicit Actions(Session& session);

    void onFinished(const core::MutationFinishedEvent& event);
    // "" when free; otherwise the running mutation (conflicting actions are disabled).
    std::string busy() const;
    std::string busyTooltip() const;

    core::RequestId run(std::string label, std::function<void(core::MutationContext&)> fn, Callback done = {},
        bool network = false, bool journal = true);

    // ---- files (§4.4) ---------------------------------------------------------------------
    void stage(const std::vector<std::string>& paths);
    void unstage(const std::vector<std::string>& paths);
    void discard(const std::vector<std::string>& tracked, const std::vector<std::string>& untracked);
    void stageAll();
    void unstageAll();
    void stageModified();
    void intentToAdd(const std::vector<std::string>& paths);
    void markResolved(const std::vector<std::string>& paths);
    // First-class conflicts in working tree files: side `side` (0-based) for every region, or
    // only region `region` (0-based).
    void takeConflictSide(const std::vector<std::string>& paths, int side, int region = -1);
    // Stages 1–3 from the file's regions, then the configured merge tool.
    void mergeToolFirstClass(const std::string& path);
    void deleteFiles(const std::vector<std::string>& paths);
    // A patch (hunk/line staging, discard, Apply patch…).
    void applyPatch(const std::string& label, const std::string& patch, bool cached, bool reverse, Callback done = {});

    // ---- commits (§4.3) -------------------------------------------------------------------
    void commit(const std::string& message, bool noVerify, CommitMode mode, const std::vector<std::string>& selected,
        Callback done = {});
    void amend(const std::string& message, bool noVerify, bool messageOnly, Callback done = {});
    void newCommit(const std::vector<std::string>& parents, bool detach, const std::string& message = {});
    void checkout(const std::string& target, bool detach, bool stashFirst = false);
    void moveHead(bool toChild, const core::Oid& child = {});

    // ---- branches, tags, remotes (§4.7) ---------------------------------------------------
    void createBranch(const std::string& name, const std::string& at, bool checkoutAfter);
    void renameBranch(const std::string& from, const std::string& to);
    void deleteBranch(const std::string& name, bool force, const std::vector<std::string>& remotes, bool local);
    void moveBranch(const std::string& name, const std::string& to);
    void setUpstream(const std::string& branch, const std::string& upstream);
    void unsetUpstream(const std::string& branch);
    void fastForward(const std::string& branch);
    void createTag(const std::string& name, const std::string& at, const std::string& message);
    void deleteTag(const std::string& name);
    void pushTag(const std::string& remote, const std::string& tag);
    void deleteRemoteTag(const std::string& remote, const std::string& tag);
    void addRemote(const std::string& name, const std::string& url);
    void removeRemote(const std::string& name);
    void setRemoteUrl(const std::string& name, const std::string& url);
    void setPruneOnFetch(const std::string& name, bool prune);

    // ---- history editing (§4.3; in-memory rewrites) --------------------------------------
    // Builds the plan on the worker from the repository as it is then.
    using PlanBuilder = std::function<gg::rewrite::Plan(git_repository* repo)>;
    // Computes the rewrite in memory; asks for pre-flight decisions (non-text conflicts) and
    // confirmation (published commits, branches checked out elsewhere); then applies it as one
    // operation. Newly conflicted commits are reported in a notification.
    void rewrite(const std::string& label, PlanBuilder build, Callback done = {});
    void reword(const core::Oid& commit, const std::string& message);
    void editAuthor(const core::Oid& commit, const std::string& name, const std::string& email);
    // A detached copy of the commit (or of it and its descendants) on its parent.
    void duplicate(const core::Oid& commit, bool withDescendants);
    // The commit alone, or with its descendants, onto `destination`.
    void rebaseOnto(const core::Oid& commit, const std::string& destination, bool withDescendants);
    // Folds `commit` into `target` (its parent by default); combine or keep the target's message.
    void squash(const core::Oid& commit, const std::string& target, bool combineMessages); // target "" = parent
    // Folds the commit's descendants (a linear chain) into it.
    void squashDescendants(const core::Oid& commit);
    void split(const core::Oid& commit, const std::vector<std::string>& paths, const std::string& firstMessage);
    void abandon(const core::Oid& commit, bool withDescendants, std::function<void()> then = {});
    void restorePaths(const core::Oid& commit, const std::string& from, const std::vector<std::string>& paths);
    // `git restore --source=<from> --staged --worktree -- paths` (plain git).
    void restoreWorktree(const std::string& from, const std::vector<std::string>& paths);
    void mergeNative(const std::string& branch);
    // HEAD's own commits (not on `branch`) replayed onto it.
    void rebaseHeadOnto(const std::string& branch);
    void simplifyParents(const core::Oid& commit);
    void insertCommit(const core::Oid& at, bool before, const std::string& message);
    void mergeIntoHead(const std::string& branch, const std::string& message);
    enum class MoveTo { Parent, Child, Active, WorkingTree, Revert };
    // Moves the commit's changes to selected files (`paths`) or selected lines (`patch`, old →
    // new as the commit's diff has them) to its parent, its child, the checked-out commit or
    // the working tree ("uncommit"); Revert takes them out of the commit.
    void moveChanges(const core::Oid& commit, MoveTo to, const std::vector<std::string>& paths, const std::string& patch);
    // Working tree files folded into `commit` (its descendants rebased; the files stay as they
    // are on disk).
    void absorb(const core::Oid& commit, const std::vector<std::string>& paths);
    // Moves (or copies) `commit` right after `anchor` (or before it) in a linear chain.
    void reorder(const core::Oid& commit, const core::Oid& anchor, bool after, bool copy);

    // ---- network (§4.8) -------------------------------------------------------------------
    void fetch(const std::string& remote, bool prune, bool tags); // remote "" = all
    void pull(PullMode mode, bool autostash = false);
    void push(const std::string& remote, const std::string& localBranch, const std::string& remoteBranch,
        bool setUpstream, bool forceWithLease, bool tags = false);

    // ---- stash (§4.9) ---------------------------------------------------------------------
    void stashPush(const std::string& message, bool keepIndex, bool untracked, bool stagedOnly,
        const std::vector<std::string>& paths, Callback done = {});
    void stashApply(int index, bool pop, bool restoreIndex);
    void stashDrop(int index);
    void stashClear();
    void stashBranch(int index, const std::string& branch);
    void stashApplyFile(int index, const std::string& path);

    // ---- native in-progress operations and conflicts (§4.10) --------------------------------
    void continueOperation();
    void skipOperation();
    void abortOperation();
    void takeSide(const std::vector<std::string>& paths, Side side);
    void commitWithConflicts();
    void saveMergeMessage(const std::string& message);

    // ---- undo (§5 U1) ---------------------------------------------------------------------
    void undo(bool redo);
    void restore(const std::string& operationId);

    // ---- hooks and old gg data -------------------------------------------------------------
    void installHooks(Callback done = {});
    void uninstallHooks(Callback done = {});
    void cleanUpOldGgRefs(const std::vector<std::pair<std::string, std::string>>& keepBranches);

    // External tools (not journaled).
    void openInEditor(const std::string& path);
    void externalDiff(const std::string& path, const std::string& from, const std::string& to);
    void mergeTool(const std::string& path);

private:
    void handleDefault(const core::MutationFinishedEvent& event);

    struct RewriteState;
    void rewritePrepare(const std::shared_ptr<RewriteState>& state);
    void rewriteDecide(const std::shared_ptr<RewriteState>& state);
    void rewriteApply(const std::shared_ptr<RewriteState>& state);

    Session& m_session;
    std::map<core::RequestId, Callback> m_callbacks;
};

// Joins paths after "--" for a git command line.
std::vector<std::string> withPaths(std::vector<std::string> args, const std::vector<std::string>& paths);

} // namespace ggui
