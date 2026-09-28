// History editing through the in-memory rewrite engine (REBUILD_PLAN §4.3, §4.10 pre-flight).
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <set>

namespace ggui {

using core::MutationContext;
using core::MutationError;
using core::Outcome;
namespace rw = gg::rewrite;

struct Actions::RewriteState {
    std::string label;
    PlanBuilder build;
    Callback done;
    bool autostash = false;
    std::map<std::string, rw::Resolution> resolutions; // pre-flight decisions so far
    // Commits that become empty: the plan asks (Emptied::Ask) until the user answers.
    bool askEmpty = false;
    std::optional<rw::Emptied> emptied;
    rw::Result preview;
    // Pre-flight: the resolution behind each combo entry, per conflict.
    std::vector<std::vector<rw::Resolution>> choices;
};

namespace {

std::string shortId(const std::string& id) { return id.substr(0, 10); }

std::string modeText(std::uint32_t mode)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06o", mode);
    return buf;
}

} // namespace

void Actions::rewrite(const std::string& label, PlanBuilder build, Callback done, bool autostash)
{
    auto state = std::make_shared<RewriteState>();
    state->label = label;
    state->build = std::move(build);
    state->done = std::move(done);
    state->autostash = autostash;
    rewritePrepare(state);
}

void Actions::rewritePrepare(const std::shared_ptr<RewriteState>& state)
{
    // Computed in memory only (nothing is written): no journal operation.
    run(
        state->label + " (preparing)",
        [state](MutationContext& ctx) {
            rw::Plan plan = state->build(ctx.repo());
            plan.resolutions = state->resolutions;
            state->askEmpty = plan.emptied == rw::Emptied::Ask;
            if (state->emptied)
                plan.emptied = *state->emptied;
            rw::Rewriter rewriter(ctx.cwd());
            state->preview = rewriter.compute(plan);
            if (!state->preview.ok && state->preview.unresolved.empty())
                throw MutationError{Outcome::Failed, state->preview.error, {}};
        },
        [this, state](const core::MutationFinishedEvent& e) {
            if (e.outcome != Outcome::Ok) {
                if (state->done)
                    state->done(e);
                else
                    handleDefault(e);
                return;
            }
            rewriteDecide(state);
        },
        false, false);
}

