#include "libgg/Undo.hpp"

#include "libgg/GitRunner.hpp"
#include "libgg/Journal.hpp"
#include "libgg/NativeRebase.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Thread.hpp"

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

} // namespace

UndoResult undo(git_repository* repo, bool redo, const std::string& src, const std::string& targetId)
{
    assertNotUiThread("gg::undo");
    UndoResult result;
    const bool bare = git_repository_is_bare(repo) == 1;
    const fs::path cwd = bare ? fs::path(git_repository_path(repo)) : fs::path(git_repository_workdir(repo));
    journal::Journal journal{fs::path(git_repository_commondir(repo))};
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

    // Plain git commands that updated the working tree (recorded by the hooks, without the
    // index): carry a clean index and working tree back along with HEAD, like a checkout would.
    // With local changes only the refs move (nothing is lost). A plain commit keeps its changes.
    if (!plan.index && !bare && plan.target->src == "git" && updatesWorktree(plan.target->cmd)) {
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

    OperationRecorder recorder(repo, src, result.label, !bare);
    recorder.setUndoes(plan.target->id, redo);
    recorder.begin();

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
    std::string headSymbolic;
    for (const auto& c : plan.restore) {
        const std::string ref = c.ref == journal::headKey(wt) ? std::string("HEAD") : c.ref;
        if (isSymbolic(c.newValue)) {
            if (c.oldValue != c.newValue)
                headSymbolic = c.newValue.substr(4); // restored with symbolic-ref below
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
    RunResult r = git(cwd, {"update-ref", "--create-reflog", "-m", result.label, "--stdin"}, input);
    if (r.ok() && !headSymbolic.empty())
        r = git(cwd, {"symbolic-ref", "-m", result.label, "HEAD", headSymbolic});
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
    recorder.finish(true, followWorktree);
    result.ok = true;
    return result;
}

} // namespace gg
