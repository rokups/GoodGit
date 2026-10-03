#include "libgg/Journal.hpp"

#include "libgg/Keep.hpp"
#include "libgg/Process.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gg::journal {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool isHeadKey(const std::string& ref)
{
    return ref == "HEAD" || (ref.rfind("worktrees/", 0) == 0 && ref.size() > 5 && ref.compare(ref.size() - 5, 5, "/HEAD") == 0);
}

// Git-style lock: create the lock file exclusively; retry for up to 1 s; remove stale locks.
class Lock {
public:
    explicit Lock(const fs::path& path) : m_path(path)
    {
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(1);
        for (;;) {
#ifdef _WIN32
            HANDLE h = CreateFileW(m_path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                m_locked = true;
                return;
            }
#else
            const int fd = ::open(m_path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
            if (fd >= 0) {
                ::close(fd);
                m_locked = true;
                return;
            }
#endif
            std::error_code ec;
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(m_path, ec);
            if (!ec && age > minutes(10)) {
                fs::remove(m_path, ec); // stale
                continue;
            }
            if (steady_clock::now() > deadline)
                return;
            std::this_thread::sleep_for(milliseconds(10));
        }
    }
    ~Lock()
    {
        if (m_locked) {
            std::error_code ec;
            fs::remove(m_path, ec);
        }
    }
    bool locked() const { return m_locked; }

private:
    fs::path m_path;
    bool m_locked = false;
};

void mergeRefs(Operation& op, const json& updates)
{
    if (!updates.is_array())
        return;
    for (const auto& u : updates) {
        if (!u.is_array() || u.size() != 3 || !u[0].is_string() || !u[1].is_string() || !u[2].is_string())
            continue;
        const std::string ref = u[0].get<std::string>();
        auto it = std::find_if(op.refs.begin(), op.refs.end(), [&](const RefChange& c) { return c.ref == ref; });
        if (it == op.refs.end())
            op.refs.push_back(RefChange{ref, u[1].get<std::string>(), u[2].get<std::string>()});
        else
            it->newValue = u[2].get<std::string>();
    }
}

} // namespace

Journal::Journal(fs::path commonDir) : m_dir(std::move(commonDir) / "gg"), m_path(m_dir / "journal") { }

std::string Journal::newOperationId()
{
    static std::mt19937_64 rng(std::random_device{}() ^ static_cast<std::uint64_t>(nowMs()));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(rng() & 0xffffffffu));
    return std::to_string(nowMs()) + "-" + buf;
}