void Actions::rewriteDecide(const std::shared_ptr<RewriteState>& state)
{
    const rw::Result& r = state->preview;
    auto& dialogs = m_session.app().dialogs();
    // 1. Non-text conflicts: every one needs a decision before anything is written.
    if (!r.unresolved.empty()) {
        Form f;
        f.title = "Resolve conflicts before rewriting";
        f.message = "These conflicts cannot be written into the files as first-class conflicts. "
                    "Choose how to resolve each one; nothing changes until all are decided.";
        state->choices.clear();
        for (size_t i = 0; i < r.unresolved.size(); ++i) {
            const auto& c = r.unresolved[i];
            std::string what = shortId(c.commit) + " " + c.subject + ": " + c.path + " (" + c.kind + ")";
            if (c.kind == "rename")
                what += "\n    side A: " + (c.oursPath.empty() ? std::string("deleted") : c.oursPath)
                    + ", side B: " + (c.theirsPath.empty() ? std::string("deleted") : c.theirsPath);
            f.add(Field{Field::Info, "conflict_" + std::to_string(i), "", what});
            Field combo{Field::Combo, "choice_" + std::to_string(i), "Resolution"};
            std::vector<rw::Resolution> options;
            auto add = [&](const std::string& label, rw::Resolution res) {
                combo.options.push_back(label);
                options.push_back(res);
            };
            if (c.kind == "rename") {
                if (c.hasTheirs)
                    add("Take side B (" + c.theirsPath + ")", rw::Resolution{rw::Choice::Theirs, {}, {}});
                if (c.hasOurs)
                    add("Take side A (" + c.oursPath + ")", rw::Resolution{rw::Choice::Ours, {}, {}});
                if (c.hasBase)
                    add("Take the base (" + c.path + ")", rw::Resolution{rw::Choice::Base, {}, {}});
                add("Keep deleted", rw::Resolution{rw::Choice::Delete, {}, {}});
            } else if (c.kind == "mode") {
                add("Keep mode " + modeText(c.oursMode) + " (side A)", rw::Resolution{rw::Choice::Theirs, {}, c.oursMode});
                add("Keep mode " + modeText(c.theirsMode) + " (side B)", rw::Resolution{rw::Choice::Theirs, {}, c.theirsMode});
            } else {
                if (c.hasTheirs)
                    add(std::string(c.hasOurs ? "Take side B" : "Keep present") + " (" + shortId(c.commit) + ")",
                        rw::Resolution{rw::Choice::Theirs, {}, {}});
                if (c.hasOurs)
                    add(std::string(c.hasTheirs ? "Take side A" : "Keep present") + " (the new base)",
                        rw::Resolution{rw::Choice::Ours, {}, {}});
                if (c.hasBase)
                    add("Take the base", rw::Resolution{rw::Choice::Base, {}, {}});
                if (!c.hasOurs || !c.hasTheirs)
                    add("Keep deleted", rw::Resolution{rw::Choice::Delete, {}, {}});
                add("Use a file from disk", rw::Resolution{rw::Choice::File, {}, {}});
                f.add(combo);
                f.add(Field{Field::Text, "file_" + std::to_string(i), "File (for \"Use a file from disk\")"});
                state->choices.push_back(std::move(options));
                continue;
            }
            f.add(combo);
            state->choices.push_back(std::move(options));
        }
        f.buttons.push_back({"Continue", [this, state](Form& form) {
                                 const rw::Result& pending = state->preview;
                                 for (size_t i = 0; i < pending.unresolved.size(); ++i) {
                                     const auto& c = pending.unresolved[i];
                                     const auto& opts = state->choices[i];
                                     const int k = form.choice("choice_" + std::to_string(i));
                                     rw::Resolution res = opts[static_cast<size_t>(std::clamp(k, 0, static_cast<int>(opts.size()) - 1))];
                                     if (res.choice == rw::Choice::File)
                                         res.file = gg::trim(form.text("file_" + std::to_string(i)));
                                     state->resolutions[rw::NonTextConflict::key(c.step, c.path)] = res;
                                 }
                                 rewritePrepare(state); // choices can change later commits
                             }});
        f.buttons.push_back({"Cancel", {}});
        dialogs.open(std::move(f));
        return;
    }
    // 2. Commits that become empty (git rebase -i stops on them): keep or drop them.
    if (state->askEmpty && !state->emptied && !r.becameEmpty.empty()) {
        Form f;
        f.title = "Commits become empty";
        f.message = "The changes of these commits are already in the commits they go onto. Keep them as "
                    "empty commits or drop them?";
        for (size_t i = 0; i < r.becameEmpty.size(); ++i) {
            const auto& e = r.becameEmpty[i];
            f.add(Field{Field::Info, "empty_" + std::to_string(i), "",
                (e.commit.empty() ? std::string() : shortId(e.commit) + " ") + e.subject});
        }
        f.buttons.push_back({"Keep them", [this, state](Form&) {
                                 state->emptied = rw::Emptied::Keep;
                                 rewriteDecide(state); // the computed result already keeps them
                             }});
        f.buttons.push_back({"Drop them", [this, state](Form&) {
                                 state->emptied = rw::Emptied::Drop;
                                 rewritePrepare(state);
                             }});
        f.buttons.push_back({"Cancel", {}});
        dialogs.open(std::move(f));
        return;
    }
    if (!r.changed()) {
        m_session.app().notify(App::Notice::Info, state->label, "Nothing to change.");
        return;
    }
    // 3. Confirmation for published commits and branches checked out in other worktrees.
    std::string warnings;
    if (!r.published.empty())
        warnings += std::to_string(r.published.size()) + " of the rewritten commits are already on a remote. "
                                                           "Rewriting them makes your branch diverge from it.\n";
    for (const auto& mv : r.moves)
        if (mv.otherWorktree)
            warnings += mv.ref.substr(11) + " is checked out in another worktree; its files there do not change.\n";
    if (!warnings.empty()) {
        Form f;
        f.title = "Rewrite published history?";
        f.message = warnings + "\nContinue?";
        f.buttons.push_back({"Rewrite", [this, state](Form&) { rewriteApply(state); }});
        f.buttons.push_back({"Cancel", {}});
        dialogs.open(std::move(f));
        return;
    }
    rewriteApply(state);
}

void Actions::rewriteApply(const std::shared_ptr<RewriteState>& state)
{
    run(
        state->label,
        [state](MutationContext& ctx) {
            rw::Plan plan = state->build(ctx.repo());
            plan.resolutions = state->resolutions;
            if (state->emptied)
                plan.emptied = *state->emptied;
            rw::Rewriter rewriter(ctx.cwd());
            rw::Result r = rewriter.compute(plan);
            if (!r.ok)
                throw MutationError{Outcome::Refused,
                    r.unresolved.empty() ? r.error : "the history changed while deciding; try again", {}};
            // Autostash (git rebase --autostash): tracked changes out of the way, back after.
            auto stashTip = [&] { return ctx.gitMayFail({"rev-parse", "-q", "--verify", "refs/stash"}).out; };
            bool stashed = false;
            if (state->autostash) {
                const std::string before = stashTip();
                ctx.git({"stash", "push", "-q", "-m", "ggui: autostash"});
                stashed = stashTip() != before;
            }
            std::string error;
            if (!rewriter.apply(plan, r, error)) {
                if (stashed)
                    ctx.gitMayFail({"stash", "pop", "-q", "--index"});
                throw MutationError{Outcome::Failed, error, error};
            }
            if (stashed && !ctx.gitMayFail({"stash", "pop", "-q", "--index"}).ok())
                ctx.info = "Applying the autostash gave conflicts; your changes are safe in the stash.";
            ctx.worktreeFollowsIndex = r.headAfter != r.headBefore || stashed;
            state->preview = std::move(r);
        },
        [this, state](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok && !state->preview.conflicted.empty()) {
                std::string list;
                for (const auto& id : state->preview.conflicted)
                    list += (list.empty() ? "" : ", ") + shortId(id);
                m_session.app().notify(App::Notice::Warning, state->label,
                    std::to_string(state->preview.conflicted.size()) + " commit(s) now have first-class conflicts: " + list);
            }
            if (e.outcome == Outcome::Ok && !state->preview.resolved.empty()) {
                std::string list;
                for (const auto& id : state->preview.resolved)
                    list += (list.empty() ? "" : ", ") + shortId(id);
                m_session.app().notify(App::Notice::Info, state->label,
                    std::to_string(state->preview.resolved.size()) + " commit(s) no longer have first-class conflicts: " + list);
            }
            if (state->done)
                state->done(e);
            else
                handleDefault(e);
        });
}

