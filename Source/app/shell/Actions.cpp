#include "shell/Actions.hpp"

#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/EditSession.hpp>
#include <libgg/Git2.hpp>
#include <libgg/Outgoing.hpp>
#include <libgg/Markers.hpp>
#include <libgg/NewCommit.hpp>
#include <libgg/Rewrite.hpp>
#include <libgg/Undo.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
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

// "<7-hex short id> <first line of the message>", for conflict-side labels (CONF-SIDE-LABELS).
std::string commitLabel(git_repository* repo, const git_oid& id)
{
    gg::git2::Commit c = gg::git2::lookupCommit(repo, id);
    const std::string message = gg::git2::commitMessage(c.get());
    return gg::git2::toHex(id).substr(0, 7) + " " + message.substr(0, message.find('\n'));
}

// Reads a "<oid>[...]\n" ref file (MERGE_HEAD, CHERRY_PICK_HEAD, REVERT_HEAD, REBASE_HEAD) and
// labels the commit it names, or empty when absent/unreadable.
std::string headFileLabel(git_repository* repo, const fs::path& gitDir, const char* name)
{
    std::error_code ec;
    if (!fs::exists(gitDir / name, ec))
        return {};
    std::ifstream f(gitDir / name, std::ios::binary);
    std::string line;
    std::getline(f, line);
    line = gg::trim(line);
    const auto id = gg::git2::fromHex(line);
    if (!id)
        return {};
    return commitLabel(repo, *id);
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
    if (network)
        m_networkRuns.insert(id);
    if (done)
        m_callbacks[id] = std::move(done);
    return id;
}

void Actions::onFinished(const core::MutationFinishedEvent& event)
{
    if (m_networkRuns.erase(event.request))
        m_session.markRemoteTagsStale();
    if (!event.journalError.empty())
        m_session.app().notify(App::Notice::Warning, "Undo journal",
            "\"" + event.label + "\" was not recorded, so Undo cannot restore it: " + event.journalError);
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
    const bool unborn = m_session.snapshot()->headUnborn;
    run("unstage " + std::to_string(paths.size()) + " file(s)", [paths, unborn](MutationContext& ctx) {
        if (unborn)
            ctx.git(withPaths({"rm", "--cached", "-q", "-r"}, paths));
        else
            ctx.git(withPaths({"restore", "--staged"}, paths));
    });
}

