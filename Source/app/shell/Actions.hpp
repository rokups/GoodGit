// Every mutation the UI can start (product spec §4.3, §4.4, §4.7–§4.12). Each action is a
// MutationSpec run on the engine's mutation (or network) queue through plain git (G2) and
// recorded in the undo journal. Outcomes come back as MutationFinishedEvents; per-request
// callbacks let the caller react (e.g. offer "Stash and switch").
#pragma once

#include <core/Engine.hpp>
#include <libgg/NativeRebase.hpp>
#include <libgg/Rewrite.hpp>

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ggui {

// A fully staged file to discard back to HEAD (index and working tree). `remove`: the path is new
// (added, copied, renamed-to, or HEAD is unborn), so it leaves the index and is deleted; `oldPath`
// (renames) is restored from HEAD.
struct StagedDiscard {
    std::string path;
    std::string oldPath;
    bool remove = false;
};

class Session;

enum class PullMode { Config, Merge, Rebase, FastForwardOnly };
enum class CommitMode { Index, StageAllTracked, StageSelected };
enum class Side { Ours, Theirs };

// The messages git writes for a revert and for `cherry-pick -x` (§4.3):
//   Revert "<subject>"\n\nThis reverts commit <id>.
//   <message>\n\n(cherry picked from commit <id>)
// Neither ends with a newline. A message that already has that line does not get it twice.
std::string revertMessage(const std::string& message, const std::string& id);
// The message of a revert of part of a commit: the same, with "This reverts part of commit <id>:
// <what>." (<what> is the paths joined by ", " or "some lines of <path>").
std::string revertPartMessage(const std::string& message, const std::string& id, const std::string& what);
std::string cherryPickMessage(const std::string& message, const std::string& id);

class Actions {
public:
    using Callback = std::function<void(const core::MutationFinishedEvent&)>;

    explicit Actions(Session& session);

    void onFinished(const core::MutationFinishedEvent& event);
    // "" when free; otherwise the running mutation (conflicting actions are disabled).
    std::string busy() const;
    std::string busyTooltip() const;

    core::RequestId run(std::string label, std::function<void(core::MutationContext&)> fn, Callback done = {},
        bool network = false, bool journal = true, bool refreshAfter = true);

    // ---- files (§4.4) ---------------------------------------------------------------------
    void stage(const std::vector<std::string>& paths);
    void unstage(const std::vector<std::string>& paths);
    void discard(const std::vector<std::string>& tracked, const std::vector<std::string>& untracked,
        const std::vector<StagedDiscard>& staged = {});
    void stageAll();
    void unstageAll();
    void stageModified();
    void intentToAdd(const std::vector<std::string>& paths);
    void markResolved(const std::vector<std::string>& paths);
    // First-class conflicts in working tree files: side `side` (0-based) for every region, or
    // only region `region` (0-based).
    void takeConflictSide(const std::vector<std::string>& paths, int side, int region = -1);
    // Stages 1–3 from the file's regions, then the configured merge tool. `pair` k (0-based)
    // resolves sides k and k+1 (with the base between them); for a two-sided conflict it must
    // be 0. On an N-sided conflict (N >= 3) success folds the pair into one term, leaving the
    // file a first-class conflict with one side fewer.
    void mergeToolFirstClass(const std::string& path, int pair = 0);
    void deleteFiles(const std::vector<std::string>& paths);
    // A patch (hunk/line staging, discard, Apply patch…).
    void applyPatch(const std::string& label, const std::string& patch, bool cached, bool reverse, Callback done = {});

    // ---- commits (§4.3) -------------------------------------------------------------------
    void commit(const std::string& message, bool noVerify, CommitMode mode, const std::vector<std::string>& selected,
        Callback done = {});
    // Commits only the unstaged and untracked changes; the staged ones stay staged.
    void commitWorktree(const std::string& message, Callback done = {});
    void amend(const std::string& message, bool noVerify, bool messageOnly, Callback done = {});
    // `branch`: advance that local branch (at the first parent) and switch to it.
    void newCommit(const std::vector<std::string>& parents, bool detach, const std::string& message = {},
        const std::string& branch = {});
    // `edit`: Edit commit (the target detached, with an edit session to return from).
    void checkout(const std::string& target, bool detach, bool stashFirst = false, bool edit = false);
    void editCommit(const core::Oid& id);

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

    // ---- worktrees (§4.7; ActionsWorktrees.cpp) -------------------------------------------
    // Add, remove, lock and unlock are journaled with the worktree they changed, so Undo does
    // the opposite (only from the worktree whose window ran them). Prune and repair are
    // journaled without anything Undo could restore.
    struct AddWorktree {
        enum class Mode { NewBranch, ExistingBranch, Detached };
        std::string path;        // absolute
        Mode mode = Mode::NewBranch;
        std::string branch;      // the new or existing branch
        std::string start;       // new branch: its start point; detached: the commit ("" = HEAD)
        bool force = false;      // --force
        bool checkout = true;    // false: --no-checkout
        bool lock = false;       // --lock [--reason]
        std::string reason;
    };
    void addWorktree(AddWorktree request, Callback done = {});
    // `git worktree remove`; a locked worktree is unlocked first (and locked again when that
    // fails). Changes in it are refused (Outcome::LocalChanges, which offers to delete them) unless
    // `force`.
    void removeWorktree(const std::string& path, bool force);
    void lockWorktree(const std::string& path, const std::string& reason);
    void unlockWorktree(const std::string& path);
    // `git worktree prune --dry-run --verbose` (not journaled): `done` gets git's lines in `result`.
    void previewPruneWorktrees(Callback done);
    void pruneWorktrees();
    void repairWorktree(const std::string& path);
    // A new ggui process on `path` (GG_GGUI names the program, else this ggui), detached.
    void openInNewWindow(const std::string& path);

    // ---- history editing (§4.3; in-memory rewrites) --------------------------------------
    // Builds the plan on the worker from the repository as it is then.
    using PlanBuilder = std::function<gg::rewrite::Plan(git_repository* repo)>;
    // Computes the rewrite in memory; asks for pre-flight decisions (non-text conflicts) and
    // confirmation (published commits, branches checked out elsewhere); then applies it as one
    // operation. Newly conflicted commits are reported in a notification. With `autostash`
    // local changes are stashed before applying and popped after, in the same operation.
    void rewrite(const std::string& label, PlanBuilder build, Callback done = {}, bool autostash = false);
    void reword(const core::Oid& commit, const std::string& message);
    void editAuthor(const core::Oid& commit, const std::string& name, const std::string& email);
    // A detached copy of the commit (or of it and its descendants) on its parent.
    void duplicate(const core::Oid& commit, bool withDescendants);
    // The commit alone, or with its descendants, onto `destination`.
    void rebaseOnto(const core::Oid& commit, const std::string& destination, bool withDescendants);
    // Folds `commit` into `target` (its parent by default); combine or keep the target's message.
    void squash(const core::Oid& commit, const std::string& target, bool combineMessages); // target "" = parent
    // Folds the commit's descendants (a linear chain) into it.
    // One commit from `commits` (newest first, each the parent of the one before; none a merge): the
    // oldest with the rest folded into it, carrying `message`.
    void squashRange(const std::vector<core::Oid>& commits, const std::string& message);
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
    void mergeIntoHead(const std::string& branch, const std::string& message);
    // Revert (`revert`) or cherry-pick the commit onto HEAD (a merge against its first parent,
    // `-m 1`). With `andCommit`: a new commit on HEAD built in memory (text conflicts first-class,
    // one ref update, the branch or detached HEAD advances; a pick keeps the author). Without:
    // `git revert/cherry-pick --no-commit` into the index and working tree (native conflicts
    // leave the Reverting/CherryPicking state), with the message waiting in MERGE_MSG.
    void revertOrPick(const core::Oid& commit, bool revert, bool andCommit);
    // The inverse of the commit's change to `paths` or to the selected lines (`patch`, old -> new
    // as the commit's diff has them) onto HEAD (against the commit's first parent). With
    // `andCommit`: a new commit built in memory (text conflicts first-class, one Undo). Without:
    // not available yet.
    void revertChanges(const core::Oid& commit, const std::vector<std::string>& paths, const std::string& patch, bool andCommit);
    enum class MoveTo { Parent, Child, Active, WorkingTree, Discard };
    // Moves the commit's changes to selected files (`paths`) or selected lines (`patch`, old →
    // new as the commit's diff has them) to its parent, its child, the checked-out commit or
    // the working tree ("uncommit"); Discard rewrites the commit so it no longer
    // makes them (its descendants are rebased; published commits ask first).
    void moveChanges(const core::Oid& commit, MoveTo to, const std::vector<std::string>& paths, const std::string& patch);
    // Working tree files folded into `commit` (its descendants rebased; the files stay as they
    // are on disk).
    void absorb(const core::Oid& commit, const std::vector<std::string>& paths);
    // Moves (or copies) `commit` right after `anchor` (or before it) in a linear chain.
    void reorder(const core::Oid& commit, const core::Oid& anchor, bool after, bool copy);

    // ---- network (§4.8) -------------------------------------------------------------------
    void fetch(const std::string& remote, bool prune, bool tags); // remote "" = all
    // <remote>/HEAD, the remote's default branch: re-read from the remote (remove = false) or deleted.
    void pull(PullMode mode, bool autostash = false);
    void push(const std::string& remote, const std::string& localBranch, const std::string& remoteBranch,
        bool setUpstream, bool forceWithLease, bool tags = false);

    // ---- stash (§4.9) ---------------------------------------------------------------------
    void stashPush(const std::string& message, bool keepIndex, bool untracked, bool stagedOnly,
        const std::vector<std::string>& paths, Callback done = {});
    void stashApply(int index, bool pop, bool restoreIndex);
    void stashDrop(int index);
    // Rewrites the message of stash@{index} (a new stash commit, same content and position).
    void stashReword(int index, const core::Oid& commit, const std::string& message);
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

    // ---- native interactive rebase (§4.13 execution 2) ------------------------------------------
    struct NativeRebase {
        std::vector<std::string> args;   // after `git rebase -i --empty=<empty>`: options, upstream, branch
        std::string empty = "stop";      // --empty: keep, drop or stop (Ask)
        gg::native::Prepared prepared;   // the todo and typed messages for `git gg sequence-editor`
        bool updateRefs = false;         // the todo has update-ref rows (git >= 2.38)
        std::string tipRef;              // the branch (or HEAD) must still be at `tip`
        std::string tip;
        bool checkTip = false;
    };
    // `git rebase -i` with GIT_SEQUENCE_EDITOR / GIT_EDITOR = `git gg sequence-editor`. A stop
    // (edit, break, failing exec, conflicts, a commit that became empty) is not an error: the
    // result is "stopped" and the message says why.
    void nativeRebase(NativeRebase request, Callback done = {});
    // Stopped interactive rebase: `git commit --amend --no-edit` when something is staged, then
    // `git rebase --continue`.
    void amendAndContinue();
    // "Edit remaining todo": `git rebase --edit-todo` with the sequence editor writing `todo`, so
    // git-rebase-todo is written exactly as --edit-todo writes it. Refused when git's list is no
    // longer `expected` (the rebase moved on meanwhile). The typed messages are kept for later
    // Continue steps.
    void editRemainingTodo(std::string expected, std::string todo, std::map<std::string, std::string> messages,
        Callback done = {});

    // ---- undo (§5 U1) ---------------------------------------------------------------------
    void undo(bool redo);
    void restore(const std::string& operationId);

    // External tools (not journaled).
    void openInEditor(const std::string& path);
    void externalDiff(const std::string& path, const std::vector<std::string>& revs);
    void mergeTool(const std::string& path);

private:
    void handleDefault(const core::MutationFinishedEvent& event);
    // A step of a stopped rebase finished: stopping again shows git's message, errors a popup.
    void onRebaseStep(const core::MutationFinishedEvent& event);

    struct RewriteState;
    void rewritePrepare(const std::shared_ptr<RewriteState>& state);
    void rewriteDecide(const std::shared_ptr<RewriteState>& state);
    void rewriteApply(const std::shared_ptr<RewriteState>& state);

    Session& m_session;
    std::map<core::RequestId, Callback> m_callbacks;
    std::set<core::RequestId> m_networkRuns; // fetch, pull, push...: the remotes' tags may have changed
};

// `git rebase <args>` moving a stopped rebase on (ActionsRebase.cpp): git's editor gets the
// messages typed for this rebase; stopping again further on is not an error (ctx.info says why).
void rebaseStep(core::MutationContext& ctx, std::vector<std::string> args);

// Joins paths after "--" for a git command line.
std::vector<std::string> withPaths(std::vector<std::string> args, const std::vector<std::string>& paths);

} // namespace ggui