// ---- actions built on it ------------------------------------------------------------------

void Actions::reword(const core::Oid& commit, const std::string& message)
{
    const std::string id = commit.hex();
    std::string text = message;
    if (!text.empty() && text.back() != '\n')
        text.push_back('\n');
    rewrite("reword " + id.substr(0, 10), [id, text](git_repository* repo) {
        rw::Plan plan = rw::replayPlan(repo, {id});
        for (auto& s : plan.steps)
            if (s.source == id)
                s.message = text;
        plan.reflogMessage = "ggui: reword";
        plan.rewriteKind = "amend";
        return plan;
    });
}

void Actions::editAuthor(const core::Oid& commit, const std::string& name, const std::string& email)
{
    const std::string id = commit.hex();
    rewrite("edit author of " + id.substr(0, 10), [id, name, email](git_repository* repo) {
        rw::Plan plan = rw::replayPlan(repo, {id});
        gg::git2::Commit c = gg::git2::lookupCommit(repo, *gg::git2::fromHex(id));
        const git_signature* old = git_commit_author(c.get());
        for (auto& s : plan.steps)
            if (s.source == id)
                s.author = rw::Person{name, email, old->when.time, old->when.offset};
        plan.reflogMessage = "ggui: edit author";
        plan.rewriteKind = "amend";
        return plan;
    });
}

} // namespace ggui

