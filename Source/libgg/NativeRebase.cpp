#include "libgg/NativeRebase.hpp"

#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"
#include "libgg/Journal.hpp"
#include "libgg/Operation.hpp"
#include "libgg/Todo.hpp"

#include <git2.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace gg::native {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

struct Group {
    std::string op;
    std::string identity;
    std::string src;
};

std::optional<Group> readGroup(const fs::path& dir)
{
    const auto text = readFile(dir / "operation");
    if (!text)
        return std::nullopt;
    const auto lines = splitLines(*text);
    if (lines.empty() || lines[0].empty())
        return std::nullopt;
    // The third line is the opener's src; older state files (managed hooks of older versions: ids "git-<pid>-<start>") have none.
    std::string src = lines.size() > 2 ? lines[2] : std::string();
    if (src.empty())
        src = lines[0].rfind("git-", 0) == 0 ? "git" : "ggui";
    return Group{lines[0], lines.size() > 1 ? lines[1] : std::string(), src};
}

// Removes the state directory, and gg/rebase when no other worktree has one.
void removeState(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove(dir.parent_path(), ec); // only when empty
}

// Older git (2.36 at least) puts HEAD back on the rebased branch (finish, abort) without a ref
// transaction, so nothing reports it. When the rebase's operation recorded HEAD and HEAD is
// not where the journal last saw it, the move is recorded before the operation ends. Only when the
// finish was seen (post-rewrite, or ggui's own step): a rebase found finished later may have moved
// refs outside the journal, and Undo must refuse it.
void recordHeadNow(git_repository* repo, journal::Writer& journal, const std::string& op)
{
    std::string now;
    git_reference* head = nullptr;
    if (git_reference_lookup(&head, repo, "HEAD") != 0) {
        git_error_clear();
        return;
    }
    if (git_reference_type(head) == GIT_REFERENCE_SYMBOLIC)
        now = std::string("ref:") + git_reference_symbolic_target(head);
    else if (const git_oid* id = git_reference_target(head))
        now = git2::toHex(*id);
    git_reference_free(head);
    const std::string key = journal::headKey(worktreeKey(repo));
    for (const auto& o : journal.read())
        if (o.id == op)
            for (const auto& r : o.refs)
                if (r.ref == key && !now.empty() && r.newValue != now)
                    journal.appendRefs(op, {journal::RefChange{key, r.newValue, now}});
}

} // namespace

std::string rebaseIdentity(const fs::path& gitDir)
{
    // The merge backend keeps its state in rebase-merge/; the apply backend (git rebase --apply)
    // in rebase-apply/, where the "rebasing" marker tells it from git am ("applying").
    fs::path dir = gitDir / "rebase-merge";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        dir = gitDir / "rebase-apply";
        if (!fs::is_directory(dir, ec) || !fs::exists(dir / "rebasing", ec))
            return {};
    }
    std::string out;
    for (const char* name : {"orig-head", "onto", "head-name"}) {
        if (!out.empty())
            out += ' ';
        out += trim(readFile(dir / name).value_or(""));
    }
    return out;
}

std::string rebaseIdentity(git_repository* repo) { return rebaseIdentity(fs::path(git_repository_path(repo))); }

std::optional<std::vector<std::string>> replayedCommits(git_repository* repo)
{
    const fs::path dir = fs::path(git_repository_path(repo)) / "rebase-merge";
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return std::nullopt;
    std::vector<std::string> out;
    for (const char* name : {"done", "git-rebase-todo"}) {
        const auto rows = todo::replayedCommits(todo::parse(readFile(dir / name).value_or("")));
        out.insert(out.end(), rows.begin(), rows.end());
    }
    return out;
}

fs::path stateDir(git_repository* repo)
{
    return fs::path(git_repository_commondir(repo)) / "gg" / "rebase" / worktreeKey(repo);
}

std::string groupOperation(git_repository* repo)
{
    const auto group = readGroup(stateDir(repo));
    if (!group)
        return {};
    const std::string identity = rebaseIdentity(repo);
    return !identity.empty() && identity == group->identity ? group->op : std::string();
}

std::optional<GroupInfo> openGroup(git_repository* repo)
{
    const auto group = readGroup(stateDir(repo));
    if (!group)
        return std::nullopt;
    const std::string identity = rebaseIdentity(repo);
    return GroupInfo{group->op, group->src, !identity.empty() && identity == group->identity};
}