void Actions::discard(const std::vector<std::string>& tracked, const std::vector<std::string>& untracked,
    const std::vector<StagedDiscard>& staged)
{
    run("discard changes", [tracked, untracked, staged](MutationContext& ctx) {
        if (!tracked.empty())
            ctx.git(withPaths({"restore", "--worktree"}, tracked));
        // Fully staged files: new paths leave the index and disk, the rest (and renames' old
        // paths) come back from HEAD in both places.
        std::vector<std::string> remove, restore;
        for (const auto& s : staged) {
            if (s.remove)
                remove.push_back(s.path);
            else
                restore.push_back(s.path);
            if (!s.oldPath.empty())
                restore.push_back(s.oldPath);
        }
        if (!remove.empty())
            ctx.git(withPaths({"rm", "-f", "-q"}, remove));
        if (!restore.empty())
            ctx.git(withPaths({"restore", "--staged", "--worktree", "--source=HEAD"}, restore));
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
    const bool unborn = m_session.snapshot()->headUnborn;
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

// Commits only the working tree's own changes (unstaged tracked edits and untracked files), leaving
// the staged changes staged. The commit is made from a temporary index A (HEAD + the unstaged patch
// + untracked files) through GIT_INDEX_FILE, so hooks and an unborn HEAD behave as for any commit.
// The new real index is then built in a second temporary index B (the new HEAD + the staged patch)
// and renamed over the real one; if the staged patch cannot be replayed, HEAD is put back.
void Actions::commitWorktree(const std::string& message, Callback done)
{
    run("commit working tree",
        [message](MutationContext& ctx) {
            const fs::path gitDir = fs::absolute(git_repository_path(ctx.repo()));
            if (fs::exists(gitDir / "MERGE_HEAD"))
                throw MutationError{Outcome::Refused, "Finish the merge first: use the Commit dialog", {}};
            const fs::path indexA = gitDir / "gg-commit-index-a";
            const fs::path indexB = gitDir / "gg-commit-index-b";
            struct Cleanup {
                fs::path a, b;
                ~Cleanup()
                {
                    std::error_code ec;
                    fs::remove(a, ec);
                    fs::remove(b, ec);
                }
            } cleanup{indexA, indexB};
            std::error_code ec;
            fs::remove(indexA, ec);
            fs::remove(indexB, ec);
            const std::string oldHead = gg::trim(ctx.gitMayFail({"rev-parse", "-q", "--verify", "HEAD"}).out);
            auto diffOf = [&](bool cached) {
                std::vector<std::string> args{"diff", "--binary", "--no-renames", "--no-ext-diff", "--no-textconv",
                    "--src-prefix=a/", "--dst-prefix=b/"};
                if (cached)
                    args.insert(args.begin() + 1, "--cached");
                return ctx.git(args).out;
            };
            const std::string staged = diffOf(true);
            const std::string unstaged = diffOf(false);
            const std::string untracked = ctx.git({"ls-files", "--others", "--exclude-standard", "-z"}).out;
            auto useIndex = [&](const fs::path& f) {
                ctx.env.erase(std::remove_if(ctx.env.begin(), ctx.env.end(),
                                  [](const auto& e) { return e.first == "GIT_INDEX_FILE"; }),
                    ctx.env.end());
                ctx.env.emplace_back("GIT_INDEX_FILE", f.string());
            };
            auto resetTo = [&](const std::string& head) {
                ctx.git(head.empty() ? std::vector<std::string>{"read-tree", "--empty"}
                                     : std::vector<std::string>{"read-tree", head});
            };
            useIndex(indexA);
            resetTo(oldHead);
            if (!unstaged.empty()) {
                const auto r = ctx.gitMayFail({"apply", "--cached", "--binary", "--whitespace=nowarn", "-"}, unstaged);
                if (!r.ok())
                    throw MutationError{Outcome::Refused,
                        "The unstaged changes of a file that is also staged do not apply on HEAD; stage or commit that "
                        "file first",
                        r.err};
            }
            if (!untracked.empty())
                ctx.git({"add", "-A", "--pathspec-from-file=-", "--pathspec-file-nul"}, untracked);
            ctx.git({"commit", "-q", "-F", "-"}, message);
            const std::string newHead = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
            useIndex(indexB);
            resetTo("HEAD");
            bool ok = true;
            if (!staged.empty()) {
                ok = ctx.gitMayFail({"apply", "--cached", "--binary", "--whitespace=nowarn", "-"}, staged).ok();
                if (!ok) {
                    resetTo("HEAD");
                    ok = ctx.gitMayFail({"apply", "--cached", "--3way", "-"}, staged).ok();
                }
            }
            ctx.env.pop_back();
            if (!ok) {
                ctx.git(oldHead.empty() ? std::vector<std::string>{"update-ref", "-d", "HEAD"}
                                        : std::vector<std::string>{"update-ref", "HEAD", oldHead});
                throw MutationError{Outcome::Refused,
                    "The staged changes cannot be kept staged on top of the new commit; nothing was committed", {}};
            }
            fs::rename(indexB, gitDir / "index");
            ctx.result = newHead;
        },
        std::move(done));
}

namespace {

fs::path editSessionFile(MutationContext& ctx)
{
    return gg::edit::sessionFile(git_repository_path(ctx.repo()), git_repository_commondir(ctx.repo()));
}

} // namespace

void Actions::amend(const std::string& message, bool noVerify, bool messageOnly, Callback done,
    std::function<void()> declined)
{
    // Like every rewrite: what would be rewritten (HEAD and its descendants) and is already on a
    // remote is found first (in memory, nothing written), and asked about.
    auto published = std::make_shared<size_t>(0);
    run(
        std::string(messageOnly ? "reword HEAD" : "amend") + " (preparing)",
        [published](MutationContext& ctx) {
            const std::string head = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
            gg::rewrite::Plan plan = gg::rewrite::replayPlan(ctx.repo(), {head});
            for (auto& st : plan.steps)
                if (st.source == head)
                    st.message = "amend preflight\n"; // HEAD counts as rewritten
            plan.keepHead = true;
            gg::rewrite::Rewriter rewriter(ctx.cwd());
            *published = rewriter.compute(plan).published.size();
        },
        [this, published, message, noVerify, messageOnly, done = std::move(done), declined = std::move(declined)](
            const core::MutationFinishedEvent& e) mutable {
            // A failed look goes on to the real amend, which reports its own error.
            if (e.outcome != Outcome::Ok || *published == 0) {
                amendNow(message, noVerify, messageOnly, std::move(done));
                return;
            }
            Form f;
            f.title = "Rewrite published history?";
            f.message = std::to_string(*published) + " of the rewritten commits are already on a remote. "
                                                     "Rewriting them makes your branch diverge from it.\n\nContinue?";
            f.buttons.push_back({"Rewrite", [this, message, noVerify, messageOnly, done](Form&) {
                                     amendNow(message, noVerify, messageOnly, done);
                                 }});
            f.buttons.push_back({"Cancel", [declined](Form&) {
                                     if (declined)
                                         declined();
                                 }});
            m_session.app().dialogs().open(std::move(f));
        },
        false, false, false);
}

void Actions::amendNow(const std::string& message, bool noVerify, bool messageOnly, Callback done)
{
    const std::string label = messageOnly ? "reword HEAD" : "amend";
    if (!done)
        done = [this, label](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::Ok && !e.message.empty())
                m_session.app().notify(App::Notice::Warning, label, e.message);
            else
                handleDefault(e);
        };
    run(label,
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
            // Descendants of the amended commit (an edited commit in the middle of a stack, a
            // conflicted commit, …) are restacked onto it, all or nothing: the restack is first
            // computed in memory with the index as the new tree, so one that cannot be done
            // refuses before the commit (and its hooks) run. Should it still fail afterwards (a
            // hook changed the commit), HEAD goes back to the original commit. All of it is one
            // operation, undone in one step.
            const auto desc = gg::rewrite::descendants(ctx.repo(), {before});
            if (desc.size() > 1) {
                gg::rewrite::Plan pre = gg::rewrite::replayPlan(ctx.repo(), {before});
                for (auto& st : pre.steps)
                    if (st.source == before) {
                        const auto tree = ctx.gitMayFail({"write-tree"});
                        if (!messageOnly && tree.ok())
                            st.tree = gg::trim(tree.out);
                        if (!message.empty())
                            st.message = message.back() == '\n' ? message : message + "\n";
                    }
                pre.keepHead = true;
                gg::rewrite::Rewriter rewriter(ctx.cwd());
                const gg::rewrite::Result r = rewriter.compute(pre);
                if (!r.unresolved.empty()) {
                    const auto& u = r.unresolved.front();
                    const std::string error = "restacking " + u.commit.substr(0, 10) + " would give a " + u.kind
                        + " conflict in " + u.path;
                    throw MutationError{Outcome::Refused, "cannot amend: " + error, error};
                }
                if (!r.ok)
                    throw MutationError{Outcome::Refused, "cannot amend: restacking the descendants failed: " + r.error,
                        r.error};
            }
            ctx.git(args, message);
            ctx.result = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
            if (desc.size() > 1) {
                gg::rewrite::Plan plan;
                for (const auto& c : desc)
                    if (c != before) {
                        gg::rewrite::Step st;
                        st.source = c;
                        plan.steps.push_back(st);
                    }
                plan.replaced[before] = ctx.result;
                plan.reflogMessage = "ggui: amend (rebase descendants)";
                plan.rewriteKind = "amend";
                plan.keepHead = true;
                gg::rewrite::Rewriter rewriter(ctx.cwd());
                gg::rewrite::Result r = rewriter.compute(plan);
                std::string error = r.unresolved.empty() ? r.error : "unresolved non-text conflicts";
                if (!r.ok || !r.unresolved.empty() || !rewriter.apply(plan, r, error)) {
                    ctx.git({"update-ref", "-m", "ggui: amend (rolled back)", "HEAD", before, ctx.result});
                    ctx.result = before;
                    throw MutationError{Outcome::Failed, "amend undone: restacking the descendants failed: " + error,
                        error};
                }
                ctx.keepExtra = r.keepExtra;
                if (!r.conflicted.empty()) {
                    std::string list;
                    for (const auto& id : r.conflicted)
                        list += (list.empty() ? "" : ", ") + id.substr(0, 10);
                    ctx.info = std::to_string(r.conflicted.size()) + " restacked commit(s) now have first-class conflicts: "
                        + list;
                }
            }
            // An edit session follows the amended commit.
            const fs::path file = editSessionFile(ctx);
            if (auto session = gg::edit::read(file); session && session->commit == before) {
                session->commit = ctx.result;
                gg::edit::write(file, *session);
            }
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

void Actions::mergeToolFirstClass(const std::string& path, int pair)
{
    run("merge tool " + path,
        [path, pair](MutationContext& ctx) {
            // Stages 1–3 from the region terms for sides `pair`/`pair+1`, then git mergetool
            // (which stages the result). If the tool gives up, the index goes back to HEAD for
            // the path. On an N-sided file (N >= 3) success folds the resolved pair into one
            // term and leaves the file a first-class conflict with one side fewer.
            std::ifstream in(ctx.cwd() / path, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            in.close();
            const std::string text = ss.str();
            gg::markers::Merge m = gg::markers::toMerge(text);
            const int n = static_cast<int>(m.adds.size());
            if (n < 2)
                throw MutationError{Outcome::Refused, "not a conflict; take a side first", {}};
            if (pair < 0 || pair + 1 >= n)
                throw MutationError{Outcome::Refused, "no such pair of sides in this conflict", {}};
            // "<mode> <id> <stage>\t<path>" (ls-files --format needs git 2.38).
            const std::string staged = ctx.git({"ls-files", "-s", "-z", "--", path}).out;
            std::string mode = staged.substr(0, staged.find(' '));
            if (mode.empty())
                mode = "100644"; // not in the index yet
            auto blob = [&](const std::string& content) { return gg::trim(ctx.git({"hash-object", "-w", "--stdin"}, content).out); };
            const std::string zero(gg::git2::hexSize(gg::git2::oidType(ctx.repo())), '0');
            std::string input = "0 " + zero + "\t" + path + "\n";
            input += mode + " " + blob(m.removes[static_cast<size_t>(pair)]) + " 1\t" + path + "\n";
            input += mode + " " + blob(m.adds[static_cast<size_t>(pair)]) + " 2\t" + path + "\n";
            input += mode + " " + blob(m.adds[static_cast<size_t>(pair) + 1]) + " 3\t" + path + "\n";
            ctx.git({"update-index", "--index-info"}, input);
            const auto r = ctx.gitMayFail(withPaths({"mergetool", "-y"}, {path}));
            if (!r.ok()) {
                ctx.gitMayFail({"reset", "-q", "--", path});
                if (n > 2) {
                    // The tool may have written into the working tree file before giving up.
                    std::ifstream check(ctx.cwd() / path, std::ios::binary);
                    std::ostringstream cs;
                    cs << check.rdbuf();
                    check.close();
                    if (cs.str() != text) {
                        std::ofstream restore(ctx.cwd() / path, std::ios::binary | std::ios::trunc);
                        restore << text;
                    }
                }
                throw MutationError{Outcome::Failed, r.message(), r.message()};
            }
            if (n > 2) {
                std::ifstream res(ctx.cwd() / path, std::ios::binary);
                std::ostringstream rs;
                rs << res.rdbuf();
                res.close();
                gg::markers::Merge folded = m;
                folded.adds[static_cast<size_t>(pair)] = rs.str();
                folded.adds.erase(folded.adds.begin() + pair + 1);
                folded.removes.erase(folded.removes.begin() + pair);
                // Surviving sides keep their old labels (the pair just resolved has no single
                // label of its own: it is a merge-tool result, not one side any more).
                auto options = gg::conflicts::writeOptions(ctx.repo(), path);
                options.termLabels = gg::markers::termLabels(text);
                const std::string materialized = gg::markers::materialize(std::move(folded), options);
                std::ofstream out(ctx.cwd() / path, std::ios::binary | std::ios::trunc);
                out << materialized;
                out.close();
                ctx.gitMayFail({"reset", "-q", "--", path});
            }
        },
        {}, true, false);
}

void Actions::newCommit(const std::vector<std::string>& parents, bool detach, const std::string& message, const std::string& branch)
{
    run(detach ? "new detached commit" : "new commit", [parents, detach, message, branch](MutationContext& ctx) {
        gg::NewCommitOptions opts;
        opts.branch = branch;
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

void Actions::checkout(const std::string& target, bool detach, bool stashFirst, bool edit)
{
    const std::string label = edit ? "edit " + target.substr(0, 10)
        : detach                    ? "check out " + target.substr(0, 10)
                                    : "switch to " + target;
    const bool expand = m_session.app().settings().data().expandConflictStages;
    run(label,
        [target, detach, stashFirst, expand, edit](MutationContext& ctx) {
            // Edit commit: the branch to return to is the one HEAD is on (or the current edit
            // session's) if it contains the commit, else the first local branch that does.
            std::string branch;
            if (edit) {
                std::vector<std::string> candidates{gg::trim(ctx.gitMayFail({"symbolic-ref", "-q", "--short", "HEAD"}).out)};
                if (auto session = gg::edit::read(editSessionFile(ctx)))
                    candidates.push_back(session->branch);
                for (const auto& b : candidates)
                    if (!b.empty() && ctx.gitMayFail({"merge-base", "--is-ancestor", target, "refs/heads/" + b}).ok()) {
                        branch = b;
                        break;
                    }
                if (branch.empty())
                    for (const auto& b : gg::splitLines(ctx.gitMayFail(
                             {"for-each-ref", "--contains", target, "--format=%(refname:short)", "refs/heads/"}).out))
                        if (!b.empty()) {
                            branch = b;
                            break;
                        }
                if (branch.empty())
                    throw MutationError{Outcome::Refused, "The commit is not on a local branch", {}};
            }
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
            if (edit) {
                const std::string head = gg::trim(ctx.git({"rev-parse", "HEAD"}).out);
                const int n = static_cast<int>(gg::rewrite::descendants(ctx.repo(), {head}).size()) - 1;
                gg::edit::write(editSessionFile(ctx), {head, branch, n});
            }
        },
        [this, target, detach, edit](const core::MutationFinishedEvent& e) {
            if (e.outcome == Outcome::LocalChanges) {
                Form f;
                f.title = "Stash and switch";
                f.message = "Your local changes would be overwritten by the checkout.\n\n" + e.message
                    + "\n\nStash them first and then switch?";
                f.buttons.push_back(
                    {"Stash and switch", [this, target, detach, edit](Form&) { checkout(target, detach, true, edit); }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
                return;
            }
            handleDefault(e);
        });
}

void Actions::editCommit(const core::Oid& id)
{
    checkout(id.hex(), true, false, true);
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
    const bool current = snap->headBranch == name; // "" when detached
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
    const bool current = snap->headBranch == branch; // "" when detached
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
                const auto conflicted = gg::outgoing::conflictedOutgoing(ctx.cwd(), local, remote,
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
                const auto broken = gg::outgoing::brokenOutgoing(ctx.cwd(), local, remote,
                    remoteOid.ok() ? gg::trim(remoteOid.out) : std::string());
                if (!broken.empty()) {
                    std::string list;
                    for (const auto& c : broken) {
                        list += c.id + " " + c.subject + "\n";
                        for (const auto& f : c.files) {
                            std::string lines;
                            for (size_t i = 0; i < f.lines.size(); ++i)
                                lines += (i ? ", " : "") + std::to_string(f.lines[i]);
                            list += "    " + f.path + " line " + lines + "\n";
                        }
                    }
                    throw MutationError{Outcome::Refused, "The pushed commits left broken conflict markers", list};
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
                m_session.app().dialogs().pushRefused(m_session, e.message, e.detail);
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

void Actions::stashReword(int index, const core::Oid& commit, const std::string& message)
{
    const std::string oldId = commit.hex();
    std::string text = message;
    if (!text.empty() && text.back() != '\n')
        text.push_back('\n');
    m_session.stashRewordPending(true);
    Session* session = &m_session;
    run(
        "reword stash@{" + std::to_string(index) + "}",
        [index, oldId, text](MutationContext& ctx) {
            // New stash commit: same tree, parents and author; only the message differs.
            const auto ids = gg::splitLines(ctx.git({"rev-parse", oldId + "^{tree}", oldId + "^@"}).out);
            std::vector<std::string> args{"commit-tree"};
            bool tree = true;
            for (const auto& line : ids) {
                const std::string id = gg::trim(line);
                if (id.empty())
                    continue;
                if (tree)
                    args.push_back(id);
                else {
                    args.emplace_back("-p");
                    args.push_back(id);
                }
                tree = false;
            }
            const auto who = gg::splitLines(ctx.git({"log", "-1", "--date=raw", "--format=%an%n%ae%n%ad", oldId}).out);
            if (who.size() >= 3) {
                ctx.env.emplace_back("GIT_AUTHOR_NAME", gg::trim(who[0]));
                ctx.env.emplace_back("GIT_AUTHOR_EMAIL", gg::trim(who[1]));
                ctx.env.emplace_back("GIT_AUTHOR_DATE", gg::trim(who[2]));
            }
            const std::string fresh = gg::trim(ctx.git(args, text).out);
            ctx.env.clear();

            // The stash list, newest first: sha and reflog subject.
            struct Entry {
                std::string sha, subject;
            };
            std::vector<Entry> entries;
            for (const auto& line : gg::splitLines(ctx.git({"stash", "list", "--format=%H%x00%gs"}).out)) {
                const auto nul = line.find('\0');
                if (nul != std::string::npos)
                    entries.push_back({line.substr(0, nul), gg::trim(line.substr(nul + 1))});
            }
            if (index < 0 || static_cast<size_t>(index) >= entries.size() || entries[index].sha != oldId)
                throw MutationError{Outcome::Refused, "The stash list changed; select the stash again", {}};
            std::string subject = text.substr(0, text.find('\n'));
            // Drop stash@{0}..stash@{index} and store them back oldest first, so the order stays.
            std::vector<Entry> removed(entries.begin(), entries.begin() + index + 1);
            removed[index] = {fresh, subject};
            size_t stored = 0;
            auto restore = [&](const std::vector<Entry>& list) {
                stored = 0;
                for (size_t i = list.size(); i-- > 0;) {
                    const auto r = ctx.gitMayFail({"stash", "store", "-m", list[i].subject, list[i].sha});
                    if (!r.ok())
                        return r.message();
                    ++stored;
                }
                return std::string();
            };
            for (int i = 0; i <= index; ++i)
                ctx.git({"stash", "drop", "-q"});
            const std::string failure = restore(removed);
            if (!failure.empty()) {
                // Undo the partial rewrite: drop what was stored, then store the originals back.
                for (size_t i = 0; i < stored; ++i)
                    ctx.gitMayFail({"stash", "drop", "-q"});
                restore(std::vector<Entry>(entries.begin(), entries.begin() + index + 1));
                throw MutationError{Outcome::Failed, "Could not rewrite the stash list", failure};
            }
            ctx.result = fresh;
        },
        [session, oldId, index](const core::MutationFinishedEvent& e) {
            session->stashRewordPending(false);
            if (e.outcome == core::Outcome::Ok) {
                session->stashRewordDone(oldId, e.result, index);
                return;
            }
            if (e.outcome != core::Outcome::Cancelled)
                session->app().showError(e.label, e.detail.empty() ? e.message : e.detail);
        });
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
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --continue", [cmd](MutationContext& ctx) {
        if (cmd == "rebase")
            return rebaseStep(ctx, {"--continue"});
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "merge")
            ctx.git({"commit", "--no-edit", "-q"});
        else
            ctx.git({cmd, "--continue"});
        ctx.worktreeFollowsIndex = true;
    }, [this](const core::MutationFinishedEvent& e) { onRebaseStep(e); });
}

void Actions::skipOperation()
{
    const auto snap = m_session.snapshot();
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --skip", [cmd](MutationContext& ctx) {
        if (cmd == "rebase")
            return rebaseStep(ctx, {"--skip"});
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "bisect")
            ctx.git({"bisect", "skip"});
        else
            ctx.git({cmd, "--skip"});
        ctx.worktreeFollowsIndex = true;
    }, [this](const core::MutationFinishedEvent& e) { onRebaseStep(e); });
}

void Actions::abortOperation()
{
    const auto snap = m_session.snapshot();
    const std::string cmd = operationCommand(snap->state);
    run(cmd + " --abort", [cmd](MutationContext& ctx) {
        if (cmd == "rebase")
            return rebaseStep(ctx, {"--abort"});
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
    if (!status)
        return;
    const std::string cmd = operationCommand(snap->state);
    std::vector<std::string> paths;
    for (const auto& e : status->conflicted)
        if (!e.firstClass)
            paths.push_back(e.path);
    run("commit with conflicts", [cmd, paths](MutationContext& ctx) {
        // Text-only: write diff3 regions from stages 1–3 (base, ours, theirs), stage, finish.
        // Sides are labelled from the operation's own state: ours is HEAD, theirs whichever of
        // MERGE_HEAD/CHERRY_PICK_HEAD/REVERT_HEAD/REBASE_HEAD is present, base a plain "base".
        const fs::path gitDir = git_repository_path(ctx.repo());
        std::string oursLabel;
        git_oid head;
        if (git_reference_name_to_id(&head, ctx.repo(), "HEAD") == 0)
            oursLabel = commitLabel(ctx.repo(), head);
        git_error_clear();
        std::string theirsLabel;
        for (const char* name : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "REBASE_HEAD"}) {
            theirsLabel = headFileLabel(ctx.repo(), gitDir, name);
            if (!theirsLabel.empty())
                break;
        }
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
            auto options = gg::conflicts::writeOptions(ctx.repo(), p);
            options.sideLabels = {oursLabel, theirsLabel};
            options.baseLabels = {"base"};
            contents.emplace_back(p, gg::markers::mergeFiles(base.value_or(""), *ours, *theirs, options));
        }
        for (const auto& [p, text] : contents) {
            std::ofstream out(ctx.cwd() / p, std::ios::binary | std::ios::trunc);
            out << text;
        }
        if (!paths.empty())
            ctx.git(withPaths({"add"}, paths));
        if (cmd == "rebase")
            return rebaseStep(ctx, {"--continue"});
        ctx.env.emplace_back("GIT_EDITOR", "true");
        if (cmd == "merge" || cmd.empty())
            ctx.git({"commit", "--no-edit", "-q", "--no-verify"});
        else
            ctx.git({cmd, "--continue"});
        ctx.worktreeFollowsIndex = true;
    }, [this](const core::MutationFinishedEvent& e) { onRebaseStep(e); });
}

void Actions::saveMergeMessage(const std::string& message)
{
    const auto snap = m_session.snapshot();
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

void Actions::externalDiff(const std::string& path, const std::vector<std::string>& revs)
{
    run("external diff " + path,
        [path, revs](MutationContext& ctx) {
            std::vector<std::string> args{"difftool", "-y"};
            args.insert(args.end(), revs.begin(), revs.end());
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