namespace ggui {

namespace {

using gg::git2::Commit;
using gg::git2::fromHex;
using gg::git2::lookupCommit;
using gg::git2::toHex;

std::vector<std::string> parentsOf(git_repository* repo, const std::string& id)
{
    Commit c = lookupCommit(repo, *fromHex(id));
    std::vector<std::string> out;
    for (unsigned i = 0; i < git_commit_parentcount(c.get()); ++i)
        out.push_back(toHex(*git_commit_parent_id(c.get(), i)));
    return out;
}

std::string messageOf(git_repository* repo, const std::string& id)
{
    return gg::git2::commitMessage(lookupCommit(repo, *fromHex(id)).get());
}

bool isAncestor(git_repository* repo, const std::string& ancestor, const std::string& of)
{
    if (ancestor == of)
        return true;
    const git_oid a = *fromHex(ancestor), b = *fromHex(of);
    const int r = git_graph_descendant_of(repo, &b, &a);
    git_error_clear();
    return r == 1;
}

// The descendants' plan without some commits (kept by callers as other steps).
rw::Plan replayWithout(git_repository* repo, const std::vector<std::string>& changed, const std::set<std::string>& skip)
{
    rw::Plan plan;
    for (const auto& c : rw::descendants(repo, changed)) {
        if (skip.count(c))
            continue;
        rw::Step s;
        s.source = c;
        plan.steps.push_back(std::move(s));
    }
    return plan;
}

// The first-parent line from `commit` to the tip of HEAD (or of a local branch containing it),
// oldest first; empty when a commit on it has several parents.
std::vector<std::string> chainToTip(git_repository* repo, const std::string& commit)
{
    auto contains = [&](const git_oid& tip) { return isAncestor(repo, commit, toHex(tip)); };
    std::optional<git_oid> tip;
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") == 0 && contains(head))
        tip = head;
    git_error_clear();
    if (!tip)
        gg::git2::forEachReference(repo, [&](git_reference* ref) {
            if (!tip && std::string(git_reference_name(ref)).rfind("refs/heads/", 0) == 0
                && git_reference_type(ref) == GIT_REFERENCE_DIRECT && contains(*git_reference_target(ref)))
                tip = *git_reference_target(ref);
            return true;
        });
    if (!tip)
        return {commit};
    std::vector<std::string> chain;
    std::string c = toHex(*tip);
    while (true) {
        chain.push_back(c);
        if (c == commit)
            break;
        const auto ps = parentsOf(repo, c);
        if (ps.size() != 1)
            return {};
        c = ps.front();
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// Steps replaying the descendants of `commits` that are not in `skip` (after the caller's steps).
void appendOthers(git_repository* repo, rw::Plan& plan, const std::vector<std::string>& commits, const std::set<std::string>& skip)
{
    for (const auto& c : rw::descendants(repo, commits)) {
        if (skip.count(c))
            continue;
        rw::Step s;
        s.source = c;
        plan.steps.push_back(std::move(s));
    }
}

void refuse(const std::string& why) { throw MutationError{Outcome::Refused, why, {}}; }

} // namespace

void Actions::duplicate(const core::Oid& commit, bool withDescendants)
{
    const std::string id = commit.hex();
    rewrite(std::string(withDescendants ? "duplicate branch from " : "duplicate ") + id.substr(0, 10),
        [id, withDescendants](git_repository* repo) {
            std::vector<std::string> chain{id};
            if (withDescendants) {
                chain = chainToTip(repo, id);
                if (chain.empty())
                    refuse("the commits after this one are not a single line (merge commits)");
            }
            rw::Plan plan;
            std::set<std::string> inChain(chain.begin(), chain.end());
            for (const auto& c : chain) {
                rw::Step s;
                s.source = c;
                s.key = "copy:" + c;
                s.forceNew = true;
                s.mapSource = false;
                s.sourceParents = false;
                for (const auto& p : parentsOf(repo, c))
                    s.parents.push_back(inChain.count(p) ? "copy:" + p : "=" + p);
                plan.steps.push_back(std::move(s));
            }
            plan.keepBranches = true;
            plan.detachHeadAt = "copy:" + chain.back();
            plan.reflogMessage = "ggui: duplicate";
            plan.rewriteKind = "rebase";
            return plan;
        });
}

// A revision (branch, tag, id…) as a commit id, on the worker.
static std::string resolveCommit(git_repository* repo, const std::string& rev)
{
    const auto oid = gg::git2::resolve(repo, rev);
    if (!oid)
        refuse("unknown revision '" + rev + "'");
    return toHex(*oid);
}

void Actions::rebaseOnto(const core::Oid& commit, const std::string& destination, bool withDescendants)
{
    const std::string id = commit.hex();
    rewrite(std::string("rebase ") + (withDescendants ? "branch from " : "") + id.substr(0, 10) + " onto " + destination,
        [id, destination, withDescendants](git_repository* repo) {
            const std::string dest = resolveCommit(repo, destination);
            if (isAncestor(repo, id, dest))
                refuse("cannot rebase onto the commit's own descendant");
            rw::Plan plan;
            plan.rebaseLike = true;
            plan.upstream = dest;
            plan.reflogMessage = "ggui: rebase";
            if (withDescendants) {
                plan = [&] {
                    rw::Plan p = rw::replayPlan(repo, {id});
                    p.rebaseLike = true;
                    p.upstream = dest;
                    p.reflogMessage = "ggui: rebase";
                    return p;
                }();
                for (auto& s : plan.steps)
                    if (s.source == id) {
                        s.sourceParents = false;
                        s.parents = {"=" + dest};
                    }
                return plan;
            }
            // Only this commit moves; its children close the gap on its parent.
            rw::Step moved;
            moved.source = id;
            moved.key = "moved";
            moved.mapSource = false;
            moved.sourceParents = false;
            moved.parents = {"=" + dest};
            plan.steps.push_back(moved);
            rw::Plan rest = replayWithout(repo, {id}, {id});
            plan.steps.insert(plan.steps.end(), rest.steps.begin(), rest.steps.end());
            plan.dropped = {id};
            // Branches at the commit follow it.
            gg::git2::forEachReference(repo, [&](git_reference* ref) {
                const std::string name = git_reference_name(ref);
                if (name.rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT
                    && toHex(*git_reference_target(ref)) == id)
                    plan.refsToSteps[name] = "moved";
                return true;
            });
            return plan;
        });
}

void Actions::squash(const core::Oid& commit, const std::string& target, bool combineMessages)
{
    const std::string id = commit.hex();
    rewrite("squash " + id.substr(0, 10), [id, target, combineMessages](git_repository* repo) {
        const auto parents = parentsOf(repo, id);
        if (parents.size() != 1)
            refuse(parents.empty() ? "a root commit has no parent to squash into" : "a merge commit cannot be squashed");
        const std::string t = target.empty() ? parents.front() : resolveCommit(repo, target);
        if (t == id || !isAncestor(repo, t, id))
            refuse("the target must be an ancestor of the commit");
        rw::Plan plan;
        plan.reflogMessage = "ggui: squash";
        for (const auto& c : rw::descendants(repo, {t})) {
            if (c == id)
                continue;
            rw::Step s;
            s.source = c;
            plan.steps.push_back(s);
            if (c == t) {
                rw::Step fold;
                fold.kind = rw::Step::Kind::Squash;
                fold.source = id;
                if (combineMessages) {
                    std::string a = messageOf(repo, t), b = messageOf(repo, id);
                    while (!a.empty() && a.back() == '\n')
                        a.pop_back();
                    fold.message = a + "\n\n" + b;
                }
                plan.steps.push_back(fold);
            }
        }
        return plan;
    });
}

void Actions::squashDescendants(const core::Oid& commit)
{
    const std::string id = commit.hex();
    rewrite("squash descendants into " + id.substr(0, 10), [id](git_repository* repo) {
        const auto chain = chainToTip(repo, id);
        if (chain.size() < 2)
            refuse(chain.empty() ? "the descendants are not a single line" : "the commit has no descendants");
        rw::Plan plan;
        plan.reflogMessage = "ggui: squash";
        for (size_t i = 0; i < chain.size(); ++i) {
            rw::Step s;
            s.source = chain[i];
            if (i > 0)
                s.kind = rw::Step::Kind::Squash;
            plan.steps.push_back(s);
        }
        // Everything that pointed into the line now points at the squashed commit.
        gg::git2::forEachReference(repo, [&](git_reference* ref) {
            const std::string name = git_reference_name(ref);
            if (name.rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT
                && toHex(*git_reference_target(ref)) == chain.back())
                plan.refsToSteps[name] = id;
            return true;
        });
        appendOthers(repo, plan, chain, std::set<std::string>(chain.begin(), chain.end()));
        return plan;
    });
}

void Actions::split(const core::Oid& commit, const std::vector<std::string>& paths, const std::string& firstMessage)
{
    const std::string id = commit.hex();
    std::string msg = firstMessage;
    if (!msg.empty() && msg.back() != '\n')
        msg.push_back('\n');
    rewrite("split " + id.substr(0, 10), [id, paths, msg](git_repository* repo) {
        rw::Plan plan = rw::replayPlan(repo, {id});
        plan.reflogMessage = "ggui: split";
        for (size_t i = 0; i < plan.steps.size(); ++i) {
            if (plan.steps[i].source != id)
                continue;
            rw::Step first;
            first.source = id;
            first.key = "split:" + id;
            first.onlyPaths = paths;
            first.mapSource = false;
            first.forceNew = true;
            if (!msg.empty())
                first.message = msg;
            plan.steps[i].sourceParents = false;
            plan.steps[i].parents = {"split:" + id};
            plan.steps.insert(plan.steps.begin() + static_cast<long>(i), first);
            break;
        }
        return plan;
    });
}

void Actions::abandon(const core::Oid& commit, bool withDescendants, std::function<void()> then)
{
    const std::string id = commit.hex();
    rewrite(std::string(withDescendants ? "abandon branch from " : "abandon ") + id.substr(0, 10),
        [id, withDescendants](git_repository* repo) {
            rw::Plan plan;
            plan.reflogMessage = "ggui: abandon";
            if (withDescendants) {
                plan.dropped = rw::descendants(repo, {id});
                return plan;
            }
            plan = replayWithout(repo, {id}, {id});
            plan.reflogMessage = "ggui: abandon";
            plan.dropped = {id};
            return plan;
        },
        [this, then](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok && then)
                then();
            else
                handleDefault(e);
        });
}

void Actions::restorePaths(const core::Oid& commit, const std::string& from, const std::vector<std::string>& paths)
{
    const std::string id = commit.hex();
    rewrite("restore " + std::to_string(paths.size()) + " path(s) in " + id.substr(0, 10), [id, from, paths](git_repository* repo) {
        using namespace gg::git2;
        const std::string source = resolveCommit(repo, from);
        Commit c = lookupCommit(repo, *fromHex(id));
        Commit s = lookupCommit(repo, *fromHex(source));
        Tree target = commitTree(c.get());
        Tree src = commitTree(s.get());
        std::vector<git_tree_update> updates;
        std::vector<TreeEntry> keep;
        for (const auto& p : paths) {
            git_tree_entry* raw = nullptr;
            git_tree_update u{};
            u.path = p.c_str();
            if (git_tree_entry_bypath(&raw, src.get(), p.c_str()) == 0) {
                keep.emplace_back(raw);
                u.action = GIT_TREE_UPDATE_UPSERT;
                u.id = *git_tree_entry_id(raw);
                u.filemode = git_tree_entry_filemode(raw);
            } else {
                git_error_clear();
                u.action = GIT_TREE_UPDATE_REMOVE;
            }
            updates.push_back(u);
        }
        git_oid tree;
        check(git_tree_create_updated(&tree, repo, target.get(), updates.size(), updates.data()), "git_tree_create_updated");
        rw::Plan plan = rw::replayPlan(repo, {id});
        plan.reflogMessage = "ggui: restore";
        for (auto& st : plan.steps)
            if (st.source == id)
                st.tree = toHex(tree);
        return plan;
    });
}

void Actions::simplifyParents(const core::Oid& commit)
{
    const std::string id = commit.hex();
    rewrite("simplify parents of " + id.substr(0, 10), [id](git_repository* repo) {
        const auto parents = parentsOf(repo, id);
        std::vector<std::string> kept;
        for (size_t i = 0; i < parents.size(); ++i) {
            bool redundant = false;
            for (size_t j = 0; j < parents.size() && !redundant; ++j)
                redundant = i != j && parents[i] != parents[j] && isAncestor(repo, parents[i], parents[j]);
            if (!redundant && std::find(kept.begin(), kept.end(), parents[i]) == kept.end())
                kept.push_back(parents[i]);
        }
        if (kept.size() == parents.size())
            refuse("no parent is redundant");
        rw::Plan plan = rw::replayPlan(repo, {id});
        plan.reflogMessage = "ggui: simplify parents";
        gg::git2::Commit c = gg::git2::lookupCommit(repo, *fromHex(id));
        for (auto& s : plan.steps)
            if (s.source == id) {
                s.sourceParents = false;
                for (const auto& p : kept)
                    s.parents.push_back("=" + p);
                s.tree = toHex(*git_commit_tree_id(c.get()));
            }
        return plan;
    });
}

void Actions::insertCommit(const core::Oid& at, bool before, const std::string& message)
{
    const std::string id = at.hex();
    rewrite(std::string("new commit ") + (before ? "before " : "after ") + id.substr(0, 10),
        [id, before, message](git_repository* repo) { return rw::insertPlan(repo, id, before, message); });
}

void Actions::mergeIntoHead(const std::string& branch, const std::string& message)
{
    std::string msg = message.empty() ? "Merge branch '" + branch + "'\n" : message;
    if (msg.back() != '\n')
        msg.push_back('\n');
    rewrite("merge " + branch + " into HEAD", [branch, msg](git_repository* repo) {
        git_oid head, other;
        if (git_reference_name_to_id(&head, repo, "HEAD") != 0)
            refuse("HEAD has no commit to merge into");
        // A branch (local or remote-tracking) by name, else any revision (a commit from History).
        if (git_reference_name_to_id(&other, repo, ("refs/heads/" + branch).c_str()) != 0
            && git_reference_name_to_id(&other, repo, ("refs/remotes/" + branch).c_str()) != 0)
            other = *fromHex(resolveCommit(repo, branch));
        git_error_clear();
        if (isAncestor(repo, toHex(other), toHex(head)))
            refuse(branch + " is already merged");
        rw::Plan plan;
        rw::Step merge;
        merge.kind = rw::Step::Kind::Merge;
        merge.key = "merge";
        merge.sourceParents = false;
        merge.forceNew = true;
        merge.parents = {"=" + toHex(head), "=" + toHex(other)};
        merge.message = msg;
        plan.steps.push_back(merge);
        plan.reflogMessage = "ggui: merge " + branch;
        git_reference* rawHead = nullptr;
        if (git_reference_lookup(&rawHead, repo, "HEAD") == 0) {
            gg::git2::Reference h(rawHead);
            if (git_reference_type(h.get()) == GIT_REFERENCE_SYMBOLIC)
                plan.refsToSteps[git_reference_symbolic_target(h.get())] = "merge";
            else
                plan.detachHeadAt = "merge";
        }
        git_error_clear();
        return plan;
    });
}

void Actions::reorder(const core::Oid& commit, const core::Oid& anchor, bool after, bool copy)
{
    const std::string id = commit.hex(), at = anchor.hex();
    rewrite(std::string(copy ? "copy " : "move ") + id.substr(0, 10) + (after ? " after " : " before ") + at.substr(0, 10),
        [id, at, after, copy](git_repository* repo) {
            if (id == at)
                refuse("a commit cannot move relative to itself");
            // Work on the linear chain from the older of the two to its tip.
            const std::string base = isAncestor(repo, id, at) ? id : at;
            if (!isAncestor(repo, base, id) || !isAncestor(repo, base, at))
                refuse("both commits must be on the same line of history");
            // The line from the older one to the tip that contains the younger one.
            const std::string younger = base == id ? at : id;
            auto chain = chainToTip(repo, younger);
            if (chain.empty())
                refuse("the commits in between are not a single line");
            {
                std::vector<std::string> back = chainToTip(repo, base);
                if (back.empty() || std::find(back.begin(), back.end(), younger) == back.end())
                    refuse("both commits must be on the same line of history");
                chain = back;
            }
            std::vector<std::string> order;
            for (const auto& c : chain)
                if (c != id || copy)
                    order.push_back(c);
            auto pos = std::find(order.begin(), order.end(), at);
            if (pos == order.end())
                refuse("the anchor commit is not on the line");
            const std::string moving = copy ? "copy:" + id : id;
            order.insert(after ? pos + 1 : pos, moving);
            rw::Plan plan;
            plan.reflogMessage = copy ? "ggui: copy" : "ggui: reorder";
            // The first commit of the new order takes the line's old parent (none at the root).
            const auto baseParents = parentsOf(repo, chain.front());
            std::string previous = baseParents.empty() ? std::string() : "=" + baseParents.front();
            for (const auto& c : order) {
                rw::Step s;
                s.source = c == moving ? id : c;
                s.key = c;
                s.sourceParents = false;
                if (!previous.empty())
                    s.parents = {previous};
                if (c == moving && copy) {
                    s.forceNew = true;
                    s.mapSource = false;
                }
                plan.steps.push_back(s);
                previous = c;
            }
            appendOthers(repo, plan, chain, std::set<std::string>(chain.begin(), chain.end()));
            // Whatever pointed at the old tip points at the new one.
            gg::git2::forEachReference(repo, [&](git_reference* ref) {
                const std::string name = git_reference_name(ref);
                if (name.rfind("refs/heads/", 0) == 0 && git_reference_type(ref) == GIT_REFERENCE_DIRECT
                    && toHex(*git_reference_target(ref)) == chain.back())
                    plan.refsToSteps[name] = order.back();
                return true;
            });
            git_oid head;
            if (git_repository_head_detached(repo) == 1 && git_reference_name_to_id(&head, repo, "HEAD") == 0
                && toHex(head) == chain.back())
                plan.detachHeadAt = order.back();
            git_error_clear();
            return plan;
        });
}

} // namespace ggui

namespace ggui {

void Actions::restoreWorktree(const std::string& from, const std::vector<std::string>& paths)
{
    run("restore " + std::to_string(paths.size()) + " path(s) from " + from, [from, paths](MutationContext& ctx) {
        std::vector<std::string> args{"restore", "--source=" + from, "--staged", "--worktree", "--"};
        args.insert(args.end(), paths.begin(), paths.end());
        ctx.git(args);
        ctx.worktreeFollowsIndex = true;
    });
}

void Actions::mergeNative(const std::string& branch)
{
    // Plain git merge: may stop with index conflicts (then the native conflict flow takes over).
    run("merge " + branch, [branch](MutationContext& ctx) {
        ctx.env.emplace_back("GIT_EDITOR", "true");
        ctx.worktreeFollowsIndex = true;
        const auto r = ctx.gitMayFail({"merge", "--no-edit", branch});
        if (!r.ok() && !std::filesystem::exists(ctx.cwd() / ".git" / "MERGE_HEAD"))
            throw MutationError{Outcome::Failed, r.message(), r.message()};
    });
}

void Actions::rebaseHeadOnto(const std::string& branch)
{
    rewrite("rebase HEAD onto " + branch, [branch](git_repository* repo) {
        const std::string dest = resolveCommit(repo, branch);
        git_oid head;
        if (git_reference_name_to_id(&head, repo, "HEAD") != 0)
            refuse("HEAD has no commit");
        // HEAD's own commits: reachable from HEAD, not from the destination (oldest first).
        git_revwalk* raw = nullptr;
        gg::git2::check(git_revwalk_new(&raw, repo), "git_revwalk_new");
        gg::git2::Revwalk walk(raw);
        git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE);
        git_revwalk_push(walk.get(), &head);
        const git_oid d = *fromHex(dest);
        git_revwalk_hide(walk.get(), &d);
        std::vector<std::string> own;
        git_oid id;
        while (git_revwalk_next(&id, walk.get()) == 0)
            own.push_back(toHex(id));
        git_error_clear();
        if (own.empty())
            refuse("HEAD is already on " + branch);
        rw::Plan plan;
        plan.rebaseLike = true;
        plan.upstream = dest;
        plan.reflogMessage = "ggui: rebase onto " + branch;
        std::set<std::string> mine(own.begin(), own.end());
        for (const auto& c : own) {
            rw::Step s;
            s.source = c;
            s.sourceParents = false;
            for (const auto& p : parentsOf(repo, c))
                if (mine.count(p))
                    s.parents.push_back(p);
            if (s.parents.empty())
                s.parents.push_back("=" + dest);
            plan.steps.push_back(s);
        }
        return plan;
    });
}

} // namespace ggui

