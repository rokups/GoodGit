#include "libgg/Reconcile.hpp"

#include "libgg/Files.hpp"
#include "libgg/Journal.hpp"
#include "libgg/NativeRebase.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Process.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    ec = replaceFile(tmp, path);
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

// A reflog; by default this worktree's HEAD one (libgit2 reads a linked worktree's own logs/HEAD:
// HEAD is a per-worktree ref). Index 0 is the newest entry. A missing reflog has no entries.
struct HeadLog {
    gg::git2::Reflog log;
    size_t count = 0;
    explicit HeadLog(git_repository* repo, const char* name = "HEAD")
    {
        git_reflog* raw = nullptr;
        if (git_reflog_read(&raw, repo, name) == 0) {
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
    // ("rebase: fast-forward" is a step of a rebase too, when the picks fast-forward: it joins the
    // rebase in progress, and is an operation of its own outside one.)
    return action == "rebase" || action == "rebase finished"
        || (endsWith(action, ")") && (startsWith(action, "rebase") || startsWith(action, "pull")));
}

// The apply backend of older git (2.36) writes no "(start)" / "(finish)" / "(abort)": the start is
// "rebase: checkout <onto>", the picks "rebase: <subject>", the finish "rebase finished: returning
// to refs/heads/<branch>" and the abort "rebase: updating HEAD" (--skip writes nothing).
// "checkout ..." is a start only outside a rebase (inside one it is a pick whose subject begins so).
// (Every git writes "rebase: checkout <branch>" for git rebase <upstream> <branch> when the branch
// is up to date: a start that nothing ends, so what follows it in the same pass joins it.)
bool legacyStart(const std::string& msg) { return startsWith(msg, "rebase: checkout "); }

// "rebase: updating HEAD" is an abort unless the rebase goes on after it (then it is a pick with
// that subject): the next rebase entry of the pass is a start, or there is none and no rebase is
// in progress now.
bool legacyAbort(const std::vector<Entry>& entries, size_t i, bool rebasing)
{
    if (entries[i].msg != "rebase: updating HEAD")
        return false;
    for (size_t j = i + 1; j < entries.size(); ++j) {
        const std::string action = actionPart(entries[j].msg);
        if (isRebaseAction(action))
            return endsWith(action, " (start)") || legacyStart(entries[j].msg);
    }
    return !rebasing;
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

// `branch` is the short name of the branch whose own reflog the message comes from (else empty).
std::string labelFor(const std::string& msg, const std::string& branch = {})
{
    const std::string action = actionPart(msg);
    const std::string word = firstWord(action);
    if (startsWith(msg, "update by push"))
        return "git push";
    if (startsWith(msg, "WIP on ") || startsWith(msg, "On "))
        return "git stash"; // refs/stash
    if (startsWith(msg, "Branch: renamed"))
        return "git branch -m";
    if (startsWith(msg, "Branch: copied"))
        return "git branch -c";
    if (startsWith(msg, "branch: Created from"))
        return branch.empty() ? "git branch" : "git branch " + branch;
    if (startsWith(msg, "branch: Reset to"))
        return branch.empty() ? "git branch -f" : "git branch -f " + branch;
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
    std::vector<Entry> entries; // the HEAD entries this operation was made from (branch steps that match one join it)
    // A rebase sequence (start ... finish/abort of one git rebase, as far as the pass saw it).
    bool rebase = false;
    bool started = false;  // its "(start)" entry is in this pass
    bool finished = false; // its "(finish)" or "(abort)" entry is
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

// "Branch: renamed refs/heads/a to refs/heads/b" in b's reflog (the log of a is carried over,
// so the entry's old and new are equal and the history before it is a's). `git branch -c a b`
// copies the log the same way and appends "Branch: copied refs/heads/a to refs/heads/b". On the
// checked-out branch git also writes the rename to HEAD's reflog, as X -> 0 then 0 -> X.
bool isRenameMsg(const std::string& msg) { return startsWith(msg, "Branch: renamed "); }

bool isRenameInto(const std::string& msg, const std::string& ref)
{
    return isRenameMsg(msg) && endsWith(msg, " to " + ref);
}

bool isCopyInto(const std::string& msg, const std::string& ref)
{
    return startsWith(msg, "Branch: copied ") && endsWith(msg, " to " + ref);
}

// The source ref of a rename or copy message ("refs/heads/a"), else empty.
std::string renameSource(const std::string& msg)
{
    const size_t skip = startsWith(msg, "Branch: copied ") ? 15 : 16;
    const size_t to = msg.rfind(" to ");
    return (!isRenameMsg(msg) && !startsWith(msg, "Branch: copied ")) || to == std::string::npos || to < skip
        ? std::string()
        : msg.substr(skip, to - skip);
}

// "rebase (finish): refs/heads/feat onto <sha>" (also "rebase -i (finish): ..."): the entry git
// writes to the rebased branch's own reflog.
bool isRebaseFinishOf(const std::string& msg, const std::string& ref)
{
    const std::string action = actionPart(msg);
    return isRebaseAction(action) && endsWith(action, " (finish)")
        && startsWith(msg.substr(std::min(msg.size(), action.size() + 2)), ref + " onto ");
}

// The target ref of a rename or copy message.
std::string renameTarget(const std::string& msg)
{
    const size_t to = msg.rfind(" to ");
    return renameSource(msg).empty() || to == std::string::npos ? std::string() : msg.substr(to + 4);
}

// Turns the consumed entries into operations (see the plan: "HEAD value derivation"). `known`
// follows the walk: what the journal would say after each operation.
// `rebaseOpen`: the entries before the first rebase entry belong to a rebase that an earlier pass
// saw start (its operation is open): what git commands ran in it (an amend at an edit stop) are
// part of it.
std::vector<Pending> deriveOps(const std::vector<Entry>& entries, std::map<std::string, std::string>& known,
    const std::map<std::string, std::string>& current, const std::string& myHead, const std::string& zero,
    bool rebaseOpen, bool rebasing)
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
    bool inRebase = rebaseOpen; // between a rebase's (start) and its (finish) or (abort)
    std::string symAtStart; // the branch HEAD was on when the rebase in this pass started
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        const std::string action = actionPart(e.msg);
        const bool rebase = isRebaseAction(action);
        const bool start = rebase && (endsWith(action, " (start)") || (!inRebase && legacyStart(e.msg)));
        const bool abort = rebase && (endsWith(action, " (abort)") || legacyAbort(entries, i, rebasing));
        const bool end = rebase && (endsWith(action, " (finish)") || action == "rebase finished" || abort);
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
        // Renaming the checked-out branch: HEAD follows it (the entries X -> 0 and 0 -> X carry no
        // change of the branch itself; the branch's own reflog and deriveBranchOps have that).
        const bool renameEntry = isRenameMsg(e.msg);
        if (renameEntry && !sym.empty() && sym == renameSource(e.msg))
            sym = renameTarget(e.msg);
        const std::string headVal = sym.empty() ? e.newId : "ref:" + sym;

        Pending p;
        p.cmd = e.msg;
        p.label = labelFor(e.msg);
        p.time = e.time * 1000;
        p.prefix = rebase ? rebasePrefix(action) : std::string();
        p.rebase = rebase;
        p.started = start;
        p.finished = end;
        p.entries.push_back(e);
        if (headVal != prevHead)
            p.add(myHead, prevHead, headVal);
        if (!sym.empty()) {
            const bool startsBranch = checkout && (!known.count(sym) || known[sym] == zero) && current.count(sym);
            if (startsBranch)
                p.add(sym, zero, e.newId); // checkout -b / switch -c created the branch
            else if (rebase && (ret != std::string::npos || abort))
                p.add(sym, known.count(sym) ? known[sym] : zero, e.newId); // back on the (rebased) branch
            else if (!checkout && !start && !renameEntry)
                p.add(sym, e.oldId, e.newId);
        }
        for (const auto& c : p.changes)
            known[c.ref] = c.newValue;
        prevHead = headVal;

        // Every entry of a rebase in progress is part of it: its own steps, and what the user runs
        // at a stop (commit --amend, a commit resolving conflicts).
        if (start) {
            inRebase = true;
            joinable = -1; // a new rebase: its own operation
        } else if (rebase && (endsWith(action, ")") || end)) {
            inRebase = true; // a step of a rebase whose start this pass did not see ("rebase: fast-forward" has no parentheses: it only joins a rebase already open)
        }
        if (inRebase && joinable >= 0) {
            for (const auto& c : p.changes)
                ops[joinable].add(c.ref, c.oldValue, c.newValue);
            ops[joinable].entries.push_back(e);
            ops[joinable].finished = end;
        } else {
            p.rebase = inRebase;
            ops.push_back(std::move(p));
            joinable = inRebase ? static_cast<int>(ops.size()) - 1 : -1;
        }
        if (end) {
            inRebase = false;
            joinable = -1;
        }
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
    // Drop what changed nothing (stash's "reset: moving to HEAD", a rebase that ended where it
    // started). A rebase sequence stays: run() may have to finish the group it belongs to.
    std::vector<Pending> kept;
    for (auto& p : ops) {
        std::erase_if(p.changes, [](const journal::RefChange& c) { return c.oldValue == c.newValue; });
        if (!p.changes.empty() || p.rebase)
            kept.push_back(std::move(p));
    }
    return kept;
}

// ---- branch reflogs ---------------------------------------------------------------------------

constexpr size_t kMaxChain = 1000; // a longer chain is not worth reading: the lump stands for it

bool isBranchRef(const std::string& ref) { return startsWith(ref, "refs/heads/") || ref == "refs/stash"; }

// The reflog entries (oldest first) that take `ref` from `knownValue` to `actual`: the log is walked
// newest to oldest while it is chain-consistent (each entry's new is the younger one's old, the
// newest's new is the ref's value now) until an entry starts at knownValue. nullopt: the chain
// does not reach it (expired, deleted, rewritten by stash drop, too long).
std::optional<std::vector<Entry>> chainSteps(
    git_repository* repo, const std::string& ref, const std::string& knownValue, const std::string& actual)
{
    if (startsWith(actual, "ref:") || startsWith(knownValue, "ref:"))
        return std::nullopt;
    const HeadLog log(repo, ref.c_str());
    std::vector<Entry> steps;
    std::string expected = actual;
    for (size_t i = 0; i < log.count && i < kMaxChain; ++i) {
        Entry e = log.at(i);
        if (e.newId != expected)
            return std::nullopt;
        // (checked before "reached": a rename over a ref with the same value still starts here)
        if (isRenameInto(e.msg, ref) || isCopyInto(e.msg, ref)) {
            e.oldId = knownValue; // the ref was created (or replaced) by the rename or copy
            steps.push_back(std::move(e));
            std::reverse(steps.begin(), steps.end());
            return steps;
        }
        const bool reached = e.oldId == knownValue;
        expected = e.oldId;
        steps.push_back(std::move(e));
        if (reached) {
            std::reverse(steps.begin(), steps.end());
            return steps;
        }
    }
    return std::nullopt;
}

// Operations for the branches (refs/heads/*, refs/stash) that still differ from `known` after the
// HEAD-derived ops: one per reflog step (see chainSteps), or joined into the HEAD-derived op that
// is the same command (same old, new and message; a rename step by its message alone, since its
// HEAD entries carry the zero id). `known` is updated for the refs handled here; a
// ref whose chain does not reach its known value (and every deletion) stays for the lump.
std::vector<Pending> deriveBranchOps(git_repository* repo, std::vector<Pending>& headOps,
    std::map<std::string, std::string>& known, const std::map<std::string, std::string>& current, const std::string& zero)
{
    std::vector<Pending> out;
    std::vector<std::string> drifted;
    for (const auto& [ref, value] : known) {
        if (!isBranchRef(ref))
            continue;
        const auto it = current.find(ref);
        if (it != current.end() && it->second != value)
            drifted.push_back(ref); // (a deleted branch has no reflog: the lump)
    }
    for (const std::string& ref : drifted) {
        const std::string& actual = current.at(ref);
        const auto steps = chainSteps(repo, ref, known[ref], actual);
        if (!steps)
            continue;
        const std::string name = startsWith(ref, "refs/heads/") ? ref.substr(11) : std::string();
        for (const Entry& e : *steps) {
            if (e.oldId == e.newId)
                continue;
            // The same command: a rename by its message (HEAD's entries of it have other old/new
            // values), anything else by (old, new, msg).
            const bool rename = isRenameInto(e.msg, ref);
            auto joined = std::find_if(headOps.begin(), headOps.end(), [&](const Pending& p) {
                return std::any_of(p.entries.begin(), p.entries.end(), [&](const Entry& h) {
                    return rename ? h.msg == e.msg : h.oldId == e.oldId && h.newId == e.newId && h.msg == e.msg;
                });
            });
            // A rebase's own move of the branch: git writes it before HEAD's "(finish)" entry, so a
            // pass in between sees the move and the rebase without its finish. One operation, when
            // the branch ends where HEAD stands after the rebase's last step (not another
            // worktree's rebase).
            if (joined == headOps.end() && isRebaseFinishOf(e.msg, ref)) {
                const auto open = std::find_if(headOps.rbegin(), headOps.rend(), [&](const Pending& h) {
                    return h.rebase && !h.finished && !h.entries.empty() && h.entries.back().newId == e.newId;
                });
                if (open != headOps.rend())
                    joined = std::prev(open.base());
            }
            Pending fresh;
            Pending* p = &fresh;
            if (joined != headOps.end())
                p = &*joined;
            else {
                fresh.cmd = e.msg;
                fresh.label = labelFor(e.msg, name);
                fresh.time = e.time * 1000;
            }
            p->add(ref, e.oldId, e.newId);
            if (rename) {
                // One operation: a deleted, b created.
                const std::string source = renameSource(e.msg);
                const auto src = known.find(source);
                if (!source.empty() && source != ref && src != known.end() && src->second != zero && !current.count(source)) {
                    p->add(source, src->second, zero);
                    src->second = zero;
                }
            }
            if (joined != headOps.end())
                continue;
            out.push_back(std::move(fresh));
        }
        known[ref] = actual;
    }
    std::stable_sort(out.begin(), out.end(), [](const Pending& a, const Pending& b) { return a.time < b.time; });
    return out;
}

// HEAD-derived and branch operations in one list: by time, HEAD order kept, HEAD first within one
// second (timestamps have a 1 s granularity). A branch operation never comes before a HEAD
// operation that changed one of its refs, whatever the clocks say.
std::vector<Pending> mergeOps(std::vector<Pending> headOps, std::vector<Pending> branchOps)
{
    std::vector<Pending> merged;
    size_t h = 0;
    for (auto& b : branchOps) {
        size_t minIdx = 0;
        for (size_t i = h; i < headOps.size(); ++i)
            for (const auto& c : b.changes)
                if (std::any_of(headOps[i].changes.begin(), headOps[i].changes.end(),
                        [&](const journal::RefChange& r) { return r.ref == c.ref; }))
                    minIdx = i + 1;
        while (h < headOps.size() && (headOps[h].time <= b.time || h < minIdx))
            merged.push_back(std::move(headOps[h++]));
        merged.push_back(std::move(b));
    }
    while (h < headOps.size())
        merged.push_back(std::move(headOps[h++]));
    return merged;
}

// The label of the lump: what a remote-tracking ref's own reflog says about the change (a fetch, a
// push, a pull), else "external changes". At most three reflogs are read, however many refs changed.
std::string lumpLabel(git_repository* repo, const std::vector<journal::RefChange>& changes)
{
    const std::string zero = zeroId(repo);
    int tried = 0;
    for (const auto& c : changes) {
        if (!startsWith(c.ref, "refs/remotes/") || startsWith(c.newValue, "ref:") || c.newValue == zero)
            continue;
        if (++tried > 3)
            break;
        const HeadLog log(repo, c.ref.c_str());
        if (log.count == 0)
            continue;
        const Entry e = log.at(0);
        if (e.newId != c.newValue) // the log does not explain the value the ref has now
            continue;
        if (startsWith(e.msg, "fetch") || startsWith(e.msg, "update by push") || startsWith(e.msg, "pull"))
            return labelFor(e.msg);
    }
    return "external changes";
}

} // namespace

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

    // The newest journal change of every ref: an operation made from reflog entries that the
    // journal has recorded already (a cursor left behind by an unlocked update, or by a crash
    // between the append and the cursor) is not written twice.
    std::map<std::string, journal::RefChange> newest;
    for (const auto& op : ops)
        for (const auto& r : op.refs)
            newest[r.ref] = r;
    auto journaled = [&](const std::vector<journal::RefChange>& cs) {
        return !cs.empty() && std::all_of(cs.begin(), cs.end(), [&](const journal::RefChange& c) {
            const auto it = newest.find(c.ref);
            return it != newest.end() && it->second.oldValue == c.oldValue && it->second.newValue == c.newValue;
        });
    };
    auto noteFirst = [&](const std::string& id) {
        if (state.journal.empty()) { // the first operation of a new journal
            state.journal = id;
            identityChanged = true;
        }
    };
    const char* workdir = git_repository_workdir(repo);

    // One operation per plain git command found in HEAD's reflog since the cursor; a rebase, from
    // its start to its finish, is one operation over however many passes it takes (an open one
    // stays open, as ggui's does).
    const std::optional<Cursor> cursor = readCursor(state, myHead);
    bool cursorChanged = false;
    {
        const std::vector<Entry> entries = entriesSince(*headLog, cursor);
        // Does the pass continue a rebase an earlier pass opened? Yes while it is in progress; a
        // finished one only when the pass has its finish or abort (and no newer start before it):
        // a group without such evidence was quit, or its entries expired.
        const auto group = native::openGroup(repo);
        const bool rebasing = !native::rebaseIdentity(repo).empty();
        bool rebaseOpen = false;
        if (group) {
            rebaseOpen = group->active;
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::string action = actionPart(entries[i].msg);
                if (!isRebaseAction(action))
                    continue;
                if (endsWith(action, " (start)") || legacyStart(entries[i].msg))
                    break;
                if (endsWith(action, " (finish)") || endsWith(action, " (abort)") || action == "rebase finished"
                    || legacyAbort(entries, i, rebasing)) {
                    rebaseOpen = true;
                    break;
                }
            }
        }
        bool opened = false; // this pass opened a group whose rebase is already gone (see below)
        std::vector<Pending> headOps = deriveOps(entries, known, current, myHead, zero, rebaseOpen, rebasing);
        std::vector<Pending> branchOps = deriveBranchOps(repo, headOps, known, current, zero);
        const std::vector<Pending> derived = mergeOps(std::move(headOps), std::move(branchOps));
        for (const auto& p : derived) {
            const bool dup = journaled(p.changes); // (a finish is still carried out for its group)
            std::string err;
            auto fail = [&] {
                if (error)
                    *error = err;
                return result; // the cursor stays: the entries are consumed again next time
            };
            // The group this sequence belongs to: the open one while its rebase runs, or a finished
            // one when this pass saw its finish. Any other remembered group is stale (the rebase
            // was quit, or a newer one started): it ends here, without evidence of more.
            auto open = p.rebase ? native::openGroup(repo) : std::nullopt;
            if (open && !(open->active || (p.finished && !p.started))) {
                native::closeFinishedGroup(repo, t);
                open.reset();
            }
            if (open) {
                if (!p.changes.empty() && !dup) {
                    if (!t.appendRefs(open->op, p.changes, &err))
                        return fail();
                    for (const auto& c : p.changes)
                        newest[c.ref] = c;
                    ++result.appended;
                }
                if (p.finished) {
                    // A rebase ggui started, finished in a terminal: the index at its end joins the
                    // one ggui recorded at its start (plain git's own has none).
                    if (open->src != "git" && workdir)
                        t.appendIndex(open->op, journal::IndexChange{wt, "", indexTree(workdir), true}, &err);
                    native::finishGroup(repo, t, true);
                    ++result.appended; // (the op list changed: it ended)
                }
                continue;
            }
            if (dup)
                continue;
            // A rebase without its finish in this pass stays open, also when its directory is gone
            // already: git writes the finish entry before it removes the directory, so a pass that
            // read the reflog just before it has no finish yet but finds no rebase either. The
            // finish joins in the next pass; a quit rebase is ended by the pass after this one.
            const bool opens = p.rebase && !p.finished;
            if (p.changes.empty() && !(opens && rebasing))
                continue;
            journal::Operation op;
            op.id = journal::Journal::newOperationId();
            op.src = "git";
            op.label = p.label;
            op.cmd = p.cmd;
            op.wt = wt;
            op.time = p.time;
            if (!t.begin(op, &err) || !t.appendRefs(op.id, p.changes, &err))
                return fail();
            if (opens) {
                native::rememberGroup(repo, t, op.id, "git"); // stays open until the rebase ends
                opened = opened || !rebasing;
            } else if (!t.end(op.id, true, &err))
                return fail();
            for (const auto& c : p.changes)
                newest[c.ref] = c;
            ++result.appended;
            noteFirst(op.id);
        }
        // A remembered operation whose rebase is gone, with no reflog entry to say so (git rebase
        // --quit, an expired log): ends now. Not the group this pass opened without a rebase in
        // progress: its finish entry is for the next pass; that pass ends it when it has none.
        if (!opened)
            native::closeFinishedGroup(repo, t);
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
        op.label = lumpLabel(repo, changes);
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
