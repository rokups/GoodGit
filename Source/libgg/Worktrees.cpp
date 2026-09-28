#include "libgg/Worktrees.hpp"

#include "libgg/GitRunner.hpp"

#include <algorithm>
#include <cctype>

namespace gg::worktrees {

namespace fs = std::filesystem;

std::vector<Entry> parse(const std::string& text)
{
    std::vector<Entry> entries;
    Entry current;
    bool open = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\0', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string field = text.substr(start, end - start);
        start = end + 1;
        if (field.empty()) { // end of a record
            if (open)
                entries.push_back(std::move(current));
            current = Entry{};
            open = false;
            continue;
        }
        const size_t space = field.find(' ');
        const std::string key = field.substr(0, space);
        const std::string value = space == std::string::npos ? std::string() : field.substr(space + 1);
        if (key == "worktree") {
            if (open)
                entries.push_back(std::move(current));
            current = Entry{};
            current.path = fs::path(value);
            current.main = entries.empty();
            open = true;
        } else if (key == "HEAD") {
            current.head = value;
        } else if (key == "branch") {
            current.branch = value;
        } else if (key == "detached") {
            current.detached = true;
        } else if (key == "bare") {
            current.bare = true;
        } else if (key == "locked") {
            current.locked = true;
            current.lockReason = value;
        } else if (key == "prunable") {
            current.prunable = true;
            current.prunableReason = value;
        }
    }
    if (open)
        entries.push_back(std::move(current));
    return entries;
}

std::vector<Entry> list(const fs::path& cwd, std::string* error)
{
    const RunResult r = git(cwd, {"worktree", "list", "--porcelain", "-z"});
    if (!r.ok()) {
        if (error)
            *error = r.message();
        return {};
    }
    return parse(r.out);
}

bool samePath(const fs::path& a, const fs::path& b)
{
    std::error_code ec1, ec2;
    fs::path ca = fs::weakly_canonical(a, ec1);
    fs::path cb = fs::weakly_canonical(b, ec2);
    if (ec1)
        ca = a.lexically_normal();
    if (ec2)
        cb = b.lexically_normal();
    std::string sa = ca.generic_string();
    std::string sb = cb.generic_string();
    while (sa.size() > 1 && sa.back() == '/')
        sa.pop_back();
    while (sb.size() > 1 && sb.back() == '/')
        sb.pop_back();
#ifdef _WIN32
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    return lower(sa) == lower(sb);
#else
    return sa == sb;
#endif
}

const Entry* find(const std::vector<Entry>& entries, const fs::path& path)
{
    for (const auto& e : entries)
        if (samePath(e.path, path))
            return &e;
    return nullptr;
}

journal::WorktreeChange describe(const Entry& e, const std::string& action)
{
    journal::WorktreeChange c;
    c.action = action;
    c.path = e.path.string();
    if (action == "add" || action == "remove") {
        c.head = e.head;
        c.branch = e.branch;
        c.locked = e.locked;
    }
    if (e.locked)
        c.reason = e.lockReason;
    return c;
}

namespace {

std::string shortBranch(const std::string& ref)
{
    return ref.rfind("refs/heads/", 0) == 0 ? ref.substr(11) : ref;
}

bool emptyOrMissing(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec))
        return true;
    return fs::is_directory(dir, ec) && fs::is_empty(dir, ec);
}

} // namespace

