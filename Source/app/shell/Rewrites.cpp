// History editing through the in-memory rewrite engine (product spec §4.3, §4.10 pre-flight).
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Keep.hpp>

#include <algorithm>
#include <cctype>
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
            // Before anything that can throw: apply deleted the keep refs of the rewritten commits.
            ctx.keepExtra = r.keepExtra;
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

void Actions::squashRange(const std::vector<core::Oid>& commits, const std::string& message)
{
    std::vector<std::string> ids; // oldest first
    for (auto it = commits.rbegin(); it != commits.rend(); ++it)
        ids.push_back(it->hex());
    rewrite("squash " + std::to_string(ids.size()) + " commits", [ids, message](git_repository* repo) {
        if (ids.size() < 2)
            refuse("nothing to squash");
        for (size_t i = 0; i < ids.size(); ++i) {
            const auto parents = parentsOf(repo, ids[i]);
            if (parents.size() > 1)
                refuse("a merge commit cannot be squashed");
            if (i > 0 && (parents.empty() || parents.front() != ids[i - 1]))
                refuse("the commits to squash are not adjacent");
        }
        std::string text = message;
        while (!text.empty() && text.back() == '\n')
            text.pop_back();
        text.push_back('\n');
        const std::set<std::string> folded(ids.begin() + 1, ids.end());
        rw::Plan plan;
        plan.reflogMessage = "ggui: squash";
        for (const auto& c : rw::descendants(repo, {ids.front()})) {
            if (folded.count(c))
                continue;
            rw::Step s;
            s.source = c;
            plan.steps.push_back(s);
            if (c == ids.front()) {
                for (size_t i = 1; i < ids.size(); ++i) {
                    rw::Step fold;
                    fold.kind = rw::Step::Kind::Squash;
                    fold.source = ids[i];
                    fold.message = text;
                    plan.steps.push_back(fold);
                }
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
    rewrite(std::string(withDescendants ? "drop commit and descendants from " : "drop commit ") + id.substr(0, 10),
        [id, withDescendants](git_repository* repo) {
            rw::Plan plan;
            plan.reflogMessage = "ggui: drop";
            if (withDescendants) {
                plan.dropped = rw::descendants(repo, {id});
                return plan;
            }
            plan = replayWithout(repo, {id}, {id});
            plan.reflogMessage = "ggui: drop";
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
        if (const std::string target = gg::git2::headTarget(repo); !target.empty())
            plan.refsToSteps[target] = "merge";
        else
            plan.detachHeadAt = "merge";
        return plan;
    });
}

namespace {

std::string trimEnd(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

// HEAD's commit, which a revert or cherry-pick of `id` goes onto (refused when there is none,
// or for a pick of a commit HEAD already contains).
std::string pickOnto(git_repository* repo, const std::string& id, bool revert)
{
    git_oid head;
    if (git_reference_name_to_id(&head, repo, "HEAD") != 0) {
        git_error_clear();
        refuse("HEAD has no commit yet");
    }
    if (!revert && isAncestor(repo, id, toHex(head)))
        refuse("the commit is already in HEAD's history");
    return toHex(head);
}

// A commit's change to a selection of files (`paths`) or lines (`patch`, old -> new as the
// commit's diff has them), measured against its FIRST parent.
struct SelectionTrees {
    std::string parent, parentTree, ownTree;
    std::vector<std::string> paths;
    std::string patch;
    // The selection's change applied to a tree (forward) or taken out of one (backward).
    std::string forward(git_repository* repo, const std::string& tree) const
    {
        return patch.empty() ? withFiles(repo, tree, ownTree) : rw::applyPatchToTree(repo, tree, rw::contentPatch(patch, true));
    }
    std::string backward(git_repository* repo, const std::string& tree) const
    {
        return patch.empty() ? withFiles(repo, tree, parentTree)
                             : rw::applyPatchToTree(repo, tree, rw::reversePatch(rw::contentPatch(patch, false)));
    }
    // `tree` with the selected files taken from `fromTree` (removed where it lacks them).
    std::string withFiles(git_repository* repo, const std::string& tree, const std::string& fromTree) const
    {
        using namespace gg::git2;
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
    }
};

// What a selection reverts, for the message: the files, or the file the lines are in.
std::string revertWhat(const std::vector<std::string>& paths, const std::string& patch)
{
    if (patch.empty()) {
        std::string desc;
        for (const auto& p : paths)
            desc += (desc.empty() ? "" : ", ") + p;
        return desc;
    }
    // "diff --git a/<old> b/<new>": the file as this commit has it.
    const std::string first = patch.substr(0, patch.find('\n'));
    const size_t sep = first.find(" b/");
    return "some lines of " + (sep == std::string::npos ? std::string("a file") : first.substr(sep + 3));
}

SelectionTrees selectionTrees(git_repository* repo, const std::string& id, const std::vector<std::string>& paths,
    const std::string& patch)
{
    using namespace gg::git2;
    const auto parents = parentsOf(repo, id);
    if (parents.empty())
        refuse("the commit has no parent");
    SelectionTrees t;
    t.parent = parents.front();
    t.parentTree = toHex(*git_commit_tree_id(lookupCommit(repo, *fromHex(t.parent)).get()));
    t.ownTree = toHex(*git_commit_tree_id(lookupCommit(repo, *fromHex(id)).get()));
    t.paths = paths;
    t.patch = patch;
    return t;
}

} // namespace

std::string revertMessage(const std::string& message, const std::string& id)
{
    return "Revert \"" + trimEnd(message.substr(0, message.find('\n'))) + "\"\n\nThis reverts commit " + id + ".";
}

std::string revertPartMessage(const std::string& message, const std::string& id, const std::string& what)
{
    return "Revert \"" + trimEnd(message.substr(0, message.find('\n'))) + "\"\n\nThis reverts part of commit " + id + ": " + what + ".";
}

std::string cherryPickMessage(const std::string& message, const std::string& id)
{
    const std::string body = trimEnd(message);
    const std::string line = "(cherry picked from commit " + id + ")";
    for (const auto& l : gg::splitLines(body))
        if (trimEnd(l) == line)
            return body;
    return body + "\n\n" + line;
}

namespace {

// A revert or cherry-pick of several commits: `commits` is the History order (newest first); a
// revert takes them in that order, a cherry-pick oldest first (as `git cherry-pick a b c`).
std::vector<std::string> pickOrder(const std::vector<core::Oid>& commits, bool revert)
{
    std::vector<std::string> ids;
    for (const auto& c : commits)
        ids.push_back(c.hex());
    if (!revert)
        std::reverse(ids.begin(), ids.end());
    return ids;
}

// HEAD's commit for all of `ids` (see pickOnto). A refusal names the commit when there are several.
std::string pickAllOnto(git_repository* repo, const std::vector<std::string>& ids, bool revert)
{
    std::string head;
    for (const auto& id : ids) {
        try {
            head = pickOnto(repo, id, revert);
        } catch (MutationError& e) {
            if (ids.size() > 1)
            {
                const std::string message = messageOf(repo, id);
                e.message = "commit " + id.substr(0, 10) + " (" + trimEnd(message.substr(0, message.find('\n'))) + "): " + e.message;
            }
            throw;
        }
    }
    return head;
}

std::string pickMessage(git_repository* repo, const std::string& id, bool revert)
{
    const std::string message = messageOf(repo, id);
    return revert ? revertMessage(message, id) : cherryPickMessage(message, id);
}

} // namespace

void Actions::revertOrPick(const std::vector<core::Oid>& commits, bool revert, bool commitIt)
{
    if (commits.empty())
        return;
    const std::vector<std::string> ids = pickOrder(commits, revert);
    const std::string label = std::string(revert ? "revert " : "cherry-pick ")
        + (ids.size() == 1 ? ids.front().substr(0, 10) : std::to_string(ids.size()) + " commits");
    if (commitIt) {
        // In memory, like Merge into HEAD: new commits on HEAD (each on the one before), one ref update, one Undo.
        rewrite(label, [ids, revert](git_repository* repo) {
            const std::string head = pickAllOnto(repo, ids, revert);
            rw::Plan plan;
            for (size_t i = 0; i < ids.size(); ++i) {
                rw::Step s;
                s.source = ids[i];
                s.key = "pick" + std::to_string(i);
                s.revert = revert;
                s.forceNew = true;
                s.mapSource = false;
                s.sourceParents = false;
                s.parents = {i == 0 ? "=" + head : "pick" + std::to_string(i - 1)};
                s.message = pickMessage(repo, ids[i], revert) + "\n";
                plan.steps.push_back(std::move(s));
            }
            const std::string last = plan.steps.back().key;
            plan.emptied = rw::Emptied::Ask; // nothing left to change: keep an empty commit or stop
            plan.reflogMessage = std::string("ggui: ") + (revert ? "revert" : "cherry-pick");
            if (const std::string target = gg::git2::headTarget(repo); !target.empty())
                plan.refsToSteps[target] = last;
            else
                plan.detachHeadAt = last;
            return plan;
        });
        return;
    }
    // Plain git into the index and working tree, one commit after the other (a merge commit needs
    // -m 1, a plain commit rejects it); conflicts stop natively, at the commit that has them.
    run(label + " (no commit)",
        [ids, revert](MutationContext& ctx) {
            git_repository* repo = ctx.repo();
            pickAllOnto(repo, ids, revert);
            // Abort (git reset --merge) would drop staged changes along with the pick's.
            if (!ctx.gitMayFail({"diff", "--cached", "--quiet"}).ok())
                refuse("the index has staged changes: commit, stash or unstage them first");
            ctx.env.emplace_back("GIT_EDITOR", "true");
            ctx.worktreeFollowsIndex = true;
            std::string pending; // the messages of the commits applied so far, blank line between
            bool conflicts = false;
            std::string stoppedAt;
            size_t stoppedIndex = 0;
            for (size_t i = 0; i < ids.size() && !conflicts; ++i) {
                const std::string& id = ids[i];
                std::vector<std::string> args{revert ? "revert" : "cherry-pick", "--no-commit"};
                if (parentsOf(repo, id).size() > 1) {
                    args.emplace_back("-m");
                    args.emplace_back("1");
                }
                args.push_back(id);
                const auto r = ctx.gitMayFail(args);
                conflicts = !gg::trim(ctx.gitMayFail({"ls-files", "-u"}).out).empty();
                if (!r.ok() && !conflicts) {
                    const std::string all = r.err + r.out;
                    std::string message = r.message();
                    std::string detail = all;
                    if (ids.size() > 1) {
                        // Nothing of an earlier commit stays: the index was clean, so reset --merge restores it.
                        const bool undone = i == 0 || ctx.gitMayFail({"reset", "--merge"}).ok();
                        const std::string which = "commit " + id.substr(0, 10) + " (" + std::to_string(i + 1) + " of "
                            + std::to_string(ids.size()) + ") failed: ";
                        const std::string after = i == 0 ? "" : undone ? "The earlier commits are undone." : "The changes of the earlier commits stay in the index.";
                        message = which + message + (after.empty() ? "" : ". " + after);
                        // The error dialog shows the detail when there is one.
                        detail = which + gg::trim(all) + (after.empty() ? "" : "\n" + after);
                    }
                    throw MutationError{core::classifyFailure(all), message, detail};
                }
                pending += (pending.empty() ? "" : "\n\n") + pickMessage(repo, id, revert);
                stoppedAt = id;
                stoppedIndex = i;
            }
            // The pending message (git commit and --continue take it from MERGE_MSG).
            {
                std::ofstream out(std::filesystem::path(git_repository_path(repo)) / "MERGE_MSG", std::ios::binary | std::ios::trunc);
                out << pending << "\n";
            }
            if (!conflicts)
                return;
            // git revert leaves REVERT_HEAD; cherry-pick --no-commit leaves no state: the
            // pick in progress (Continue/Abort) as a plain cherry-pick would leave it.
            if (!revert)
                ctx.git({"update-ref", "CHERRY_PICK_HEAD", stoppedAt});
            const std::string what = revert ? "revert" : "cherry-pick";
            if (ids.size() == 1) {
                ctx.info = "The " + what + " has conflicts: resolve them, then Continue (or commit), or Abort.";
            } else {
                const size_t left = ids.size() - stoppedIndex - 1;
                ctx.info = "The " + what + " has conflicts in commit " + stoppedAt.substr(0, 10) + " (" + std::to_string(stoppedIndex + 1)
                    + " of " + std::to_string(ids.size()) + "). "
                    + (left == 0 ? std::string()
                                 : std::to_string(left) + (left == 1 ? " commit is" : " commits are") + " not applied. ")
                    + "Resolve the conflicts, then Continue (or commit), or Abort.";
            }
        },
        [this, label](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok && !e.message.empty())
                m_session.app().notify(App::Notice::Warning, label, e.message);
            else
                handleDefault(e);
        });
}

void Actions::revertChanges(const core::Oid& commit, const std::vector<std::string>& paths, const std::string& patch, bool andCommit)
{
    const std::string id = commit.hex();
    const std::string what = patch.empty() ? std::to_string(paths.size()) + " file(s)" : std::string("lines");
    const std::string label = "revert " + what + " of " + id.substr(0, 10);
    if (!andCommit) {
        // The inverse as a patch (from the commit's tree to the same without the selection) applied
        // to the index and working tree; conflicts leave the Reverting state, as git revert does.
        run(label + " (no commit)",
            [id, paths, patch](MutationContext& ctx) {
                git_repository* repo = ctx.repo();
                pickOnto(repo, id, true);
                const SelectionTrees trees = selectionTrees(repo, id, paths, patch);
                const std::string without = trees.backward(repo, trees.ownTree);
                // Abort (git reset --merge) would drop staged changes along with the revert's.
                if (!ctx.gitMayFail({"diff", "--cached", "--quiet"}).ok())
                    refuse("the index has staged changes: commit, stash or unstage them first");
                std::vector<std::string> touched;
                {
                    const std::string out = ctx.git({"diff-tree", "-r", "-z", "--name-only", "--no-renames", trees.ownTree, without}).out;
                    size_t at = 0;
                    while (at < out.size()) {
                        const size_t end = out.find('\0', at);
                        touched.push_back(out.substr(at, end == std::string::npos ? std::string::npos : end - at));
                        if (end == std::string::npos)
                            break;
                        at = end + 1;
                    }
                }
                if (touched.empty())
                    refuse("the commit makes no such change");
                std::vector<std::string> check{"diff", "--quiet", "--"};
                for (const auto& t : touched)
                    check.push_back(":(literal)" + t);
                if (!ctx.gitMayFail(check).ok()) {
                    // The first file with local changes, for the message.
                    std::string which = touched.front();
                    for (const auto& t : touched)
                        if (!ctx.gitMayFail({"diff", "--quiet", "--", ":(literal)" + t}).ok()) {
                            which = t;
                            break;
                        }
                    refuse(which + " has local changes: commit, stash or discard them first");
                }
                const std::string inverse = ctx.git({"diff-tree", "-r", "-p", "--binary", "--full-index", "--no-renames", trees.ownTree, without}).out;
                ctx.worktreeFollowsIndex = true;
                const auto r = ctx.gitMayFail({"apply", "--index", "--3way", "--whitespace=nowarn", "-"}, inverse);
                const bool conflicts = !gg::trim(ctx.gitMayFail({"ls-files", "-u"}).out).empty();
                if (!r.ok() && !conflicts) {
                    const std::string all = r.err + r.out;
                    throw MutationError{core::classifyFailure(all), r.message(), all};
                }
                // The pending message (git commit and --continue take it from MERGE_MSG).
                {
                    std::ofstream out(std::filesystem::path(git_repository_path(repo)) / "MERGE_MSG", std::ios::binary | std::ios::trunc);
                    out << revertPartMessage(messageOf(repo, id), id, revertWhat(paths, patch)) << "\n";
                }
                if (!conflicts)
                    return;
                ctx.git({"update-ref", "REVERT_HEAD", id});
                ctx.info = "The revert has conflicts: resolve them, then Continue (or commit), or Abort.";
            },
            [this, label](const core::MutationFinishedEvent& e) {
                if (e.outcome == Outcome::Ok && !e.message.empty())
                    m_session.app().notify(App::Notice::Warning, label, e.message);
                else
                    handleDefault(e);
            });
        return;
    }
    // In memory, like revertOrPick: one new commit on HEAD, one ref update, one Undo.
    rewrite(label, [id, paths, patch](git_repository* repo) {
        const std::string head = pickOnto(repo, id, true);
        const SelectionTrees trees = selectionTrees(repo, id, paths, patch);
        const std::string without = trees.backward(repo, trees.ownTree);
        const std::string desc = revertWhat(paths, patch);
        rw::Step s;
        s.source = id;
        s.key = "pick";
        s.revert = true;
        s.revertTree = without;
        s.forceNew = true;
        s.mapSource = false;
        s.sourceParents = false;
        s.parents = {"=" + head};
        s.message = revertPartMessage(messageOf(repo, id), id, desc) + "\n";
        rw::Plan plan;
        plan.steps.push_back(std::move(s));
        plan.emptied = rw::Emptied::Ask;
        plan.reflogMessage = "ggui: revert changes";
        if (const std::string target = gg::git2::headTarget(repo); !target.empty())
            plan.refsToSteps[target] = "pick";
        else
            plan.detachHeadAt = "pick";
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
    }, {}, false, true, true, true);
}

void Actions::rebaseHeadOnto(const std::string& branch) { rebaseTipOnto("HEAD", branch); }

void Actions::rebaseTipOnto(const std::string& tipRev, const std::string& destination)
{
    // A full commit id reads as its short form in the label and the reflog message.
    const bool fullId = tipRev.size() == 40 && tipRev.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    const std::string shown = fullId ? tipRev.substr(0, 10) : tipRev;
    rewrite("rebase " + shown + " onto " + destination, [tipRev, shown, destination](git_repository* repo) {
        const std::string dest = resolveCommit(repo, destination);
        const bool isHead = tipRev == "HEAD";
        std::string tip;
        if (isHead) {
            git_oid head;
            if (git_reference_name_to_id(&head, repo, "HEAD") != 0)
                refuse("HEAD has no commit");
            tip = toHex(head);
        } else {
            tip = resolveCommit(repo, tipRev);
            // A branch checked out elsewhere would move under that worktree's files.
            const std::string shortName = tipRev.rfind("refs/heads/", 0) == 0 ? tipRev.substr(11) : tipRev;
            const auto elsewhere = gg::git2::branchesInOtherWorktrees(repo);
            const auto it = elsewhere.find("refs/heads/" + shortName);
            if (it != elsewhere.end())
                refuse(tipRev + " is checked out in the worktree " + it->second);
            // The new commits need a ref to hold them: a local branch, HEAD or a keep ref reaching the tip.
            bool held = false;
            git_oid head;
            if (git_reference_name_to_id(&head, repo, "HEAD") == 0)
                held = isAncestor(repo, tip, toHex(head));
            git_error_clear();
            for (const auto& kept : gg::keep::read(repo))
                held = held || isAncestor(repo, tip, kept);
            gg::git2::forEachReference(repo, [&](git_reference* ref) {
                if (!held && std::string(git_reference_name(ref)).rfind("refs/heads/", 0) == 0
                    && git_reference_type(ref) == GIT_REFERENCE_DIRECT)
                    held = isAncestor(repo, tip, toHex(*git_reference_target(ref)));
                return true;
            });
            if (!held)
                refuse(tipRev + " is on no local branch");
        }
        // The tip's own commits: reachable from it, not from the destination (oldest first).
        const std::vector<std::string> own = rw::rangeToMove(repo, tip, dest);
        const std::string reflogMessage = isHead ? "ggui: rebase onto " + destination : "ggui: rebase " + shown + " onto " + destination;
        if (own.empty()) {
            // An empty range means the destination holds the tip; isAncestor is false only after a walk error.
            if (tip == dest || !isAncestor(repo, tip, dest))
                refuse(tipRev + " is already on " + destination);
            // The tip is behind the destination: nothing is replayed, the branch (or detached HEAD) fast-forwards.
            rw::Plan forward;
            forward.rebaseLike = true;
            forward.upstream = dest;
            forward.reflogMessage = reflogMessage;
            if (isHead) {
                if (const std::string branch = gg::git2::headTarget(repo); !branch.empty())
                    forward.refsToSteps[branch] = "=" + dest;
                else
                    forward.detachHeadAt = "=" + dest;
                return forward;
            }
            const std::string name = tipRev.rfind("refs/heads/", 0) == 0 ? tipRev : "refs/heads/" + tipRev;
            git_oid local;
            const bool isBranch = git_reference_name_to_id(&local, repo, name.c_str()) == 0 && toHex(local) == tip;
            git_error_clear();
            if (!isBranch)
                refuse(destination + " already contains " + tipRev);
            forward.refsToSteps[name] = "=" + dest;
            return forward;
        }
        rw::Plan plan = rw::replayPlan(repo, own);
        plan.rebaseLike = true;
        plan.upstream = dest;
        plan.reflogMessage = reflogMessage;
        std::set<std::string> mine(own.begin(), own.end());
        for (auto& s : plan.steps) {
            if (!mine.count(s.source))
                continue;
            s.sourceParents = false;
            s.parents.clear();
            for (const auto& p : parentsOf(repo, s.source))
                if (mine.count(p))
                    s.parents.push_back(p);
            if (s.parents.empty())
                s.parents.push_back("=" + dest);
        }
        return plan;
    });
}

} // namespace ggui

namespace ggui {

void Actions::moveChanges(const core::Oid& commit, MoveTo to, const std::vector<std::string>& paths, const std::string& patch)
{
    const std::string id = commit.hex();
    static const char* names[] = {"to parent", "to child", "to the active commit", "to the working tree", "discard"};
    const std::string what = patch.empty() ? std::to_string(paths.size()) + " file(s)" : std::string("lines");
    rewrite((to == MoveTo::Discard ? "discard " + what + " of " : "move " + what + " of ") + id.substr(0, 10) + " "
            + (to == MoveTo::Discard ? "" : names[static_cast<int>(to)]),
        [id, to, paths, patch](git_repository* repo) {
            using namespace gg::git2;
            const auto parents = parentsOf(repo, id);
            if (parents.size() != 1)
                refuse("moving changes needs a commit with exactly one parent");
            const SelectionTrees sel = selectionTrees(repo, id, paths, patch);
            const std::string& parent = sel.parent;
            const std::string& parentTree = sel.parentTree;
            const std::string& ownTree = sel.ownTree;
            auto forward = [&](const std::string& tree) { return sel.forward(repo, tree); };
            auto backward = [&](const std::string& tree) { return sel.backward(repo, tree); };
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
            case MoveTo::Discard: {
                plan = rw::replayPlan(repo, {id});
                plan.reflogMessage = to == MoveTo::Discard ? "ggui: discard changes" : "ggui: uncommit changes";
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
