#include "shell/Actions.hpp"

#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/Git2.hpp>
#include <libgg/Hooks.hpp>
#include <libgg/Markers.hpp>
#include <libgg/NewCommit.hpp>
#include <libgg/Rewrite.hpp>
#include <libgg/Undo.hpp>

#include <spdlog/spdlog.h>

#include <fstream>
#include <set>
#include <sstream>

namespace ggui {

namespace fs = std::filesystem;
using core::MutationContext;
using core::MutationError;
using core::Outcome;

std::vector<std::string> withPaths(std::vector<std::string> args, const std::vector<std::string>& paths)
{
    args.emplace_back("--");
    for (const auto& p : paths)
        args.push_back(p);
    return args;
}

Actions::Actions(Session& session) : m_session(session) { }

std::string Actions::busy() const { return m_session.engine().busyLabel(); }

std::string Actions::busyTooltip() const
{
    const std::string b = busy();
    return b.empty() ? std::string() : "Busy: " + b;
}

core::RequestId Actions::run(std::string label, std::function<void(MutationContext&)> fn, Callback done, bool network,
    bool journal, bool refreshAfter)
{
    core::MutationSpec spec;
    spec.label = std::move(label);
    spec.run = std::move(fn);
    spec.network = network;
    spec.journal = journal;
    spec.refreshAfter = refreshAfter;
    const core::RequestId id = m_session.engine().mutate(std::move(spec));
    if (done)
        m_callbacks[id] = std::move(done);
    return id;
}

void Actions::onFinished(const core::MutationFinishedEvent& event)
{
    auto it = m_callbacks.find(event.request);
    if (it != m_callbacks.end()) {
        Callback cb = std::move(it->second);
        m_callbacks.erase(it);
        cb(event);
        return;
    }
    handleDefault(event);
}

void Actions::handleDefault(const core::MutationFinishedEvent& event)
{
    switch (event.outcome) {
    case Outcome::Ok:
    case Outcome::Cancelled:
        return;
    default:
        m_session.app().showError(event.label, event.detail.empty() ? event.message : event.detail);
    }
}

// ---- files ---------------------------------------------------------------------------------------

void Actions::stage(const std::vector<std::string>& paths)
{
    run("stage " + std::to_string(paths.size()) + " file(s)",
        [paths](MutationContext& ctx) { ctx.git(withPaths({"add", "-A"}, paths)); });
}

void Actions::unstage(const std::vector<std::string>& paths)
{
    const bool unborn = m_session.snapshot() && m_session.snapshot()->headUnborn;
    run("unstage " + std::to_string(paths.size()) + " file(s)", [paths, unborn](MutationContext& ctx) {
        if (unborn)
            ctx.git(withPaths({"rm", "--cached", "-q", "-r"}, paths));
        else
            ctx.git(withPaths({"restore", "--staged"}, paths));
    });
}

void Actions::discard(const std::vector<std::string>& tracked, const std::vector<std::string>& untracked)
{
    run("discard changes", [tracked, untracked](MutationContext& ctx) {
        if (!tracked.empty())
            ctx.git(withPaths({"restore", "--worktree"}, tracked));
        if (!untracked.empty())
            ctx.git(withPaths({"clean", "-f", "-q"}, untracked));
    });
}

void Actions::stageAll()
{
    run("stage all", [](MutationContext& ctx) { ctx.git({"add", "-A"}); });
}

void Actions::unstageAll()
{
    const bool unborn = m_session.snapshot() && m_session.snapshot()->headUnborn;
    run("unstage all", [unborn](MutationContext& ctx) {
        if (unborn)
            ctx.git({"rm", "--cached", "-r", "-q", "."});
        else
            ctx.git({"reset", "-q"});
    });
}

void Actions::stageModified()
{
    run("stage all modified", [](MutationContext& ctx) { ctx.git({"add", "-u"}); });
}

void Actions::intentToAdd(const std::vector<std::string>& paths)
{
    run("intent to add", [paths](MutationContext& ctx) { ctx.git(withPaths({"add", "-N"}, paths)); });
}

void Actions::markResolved(const std::vector<std::string>& paths)
{
    run("mark resolved", [paths](MutationContext& ctx) {
        // First-class conflicts may only be marked resolved once no region is left (§4.10).
        for (const auto& p : paths) {
            std::ifstream f(ctx.cwd() / p, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            if (gg::markers::isConflicted(ss.str()))
                throw MutationError{Outcome::Refused, p + " still contains conflict regions", {}};
        }
        ctx.git(withPaths({"add"}, paths));
    });
}

void Actions::deleteFiles(const std::vector<std::string>& paths)
{
    run("delete " + std::to_string(paths.size()) + " file(s)", [paths](MutationContext& ctx) {
        for (const auto& p : paths) {
            std::error_code ec;
            fs::remove(ctx.cwd() / p, ec);
            if (ec)
                throw MutationError{Outcome::Failed, "cannot delete " + p + ": " + ec.message(), {}};
        }
    });
}

void Actions::applyPatch(const std::string& label, const std::string& patch, bool cached, bool reverse, Callback done)
{
    run(label,
        [patch, cached, reverse](MutationContext& ctx) {
            std::vector<std::string> args{"apply", "--whitespace=nowarn"};
            if (cached)
                args.emplace_back("--cached");
            if (reverse)
                args.emplace_back("-R");
            args.emplace_back("-");
            ctx.git(args, patch);
        },
        std::move(done));
}

// ---- commits -------------------------------------------------------------------------------------

void Actions::commit(const std::string& message, bool noVerify, CommitMode mode, const std::vector<std::string>& selected,
    Callback done)
{
    run("commit",
        [message, noVerify, mode, selected](MutationContext& ctx) {
            if (mode == CommitMode::StageSelected && !selected.empty())
                ctx.git(withPaths({"add", "-A"}, selected));
            std::vector<std::string> args{"commit", "-q", "-F", "-"};
            if (mode == CommitMode::StageAllTracked)
                args.emplace_back("-a");
            if (noVerify)
                args.emplace_back("--no-verify");
            ctx.git(args, message);
            ctx.result = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
        },
        std::move(done));
}

void Actions::amend(const std::string& message, bool noVerify, bool messageOnly, Callback done)
{
    run(messageOnly ? "reword HEAD" : "amend",
        [message, noVerify, messageOnly](MutationContext& ctx) {
            std::vector<std::string> args{"commit", "-q", "--amend", "--allow-empty"};
            if (messageOnly)
                args.emplace_back("--only");
            if (message.empty()) {
                args.emplace_back("--no-edit");
            } else {
                args.emplace_back("-F");
                args.emplace_back("-");
            }
            if (noVerify)
                args.emplace_back("--no-verify");
            const std::string before = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
            ctx.git(args, message);
            ctx.result = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
            // Descendants of the amended commit (a conflicted commit checked out in the middle of
            // a branch, …) are rebased onto it: their copies of a resolved conflict resolve too.
            gg::rewrite::Plan plan;
            for (const auto& c : gg::rewrite::descendants(ctx.repo(), {before}))
                if (c != before) {
                    gg::rewrite::Step st;
                    st.source = c;
                    plan.steps.push_back(st);
                }
            if (plan.steps.empty())
                return;
            plan.replaced[before] = ctx.result;
            plan.reflogMessage = "ggui: amend (rebase descendants)";
            plan.rewriteKind = "amend";
            plan.keepHead = true;
            gg::rewrite::Rewriter rewriter(ctx.cwd());
            gg::rewrite::Result r = rewriter.compute(plan);
            std::string error = r.error;
            if (!r.ok || !rewriter.apply(plan, r, error))
                throw MutationError{Outcome::Failed, "amended, but rebasing the descendants failed: " + error, error};
        },
        std::move(done));
}

void Actions::takeConflictSide(const std::vector<std::string>& paths, int side, int region)
{
    run("take side " + std::to_string(side + 1), [paths, side, region](MutationContext& ctx) {
        for (const auto& p : paths) {
            const fs::path file = ctx.cwd() / p;
            std::ifstream in(file, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            in.close();
            const std::string text = gg::markers::takeSide(ss.str(), side, region);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << text;
        }
    }, {}, false, true);
}

void Actions::mergeToolFirstClass(const std::string& path)
{
    run("merge tool " + path,
        [path](MutationContext& ctx) {
            // Stages 1–3 from the regions of the working tree file, then git mergetool (which
            // stages the result). If the tool gives up, the index goes back to HEAD for the path.
            std::ifstream in(ctx.cwd() / path, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string text = ss.str();
            if (gg::markers::parse(text).maxSides() != 2)
                throw MutationError{Outcome::Refused, "merge tools handle two-sided conflicts only; take a side first", {}};
            const std::string mode = gg::trim(ctx.git({"ls-files", "--format=%(objectmode)", "--", path}).out);
            auto blob = [&](const std::string& content) { return gg::trim(ctx.git({"hash-object", "-w", "--stdin"}, content).out); };
            const std::string zero(gg::git2::hexSize(gg::git2::oidType(ctx.repo())), '0');
            std::string input = "0 " + zero + "\t" + path + "\n";
            input += (mode.empty() ? std::string("100644") : mode) + " " + blob(gg::markers::takeBase(text)) + " 1\t" + path + "\n";
            input += (mode.empty() ? std::string("100644") : mode) + " " + blob(gg::markers::takeSide(text, 0)) + " 2\t" + path + "\n";
            input += (mode.empty() ? std::string("100644") : mode) + " " + blob(gg::markers::takeSide(text, 1)) + " 3\t" + path + "\n";
            ctx.git({"update-index", "--index-info"}, input);
            const auto r = ctx.gitMayFail(withPaths({"mergetool", "-y"}, {path}));
            if (!r.ok()) {
                ctx.gitMayFail({"reset", "-q", "--", path});
                throw MutationError{Outcome::Failed, r.message(), r.message()};
            }
        },
        {}, true, false);
}

void Actions::newCommit(const std::vector<std::string>& parents, bool detach, const std::string& message)
{
    run(detach ? "new detached commit" : "new commit", [parents, detach, message](MutationContext& ctx) {
        gg::NewCommitOptions opts;
        opts.parents = parents;
        opts.detach = detach;
        opts.message = message;
        const auto result = gg::newCommit(ctx.repo(), opts);
        if (!result.ok)
            throw MutationError{core::classifyFailure(result.error), result.error, result.error};
        ctx.result = result.commit;
        ctx.worktreeFollowsIndex = true;
    });
}

namespace {

// "Expand to index stages on checkout" (§4.10): paths whose committed content (HEAD) holds
// two-sided first-class conflicts get stages 1–3 built from the regions.
void expandConflictStages(MutationContext& ctx)
{
    git_oid head;
    if (git_reference_name_to_id(&head, ctx.repo(), "HEAD") != 0) {
        git_error_clear();
        return;
    }
    gg::conflicts::Cache cache;
    std::string input;
    for (const auto& f : gg::conflicts::commitConflicts(ctx.repo(), head, cache)) {
        if (f.sides != 2)
            continue;
        const std::string spec = "HEAD:" + f.path;
        const std::string text = ctx.git({"cat-file", "blob", spec}).out;
        const std::string mode = gg::trim(ctx.git({"ls-tree", "--format=%(objectmode)", "HEAD", "--", f.path}).out);
        auto blob = [&](const std::string& content) { return gg::trim(ctx.git({"hash-object", "-w", "--stdin"}, content).out); };
        const std::string zero(gg::git2::hexSize(gg::git2::oidType(ctx.repo())), '0');
        input += "0 " + zero + "\t" + f.path + "\n";
        input += mode + " " + blob(gg::markers::takeBase(text)) + " 1\t" + f.path + "\n";
        input += mode + " " + blob(gg::markers::takeSide(text, 0)) + " 2\t" + f.path + "\n";
        input += mode + " " + blob(gg::markers::takeSide(text, 1)) + " 3\t" + f.path + "\n";
    }
    if (!input.empty())
        ctx.git({"update-index", "--index-info"}, input);
}

// Before switching away: expanded paths (unmerged, working tree still the committed content)
// go back to the committed blob.
void collapseConflictStages(MutationContext& ctx)
{
    const std::string unmerged = ctx.gitMayFail({"diff", "--name-only", "--diff-filter=U"}).out;
    for (const auto& path : gg::splitLines(unmerged)) {
        if (path.empty())
            continue;
        const auto committed = ctx.gitMayFail({"rev-parse", "-q", "--verify", "HEAD:" + path});
        const auto onDisk = ctx.gitMayFail({"hash-object", "--", path});
        if (committed.ok() && onDisk.ok() && gg::trim(committed.out) == gg::trim(onDisk.out))
            ctx.git({"reset", "-q", "--", path});
    }
}

} // namespace

void Actions::checkout(const std::string& target, bool detach, bool stashFirst)
{
    const std::string label = detach ? "check out " + target.substr(0, 10) : "switch to " + target;
    const bool expand = m_session.app().settings().data().expandConflictStages;
    run(label,
        [target, detach, stashFirst, expand](MutationContext& ctx) {
            collapseConflictStages(ctx);
            if (stashFirst)
                ctx.git({"stash", "push", "-q", "-m", "ggui: before switching to " + target});
            if (detach)
                ctx.git({"switch", "-q", "--detach", target});
            else
                ctx.git({"switch", "-q", target});
            if (expand)
                expandConflictStages(ctx);
            ctx.worktreeFollowsIndex = true;
        },
        [this, target, detach](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::LocalChanges) {
                Form f;
                f.title = "Stash and switch";
                f.message = "Your local changes would be overwritten by the checkout.\n\n" + e.message
                    + "\n\nStash them first and then switch?";
                f.buttons.push_back({"Stash and switch", [this, target, detach](Form&) { checkout(target, detach, true); }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        });
}

void Actions::moveHead(bool toChild, const core::Oid& child)
{
    const auto snap = m_session.snapshot();
    if (!snap || snap->head.isNull())
        return;
    std::string target;
    if (toChild) {
        if (child.isNull())
            return;
        target = child.hex();
    } else {
        target = snap->head.hex() + "^";
    }
    // A branch tip becomes a branch switch; anything else detaches.
    const bool expand = m_session.app().settings().data().expandConflictStages;
    run(toChild ? "move HEAD to child" : "move HEAD to parent", [target, expand](MutationContext& ctx) {
        collapseConflictStages(ctx);
        const std::string id = gg::trim(ctx.git({"rev-parse", "--verify", target + "^{commit}"}).out);
        const std::string branches = ctx.git({"for-each-ref", "--points-at", id, "--format=%(refname:short)", "refs/heads/"}).out;
        const auto names = gg::splitLines(branches);
        if (!names.empty())
            ctx.git({"switch", "-q", names.front()});
        else
            ctx.git({"switch", "-q", "--detach", id});
        if (expand)
            expandConflictStages(ctx);
        ctx.worktreeFollowsIndex = true;
    });
}

// ---- branches, tags, remotes -----------------------------------------------------------------------

void Actions::createBranch(const std::string& name, const std::string& at, bool checkoutAfter)
{
    run("create branch " + name, [name, at, checkoutAfter](MutationContext& ctx) {
        if (checkoutAfter) {
            ctx.git({"switch", "-q", "-c", name, at});
            ctx.worktreeFollowsIndex = true;
        } else {
            ctx.git({"branch", name, at});
        }
    });
}

void Actions::renameBranch(const std::string& from, const std::string& to)
{
    run("rename branch " + from, [from, to](MutationContext& ctx) { ctx.git({"branch", "-m", from, to}); });
}

void Actions::deleteBranch(const std::string& name, bool force, const std::vector<std::string>& remotes, bool local)
{
    run("delete branch " + name,
        [name, force, remotes, local](MutationContext& ctx) {
            for (const auto& r : remotes)
                ctx.git({"push", "--progress", r, "--delete", name}, {}, true);
            if (local)
                ctx.git({"branch", force ? "-D" : "-d", name});
        },
        {}, !remotes.empty());
}

void Actions::moveBranch(const std::string& name, const std::string& to)
{
    const auto snap = m_session.snapshot();
    const bool current = snap && !snap->headDetached && snap->headBranch == name;
    run("move branch " + name, [name, to, current](MutationContext& ctx) {
        if (current) {
            // The checked-out branch: keep local changes, refuse when they conflict.
            ctx.git({"reset", "-q", "--keep", to});
            ctx.worktreeFollowsIndex = true;
        } else {
            const std::string id = gg::trim(ctx.git({"rev-parse", "--verify", to + "^{commit}"}).out);
            ctx.git({"update-ref", "-m", "ggui: move branch", "refs/heads/" + name, id});
        }
    });
}

void Actions::setUpstream(const std::string& branch, const std::string& upstream)
{
    run("set upstream of " + branch,
        [branch, upstream](MutationContext& ctx) { ctx.git({"branch", "--set-upstream-to=" + upstream, branch}); });
}

void Actions::unsetUpstream(const std::string& branch)
{
    run("unset upstream of " + branch, [branch](MutationContext& ctx) { ctx.git({"branch", "--unset-upstream", branch}); });
}

void Actions::fastForward(const std::string& branch)
{
    const auto snap = m_session.snapshot();
    const bool current = snap && !snap->headDetached && snap->headBranch == branch;
    run("fast-forward " + branch, [branch, current](MutationContext& ctx) {
        if (current) {
            ctx.git({"merge", "-q", "--ff-only", "@{upstream}"});
            ctx.worktreeFollowsIndex = true;
        } else {
            const std::string up = gg::trim(ctx.git({"rev-parse", "--symbolic-full-name", branch + "@{upstream}"}).out);
            ctx.git({"fetch", "-q", ".", up + ":refs/heads/" + branch});
        }
    });
}

void Actions::createTag(const std::string& name, const std::string& at, const std::string& message)
{
    run("create tag " + name, [name, at, message](MutationContext& ctx) {
        if (message.empty())
            ctx.git({"tag", name, at});
        else
            ctx.git({"tag", "-a", "-F", "-", name, at}, message);
    });
}

void Actions::deleteTag(const std::string& name)
{
    run("delete tag " + name, [name](MutationContext& ctx) { ctx.git({"tag", "-d", name}); });
}

void Actions::pushTag(const std::string& remote, const std::string& tag)
{
    run("push tag " + tag, [remote, tag](MutationContext& ctx) { ctx.git({"push", "--progress", remote, "refs/tags/" + tag}, {}, true); },
        {}, true);
}

void Actions::deleteRemoteTag(const std::string& remote, const std::string& tag)
{
    run("delete tag " + tag + " on " + remote,
        [remote, tag](MutationContext& ctx) { ctx.git({"push", "--progress", remote, "--delete", "refs/tags/" + tag}, {}, true); },
        {}, true);
}

void Actions::addRemote(const std::string& name, const std::string& url)
{
    run("add remote " + name, [name, url](MutationContext& ctx) { ctx.git({"remote", "add", name, url}); });
}

void Actions::removeRemote(const std::string& name)
{
    run("remove remote " + name, [name](MutationContext& ctx) { ctx.git({"remote", "remove", name}); });
}

void Actions::setRemoteUrl(const std::string& name, const std::string& url)
{
    run("set URL of " + name, [name, url](MutationContext& ctx) { ctx.git({"remote", "set-url", name, url}); }, {}, false,
        false);
}

void Actions::setPruneOnFetch(const std::string& name, bool prune)
{
    run("prune on fetch for " + name,
        [name, prune](MutationContext& ctx) {
            ctx.git({"config", "--local", "remote." + name + ".prune", prune ? "true" : "false"});
        },
        {}, false, false);
}

// ---- network ---------------------------------------------------------------------------------------

void Actions::fetch(const std::string& remote, bool prune, bool tags)
{
    const std::string label = remote.empty() ? "fetch all remotes" : "fetch " + remote;
    run(label,
        [remote, prune, tags](MutationContext& ctx) {
            std::vector<std::string> args{"fetch", "--progress"};
            if (prune)
                args.emplace_back("--prune");
            if (tags)
                args.emplace_back("--tags");
            if (remote.empty())
                args.emplace_back("--all");
            else
                args.push_back(remote);
            ctx.git(args, {}, true);
        },
        {}, true);
}

void Actions::pull(PullMode mode, bool autostash)
{
    run("pull",
        [mode, autostash](MutationContext& ctx) {
            std::vector<std::string> args{"pull", "--progress"};
            if (mode == PullMode::Merge)
                args.emplace_back("--no-rebase");
            else if (mode == PullMode::Rebase)
                args.emplace_back("--rebase");
            else if (mode == PullMode::FastForwardOnly)
                args.emplace_back("--ff-only");
            if (autostash)
                args.emplace_back("--autostash");
            ctx.env.emplace_back("GIT_EDITOR", "true");
            ctx.git(args, {}, true);
            ctx.worktreeFollowsIndex = true;
        },
        [this, mode](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::LocalChanges) {
                Form f;
                f.title = "Stash and pull";
                f.message = "Local changes block the pull.\n\n" + e.message + "\n\nStash them, pull, and re-apply them?";
                f.buttons.push_back({"Stash and pull", [this, mode](Form&) { pull(mode, true); }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        },
        true);
}

void Actions::push(const std::string& remote, const std::string& localBranch, const std::string& remoteBranch,
    bool setUpstream, bool forceWithLease, bool tags)
{
    run(tags ? "push tags to " + remote : "push " + localBranch + " to " + remote,
        [remote, localBranch, remoteBranch, setUpstream, forceWithLease, tags](MutationContext& ctx) {
            if (!tags) {
                // Never push first-class conflicts (§4.10 Safety, P1): refuse before git runs.
                const std::string local = gg::trim(ctx.git({"rev-parse", "--verify", "refs/heads/" + localBranch}).out);
                const auto remoteOid = ctx.gitMayFail({"rev-parse", "--verify", "-q", "refs/remotes/" + remote + "/" + remoteBranch});
                const auto conflicted = gg::hooks::conflictedOutgoing(ctx.cwd(), local, remote,
                    remoteOid.ok() ? gg::trim(remoteOid.out) : std::string());
                if (!conflicted.empty()) {
                    std::string list;
                    for (const auto& c : conflicted) {
                        list += c.id + " " + c.subject + "\n";
                        for (const auto& f : c.files)
                            list += "    " + f + "\n";
                    }
                    throw MutationError{Outcome::Refused, "The pushed commits contain first-class conflicts", list};
                }
            }
            std::vector<std::string> args{"push", "--progress"};
            if (setUpstream)
                args.emplace_back("--set-upstream");
            if (forceWithLease)
                args.emplace_back("--force-with-lease");
            args.push_back(remote);
            if (tags)
                args.emplace_back("--tags");
            else
                args.push_back("refs/heads/" + localBranch + ":refs/heads/" + remoteBranch);
            ctx.git(args, {}, true);
        },
        [this, remote, localBranch, remoteBranch](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Refused) {
                m_session.app().dialogs().pushRefused(m_session, e.detail);
                return;
            }
            if (e.outcome == Outcome::PushRejected) {
                Form f;
                f.title = "Push rejected";
                f.message = "The remote has commits you do not have (non-fast-forward).\n\n" + e.message;
                f.buttons.push_back({"Pull then push", [this, remote, localBranch, remoteBranch](Form&) {
                                         run("pull then push",
                                             [remote, localBranch, remoteBranch](MutationContext& ctx) {
                                                 ctx.env.emplace_back("GIT_EDITOR", "true");
                                                 ctx.git({"pull", "--progress", "--no-rebase", remote, remoteBranch}, {}, true);
                                                 ctx.git({"push", "--progress", remote,
                                                             "refs/heads/" + localBranch + ":refs/heads/" + remoteBranch},
                                                     {}, true);
                                                 ctx.worktreeFollowsIndex = true;
                                             },
                                             {}, true);
                                     }});
                f.buttons.push_back({"Force with lease...", [this, remote, localBranch, remoteBranch](Form&) {
                                         Form confirm;
                                         confirm.title = "Force push";
                                         confirm.message = "Overwrite " + remote + "/" + remoteBranch
                                             + " with your branch? Commits only on the remote will be lost there "
                                               "(--force-with-lease refuses if the remote moved again).";
                                         confirm.buttons.push_back({"Force push", [this, remote, localBranch, remoteBranch](Form&) {
                                                                        push(remote, localBranch, remoteBranch, false, true);
                                                                    }});
                                         confirm.buttons.push_back({"Cancel", {}});
                                         m_session.app().dialogs().open(std::move(confirm));
                                     }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        },
        true);
}

// ---- stash -----------------------------------------------------------------------------------------

void Actions::stashPush(const std::string& message, bool keepIndex, bool untracked, bool stagedOnly,
    const std::vector<std::string>& paths, Callback done)
{
    run("stash push",
        [message, keepIndex, untracked, stagedOnly, paths](MutationContext& ctx) {
            std::vector<std::string> args{"stash", "push", "-q"};
            if (!message.empty()) {
                args.emplace_back("-m");
                args.push_back(message);
            }
            if (keepIndex)
                args.emplace_back("--keep-index");
            if (untracked)
                args.emplace_back("--include-untracked");
            if (stagedOnly)
                args.emplace_back("--staged");
            if (!paths.empty())
                args = withPaths(args, paths);
            ctx.git(args);
            ctx.worktreeFollowsIndex = true;
        },
        std::move(done));
}

void Actions::stashApply(int index, bool pop, bool restoreIndex)
{
    const std::string ref = "stash@{" + std::to_string(index) + "}";
    run(std::string(pop ? "stash pop " : "stash apply ") + ref, [ref, pop, restoreIndex](MutationContext& ctx) {
        std::vector<std::string> args{"stash", pop ? "pop" : "apply", "-q"};
        if (restoreIndex)
            args.emplace_back("--index");
        args.push_back(ref);
        ctx.worktreeFollowsIndex = true;
        ctx.git(args);
    });
}

void Actions::stashDrop(int index)
{
    const std::string ref = "stash@{" + std::to_string(index) + "}";
    run("stash drop " + ref, [ref](MutationContext& ctx) { ctx.git({"stash", "drop", "-q", ref}); });
}

void Actions::stashClear()
{
    run("stash clear", [](MutationContext& ctx) { ctx.git({"stash", "clear"}); });
}

void Actions::stashBranch(int index, const std::string& branch)
{
    const std::string ref = "stash@{" + std::to_string(index) + "}";
    run("branch " + branch + " from " + ref, [ref, branch](MutationContext& ctx) {
        ctx.git({"stash", "branch", branch, ref});
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::stashApplyFile(int index, const std::string& path)
{
    const std::string ref = "stash@{" + std::to_string(index) + "}";
    run("apply " + path + " from " + ref,
        [ref, path](MutationContext& ctx) { ctx.git(withPaths({"restore", "--source=" + ref, "--worktree"}, {path})); });
}

// ---- in-progress operations --------------------------------------------------------------------------

namespace {

std::string operationCommand(core::RepoState s)
{
    switch (s) {
    case core::RepoState::Merging: return "merge";
    case core::RepoState::RebasingInteractive:
    case core::RepoState::Rebasing: return "rebase";
    case core::RepoState::CherryPicking: return "cherry-pick";
    case core::RepoState::Reverting: return "revert";
    case core::RepoState::Bisecting: return "bisect";
    default: return {};
    }
}

} // namespace

void Actions::continueOperation()
{
    const auto snap = m_session.snapshot();
    if (!snap)
        return;
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --continue", [cmd](MutationContext& ctx) {
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "merge")
            ctx.git({"commit", "--no-edit", "-q"});
        else
            ctx.git({cmd, "--continue"});
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::skipOperation()
{
    const auto snap = m_session.snapshot();
    if (!snap)
        return;
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --skip", [cmd](MutationContext& ctx) {
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "bisect")
            ctx.git({"bisect", "skip"});
        else
            ctx.git({cmd, "--skip"});
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::abortOperation()
{
    const auto snap = m_session.snapshot();
    if (!snap)
        return;
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --abort", [cmd](MutationContext& ctx) {
        if (cmd == "bisect")
            ctx.git({"bisect", "reset"});
        else
            ctx.git({cmd, "--abort"});
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::takeSide(const std::vector<std::string>& paths, Side side)
{
    run(side == Side::Ours ? "take ours" : "take theirs", [paths, side](MutationContext& ctx) {
        ctx.git(withPaths({"checkout", side == Side::Ours ? "--ours" : "--theirs"}, paths));
        ctx.git(withPaths({"add"}, paths));
    });
}

void Actions::commitWithConflicts()
{
    const auto snap = m_session.snapshot();
    const auto status = m_session.status();
    if (!snap || !status)
        return;
    const std::string cmd = operationCommand(snap->state);
    std::vector<std::string> paths;
    for (const auto& e : status->conflicted)
        if (!e.firstClass)
            paths.push_back(e.path);
    run("commit with conflicts", [cmd, paths](MutationContext& ctx) {
        // Text-only: write diff3 regions from stages 1–3 (base, ours, theirs), stage, finish.
        std::vector<std::pair<std::string, std::string>> contents;
        for (const auto& p : paths) {
            auto stage = [&](int n) {
                const auto r = ctx.gitMayFail({"show", ":" + std::to_string(n) + ":" + p});
                return r.ok() ? std::optional<std::string>(r.out) : std::nullopt;
            };
            const auto base = stage(1);
            const auto ours = stage(2);
            const auto theirs = stage(3);
            if (!ours || !theirs)
                throw MutationError{Outcome::Refused, p + ": modify/delete conflicts must be resolved first", {}};
            if (gg::markers::looksBinary(*ours) || gg::markers::looksBinary(*theirs)
                || (base && gg::markers::looksBinary(*base)))
                throw MutationError{Outcome::Refused, p + " is binary: resolve binary conflicts first", {}};
            contents.emplace_back(p, gg::markers::mergeFiles(base.value_or(""), *ours, *theirs));
        }
        for (const auto& [p, text] : contents) {
            std::ofstream out(ctx.cwd() / p, std::ios::binary | std::ios::trunc);
            out << text;
        }
        if (!paths.empty())
            ctx.git(withPaths({"add"}, paths));
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "merge" || cmd.empty())
            ctx.git({"commit", "--no-edit", "-q", "--no-verify"});
        else
            ctx.git({cmd, "--continue"});
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::saveMergeMessage(const std::string& message)
{
    const auto snap = m_session.snapshot();
    if (!snap)
        return;
    const fs::path file = snap->gitDir / "MERGE_MSG";
    run("edit merge message", [file, message](MutationContext&) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << message;
        if (!out)
            throw MutationError{Outcome::Failed, "cannot write " + file.string(), {}};
    }, {}, false, false);
}

// ---- undo ------------------------------------------------------------------------------------------

void Actions::undo(bool redo)
{
    run(redo ? "redo" : "undo",
        [redo](MutationContext& ctx) {
            const auto r = gg::undo(ctx.repo(), redo, "ggui");
            if (r.nothing)
                throw MutationError{Outcome::Refused, redo ? "Nothing to redo" : "Nothing to undo", {}};
            if (!r.ok)
                // detail = the refused operation, so "Stash and undo" restores that one (the stash
                // itself becomes the newest operation).
                throw MutationError{r.wouldLoseData ? Outcome::LocalChanges : Outcome::Refused, r.error,
                    r.wouldLoseData ? r.target : r.error};
            ctx.info = r.label;
        },
        [this, redo](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::LocalChanges) {
                const std::string target = e.detail;
                Form f;
                f.title = "Undo would lose changes";
                f.message = "Restoring the working tree would overwrite local changes:\n\n" + e.message
                    + "\n\nStash them first?";
                f.buttons.push_back({redo ? "Stash and redo" : "Stash and undo", [this, redo, target](Form&) {
                                         stashPush("ggui: before " + std::string(redo ? "redo" : "undo"), false, true, false,
                                             {}, [this, target](const core::MutationFinishedEvent& s) {
                                                 if (s.outcome == Outcome::Ok)
                                                     restore(target);
                                                 else
                                                     handleDefault(s);
                                             });
                                     }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        },
        false, false);
}

void Actions::restore(const std::string& operationId)
{
    run("restore",
        [operationId](MutationContext& ctx) {
            const auto r = gg::undo(ctx.repo(), false, "ggui", operationId);
            if (!r.ok)
                throw MutationError{r.wouldLoseData ? Outcome::LocalChanges : Outcome::Refused, r.error, r.error};
            ctx.info = r.label;
        },
        {}, false, false);
}

// ---- hooks and old gg data ---------------------------------------------------------------------------

void Actions::installHooks(Callback done)
{
    run("install ggui hooks",
        [](MutationContext& ctx) {
            std::string error;
            if (!gg::hooks::install(ctx.cwd(), error))
                throw MutationError{Outcome::Failed, error, error};
        },
        std::move(done), false, false);
}

void Actions::uninstallHooks(Callback done)
{
    run("remove ggui hooks",
        [](MutationContext& ctx) {
            std::string error;
            if (!gg::hooks::uninstall(ctx.cwd(), error))
                throw MutationError{Outcome::Failed, error, error};
        },
        std::move(done), false, false);
}

void Actions::cleanUpOldGgRefs(const std::vector<std::pair<std::string, std::string>>& keepBranches)
{
    const auto snap = m_session.snapshot();
    if (!snap)
        return;
    const auto refs = snap->oldGgRefs;
    run("clean up old gg data", [refs, keepBranches](MutationContext& ctx) {
        for (const auto& [branch, commit] : keepBranches)
            ctx.git({"branch", branch, commit});
        std::string input;
        for (const auto& r : refs)
            input += "delete " + r + "\n";
        ctx.git({"update-ref", "--stdin"}, input);
    });
}

// ---- external tools -----------------------------------------------------------------------------------

void Actions::openInEditor(const std::string& path)
{
    run("open " + path,
        [path](MutationContext& ctx) {
            const std::string editor = gg::trim(ctx.git({"var", "GIT_EDITOR"}).out);
            ctx.env.clear();
            gg::RunRequest r;
            r.args = {"sh", "-c", editor + " \"$@\"", editor, path};
            r.cwd = ctx.cwd();
            r.gitEnvironment = false;
            r.cLocale = false;
            const auto res = gg::run(r);
            if (!res.ok())
                throw MutationError{Outcome::Failed, res.message(), res.err};
        },
        {}, true, false);
}

void Actions::externalDiff(const std::string& path, const std::string& from, const std::string& to)
{
    run("external diff " + path,
        [path, from, to](MutationContext& ctx) {
            std::vector<std::string> args{"difftool", "-y", from};
            if (!to.empty())
                args.push_back(to);
            ctx.git(withPaths(args, {path}));
        },
        {}, true, false);
}

void Actions::mergeTool(const std::string& path)
{
    run("merge tool " + path, [path](MutationContext& ctx) { ctx.git(withPaths({"mergetool", "-y"}, {path})); }, {}, true,
        false);
}

} // namespace ggui