namespace ggui {

void Actions::moveChanges(const core::Oid& commit, MoveTo to, const std::vector<std::string>& paths, const std::string& patch)
{
    const std::string id = commit.hex();
    static const char* names[] = {"to parent", "to child", "to the active commit", "to the working tree", "revert"};
    const std::string what = patch.empty() ? std::to_string(paths.size()) + " file(s)" : std::string("lines");
    rewrite((to == MoveTo::Revert ? "revert " + what + " of " : "move " + what + " of ") + id.substr(0, 10) + " "
            + (to == MoveTo::Revert ? "" : names[static_cast<int>(to)]),
        [id, to, paths, patch](git_repository* repo) {
            using namespace gg::git2;
            const auto parents = parentsOf(repo, id);
            if (parents.size() != 1)
                refuse("moving changes needs a commit with exactly one parent");
            const std::string parent = parents.front();
            const std::string parentTree = toHex(*git_commit_tree_id(lookupCommit(repo, *fromHex(parent)).get()));
            const std::string ownTree = toHex(*git_commit_tree_id(lookupCommit(repo, *fromHex(id)).get()));
            // The selection's change applied to a tree (forward) or taken out of one (backward).
            auto withFiles = [&](const std::string& tree, const std::string& fromTree) {
                Tree target = lookupTree(repo, *fromHex(tree));
                Tree source = lookupTree(repo, *fromHex(fromTree));
                std::vector<git_tree_update> updates;
                std::vector<TreeEntry> keep;
                for (const auto& p : paths) {
                    git_tree_update u{};
                    u.path = p.c_str();
                    git_tree_entry* raw = nullptr;
                    if (git_tree_entry_bypath(&raw, source.get(), p.c_str()) == 0) {
                        keep.emplace_back(raw);
                        u.action = GIT_TREE_UPDATE_UPSERT;
                        u.id = *git_tree_entry_id(raw);
                        u.filemode = git_tree_entry_filemode(raw);
                    } else {
                        git_error_clear();
                        u.action = GIT_TREE_UPDATE_REMOVE;
                    }
                    updates.push_back(u);
                }
                git_oid out;
                check(git_tree_create_updated(&out, repo, target.get(), updates.size(), updates.data()), "git_tree_create_updated");
                return toHex(out);
            };
            auto forward = [&](const std::string& tree) {
                return patch.empty() ? withFiles(tree, ownTree) : rw::applyPatchToTree(repo, tree, patch);
            };
            auto backward = [&](const std::string& tree) {
                return patch.empty() ? withFiles(tree, parentTree) : rw::applyPatchToTree(repo, tree, rw::reversePatch(patch));
            };
            rw::Plan plan;
            plan.reflogMessage = "ggui: move changes";
            auto treeOf = [&](const std::string& c) { return toHex(*git_commit_tree_id(lookupCommit(repo, *fromHex(c)).get())); };
            switch (to) {
            case MoveTo::Parent: {
                // The parent gains them; this commit keeps its tree (so it no longer changes them).
                plan = rw::replayPlan(repo, {parent});
                plan.reflogMessage = "ggui: move changes to parent";
                const std::string gained = forward(parentTree);
                for (auto& st : plan.steps) {
                    if (st.source == parent)
                        st.tree = gained;
                    else if (st.source == id)
                        st.tree = ownTree;
                }
                break;
            }
            case MoveTo::Child:
            case MoveTo::Active: {
                std::string receiver;
                if (to == MoveTo::Child) {
                    const auto chain = chainToTip(repo, id);
                    if (chain.size() < 2)
                        refuse("the commit has no child on its line");
                    receiver = chain[1];
                } else {
                    git_oid head;
                    if (git_reference_name_to_id(&head, repo, "HEAD") != 0 || toHex(head) == id || !isAncestor(repo, id, toHex(head)))
                        refuse("the checked-out commit must come after this one");
                    receiver = toHex(head);
                }
                plan = rw::replayPlan(repo, {id});
                plan.reflogMessage = "ggui: move changes";
                const std::string without = backward(ownTree);
                const std::string receiverTree = treeOf(receiver);
                for (auto& st : plan.steps) {
                    if (st.source == id)
                        st.tree = without;
                    else if (st.source == receiver)
                        st.tree = receiverTree; // it now makes those changes itself
                }
                break;
            }
            case MoveTo::WorkingTree:
            case MoveTo::Revert: {
                plan = rw::replayPlan(repo, {id});
                plan.reflogMessage = to == MoveTo::Revert ? "ggui: revert changes" : "ggui: uncommit changes";
                const std::string without = backward(ownTree);
                for (auto& st : plan.steps)
                    if (st.source == id)
                        st.tree = without;
                plan.keepWorktree = to == MoveTo::WorkingTree;
                break;
            }
            }
            return plan;
        });
}

} // namespace ggui

