#include "libgg/Reconcile.hpp"

#include "libgg/Journal.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Process.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

namespace gg::reconcile {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::int64_t kNoPidGraceMs = 10 * 60 * 1000;

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct State {
    json doc = json::object(); // kept whole so unknown keys survive a rewrite
    bool existed = false;
    std::map<std::string, std::string> baseline;
    std::string journal; // id of the journal's first operation: tells a deleted or replaced journal
};

State readState(const fs::path& path)
{
    State st;
    st.doc = json{{"v", 1}};
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return st;
    json doc = json::parse(in, nullptr, false);
    if (!doc.is_object() || doc.value("v", 0) != 1)
        return st; // corrupt or from the future: start over, like a missing file
    st.doc = std::move(doc);
    st.existed = true;
    if (auto it = st.doc.find("journal"); it != st.doc.end() && it->is_string())
        st.journal = it->get<std::string>();
    if (auto it = st.doc.find("baseline"); it != st.doc.end() && it->is_object())
        for (auto b = it->begin(); b != it->end(); ++b)
            if (b.value().is_string())
                st.baseline[b.key()] = b.value().get<std::string>();
    return st;
}

bool writeState(const fs::path& dir, const fs::path& path, const State& st)
{
    json doc = st.doc;
    doc["v"] = 1;
    json base = json::object();
    for (const auto& [ref, value] : st.baseline)
        base[ref] = value;
    doc["baseline"] = std::move(base);
    doc["journal"] = st.journal;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path tmp = dir / ("reconcile.json.tmp" + std::to_string(selfProcess().pid));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << doc.dump() << "\n";
        out.flush();
        if (!out)
            return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// The journal operation of someone else that is still being written: its refs are not recorded
// yet, so a snapshot taken now would attribute its changes to "external changes".
bool blocksReconcile(const journal::Operation& op, std::int64_t now)
{
    if (op.ended || op.src == "git" || op.spansRebase)
        return false;
    // Our own open operation (this thread is inside an OperationRecorder) blocks too: its changes
    // are recorded by its finish(), not here.
    if (op.pid != 0)
        return processAlive(op.pid, op.pstart);
    return now - op.time < kNoPidGraceMs;
}

} // namespace

bool hooksInstalled(git_repository* repo)
{
    gg::git2::Config cfg = gg::git2::repositoryConfig(repo);
    if (gg::git2::configString(cfg.get(), "hook.ggui-reference-transaction.command").has_value())
        return true;
    const fs::path common = git_repository_commondir(repo);
    fs::path hooksDir = common / "hooks";
    if (auto p = gg::git2::configString(cfg.get(), "core.hooksPath"))
        hooksDir = fs::path(*p);
    std::error_code ec;
    return fs::exists(hooksDir / "reference-transaction.gg-previous", ec) || fs::exists(common / "gg" / "hooks" / "run", ec);
}

Result run(git_repository* repo, std::string* error)
{
    Result result;
    if (hooksInstalled(repo)) {
        result.skipped = true;
        return result;
    }
    const fs::path common = git_repository_commondir(repo);
    journal::Journal j{common};
    journal::Journal::Transaction t(j);
    if (!t.locked()) {
        result.skipped = true;
        return result;
    }
    std::string readError;
    const auto ops = t.read(&readError);
    if (!readError.empty()) {
        if (error)
            *error = readError;
        result.skipped = true;
        return result;
    }
    const std::int64_t now = nowMs();
    for (const auto& op : ops)
        if (blocksReconcile(op, now)) {
            result.deferred = true;
            return result;
        }

    const fs::path statePath = j.dir() / "reconcile.json";
    State state = readState(statePath);
    bool firstRun = !state.existed;
    // The baseline only grows on the first run and when a worktree's HEAD is first seen, so most
    // passes never rewrite the file.
    bool baselineGrew = false;
    // A journal that is not the one the baseline was made for (deleted, or replaced) makes every
    // baseline value stale: start over from what the refs are now. An empty journal alone is not
    // a reset (a repository that ggui never journaled in has none and must still see changes).
    bool identityChanged = false;
    const std::string front = ops.empty() ? std::string() : ops.front().id;
    if (state.existed && !state.journal.empty() && state.journal != front) {
        state.baseline.clear();
        firstRun = true;
        state.journal = front;
        identityChanged = true;
    } else if (state.journal.empty() && !front.empty()) {
        state.journal = front;
        identityChanged = true;
    }

    const std::string wt = worktreeKey(repo);
    const std::string myHead = journal::headKey(wt);
    // Only refs readRefValues reports are compared: another worktree's HEAD in the journal is not
    // ours to judge (and is never "deleted").
    auto relevant = [&](const std::string& ref) {
        return ref == myHead || (ref.rfind("refs/", 0) == 0 && ref.rfind("refs/gg/", 0) != 0);
    };

    const std::map<std::string, std::string> current = readRefValues(repo);
    const std::string zero = zeroId(repo);

    std::map<std::string, std::string> known;
    for (const auto& op : ops)
        for (const auto& r : op.refs)
            if (relevant(r.ref))
                known[r.ref] = r.newValue;
    for (const auto& [ref, value] : state.baseline)
        if (relevant(ref) && !known.count(ref))
            known[ref] = value;
    for (const auto& [ref, value] : current) {
        if (known.count(ref))
            continue;
        if (firstRun || ref == myHead) {
            // Never seen before, and no state to say otherwise: history is not replayed. The same
            // for this worktree's HEAD the first time it is looked at (a linked worktree that was
            // never opened here): HEAD is never "created".
            state.baseline[ref] = value;
            known[ref] = value;
            baselineGrew = true;
        } else {
            known[ref] = zero; // created since the last pass
        }
    }

    std::vector<journal::RefChange> changes;
    for (const auto& [ref, value] : known) {
        auto it = current.find(ref);
        const std::string actual = it == current.end() ? zero : it->second;
        if (value != actual)
            changes.push_back(journal::RefChange{ref, value, actual});
    }

    if (!changes.empty()) {
        journal::Operation op;
        op.id = journal::Journal::newOperationId();
        op.src = "git";
        op.label = "external changes";
        op.wt = wt;
        op.time = now;
        std::string err;
        const bool ok = t.begin(op, &err) && t.appendRefs(op.id, changes, &err) && t.end(op.id, true, &err);
        if (!ok) {
            if (error)
                *error = err;
            return result;
        }
        result.appended = 1;
        if (state.journal.empty()) { // the first operation of a new journal
            state.journal = op.id;
            identityChanged = true;
        }
    }
    if (firstRun || baselineGrew || identityChanged) {
        // A state that cannot be written makes every later pass a first run (refs created since are
        // then not seen as created): reported, never silent.
        if (!writeState(j.dir(), statePath, state) && error)
            *error = "cannot write " + statePath.string();
    }
    return result;
}

} // namespace gg::reconcile