namespace {

// Appends `line` to the journal file; the caller holds the lock.
bool writeLocked(const fs::path& path, const std::string& line, std::string* error)
{
    std::error_code ec;
    const bool fresh = !fs::exists(path, ec) || fs::file_size(path, ec) == 0;
    bool needNewline = false;
    if (!fresh) {
        std::ifstream in(path, std::ios::binary);
        in.seekg(-1, std::ios::end);
        char last = '\n';
        in.get(last);
        needNewline = last != '\n';
    }
    std::string text;
    if (fresh)
        text += json{{"gg-journal", kFormatVersion}}.dump() + "\n";
    if (needNewline)
        text += "\n"; // a torn line never merges with a new record
    text += line + "\n";
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        if (error)
            *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::string busyMessage(const fs::path& dir)
{
    return "journal busy (" + (dir / "journal.lock").string() + " exists)";
}

} // namespace

bool Journal::appendLine(const std::string& line, std::string* error)
{
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    Lock lock(m_dir / "journal.lock");
    if (!lock.locked()) {
        if (error)
            *error = busyMessage(m_dir);
        return false;
    }
    return writeLocked(m_path, line, error);
}

struct Journal::Transaction::Held {
    explicit Held(const fs::path& lockPath) : lock(lockPath) { }
    Lock lock;
};

Journal::Transaction::Transaction(const Journal& journal) : m_journal(journal)
{
    std::error_code ec;
    fs::create_directories(journal.m_dir, ec);
    m_held = std::make_unique<Held>(journal.m_dir / "journal.lock");
}

Journal::Transaction::~Transaction() = default;

bool Journal::Transaction::locked() const { return m_held && m_held->lock.locked(); }

std::vector<Operation> Journal::Transaction::read(std::string* error, size_t* skipped) const
{
    return m_journal.read(error, skipped); // reading never takes the lock
}

bool Journal::Transaction::appendLine(const std::string& line, std::string* error)
{
    if (!locked()) {
        if (error)
            *error = busyMessage(m_journal.m_dir);
        return false;
    }
    return writeLocked(m_journal.m_path, line, error);
}

bool Writer::begin(const Operation& op, std::string* error)
{
    json j{{"v", 1}, {"t", "begin"}, {"op", op.id}, {"src", op.src}, {"label", op.label},
        {"time", op.time ? op.time : nowMs()}, {"wt", op.wt}};
    // The writer: lets a later reader tell an operation whose process is gone from a live one.
    const ProcessInfo self = selfProcess();
    j["pid"] = op.pid ? op.pid : static_cast<std::int64_t>(self.pid);
    j["pstart"] = op.pid ? op.pstart : self.start;
    if (!op.undoes.empty())
        j["undoes"] = op.undoes;
    if (op.redo)
        j["redo"] = true;
    if (!op.cmd.empty())
        j["cmd"] = op.cmd;
    return appendLine(j.dump(), error);
}

bool Writer::appendRefs(const std::string& id, const std::vector<RefChange>& changes, std::string* error)
{
    if (changes.empty())
        return true;
    json u = json::array();
    for (const auto& c : changes)
        u.push_back(json::array({c.ref, c.oldValue, c.newValue}));
    return appendLine(json{{"v", 1}, {"t", "refs"}, {"op", id}, {"u", u}}.dump(), error);
}

bool Writer::appendIndex(const std::string& id, const IndexChange& c, std::string* error)
{
    json j{{"v", 1}, {"t", "index"}, {"op", id}, {"wt", c.wt}, {"before", c.before}, {"after", c.after}};
    if (c.worktree)
        j["worktree"] = true;
    return appendLine(j.dump(), error);
}

bool Writer::appendWorktree(const std::string& id, const WorktreeChange& c, std::string* error)
{
    json j{{"v", 1}, {"t", "worktree"}, {"op", id}, {"do", c.action}, {"path", c.path}};
    if (!c.head.empty())
        j["head"] = c.head;
    if (!c.branch.empty())
        j["branch"] = c.branch;
    if (c.locked)
        j["locked"] = true;
    if (!c.reason.empty())
        j["reason"] = c.reason;
    return appendLine(j.dump(), error);
}

WorktreeChange inverse(const WorktreeChange& c)
{
    static const std::map<std::string, std::string> opposite{
        {"add", "remove"}, {"remove", "add"}, {"lock", "unlock"}, {"unlock", "lock"}};
    WorktreeChange r = c;
    const auto it = opposite.find(c.action);
    r.action = it == opposite.end() ? std::string() : it->second;
    return r;
}

bool Writer::appendRewrites(const std::string& id, const std::vector<std::pair<std::string, std::string>>& map,
    std::string* error)
{
    if (map.empty())
        return true;
    json m = json::array();
    for (const auto& [a, b] : map)
        m.push_back(json::array({a, b}));
    return appendLine(json{{"v", 1}, {"t", "map"}, {"op", id}, {"m", m}}.dump(), error);
}

bool Writer::markRebase(const std::string& id, std::string* error)
{
    return appendLine(json{{"v", 1}, {"t", "rebase"}, {"op", id}}.dump(), error);
}

bool Writer::end(const std::string& id, bool ok, std::string* error)
{
    json j{{"v", 1}, {"t", "end"}, {"op", id}};
    if (!ok)
        j["ok"] = false;
    return appendLine(j.dump(), error);
}

std::vector<Operation> Journal::read(std::string* error, size_t* skipped) const
{
    std::vector<Operation> ops;
    std::map<std::string, size_t> byId;
    size_t bad = 0;
    std::ifstream in(m_path, std::ios::binary);
    if (!in) {
        if (skipped)
            *skipped = 0;
        return ops;
    }
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            ++bad;
            first = false;
            continue;
        }
        if (first && j.contains("gg-journal")) {
            first = false;
            const int version = j["gg-journal"].is_number_integer() ? j["gg-journal"].get<int>() : 0;
            if (version > kFormatVersion) {
                if (error)
                    *error = "journal written by a newer ggui (format " + std::to_string(version) + ")";
                return {};
            }
            continue;
        }
        first = false;
        if (!j.contains("t") || !j["t"].is_string() || !j.contains("op") || !j["op"].is_string()
            || (j.contains("v") && j["v"].is_number_integer() && j["v"].get<int>() > 1)) {
            ++bad;
            continue;
        }
        const std::string type = j["t"].get<std::string>();
        const std::string id = j["op"].get<std::string>();
        auto it = byId.find(id);
        if (type == "begin") {
            Operation op;
            op.id = id;
            op.src = j.value("src", std::string());
            op.label = j.value("label", std::string());
            op.wt = j.value("wt", std::string("main"));
            op.cmd = j.value("cmd", std::string());
            op.undoes = j.value("undoes", std::string());
            op.redo = j.value("redo", false);
            op.time = j.value("time", static_cast<std::int64_t>(0));
            op.pid = j.value("pid", static_cast<std::int64_t>(0));
            op.pstart = j.value("pstart", static_cast<std::uint64_t>(0));
            op.order = ops.size();
            byId[id] = ops.size();
            ops.push_back(std::move(op));
            continue;
        }
        if (it == byId.end()) {
            ++bad; // record for an operation whose begin was lost
            continue;
        }
        Operation& op = ops[it->second];
        if (type == "refs") {
            mergeRefs(op, j["u"]);
        } else if (type == "index") {
            IndexChange c;
            c.wt = j.value("wt", std::string("main"));
            c.before = j.value("before", std::string());
            c.after = j.value("after", std::string());
            c.worktree = j.value("worktree", false);
            auto ix = std::find_if(op.index.begin(), op.index.end(), [&](const IndexChange& x) { return x.wt == c.wt; });
            if (ix == op.index.end()) {
                op.index.push_back(c);
            } else {
                ix->after = c.after;
                ix->worktree = ix->worktree || c.worktree;
            }
        } else if (type == "map") {
            if (j["m"].is_array())
                for (const auto& p : j["m"])
                    if (p.is_array() && p.size() == 2 && p[0].is_string() && p[1].is_string())
                        op.rewrites.emplace_back(p[0].get<std::string>(), p[1].get<std::string>());
        } else if (type == "worktree") {
            WorktreeChange c;
            c.action = j.value("do", std::string());
            c.path = j.value("path", std::string());
            c.head = j.value("head", std::string());
            c.branch = j.value("branch", std::string());
            c.locked = j.value("locked", false);
            c.reason = j.value("reason", std::string());
            if (!inverse(c).action.empty() && !c.path.empty())
                op.worktrees.push_back(std::move(c));
        } else if (type == "rebase") {
            op.spansRebase = true;
        } else if (type == "end") {
            op.ended = true;
            op.ok = j.value("ok", true);
        }
        // Unknown record types are ignored (minor additions stay compatible).
    }
    // Operations opened for plain git commands (by the managed hooks of older versions) close when that git process is gone
    // (or after 10 minutes). One that spans a native rebase stays open until its end record.
    const std::int64_t now = nowMs();
    for (auto& op : ops) {
        if (op.ended || op.src != "git" || op.spansRebase)
            continue;
        long long pid = 0;
        unsigned long long start = 0;
        const bool parsed = std::sscanf(op.id.c_str(), "git-%lld-%llu", &pid, &start) == 2;
        if (!parsed || now - op.time > 10 * 60 * 1000 || !processAlive(pid, start))
            op.ended = true;
    }
    if (skipped)
        *skipped = bad;
    return ops;
}

