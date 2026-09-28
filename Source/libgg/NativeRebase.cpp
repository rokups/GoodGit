#include "libgg/NativeRebase.hpp"

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
};

std::optional<Group> readGroup(const fs::path& dir)
{
    const auto text = readFile(dir / "operation");
    if (!text)
        return std::nullopt;
    const auto lines = splitLines(*text);
    if (lines.empty() || lines[0].empty())
        return std::nullopt;
    return Group{lines[0], lines.size() > 1 ? lines[1] : std::string()};
}

// Removes the state directory, and gg/rebase when no other worktree has one.
void removeState(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove(dir.parent_path(), ec); // only when empty
}

} // namespace

std::string rebaseIdentity(const fs::path& gitDir)
{
    const fs::path dir = gitDir / "rebase-merge";
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return {};
    std::string out;
    for (const char* name : {"orig-head", "onto", "head-name"}) {
        if (!out.empty())
            out += ' ';
        out += trim(readFile(dir / name).value_or(""));
    }
    return out;
}

std::string rebaseIdentity(git_repository* repo) { return rebaseIdentity(fs::path(git_repository_path(repo))); }

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

void rememberGroup(git_repository* repo, journal::Journal& journal, const std::string& op)
{
    const fs::path dir = stateDir(repo);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (writeFile(dir / "operation", op + "\n" + rebaseIdentity(repo) + "\n"))
        journal.markRebase(op);
}

void closeFinishedGroup(git_repository* repo, journal::Journal& journal)
{
    const fs::path dir = stateDir(repo);
    const auto group = readGroup(dir);
    if (!group)
        return;
    const std::string identity = rebaseIdentity(repo);
    if (!identity.empty() && identity == group->identity)
        return; // still in progress
    journal.end(group->op, true);
    removeState(dir);
}

void finishGroup(git_repository* repo, journal::Journal& journal, bool ok)
{
    const fs::path dir = stateDir(repo);
    if (const auto group = readGroup(dir))
        journal.end(group->op, ok);
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
            return 0; // git's list as it is
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
    if (list.items.empty() || !list.items.back().isCommit())
        return 0;
    const std::string& current = list.items.back().commit;
    for (const auto& [commit, message] : prepared->messages) {
        if (current.empty() || !(commit.rfind(current, 0) == 0 || current.rfind(commit, 0) == 0))
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
