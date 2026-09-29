// Native interactive rebase (product spec §4.13 execution 2, §4.10 native): `git rebase -i` fed
// through `git gg sequence-editor`, and the steps that move a stopped rebase on.
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "util/Env.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ggui {

namespace fs = std::filesystem;
using core::MutationContext;
using core::MutationError;
using core::Outcome;

namespace {

// `git gg sequence-editor` next to ggui, as a shell command (git appends the file).
std::string sequenceEditorCommand()
{
    fs::path gitGg = fs::path(executableDir()) / "git-gg";
#ifdef _WIN32
    gitGg += ".exe";
#endif
    return "'" + gitGg.generic_string() + "' sequence-editor";
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// git's first lines of output, for messages.
std::string gitMessage(const gg::RunResult& res)
{
    const auto lines = gg::splitLines(gg::trim(res.err.empty() ? res.out : res.err));
    std::string text;
    for (size_t i = 0; i < lines.size() && i < 8; ++i)
        text += (i ? "\n" : "") + lines[i];
    return text;
}

// git's version as major * 100 + minor (2.38 → 238).
int gitVersion(MutationContext& ctx)
{
    const auto res = ctx.git({"version"});
    int major = 0, minor = 0;
    const auto pos = res.out.find_first_of("0123456789");
    if (pos != std::string::npos)
        std::sscanf(res.out.c_str() + pos, "%d.%d", &major, &minor);
    return major * 100 + minor;
}

void useSequenceEditor(MutationContext& ctx, const fs::path& dir)
{
    const std::string editor = sequenceEditorCommand();
    ctx.env.emplace_back("GIT_SEQUENCE_EDITOR", editor);
    ctx.env.emplace_back("GIT_EDITOR", editor);
    ctx.env.emplace_back("GG_SEQUENCE_DIR", dir.string());
}

// After a git command that may have finished the rebase: the prepared messages go with it.
void dropPreparedIfFinished(git_repository* repo)
{
    if (gg::native::rebaseIdentity(repo).empty())
        gg::native::discardPrepared(repo);
}

} // namespace

// `git rebase <args>` for a stopped rebase (Continue, Skip, Abort, Amend and continue, Commit
// with conflicts). Git's editor gets the messages typed in the todo editor for this rebase, or
// keeps git's own text. Stopping again further on (the next edit, break, failing exec or
// conflict) is not an error: git's message comes back as information.
void rebaseStep(MutationContext& ctx, std::vector<std::string> args)
{
    git_repository* repo = ctx.repo();
    const fs::path done = fs::path(git_repository_path(repo)) / "rebase-merge" / "done";
    const std::string doneBefore = readText(done);
    if (gg::native::preparedFor(repo))
        useSequenceEditor(ctx, gg::native::stateDir(repo));
    else
        ctx.env.emplace_back("GIT_EDITOR", "true");
    args.insert(args.begin(), "rebase");
    const auto res = ctx.gitMayFail(args);
    ctx.worktreeFollowsIndex = true;
    dropPreparedIfFinished(repo);
    const bool stopped = !gg::native::rebaseIdentity(repo).empty();
    if (res.ok()) {
        if (stopped)
            ctx.info = gitMessage(res);
        return;
    }
    if (stopped && readText(done) != doneBefore) {
        ctx.info = gitMessage(res);
        return;
    }
    const std::string all = res.err + res.out;
    throw MutationError{core::classifyFailure(all), gitMessage(res), all};
}

void Actions::nativeRebase(NativeRebase request, Callback done)
{
    run(
        "interactive rebase (git rebase -i)",
        [request](MutationContext& ctx) {
            git_repository* repo = ctx.repo();
            if (request.checkTip) {
                git_oid now;
                const std::string ref = request.tipRef.empty() ? std::string("HEAD") : request.tipRef;
                const bool found = git_reference_name_to_id(&now, repo, ref.c_str()) == 0;
                git_error_clear();
                if (!found || gg::git2::toHex(now) != request.tip)
                    throw MutationError{Outcome::Refused,
                        (ref.rfind("refs/heads/", 0) == 0 ? ref.substr(11) : ref)
                            + " moved since the list was read; open the editor again",
                        {}};
            }
            const int version = gitVersion(ctx);
            if (request.updateRefs && version < 238)
                throw MutationError{Outcome::Refused,
                    "update-ref rows need git 2.38 or newer: turn off Update refs (or remove the rows), or run the "
                    "list in memory",
                    {}};
            // git 2.45 renamed --empty=ask to stop (ask still works, with a warning).
            const std::string empty = request.empty == "stop" && version < 245 ? "ask" : request.empty;
            const fs::path dir = gg::native::stateDir(repo);
            std::string error;
            if (!gg::native::writePrepared(dir, request.prepared, error))
                throw MutationError{Outcome::Failed, error, {}};
            // --root makes a "squash-onto" commit on the empty tree without writing that tree:
            // write it, so the repository stays complete (git fsck).
            if (std::find(request.args.begin(), request.args.end(), "--root") != request.args.end())
                ctx.git({"mktree"}, "");
            useSequenceEditor(ctx, dir);
            std::vector<std::string> args{"rebase", "-i", "--empty=" + empty};
            args.insert(args.end(), request.args.begin(), request.args.end());
            const auto res = ctx.gitMayFail(args);
            ctx.worktreeFollowsIndex = true;
            const std::string identity = gg::native::rebaseIdentity(repo);
            if (!identity.empty()) {
                // Stopped: the messages stay for the Continue steps of this rebase.
                gg::native::Prepared prepared = request.prepared;
                prepared.identity = identity;
                gg::native::writePrepared(dir, prepared, error);
                ctx.result = "stopped";
                ctx.info = gitMessage(res);
                return;
            }
            dropPreparedIfFinished(repo);
            if (!res.ok()) {
                const std::string all = res.err + res.out;
                throw MutationError{core::classifyFailure(all), gitMessage(res), all};
            }
        },
        std::move(done));
}

void Actions::amendAndContinue()
{
    run("amend and continue the rebase", [](MutationContext& ctx) {
        const auto staged = ctx.gitMayFail({"diff", "--cached", "--quiet"});
        if (!staged.ok()) {
            ctx.env.emplace_back("GIT_EDITOR", "true");
            ctx.git({"commit", "--amend", "--no-edit", "-q"});
            ctx.env.clear();
        }
        rebaseStep(ctx, {"--continue"});
    }, [this](const core::MutationFinishedEvent& e) { onRebaseStep(e); });
}

void Actions::editRemainingTodo(std::string expected, std::string todo, std::map<std::string, std::string> messages,
    Callback done)
{
    run(
        "edit the remaining rebase todo",
        [expected, todo, messages](MutationContext& ctx) {
            git_repository* repo = ctx.repo();
            const std::string identity = gg::native::rebaseIdentity(repo);
            const fs::path file = fs::path(git_repository_path(repo)) / "rebase-merge" / "git-rebase-todo";
            if (identity.empty() || readText(file) != expected)
                throw MutationError{Outcome::Refused,
                    "The rebase moved on since the todo was read; open Edit remaining todo again", {}};
            // Messages typed earlier for this rebase stay; the new ones replace them.
            gg::native::Prepared prepared = gg::native::preparedFor(repo).value_or(gg::native::Prepared{});
            for (const auto& [commit, message] : messages)
                prepared.messages[commit] = message;
            prepared.identity = identity;
            prepared.todo = todo;
            const fs::path dir = gg::native::stateDir(repo);
            std::string error;
            if (!gg::native::writePrepared(dir, prepared, error))
                throw MutationError{Outcome::Failed, error, {}};
            useSequenceEditor(ctx, dir);
            ctx.git({"rebase", "--edit-todo"});
            // Only git-rebase-todo is ggui's to hand over once.
            prepared.todo.clear();
            gg::native::writePrepared(dir, prepared, error);
        },
        std::move(done));
}

void Actions::onRebaseStep(const core::MutationFinishedEvent& e)
{
    if (e.outcome == Outcome::Ok && !e.message.empty())
        m_session.app().notify(App::Notice::Info, "Interactive rebase stopped", e.message);
    else
        handleDefault(e);
}

} // namespace ggui