namespace ggui {

void Actions::absorb(const core::Oid& commit, const std::vector<std::string>& paths)
{
    const std::string id = commit.hex();
    rewrite("fold " + std::to_string(paths.size()) + " file(s) into " + id.substr(0, 10), [id, paths](git_repository* repo) {
        const char* wd = git_repository_workdir(repo);
        if (!wd)
            refuse("a bare repository has no working tree");
        git_oid head;
        if (git_reference_name_to_id(&head, repo, "HEAD") != 0 || !isAncestor(repo, id, toHex(head)))
            refuse("the commit must be the checked-out one or one of its ancestors");
        rw::Plan plan = rw::replayPlan(repo, {id});
        plan.reflogMessage = "ggui: fold working tree changes";
        plan.keepWorktree = true; // the files already are what the commits will hold
        for (auto& st : plan.steps)
            if (st.source == id)
                for (const auto& p : paths) {
                    const std::filesystem::path file = std::filesystem::path(wd) / p;
                    if (std::filesystem::exists(file)) {
                        std::ifstream in(file, std::ios::binary);
                        std::ostringstream ss;
                        ss << in.rdbuf();
                        st.setFiles.emplace_back(p, ss.str());
                    } else {
                        st.setFiles.emplace_back(p, std::nullopt);
                    }
                }
        return plan;
    });
}

} // namespace ggui
