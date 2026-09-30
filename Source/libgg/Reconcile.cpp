#include "libgg/Reconcile.hpp"

#include "libgg/Journal.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Process.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>

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
    st.doc = json{{"v", 1}, {"cursors", json::object()}};
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return st;
    json doc = json::parse(in, nullptr, false);
    if (!doc.is_object() || doc.value("v", 0) != 1)
        return st; // corrupt or from the future: start over, like a missing file
    st.doc = std::move(doc);
    st.existed = true;
    if (auto it = st.doc.find("cursors"); it == st.doc.end() || !it->is_object())
        st.doc["cursors"] = json::object();
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


// ---- HEAD reflog ------------------------------------------------------------------------------

struct Entry {
    std::string oldId, newId, msg;
    std::int64_t time = 0; // seconds
    bool operator==(const Entry& o) const { return oldId == o.oldId && newId == o.newId && time == o.time && msg == o.msg; }
};

// This worktree's HEAD reflog (libgit2 reads a linked worktree's own logs/HEAD: HEAD is a
// per-worktree ref). Index 0 is the newest entry.
struct HeadLog {
    gg::git2::Reflog log;
    size_t count = 0;
    explicit HeadLog(git_repository* repo)
    {
        git_reflog* raw = nullptr;
        if (git_reflog_read(&raw, repo, "HEAD") == 0) {
            log.reset(raw);
            count = git_reflog_entrycount(raw);
        } else {
            git_error_clear();
        }
    }
    Entry at(size_t i) const
    {
        const git_reflog_entry* e = git_reflog_entry_byindex(log.get(), i);
        Entry r;
        r.oldId = gg::git2::toHex(*git_reflog_entry_id_old(e));
        r.newId = gg::git2::toHex(*git_reflog_entry_id_new(e));
        if (const char* m = git_reflog_entry_message(e))
            r.msg = m;
        while (!r.msg.empty() && (r.msg.back() == '\n' || r.msg.back() == '\r'))
            r.msg.pop_back();
        if (const git_signature* c = git_reflog_entry_committer(e))
            r.time = static_cast<std::int64_t>(c->when.time);
        return r;
    }
    // The newest entry, as the cursor stores it. An empty (or missing) reflog is {"n":0}: every
    // entry that appears later is new.
    json tipJson() const
    {
        if (count == 0)
            return json{{"n", 0}};
        const Entry e = at(0);
        return json{{"n", count}, {"old", e.oldId}, {"new", e.newId}, {"time", e.time}, {"msg", e.msg}};
    }
};

struct Cursor {
    size_t n = 0;
    Entry tip;
};

std::optional<Cursor> readCursor(const State& st, const std::string& key)
{
    const auto c = st.doc["cursors"].find(key);
    if (c == st.doc["cursors"].end() || !c->is_object())
        return std::nullopt;
    Cursor r;
    if (c->contains("n") && (*c)["n"] == 0)
        return r; // an empty reflog
    if (!c->contains("n") || !(*c)["n"].is_number_unsigned() || !c->contains("old") || !c->contains("new")
        || !c->contains("time") || !c->contains("msg"))
        return std::nullopt;
    r.n = (*c)["n"].get<size_t>();
    if (!(*c)["old"].is_string() || !(*c)["new"].is_string() || !(*c)["msg"].is_string() || !(*c)["time"].is_number_integer())
        return std::nullopt;
    r.tip.oldId = (*c)["old"].get<std::string>();
    r.tip.newId = (*c)["new"].get<std::string>();
    r.tip.msg = (*c)["msg"].get<std::string>();
    r.tip.time = (*c)["time"].get<std::int64_t>();
    return r;
}

// Sets the cursor to the reflog's tip. True when it changed.
bool setCursor(State& st, const std::string& key, const HeadLog& log)
{
    json& cursors = st.doc["cursors"];
    const json tip = log.tipJson();
    const auto it = cursors.find(key);
    if (it != cursors.end() && *it == tip)
        return false;
    cursors[key] = tip;
    return true;
}