std::string check(const fs::path& cwd, const journal::WorktreeChange& c, const std::vector<std::string>& restoredBranches)
{
    std::string error;
    const auto entries = list(cwd, &error);
    if (!error.empty())
        return error;
    const Entry* e = find(entries, c.path);
    if (c.action == "add") {
        if (e)
            return "a worktree is already registered at " + c.path;
        if (!emptyOrMissing(c.path))
            return c.path + " already exists and is not empty";
        if (!c.branch.empty()) {
            const bool restored = std::find(restoredBranches.begin(), restoredBranches.end(), c.branch) != restoredBranches.end();
            if (!restored && !git(cwd, {"rev-parse", "--verify", "-q", c.branch}).ok())
                return "the branch " + shortBranch(c.branch) + " no longer exists";
            for (const auto& other : entries)
                if (other.branch == c.branch)
                    return "the branch " + shortBranch(c.branch) + " is checked out in " + other.path.string();
        } else if (c.head.empty() || !git(cwd, {"cat-file", "-e", c.head + "^{commit}"}).ok()) {
            return "commit " + c.head.substr(0, 10) + " no longer exists";
        }
        return {};
    }
    if (!e)
        return "the worktree " + c.path + " is no longer registered";
    if (e->main)
        return "the main worktree cannot be changed";
    if (c.action == "remove") {
        if (samePath(e->path, cwd))
            return "the worktree " + c.path + " is the one Undo runs in";
        if (!c.branch.empty() ? e->branch != c.branch : (!e->detached || e->head != c.head))
            return "the worktree " + c.path + " has moved on (HEAD is now "
                + (e->branch.empty() ? e->head.substr(0, 10) : shortBranch(e->branch)) + ")";
        std::error_code ec;
        if (fs::exists(e->path, ec)) {
            const RunResult st = git(e->path, {"status", "--porcelain", "-z", "--untracked-files=all"});
            if (!st.ok())
                return st.message();
            if (!st.out.empty())
                return "the worktree " + c.path + " has uncommitted changes or untracked files";
        }
        return {};
    }
    if (c.action == "lock")
        return e->locked ? "the worktree " + c.path + " is locked already" : std::string();
    if (c.action == "unlock")
        return e->locked ? std::string() : "the worktree " + c.path + " is not locked";
    return "unknown worktree change \"" + c.action + "\"";
}

Applied apply(const fs::path& cwd, const journal::WorktreeChange& c)
{
    Applied result;
    auto fail = [&](const RunResult& r) {
        result.error = r.message();
        return result;
    };
    if (c.action == "add") {
        std::vector<std::string> args{"worktree", "add", "-q"};
        if (c.locked) {
            args.emplace_back("--lock");
            if (!c.reason.empty()) {
                args.emplace_back("--reason");
                args.push_back(c.reason);
            }
        }
        if (c.branch.empty()) {
            args.emplace_back("--detach");
            args.push_back(c.path);
            args.push_back(c.head);
        } else {
            args.push_back(c.path);
            args.push_back(shortBranch(c.branch));
        }
        const RunResult r = git(cwd, args);
        if (!r.ok())
            return fail(r);
        const auto entries = list(cwd);
        const Entry* e = find(entries, c.path);
        result.done = e ? describe(*e, "add") : c;
        result.ok = true;
        return result;
    }
    const auto entries = list(cwd, &result.error);
    const Entry* e = find(entries, c.path);
    if (!e) {
        if (result.error.empty())
            result.error = "the worktree " + c.path + " is no longer registered";
        return result;
    }
    const std::string path = e->path.string();
    if (c.action == "remove") {
        result.done = describe(*e, "remove");
        if (e->locked) {
            const RunResult u = git(cwd, {"worktree", "unlock", path});
            if (!u.ok())
                return fail(u);
        }
        const RunResult r = git(cwd, {"worktree", "remove", path});
        if (!r.ok()) {
            if (e->locked)
                git(cwd, e->lockReason.empty() ? std::vector<std::string>{"worktree", "lock", path}
                                               : std::vector<std::string>{"worktree", "lock", "--reason", e->lockReason, path});
            return fail(r);
        }
        result.ok = true;
        return result;
    }
    if (c.action == "lock") {
        const RunResult r = git(cwd, c.reason.empty() ? std::vector<std::string>{"worktree", "lock", path}
                                                      : std::vector<std::string>{"worktree", "lock", "--reason", c.reason, path});
        if (!r.ok())
            return fail(r);
        result.done = journal::WorktreeChange{"lock", path, {}, {}, false, c.reason};
        result.ok = true;
        return result;
    }
    if (c.action == "unlock") {
        const RunResult r = git(cwd, {"worktree", "unlock", path});
        if (!r.ok())
            return fail(r);
        result.done = journal::WorktreeChange{"unlock", path, {}, {}, false, e->lockReason};
        result.ok = true;
        return result;
    }
    result.error = "unknown worktree change \"" + c.action + "\"";
    return result;
}

} // namespace gg::worktrees
