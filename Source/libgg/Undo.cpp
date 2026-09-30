#include "libgg/Undo.hpp"

#include "libgg/GitRunner.hpp"
#include "libgg/Journal.hpp"
#include "libgg/NativeRebase.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Reconcile.hpp"
#include "libgg/Thread.hpp"
#include "libgg/Worktrees.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <set>

namespace gg {

namespace fs = std::filesystem;

namespace {

bool isZero(const std::string& v) { return !v.empty() && v.find_first_not_of('0') == std::string::npos; }
bool isSymbolic(const std::string& v) { return v.rfind("ref:", 0) == 0; }

// Whether a plain git command line ("git -c x reset --hard HEAD~1") updates the working tree.
bool updatesWorktree(const std::string& cmd)
{
    std::istringstream words(cmd);
    std::vector<std::string> args;
    for (std::string w; words >> w;)
        args.push_back(w);
    size_t i = 0;
    if (i < args.size() && fs::path(args[i]).filename().string().rfind("git", 0) == 0)
        ++i;
    while (i < args.size() && args[i].rfind("-", 0) == 0)
        i += (args[i] == "-c" || args[i] == "-C") ? 2 : 1; // global options
    if (i >= args.size())
        return false;
    const std::string sub = args[i];
    static const std::set<std::string> worktreeCommands{"checkout", "switch", "merge", "rebase", "pull", "cherry-pick",
        "revert", "am", "stash"};
    if (worktreeCommands.count(sub))
        return true;
    if (sub == "reset")
        for (size_t k = i + 1; k < args.size(); ++k)
            if (args[k] == "--hard" || args[k] == "--merge" || args[k] == "--keep")
                return true;
    return false;
}

// Whether a plain git operation updated the working tree. The hooks of the old gg recorded the
// command line (ids "git-<pid>-<start>"); the reconciler records the reflog message ("checkout:
// moving from a to b"), and its action word says: commit never carries, the others do (the
// clean-tree check then decides, which also covers the reset modes).
bool updatesWorktree(const journal::Operation& op)
{
    if (op.id.rfind("git-", 0) == 0)
        return updatesWorktree(op.cmd);
    static const std::set<std::string> worktreeActions{"checkout", "rebase", "merge", "pull", "cherry-pick", "revert", "am",
        "reset"};
    return worktreeActions.count(reconcile::reflogActionWord(op.cmd)) > 0;
}

} // namespace

UndoResult undo(git_repository* repo, bool redo, const std::string& src, const std::string& targetId)
{
    assertNotUiThread("gg::undo");
    UndoResult result;
    const bool bare = git_repository_is_bare(repo) == 1;
    const fs::path cwd = bare ? fs::path(git_repository_path(repo)) : fs::path(git_repository_workdir(repo));
    journal::Journal journal{fs::path(git_repository_commondir(repo))};
    // Plain git since the last pass becomes journal operations first: Undo covers it too.
    {
        std::string ignored;
        reconcile::run(repo, &ignored);
    }
    // A native rebase finished where the hooks could not see it: its operation ends now.
    native::closeFinishedGroup(repo, journal);
    std::string readError;
    auto ops = journal.read(&readError);
    if (!readError.empty()) {
        result.error = readError;
        return result;
    }
    const std::string wt = worktreeKey(repo);
    const auto current = readRefValues(repo);
    const std::string zero = zeroId(repo);
    auto currentValue = [&](const std::string& ref) {
        auto it = current.find(ref);
        return it == current.end() ? zero : it->second;
    };
    journal::UndoPlan plan;
    if (targetId.empty()) {
        plan = journal::planUndo(ops, wt, redo, currentValue);
    } else {
        // Restore one specific operation: treat it as the newest candidate.
        std::vector<journal::Operation> only;
        for (const auto& op : ops)
            if (op.id == targetId)
                only.push_back(op);
        if (only.empty()) {
            result.error = "operation " + targetId + " not found in the journal";
            return result;
        }
        only.front().undoes.clear(); // restoring an undo = undoing it as a plain operation
        only.front().redo = false;
        only.front().ended = true;
        only.front().ok = true;
        plan = journal::planUndo(only, wt, false, currentValue);
        if (plan.target)
            plan.target = &*std::find_if(ops.begin(), ops.end(), [&](const auto& o) { return o.id == targetId; });
    }
    if (!plan.target) {
        result.nothing = true;
        result.error = plan.error;
        return result;
    }
    if (!plan.ok) {
        result.error = plan.error;
        return result;
    }
    result.target = plan.target->id;
    // Undo and redo are labelled after the original operation they act on, however many
    // undo/redo steps lie in between ("undo \"new commit\"", not "undo \"redo \"new commit\"\"").
    std::string what = plan.target->label;
    for (const journal::Operation* op = plan.target; op && !op->undoes.empty();) {
        const auto it = std::find_if(ops.begin(), ops.end(), [&](const journal::Operation& o) { return o.id == op->undoes; });
        op = it == ops.end() ? nullptr : &*it;
        if (op)
            what = op->label;
    }
    result.label = std::string(redo ? "redo" : "undo") + " \"" + what + "\"";
    // A rebase in progress has an open operation (ggui's, or the reconciler's for a plain git
    // rebase), which cannot be undone until its end; undoing what came before would pull refs out
    // from under git.
    if (!native::rebaseIdentity(repo).empty()) {
        result.error = "Cannot " + std::string(redo ? "redo" : "undo") + " \"" + what + "\": finish or abort the rebase first";
        return result;
    }

    // Plain git commands that updated the working tree (recorded without the
    // index): carry a clean index and working tree back along with HEAD, like a checkout would.
    // With local changes only the refs move (nothing is lost). A plain commit keeps its changes.
    if (!plan.index && !bare && plan.target->src == "git" && updatesWorktree(*plan.target)) {
        // A HEAD value: an id, or "ref:<branch>" followed to that branch's value now or after the
        // restore ("" when it has none).
        auto resolve = [&](const std::string& value, bool afterRestore) -> std::string {
            if (!isSymbolic(value))
                return value;
            const std::string name = trim(value.substr(4));
            std::string v = currentValue(name);
            if (afterRestore)
                for (const auto& c : plan.restore)
                    if (c.ref == name)
                        v = c.newValue;
            return isZero(v) ? std::string() : v;
        };
        const std::string headKey = journal::headKey(wt);
        std::string headNow = currentValue(headKey);
        std::string headAfter = headNow;
        for (const auto& c : plan.restore)
            if (c.ref == headKey)
                headAfter = c.newValue;
        const std::string from = resolve(headNow, false);
        const std::string to = resolve(headAfter, true);
        if (!from.empty() && !to.empty() && from != to) {
            const bool clean = git(cwd, {"diff", "--quiet", "HEAD"}).ok() && git(cwd, {"diff", "--quiet", "--cached", "HEAD"}).ok();
            if (clean) {
                journal::IndexChange carry;
                carry.wt = wt;
                carry.before = trim(git(cwd, {"rev-parse", from + "^{tree}"}).out);
                carry.after = trim(git(cwd, {"rev-parse", to + "^{tree}"}).out);
                carry.worktree = true;
                plan.index = carry; // (both are commits: their trees exist)
            }
        }
    }

    // Worktrees the operation added, removed, locked or unlocked: every opposite change must be
    // possible before anything is changed (a worktree that has changes or moved on is not removed).
    auto removesFirst = [](const journal::WorktreeChange& c) { return c.action == "remove" || c.action == "unlock"; };
    std::vector<std::string> restoredBranches;
    for (const auto& c : plan.restore)
        if (!isZero(c.newValue) && !isSymbolic(c.newValue))
            restoredBranches.push_back(c.ref);
    for (const auto& c : plan.worktrees)
        if (std::string why = worktrees::check(cwd, c, restoredBranches); !why.empty()) {
            result.error = "Cannot " + std::string(redo ? "redo" : "undo") + " \"" + what + "\": " + why;
            return result;
        }

    // A branch the restore deletes must not be checked out in a worktree (git refuses to delete
    // one too), unless that worktree is removed first by this very undo.
    const bool headRestored = std::any_of(plan.restore.begin(), plan.restore.end(),
        [&](const journal::RefChange& c) { return c.ref == journal::headKey(wt); });
    for (const auto& c : plan.restore) {
        if (!isZero(c.newValue) || c.ref.rfind("refs/heads/", 0) != 0)
            continue;
        for (const auto& e : worktrees::list(cwd)) {
            if (e.branch != c.ref || (headRestored && worktrees::samePath(e.path, cwd)))
                continue;
            const bool removed = std::any_of(plan.worktrees.begin(), plan.worktrees.end(), [&](const journal::WorktreeChange& w) {
                return w.action == "remove" && worktrees::samePath(w.path, e.path);
            });
            if (!removed) {
                result.error = "Cannot " + std::string(redo ? "redo" : "undo") + " \"" + what + "\": the branch "
                    + c.ref.substr(11) + " is checked out in " + e.path.string();
                return result;
            }
        }
    }

    OperationRecorder recorder(repo, src, result.label, !bare);
    recorder.setUndoes(plan.target->id, redo);
    recorder.begin();
    auto applyWorktrees = [&](bool removals) {
        for (const auto& c : plan.worktrees) {
            if (removesFirst(c) != removals)
                continue;
            worktrees::Applied a = worktrees::apply(cwd, c);
            if (!a.ok) {
                result.error = a.error;
                return false;
            }
            recorder.addWorktree(a.done);
        }
        return true;
    };
    // 0. Worktrees removed or unlocked first (a branch it had checked out may then be deleted).
    if (!applyWorktrees(true)) {
        recorder.finish(false);
        return result;
    }

    // 1. Index / working tree first: nothing is changed when the working tree would lose data.
    bool followWorktree = false;
    if (plan.index && !bare) {
        RunResult r;
        if (plan.index->worktree) {
            r = git(cwd, {"read-tree", "-m", "-u", plan.index->before, plan.index->after});
            followWorktree = true;
        } else {
            r = git(cwd, {"read-tree", plan.index->after});
        }
        if (!r.ok()) {
            result.wouldLoseData = plan.index->worktree;
            result.error = r.message();
            recorder.finish(false);
            return result;
        }
    }

    // A HEAD whose earlier value is unknown (recorded as the null id) cannot be restored.
    for (const auto& c : plan.restore)
        if (c.ref == journal::headKey(wt) && isZero(c.newValue)) {
            result.error = "the previous value of HEAD was not recorded; use the Reflog to go back";
            recorder.finish(false);
            if (plan.index && !bare) // nothing else changed yet except the index
                git(cwd, followWorktree ? std::vector<std::string>{"read-tree", "-m", "-u", plan.index->after, plan.index->before}
                                        : std::vector<std::string>{"read-tree", plan.index->before});
            return result;
        }

    // 2. Refs, verified against their current values, in one transaction.
    std::string input = "option no-deref\n";
    // Symbolic refs (HEAD, refs/remotes/<r>/HEAD, ...) going back to a target: set with
    // symbolic-ref after the transaction, each one on its own.
    std::vector<std::pair<std::string, std::string>> symbolics;
    // HEAD on a branch going back to a detached commit (a rebase that finished in a terminal moved
    // the branch too): git refuses one transaction that updates HEAD and its referent, so HEAD
    // is detached first, on its own, and put back on the branch should the rest fail.
    std::string detachTo, reattach;
    for (const auto& c : plan.restore) {
        const std::string ref = c.ref == journal::headKey(wt) ? std::string("HEAD") : c.ref;
        if (ref == "HEAD" && isSymbolic(c.oldValue) && !isSymbolic(c.newValue) && !isZero(c.newValue)) {
            detachTo = c.newValue;
            reattach = c.oldValue.substr(4);
            continue;
        }
        if (isSymbolic(c.newValue)) {
            if (c.oldValue != c.newValue)
                symbolics.emplace_back(ref, c.newValue.substr(4));
            continue;
        }
        const std::string old = isSymbolic(c.oldValue) ? std::string() : c.oldValue;
        if (isZero(c.newValue))
            input += "delete " + ref + (old.empty() ? "" : " " + old) + "\n";
        else if (isZero(c.oldValue))
            input += "create " + ref + " " + c.newValue + "\n";
        else
            input += "update " + ref + " " + c.newValue + (old.empty() ? "" : " " + old) + "\n";
    }
    RunResult r;
    if (!detachTo.empty())
        r = git(cwd, {"update-ref", "--no-deref", "-m", result.label, "HEAD", detachTo});
    if (detachTo.empty() || r.ok())
        r = git(cwd, {"update-ref", "--create-reflog", "-m", result.label, "--stdin"}, input);
    if (!r.ok() && !detachTo.empty())
        git(cwd, {"symbolic-ref", "HEAD", reattach});
    for (const auto& [ref, target] : symbolics) {
        if (!r.ok())
            break;
        r = git(cwd, {"symbolic-ref", "-m", result.label, ref, target});
    }
    if (!r.ok()) {
        // Put the index back the way it was.
        if (plan.index && !bare) {
            if (followWorktree)
                git(cwd, {"read-tree", "-m", "-u", plan.index->after, plan.index->before});
            else
                git(cwd, {"read-tree", plan.index->before});
        }
        result.error = r.message();
        recorder.finish(false);
        return result;
    }
    // 3. Worktrees added or locked last (a branch they check out exists again by now). When that
    //    fails, the refs go back to where they were, so the operation changed nothing it cannot show.
    if (!applyWorktrees(false)) {
        std::string back = "option no-deref\n";
        for (const auto& c : plan.restore) {
            if (c.ref == journal::headKey(wt) || isSymbolic(c.newValue) || isSymbolic(c.oldValue))
                continue;
            if (isZero(c.oldValue))
                back += "delete " + c.ref + " " + c.newValue + "\n";
            else if (isZero(c.newValue))
                back += "create " + c.ref + " " + c.oldValue + "\n";
            else
                back += "update " + c.ref + " " + c.oldValue + " " + c.newValue + "\n";
        }
        git(cwd, {"update-ref", "-m", result.label + " (failed)", "--stdin"}, back);
        if (!detachTo.empty())
            git(cwd, {"symbolic-ref", "HEAD", reattach}); // HEAD was detached first: back on its branch
        recorder.finish(false);
        return result;
    }
    recorder.finish(true, followWorktree);
    result.ok = true;
    return result;
}

} // namespace gg