bool Journal::hasOpenOperation(const std::string& id, size_t bytes) const
{
    std::ifstream in(m_path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    const size_t start = size > bytes ? size - bytes : 0;
    in.seekg(static_cast<std::streamoff>(start));
    std::string tail((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string key = "\"op\":\"" + id + "\"";
    // Any record of the operation means it was begun: its begin, or a later record when the begin
    // is further back (git before 2.51 fetches with one ref transaction per ref, so a big fetch
    // writes more than the tail). A process keeps its operation even after a post-* hook of an older version ended it.
    return tail.find(key) != std::string::npos;
}

std::string worktreeKeyForGitDir(const fs::path& gitDir, const fs::path& commonDir)
{
    const auto norm = [](fs::path p) {
        p = p.lexically_normal();
        std::string s = p.generic_string();
        while (s.size() > 1 && s.back() == '/')
            s.pop_back();
        return s;
    };
    if (norm(gitDir) == norm(commonDir))
        return "main";
    fs::path g = fs::path(norm(gitDir));
    return g.filename().string();
}

std::string headKey(const std::string& worktree)
{
    return worktree == "main" || worktree.empty() ? std::string("HEAD") : "worktrees/" + worktree + "/HEAD";
}

bool visibleFrom(const Operation& op, const std::string& wt)
{
    if (!op.worktrees.empty())
        return (op.wt.empty() ? std::string("main") : op.wt) == wt;
    const std::string head = headKey(wt);
    for (const auto& r : op.refs)
        if (r.ref == head || (!isHeadKey(r.ref) && !keep::isKeepRef(r.ref))) // (keep refs are repository-wide housekeeping)
            return true;
    for (const auto& i : op.index)
        if (i.wt == wt)
            return true;
    return false;
}

bool Operation::keepOnly() const
{
    return !refs.empty() && index.empty() && worktrees.empty() && undoes.empty()
        && std::all_of(refs.begin(), refs.end(), [](const RefChange& r) { return keep::isKeepRef(r.ref); });
}

bool writeKeepHousekeeping(Writer& writer, const std::string& wt, const std::vector<RefChange>& changes,
    std::string* error, std::string* idOut)
{
    Operation hk;
    hk.id = Journal::newOperationId();
    if (idOut)
        *idOut = hk.id;
    hk.src = "gg";
    hk.label = "keep refs";
    hk.wt = wt;
    return writer.begin(hk, error) && writer.appendRefs(hk.id, changes, error) && writer.end(hk.id, true, error);
}

// A ref value that means "no such ref" (empty, or the null id).
static bool noRef(const std::string& v)
{
    return v.find_first_not_of('0') == std::string::npos;
}

UndoPlan planUndo(const std::vector<Operation>& ops, const std::string& wt, bool redo,
    const std::function<std::string(const std::string& ref)>& current)
{
    UndoPlan plan;
    plan.redo = redo;
    // Which operations are currently undone: X is undone when an undo op targets it and that
    // undo op is not itself undone (by a redo).
    std::map<std::string, const Operation*> byId;
    for (const auto& op : ops)
        byId[op.id] = &op;
    std::map<std::string, bool> undone;
    // Walk from the end: an operation's "undone" state depends on later operations only.
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        if (it->undoes.empty() || !it->ended || !it->ok)
            continue;
        if (undone[it->id])
            continue;
        undone[it->undoes] = true;
    }
    const Operation* target = nullptr;
    if (!redo) {
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            if (!it->ended || !it->ok || it->isUndo() || undone[it->id] || !visibleFrom(*it, wt) || it->keepOnly())
                continue;
            if (!it->restorable())
                continue; // nothing to undo (e.g. a failed or no-op command)
            target = &*it;
            break;
        }
        if (!target) {
            plan.error = "Nothing to undo";
            return plan;
        }
    } else {
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            if (!it->ended || !it->ok || !visibleFrom(*it, wt) || it->keepOnly())
                continue;
            if (it->undoes.empty() && it->restorable())
                break; // a newer normal operation: nothing to redo
            if (it->isUndo() && !undone[it->id]) {
                target = &*it;
                break;
            }
        }
        if (!target) {
            plan.error = "Nothing to redo";
            return plan;
        }
    }
    plan.target = target;
    // Refuse when refs moved outside the journal.
    for (const auto& r : target->refs) {
        if (isHeadKey(r.ref) && r.ref != headKey(wt))
            continue; // another worktree's HEAD is not ours to restore
        if (r.oldValue == r.newValue)
            continue; // back where it started (HEAD during a rebase): nothing to restore
        if (keep::isKeepRef(r.ref)) {
            // Derived state (libgg/Keep.hpp): never a reason to refuse, never restored literally. What
            // the target created goes when it is still there; what it deleted is handed to the caller,
            // whose keep::maintain keeps it only if nothing else reaches it now.
            if (noRef(r.oldValue) && !noRef(r.newValue)) {
                const std::string now = current(r.ref);
                if (!noRef(now))
                    plan.restore.push_back(RefChange{r.ref, now, r.oldValue});
            } else if (!noRef(r.oldValue))
                plan.keepExtra.push_back(r.oldValue);
            continue;
        }
        const std::string now = current(r.ref);
        if (now != r.newValue)
            plan.movedRefs.push_back(r.ref);
        plan.restore.push_back(RefChange{r.ref, now, r.oldValue});
    }
    if (!plan.movedRefs.empty()) {
        std::string list;
        for (const auto& r : plan.movedRefs)
            list += (list.empty() ? "" : ", ") + r;
        plan.error = "Refs moved outside the journal since \"" + target->label + "\": " + list;
        return plan;
    }
    for (const auto& i : target->index)
        if (i.wt == wt && !i.before.empty() && !i.after.empty()) // (a step stopped at conflicts has no tree)
            plan.index = IndexChange{wt, i.after, i.before, i.worktree};
    for (auto it = target->worktrees.rbegin(); it != target->worktrees.rend(); ++it)
        plan.worktrees.push_back(inverse(*it));
    plan.ok = true;
    return plan;
}

} // namespace gg::journal
