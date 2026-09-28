// Worktree management (REBUILD_PLAN §4.7; P4-03): every change runs `git worktree …` and is
// journaled with the worktree it changed (docs/spec/undo-journal.md §5.4).
#include "shell/Actions.hpp"

#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/Launch.hpp>
#include <libgg/Worktrees.hpp>

namespace ggui {

namespace fs = std::filesystem;
using core::MutationContext;
using core::MutationError;
using core::Outcome;

namespace {

std::string leaf(const std::string& path)
{
    const fs::path p = fs::path(path).lexically_normal();
    const std::string name = (p.has_filename() ? p : p.parent_path()).filename().string();
    return name.empty() ? path : name;
}

// The registered worktree at `path` (refused when there is none).
gg::worktrees::Entry findWorktree(MutationContext& ctx, const std::string& path)
{
    std::string error;
    const auto entries = gg::worktrees::list(ctx.cwd(), &error);
    if (!error.empty())
        throw MutationError{Outcome::Failed, error, error};
    const auto* e = gg::worktrees::find(entries, path);
    if (!e)
        throw MutationError{Outcome::Refused, path + " is not a worktree of this repository", {}};
    return *e;
}

std::vector<std::string> lockArgs(const std::string& path, const std::string& reason)
{
    if (reason.empty())
        return {"worktree", "lock", path};
    return {"worktree", "lock", "--reason", reason, path};
}

// git's lines (it reports on stderr), one per line.
std::string outputLines(const gg::RunResult& r) { return gg::trim(r.err + (r.err.empty() ? "" : "\n") + r.out); }

} // namespace

void Actions::addWorktree(AddWorktree req, Callback done)
{
    run("add worktree " + leaf(req.path),
        [req](MutationContext& ctx) {
            std::vector<std::string> args{"worktree", "add"};
            if (req.force)
                args.emplace_back("--force");
            if (!req.checkout)
                args.emplace_back("--no-checkout");
            if (req.lock) {
                args.emplace_back("--lock");
                if (!req.reason.empty()) {
                    args.emplace_back("--reason");
                    args.push_back(req.reason);
                }
            }
            const std::string start = req.start.empty() ? std::string("HEAD") : req.start;
            switch (req.mode) {
            case AddWorktree::Mode::NewBranch:
                args.insert(args.end(), {"-b", req.branch, req.path, start});
                break;
            case AddWorktree::Mode::ExistingBranch:
                args.insert(args.end(), {req.path, req.branch});
                break;
            case AddWorktree::Mode::Detached:
                args.insert(args.end(), {"--detach", req.path, start});
                break;
            }
            ctx.git(args);
            const auto entries = gg::worktrees::list(ctx.cwd());
            if (const auto* e = gg::worktrees::find(entries, req.path)) {
                ctx.worktrees.push_back(gg::worktrees::describe(*e, "add"));
                ctx.result = e->path.string();
            }
        },
        std::move(done));
}

void Actions::removeWorktree(const std::string& path, bool force)
{
    run(force ? "remove worktree " + leaf(path) + " with its changes" : "remove worktree " + leaf(path),
        [path, force](MutationContext& ctx) {
            const auto e = findWorktree(ctx, path);
            if (e.main)
                throw MutationError{Outcome::Refused, "The main worktree cannot be removed", {}};
            if (gg::worktrees::samePath(e.path, ctx.cwd()))
                throw MutationError{Outcome::Refused, "This window shows " + e.path.string() + ": open another worktree first", {}};
            const std::string p = e.path.string();
            if (e.locked)
                ctx.git({"worktree", "unlock", p});
            std::vector<std::string> args{"worktree", "remove"};
            if (force)
                args.emplace_back("--force");
            args.push_back(p);
            const auto r = ctx.gitMayFail(args);
            if (!r.ok()) {
                if (e.locked)
                    ctx.gitMayFail(lockArgs(p, e.lockReason));
                const bool changes = r.err.find("contains modified or untracked files") != std::string::npos;
                throw MutationError{changes ? Outcome::LocalChanges : core::classifyFailure(r.err), r.message(), r.err};
            }
            ctx.worktrees.push_back(gg::worktrees::describe(e, "remove"));
        },
        [this, path](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::LocalChanges) {
                Form f;
                f.title = "Remove worktree with changes";
                f.message = path + " has uncommitted changes or untracked files:\n\n" + e.message
                    + "\n\nRemove it anyway? The changes and untracked files are deleted for good: Undo re-creates "
                      "the worktree at its commit, but not them.";
                f.buttons.push_back({"Delete changes and remove", [this, path](Form&) { removeWorktree(path, true); }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        });
}

void Actions::lockWorktree(const std::string& path, const std::string& reason)
{
    run("lock worktree " + leaf(path), [path, reason](MutationContext& ctx) {
        const auto e = findWorktree(ctx, path);
        ctx.git(lockArgs(e.path.string(), reason));
        ctx.worktrees.push_back(gg::journal::WorktreeChange{"lock", e.path.string(), {}, {}, false, reason});
    });
}

void Actions::unlockWorktree(const std::string& path)
{
    run("unlock worktree " + leaf(path), [path](MutationContext& ctx) {
        const auto e = findWorktree(ctx, path);
        ctx.git({"worktree", "unlock", e.path.string()});
        ctx.worktrees.push_back(gg::journal::WorktreeChange{"unlock", e.path.string(), {}, {}, false, e.lockReason});
    });
}

void Actions::previewPruneWorktrees(Callback done)
{
    run("prune worktrees (dry run)",
        [](MutationContext& ctx) { ctx.result = outputLines(ctx.git({"worktree", "prune", "--dry-run", "--verbose"})); },
        std::move(done), false, false, false);
}

void Actions::pruneWorktrees()
{
    run("prune worktrees",
        [](MutationContext& ctx) { ctx.result = outputLines(ctx.git({"worktree", "prune", "--verbose"})); },
        [this](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok) {
                m_session.app().notify(App::Notice::Info, "Prune worktrees", e.result.empty() ? "Nothing was pruned" : e.result);
                return;
            }
            handleDefault(e);
        });
}

void Actions::repairWorktree(const std::string& path)
{
    run("repair worktree " + leaf(path),
        [path](MutationContext& ctx) { ctx.result = outputLines(ctx.git({"worktree", "repair", path})); },
        [this](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok) {
                m_session.app().notify(App::Notice::Info, "Repair worktree",
                    e.result.empty() ? "Nothing needed repairing" : e.result);
                return;
            }
            handleDefault(e);
        });
}

void Actions::openInNewWindow(const std::string& path)
{
    App* app = &m_session.app();
    app->io().post([app, path]() -> std::function<void()> {
        const fs::path program = gg::gguiProgram();
        std::string error;
        if (program.empty()) {
            error = "The ggui program was not found";
        } else {
            gg::DetachedProcess child;
            if (gg::spawnDetached({program.string(), path}, child, error))
                gg::release(child);
        }
        return [app, path, error] {
            if (!error.empty())
                app->showError("Open in new window", error);
            else
                app->notify(App::Notice::Info, "Open in new window", "Opening " + path + " in a new ggui window");
        };
    });
}

} // namespace ggui