void rememberGroup(git_repository* repo, journal::Writer& journal, const std::string& op, const std::string& src)
{
    const fs::path dir = stateDir(repo);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (writeFile(dir / "operation", op + "\n" + rebaseIdentity(repo) + "\n" + src + "\n"))
        journal.markRebase(op);
}

void closeFinishedGroup(git_repository* repo, journal::Writer& journal)
{
    const fs::path dir = stateDir(repo);
    const auto group = readGroup(dir);
    const std::string identity = rebaseIdentity(repo);
    if (!group) {
        // A todo ggui prepared for a rebase that is gone (finished in a terminal) is left behind.
        if (const auto prepared = readPrepared(dir); prepared && !prepared->identity.empty() && prepared->identity != identity)
            removeState(dir);
        return;
    }
    if (!identity.empty() && identity == group->identity)
        return; // still in progress
    journal.end(group->op, true);
    removeState(dir);
}

void finishGroup(git_repository* repo, journal::Writer& journal, bool ok)
{
    const fs::path dir = stateDir(repo);
    if (const auto group = readGroup(dir)) {
        recordHeadNow(repo, journal, group->op);
        journal.end(group->op, ok);
    }
    removeState(dir);
}

bool writePrepared(const fs::path& dir, const Prepared& prepared, std::string& error)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    json j{{"todo", prepared.todo}, {"messages", prepared.messages}, {"identity", prepared.identity}};
    if (!writeFile(dir / "prepared.json", j.dump())) {
        error = "cannot write " + (dir / "prepared.json").string();
        return false;
    }
    return true;
}

std::optional<Prepared> readPrepared(const fs::path& dir)
{
    const auto text = readFile(dir / "prepared.json");
    if (!text)
        return std::nullopt;
    const json j = json::parse(*text, nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    Prepared p;
    p.todo = j.value("todo", std::string());
    p.identity = j.value("identity", std::string());
    if (j.contains("messages") && j["messages"].is_object())
        for (const auto& [commit, message] : j["messages"].items())
            if (message.is_string())
                p.messages[commit] = message.get<std::string>();
    return p;
}

void discardPrepared(git_repository* repo)
{
    const fs::path dir = stateDir(repo);
    std::error_code ec;
    fs::remove(dir / "prepared.json", ec);
    fs::remove(dir, ec);               // only when empty
    fs::remove(dir.parent_path(), ec); // likewise
}

std::optional<Prepared> preparedFor(git_repository* repo)
{
    auto p = readPrepared(stateDir(repo));
    const std::string identity = rebaseIdentity(repo);
    if (!p || identity.empty() || p->identity != identity)
        return std::nullopt;
    return p;
}

int sequenceEditor(const fs::path& file, std::string& error)
{
    const char* dir = std::getenv("GG_SEQUENCE_DIR");
    if (!dir || !*dir) {
        error = "no prepared todo: git gg sequence-editor is run by ggui's interactive rebase";
        return 1;
    }
    const auto prepared = readPrepared(dir);
    if (!prepared) {
        error = std::string("cannot read the prepared todo in ") + dir;
        return 1;
    }
    if (file.filename() == "git-rebase-todo") {
        if (prepared->todo.empty())
            return 0; // handed over already (Edit remaining todo): git's list as it is
        if (!writeFile(file, prepared->todo)) {
            error = "cannot write " + file.string();
            return 1;
        }
        return 0;
    }
    // git's editor for a commit message (COMMIT_EDITMSG in the worktree's git dir): the commit
    // being picked, squashed or reworded is the last line of rebase-merge/done.
    const auto done = readFile(file.parent_path() / "rebase-merge" / "done");
    if (!done)
        return 0;
    const todo::Todo list = todo::parse(*done);
    // (A merge row's commit: its message for `merge -c`.)
    if (list.items.empty() || list.items.back().commit.empty()
        || !(list.items.back().isCommit() || list.items.back().action == todo::Action::Merge))
        return 0;
    const std::string& current = list.items.back().commit;
    for (const auto& [commit, message] : prepared->messages) {
        if (!(commit.rfind(current, 0) == 0 || current.rfind(commit, 0) == 0))
            continue;
        if (!writeFile(file, message)) {
            error = "cannot write " + file.string();
            return 1;
        }
        return 0;
    }
    return 0; // no typed message: git's text stays
}

} // namespace gg::native