// The entries newer than the cursor, oldest first. Empty without a cursor, and when its entry is
// not found (expired, reflog rewritten or deleted): the snapshot diff handles those. The search
// starts where the cursor's entry would be if nothing expired and never at index 0 (unless the
// count is unchanged), so an identical older entry cannot be mistaken for it. A reflog shorter
// than the cursor's count has lost entries: no match either.
std::vector<Entry> entriesSince(const HeadLog& log, const std::optional<Cursor>& cursor)
{
    std::vector<Entry> out;
    if (!cursor || log.count == 0)
        return out;
    if (cursor->n == 0) { // the reflog was empty: all of it is new
        for (size_t k = log.count; k-- > 0;)
            out.push_back(log.at(k));
        return out;
    }
    if (log.count < cursor->n)
        return out; // entries expired: where the cursor's entry would be is unknown (snapshot fallback)
    const size_t start = log.count - cursor->n;
    for (size_t i = start; i < log.count; ++i) {
        if (log.at(i) == cursor->tip) {
            for (size_t k = i; k-- > 0;)
                out.push_back(log.at(k));
            return out;
        }
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }
bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// "rebase (pick): x" -> "rebase (pick)"; "merge feat: Fast-forward" -> "merge feat".
std::string actionPart(const std::string& msg)
{
    const size_t colon = msg.find(':');
    return colon == std::string::npos ? msg : msg.substr(0, colon);
}

// A rebase run by git: "rebase (start)", "rebase -i (pick)", "pull --rebase origin main (finish)".
bool isRebaseAction(const std::string& action)
{
    // ("rebase: fast-forward" is a step of a rebase too, when the picks fast-forward.)
    return action == "rebase" || (endsWith(action, ")") && (startsWith(action, "rebase") || startsWith(action, "pull")));
}

// The action without its "(step)": the key that groups the entries of one rebase.
std::string rebasePrefix(const std::string& action)
{
    const size_t p = action.rfind(" (");
    return p == std::string::npos ? action : action.substr(0, p);
}

std::string firstWord(const std::string& s)
{
    const size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos)
        return {};
    return s.substr(b, s.find(' ', b) - b);
}

std::string labelFor(const std::string& msg)
{
    const std::string action = actionPart(msg);
    const std::string word = firstWord(action);
    if (isRebaseAction(action))
        return "git rebase";
    if (word == "checkout") {
        const size_t to = msg.find(" to ");
        if (startsWith(msg, "checkout: moving from ") && to != std::string::npos)
            return "git checkout " + msg.substr(to + 4);
        return "git checkout";
    }
    if (word == "reset") {
        const std::string prefix = "reset: moving to ";
        return startsWith(msg, prefix) ? "git reset " + msg.substr(prefix.size()) : "git reset";
    }
    if (word == "merge")
        return "git " + action;
    if (word.empty())
        return "git";
    return "git " + word; // commit (amend) -> git commit, pull origin main -> git pull, cherry-pick, revert, ...
}

// One operation being built from reflog entries: per ref the first old and the last new value.
struct Pending {
    std::string cmd, label, prefix; // prefix: the rebase a joined entry must belong to
    std::int64_t time = 0;          // ms
    std::vector<journal::RefChange> changes;
    void add(const std::string& ref, const std::string& oldValue, const std::string& newValue)
    {
        for (auto& c : changes)
            if (c.ref == ref) {
                c.newValue = newValue;
                return;
            }
        changes.push_back(journal::RefChange{ref, oldValue, newValue});
    }
};

// Turns the consumed entries into operations (see the plan: "HEAD value derivation"). `known`
// follows the walk: what the journal would say after each operation.
std::vector<Pending> deriveOps(const std::vector<Entry>& entries, std::map<std::string, std::string>& known,
    const std::map<std::string, std::string>& current, const std::string& myHead, const std::string& zero)
{
    std::vector<Pending> ops;
    const std::string kHead = known.count(myHead) ? known[myHead] : std::string();
    std::string prevHead = kHead;
    std::string sym = startsWith(prevHead, "ref:") ? prevHead.substr(4) : std::string();
    auto exists = [&](const std::string& ref) {
        const auto k = known.find(ref);
        return current.count(ref) || (k != known.end() && k->second != zero);
    };
    int joinable = -1; // index of an unfinished rebase's operation
    std::string symAtStart; // the branch HEAD was on when the rebase in this pass started
    for (const auto& e : entries) {
        const std::string action = actionPart(e.msg);
        const bool rebase = isRebaseAction(action);
        const bool start = rebase && endsWith(action, " (start)");
        const bool abort = rebase && endsWith(action, " (abort)");
        const bool end = rebase && (endsWith(action, " (finish)") || abort);
        const bool checkout = action == "checkout";
        const std::string returning = "returning to refs/heads/";
        const size_t ret = e.msg.find(returning);
        if (checkout && startsWith(e.msg, "checkout: moving from ")) {
            const size_t to = e.msg.find(" to ");
            if (to != std::string::npos) {
                const std::string ref = "refs/heads/" + e.msg.substr(to + 4);
                sym = exists(ref) ? ref : std::string();
            }
        } else if (start) {
            symAtStart = sym;
            sym.clear();
        } else if (rebase && ret != std::string::npos) {
            sym = "refs/heads/" + e.msg.substr(ret + returning.size());
        } else if (abort) {
            sym = symAtStart; // "rebase (abort): updating HEAD" (older git) does not say where HEAD goes back to
        }
        const std::string headVal = sym.empty() ? e.newId : "ref:" + sym;

        Pending p;
        p.cmd = e.msg;
        p.label = labelFor(e.msg);
        p.time = e.time * 1000;
        p.prefix = rebase ? rebasePrefix(action) : std::string();
        if (headVal != prevHead)
            p.add(myHead, prevHead, headVal);
        if (!sym.empty()) {
            const bool startsBranch = checkout && (!known.count(sym) || known[sym] == zero) && current.count(sym);
            if (startsBranch)
                p.add(sym, zero, e.newId); // checkout -b / switch -c created the branch
            else if (rebase && (ret != std::string::npos || abort))
                p.add(sym, known.count(sym) ? known[sym] : zero, e.newId); // back on the (rebased) branch
            else if (!checkout && !start)
                p.add(sym, e.oldId, e.newId);
        }
        for (const auto& c : p.changes)
            known[c.ref] = c.newValue;
        prevHead = headVal;

        if (rebase && !start && joinable >= 0 && ops[joinable].prefix == p.prefix) {
            for (const auto& c : p.changes)
                ops[joinable].add(c.ref, c.oldValue, c.newValue);
        } else {
            ops.push_back(std::move(p));
            joinable = static_cast<int>(ops.size()) - 1;
        }
        if (!rebase || end)
            joinable = -1;
    }
    // The derivation must end where HEAD actually is: the last operation that moved HEAD says so.
    const auto actual = current.find(myHead);
    if (actual != current.end() && prevHead != actual->second)
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            auto c = std::find_if(it->changes.begin(), it->changes.end(), [&](const journal::RefChange& r) { return r.ref == myHead; });
            if (c != it->changes.end()) {
                c->newValue = actual->second;
                known[myHead] = actual->second;
                break;
            }
        }
    // Drop what changed nothing (stash's "reset: moving to HEAD", a rebase that ended where it started).
    std::vector<Pending> kept;
    for (auto& p : ops) {
        std::erase_if(p.changes, [](const journal::RefChange& c) { return c.oldValue == c.newValue; });
        if (!p.changes.empty())
            kept.push_back(std::move(p));
    }
    return kept;
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

std::string reflogActionWord(const std::string& message)
{
    return firstWord(actionPart(message));
}

bool advanceCursor(git_repository* repo, std::string* error)
{
    const fs::path common = git_repository_commondir(repo);
    journal::Journal j{common};
    // The lock keeps this read-modify-write apart from a reconcile pass of another process. When it
    // is busy for as long as Lock in Journal.cpp retries (about a second) the update is made without it: the recorder's operation is
    // still open, which keeps every reconcile pass out (Result::deferred), except for the window
    // of a native rebase group, where a stale cursor costs at most one "external" duplicate.
    journal::Journal::Transaction t(j);
    const fs::path statePath = j.dir() / "reconcile.json";
    State state = readState(statePath);
    if (!state.existed)
        return true; // the first pass takes the baseline and the cursor at once
    const HeadLog log(repo);
    if (!setCursor(state, journal::headKey(worktreeKey(repo)), log))
        return true;
    if (!writeState(j.dir(), statePath, state)) {
        if (error)
            *error = "cannot write " + statePath.string();
        return false;
    }
    return true;
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
        state.doc["cursors"] = json::object(); // no cursor: HEAD's reflog is not replayed either
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

    // The reflog is read around the refs: a plain git command running right now could otherwise
    // put an entry in one of the two reads and not in the other (an entry without its ref value
    // would be journaled twice). Tips that differ between the reads mean "try again".
    std::map<std::string, std::string> current;
    std::unique_ptr<HeadLog> headLog;
    bool stable = false;
    for (int attempt = 0; attempt < 3 && !stable; ++attempt) {
        HeadLog before(repo);
        current = readRefValues(repo);
        headLog = std::make_unique<HeadLog>(repo);
        stable = before.tipJson() == headLog->tipJson();
    }
    if (!stable) {
        // Git is writing right now: nothing is decided on a half-written picture. Its own change
        // events run the next pass.
        result.deferred = true;
        return result;
    }
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

    // One operation per plain git command found in HEAD's reflog since the cursor.
    const std::optional<Cursor> cursor = readCursor(state, myHead);
    bool cursorChanged = false;
    {
        const std::vector<Entry> entries = entriesSince(*headLog, cursor);
        const std::vector<Pending> derived = deriveOps(entries, known, current, myHead, zero);
        for (const auto& p : derived) {
            journal::Operation op;
            op.id = journal::Journal::newOperationId();
            op.src = "git";
            op.label = p.label;
            op.cmd = p.cmd;
            op.wt = wt;
            op.time = p.time;
            std::string err;
            const bool ok = t.begin(op, &err) && t.appendRefs(op.id, p.changes, &err) && t.end(op.id, true, &err);
            if (!ok) {
                if (error)
                    *error = err;
                return result; // the cursor stays: the entries are consumed again next time
            }
            ++result.appended;
            if (state.journal.empty()) {
                state.journal = op.id;
                identityChanged = true;
            }
        }
        cursorChanged = setCursor(state, myHead, *headLog);
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
        ++result.appended;
        if (state.journal.empty()) { // the first operation of a new journal
            state.journal = op.id;
            identityChanged = true;
        }
    }
    if (firstRun || baselineGrew || identityChanged || cursorChanged) {
        // A state that cannot be written makes every later pass a first run (refs created since are
        // then not seen as created): reported, never silent.
        if (!writeState(j.dir(), statePath, state) && error)
            *error = "cannot write " + statePath.string();
    }
    return result;
}

} // namespace gg::reconcile
