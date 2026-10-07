// Keep refs (decision K2): refs/gg/keep/<id> keeps the commits made on a detached HEAD alive.
// keep::maintain on repositories built with plain git, and the places that call it: every
// operation (OperationRecorder::finish) and every reconcile pass.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Keep.hpp>
#include <libgg/Legacy.hpp>
#include <libgg/Operation.hpp>
#include <libgg/Reconcile.hpp>
#include <libgg/Rewrite.hpp>
#include <libgg/Undo.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

namespace ggtest {

namespace {

struct Maintained {
    bool ok = false;
    std::string error;
    std::vector<gg::journal::RefChange> changes;
};

// keep::maintain on a fresh handle of the repository at `cwd`.
Maintained maintain(const fs::path& cwd, const std::vector<std::string>& extra = {})
{
    Maintained m;
    gg::git2::Repository r = gg::git2::openRepository(cwd);
    m.ok = gg::keep::maintain(r.get(), extra, &m.changes, &m.error);
    return m;
}

// The ref names under refs/gg/keep/.
std::vector<std::string> keepRefs(Scenario& s, const fs::path& repo)
{
    std::vector<std::string> out;
    for (const auto& line : gg::splitLines(s.gitOut(repo, {"for-each-ref", "--format=%(refname)", "refs/gg/keep/"})))
        if (!line.empty())
            out.push_back(line);
    return out;
}

std::vector<std::string> names(const std::vector<std::string>& ids)
{
    std::vector<std::string> out;
    for (const auto& id : ids)
        out.push_back(gg::keep::refName(id));
    std::sort(out.begin(), out.end());
    return out;
}

// A new commit on a detached HEAD at `from`; returns its id.
std::string detachedCommit(Scenario& s, const fs::path& repo, const std::string& from, const std::string& file)
{
    s.git(repo, {"checkout", "-q", "--detach", from});
    s.commitFile(repo, file, file + "\n", "Detached " + file);
    return s.head(repo);
}

// A commit made with plain git inside an operation that creates commits, as the app's commit does:
// the operation names the commit on its worktree's detached HEAD and keeps it. Returns its id.
std::string commitOperation(Scenario& s, const fs::path& repo, const std::string& file, const std::string& content,
    const std::string& message)
{
    gg::git2::Repository r = gg::git2::openRepository(repo);
    gg::OperationRecorder rec(r.get(), "test", "commit " + file, false);
    rec.setCreatesCommits(true);
    rec.begin();
    s.commitFile(repo, file, content, message);
    rec.finish(true, false);
    return s.head(repo);
}

// detachedCommit, but the commit is made in an operation that names it (see commitOperation).
std::string detachedOperation(Scenario& s, const fs::path& repo, const std::string& from, const std::string& file)
{
    s.git(repo, {"checkout", "-q", "--detach", from});
    return commitOperation(s, repo, file, file + "\n", "Detached " + file);
}

// keep::maintain on an open handle with `ids` named.
void keepNamed(gg::git2::Repository& r, const std::vector<std::string>& ids)
{
    std::string error;
    GG_CHECK(gg::keep::maintain(r.get(), ids, nullptr, &error));
}

// What a pass in the main worktree did for the commits of another worktree: they are kept by name
// while an operation of main is open, so the keep refs are journaled as "keep refs" housekeeping.
void keepByName(const fs::path& repo, const std::vector<std::string>& ids)
{
    gg::git2::Repository r = gg::git2::openRepository(repo);
    gg::OperationRecorder rec(r.get(), "test", "keep", false);
    rec.begin();
    keepNamed(r, ids);
    rec.finish(true, false);
}

// keepByName without an operation of main's own on top: the keep ref changes are written as the
// housekeeping operation directly. Written only when maintenance changed something: with an app
// open, a pass of its own may have kept the commit first, and an operation without refs is not
// keep-only.
void keepAsHousekeeping(const fs::path& repo, const std::vector<std::string>& ids)
{
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::vector<gg::journal::RefChange> changes;
    std::string error;
    GG_CHECK(gg::keep::maintain(r.get(), ids, &changes, &error));
    gg::journal::Journal journal{repo / ".git"};
    if (!changes.empty())
        GG_CHECK(gg::journal::writeKeepHousekeeping(journal, "main", changes, &error));
}

std::vector<gg::journal::Operation> journalOps(const fs::path& repo)
{
    gg::journal::Journal journal{repo / ".git"};
    std::string error;
    return journal.read(&error);
}

size_t gitOpCount(const fs::path& repo)
{
    size_t n = 0;
    for (const auto& op : journalOps(repo))
        n += op.src == "git" ? 1 : 0;
    return n;
}

// Housekeeping operations ("keep refs", written by the recorder for keep ref changes that are not
// its own operation's).
size_t housekeepingCount(const fs::path& repo)
{
    size_t n = 0;
    for (const auto& op : journalOps(repo))
        n += op.src == "gg" && op.label == "keep refs" ? 1 : 0;
    return n;
}

// The operations whose record changes `ref` from `from` to `to` ("" = the null id).
std::vector<gg::journal::Operation> opsChanging(const fs::path& repo, const std::string& ref, bool created)
{
    std::vector<gg::journal::Operation> out;
    for (auto& op : journalOps(repo)) {
        const bool hit = std::any_of(op.refs.begin(), op.refs.end(), [&](const gg::journal::RefChange& r) {
            const bool nullOld = r.oldValue.find_first_not_of('0') == std::string::npos;
            const bool nullNew = r.newValue.find_first_not_of('0') == std::string::npos;
            return r.ref == ref && (created ? nullOld && !nullNew : !nullOld && nullNew);
        });
        if (hit)
            out.push_back(std::move(op));
    }
    return out;
}

// Everything Undo restores: refs (the keep refs too) and HEAD.
std::string refState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    return state;
}

bool becomes(Scenario& s, const fs::path& repo, const std::string& expected)
{
    const bool ok = s.waitUntil([&] { return refState(s, repo) == expected; });
    s.settle();
    return ok;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

// "New detached commit" on the commit `id` (its row is selected first).
void newDetachedOn(ImGuiTestContext* ctx, Scenario& s, const std::string& id)
{
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(id)) != nullptr; });
    ctx->ItemClick(rowRef(id).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/New detached commit");
}

bool commitReadable(Scenario& s, const fs::path& repo, const std::string& id)
{
    return s.gitMayFail(repo, {"cat-file", "-e", id + "^{commit}"}).ok();
}

// A detached, unreachable chain p <- t made with plain git in operations that name their commits
// (which keep t), then HEAD back on main: the keep ref, not HEAD, holds both commits.
struct KeptChain {
    std::string p;
    std::string t;
};

KeptChain keptChain(Scenario& s, const fs::path& repo, gg::git2::Repository& r)
{
    std::string error;
    KeptChain c;
    gg::reconcile::run(r.get(), &error); // the baseline
    c.p = detachedOperation(s, repo, "main", "p.txt");
    c.t = commitOperation(s, repo, "t.txt", "t\n", "Detached t");
    s.git(repo, {"checkout", "-q", "main"});
    gg::reconcile::run(r.get(), &error);
    return c;
}

// A rewrite in an operation of its own, as the app runs one: the plan from `build`, applied, the
// replacements handed to the recorder. Returns the rewrite's mapping (old id -> new id).
std::map<std::string, std::string> rewriteOperation(gg::git2::Repository& r, const fs::path& repo, const std::string& label,
    const std::function<gg::rewrite::Plan(git_repository*)>& build)
{
    gg::OperationRecorder rec(r.get(), "test", label, false);
    rec.begin();
    gg::rewrite::Plan plan = build(r.get());
    gg::rewrite::Rewriter rewriter(repo);
    gg::rewrite::Result result = rewriter.compute(plan);
    std::string error;
    const bool ok = result.ok && rewriter.apply(plan, result, error);
    rec.setKeepExtra(result.keepExtra);
    rec.finish(ok, false);
    return ok ? result.mapping : std::map<std::string, std::string>{};
}

gg::rewrite::Plan rewordPlan(git_repository* repo, const std::string& commit)
{
    gg::rewrite::Plan plan = gg::rewrite::replayPlan(repo, {commit});
    for (auto& step : plan.steps)
        if (step.source == commit)
            step.message = "Reworded\n";
    return plan;
}

gg::rewrite::Plan abandonPlan(git_repository* repo, const std::string& commit)
{
    gg::rewrite::Plan plan = gg::rewrite::replayPlan(repo, {commit});
    plan.steps.erase(std::remove_if(plan.steps.begin(), plan.steps.end(), [&](const gg::rewrite::Step& st) { return st.source == commit; }),
        plan.steps.end());
    plan.dropped = {commit};
    return plan;
}

// After a rewrite's own operation: nothing for a pass to report, no "keep refs" operation.
void checkNothingElse(Scenario& s, const fs::path& repo, gg::git2::Repository& r)
{
    std::string error;
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    GG_CHECK(error.empty());
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK(keepRefs(s, repo).size() <= 1u);
}

} // namespace

GG_TEST("keep", "a detached HEAD keeps nothing; a commit named for maintenance is kept and the second run changes nothing")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    const Maintained none = maintain(repo);
    GG_CHECK(none.ok);
    GG_CHECK(none.changes.empty());
    GG_CHECK(keepRefs(s, repo).empty());
    const Maintained first = maintain(repo, {x});
    GG_CHECK(first.ok);
    GG_CHECK(first.error.empty());
    GG_CHECK_EQ(first.changes.size(), 1u);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(x)), x);
    GG_CHECK(gg::keep::read(gg::git2::openRepository(repo).get()) == std::vector<std::string>{x});
    const Maintained second = maintain(repo);
    GG_CHECK(second.ok);
    GG_CHECK(second.changes.empty());
    GG_CHECK(keepRefs(s, repo) == names({x}));
}

GG_TEST("keep", "a detached HEAD at a branch tip, a tagged commit or a remote-tracking commit keeps nothing")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    GG_CHECK(maintain(repo, {s.head(repo)}).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(repo, {"tag", "t1", "main~1"});
    s.git(repo, {"tag", "-a", "-m", "annotated", "t2", "main~2"});
    s.git(repo, {"checkout", "-q", "--detach", "t1"});
    GG_CHECK(maintain(repo, {s.head(repo)}).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(repo, {"checkout", "-q", "--detach", "t2"}); // an annotated tag is peeled
    GG_CHECK(maintain(repo, {s.head(repo)}).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"update-ref", "refs/remotes/origin/topic", x});
    const Maintained m = maintain(repo, {x});
    GG_CHECK(m.ok);
    GG_CHECK(m.changes.empty());
    GG_CHECK(keepRefs(s, repo).empty());
    // Below a branch tip too.
    s.git(repo, {"checkout", "-q", "--detach", "main~3"});
    GG_CHECK(maintain(repo, {s.head(repo)}).ok);
    GG_CHECK(keepRefs(s, repo).empty());
}

GG_TEST("keep", "tips only: a named commit on the detached HEAD replaces its parent's ref; unrelated lines keep one each")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x1 = detachedCommit(s, repo, "main", "x1.txt");
    GG_CHECK(maintain(repo, {x1}).ok);
    GG_CHECK(keepRefs(s, repo) == names({x1}));
    s.commitFile(repo, "x2.txt", "x2\n", "Second on the detached HEAD");
    const std::string x2 = s.head(repo);
    const Maintained m = maintain(repo, {x2});
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x2}));
    GG_CHECK_EQ(m.changes.size(), 2u);
    // A second, unrelated line: both tips stay.
    const std::string y = detachedCommit(s, repo, "main~2", "y.txt");
    GG_CHECK(maintain(repo, {y}).ok);
    GG_CHECK(keepRefs(s, repo) == names({x2, y}));
    GG_CHECK(gg::keep::read(gg::git2::openRepository(repo).get()).size() == 2u);
    // A merge of the two lines swallows both.
    s.git(repo, {"merge", "-q", "--no-edit", x2});
    const std::string merged = s.head(repo);
    GG_CHECK(maintain(repo, {merged}).ok);
    GG_CHECK(keepRefs(s, repo) == names({merged}));
}

GG_TEST("keep", "graduation: a branch or a tag at a kept tip or a descendant of it deletes its ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo, {x}).ok);
    const std::string y = detachedCommit(s, repo, "main~1", "y.txt");
    GG_CHECK(maintain(repo, {y}).ok);
    const std::string z = detachedCommit(s, repo, "main~2", "z.txt");
    GG_CHECK(maintain(repo, {z}).ok);
    GG_CHECK(keepRefs(s, repo) == names({x, y, z}));
    s.git(repo, {"branch", "graduated", x});
    Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({y, z}));
    GG_CHECK_EQ(m.changes.size(), 1u);
    s.git(repo, {"tag", "graduated-tag", y});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({z}));
    // A branch at a descendant of the kept tip.
    s.commitFile(repo, "z2.txt", "z2\n", "After z");
    s.git(repo, {"branch", "later"});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
}

GG_TEST("keep", "leaving a kept commit keeps its ref and the commit survives gc")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo, {x}).ok);
    s.git(repo, {"checkout", "-q", "main"});
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(m.changes.empty());
    GG_CHECK(keepRefs(s, repo) == names({x}));
    s.git(repo, {"reflog", "expire", "--expire=now", "--all"});
    s.git(repo, {"gc", "-q", "--prune=now"});
    GG_CHECK(commitReadable(s, repo, x));
    // Without the ref the same commit is gone.
    const std::string lost = detachedCommit(s, repo, "main", "lost.txt");
    s.git(repo, {"checkout", "-q", "main"});
    s.git(repo, {"reflog", "expire", "--expire=now", "--all"});
    s.git(repo, {"gc", "-q", "--prune=now"});
    GG_CHECK(!commitReadable(s, repo, lost));
}

GG_TEST("keep", "ill-formed keep refs: renamed, deleted when anchored, a symbolic one deleted")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"checkout", "-q", "main"});
    s.git(repo, {"update-ref", "refs/gg/keep/foo", x});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    // Pointing at a commit a branch reaches.
    const std::string tip = s.revParse(repo, "main");
    s.git(repo, {"update-ref", "refs/gg/keep/bar", tip});
    s.git(repo, {"update-ref", gg::keep::refName(tip), tip});
    Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(m.changes.size(), 2u);
    // A symbolic ref, left alone by the reads of the target.
    s.git(repo, {"symbolic-ref", "refs/gg/keep/sym", "refs/heads/main"});
    m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(m.changes.size(), 1u);
    GG_CHECK_STR_EQ(m.changes[0].oldValue, "ref:refs/heads/main");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), tip);
    // A name that is the id of another commit than the target.
    s.git(repo, {"update-ref", "refs/gg/keep/" + tip, x});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(x)), x);
}

GG_TEST("keep", "extra ids are kept when nothing anchors them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"checkout", "-q", "main"});
    const std::string anchored = s.revParse(repo, "main~1");
    const Maintained m = maintain(repo, {x, anchored, std::string(40, 'a'), "not an id"});
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(m.changes.size(), 1u);
}

GG_TEST("keep", "worktrees: no detached HEAD is kept unless it is named, one in the middle of an operation or not")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(maintain(wt).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK(maintain(repo, {w}).ok);
    GG_CHECK(keepRefs(s, repo) == names({w}));
    // From the linked worktree's own handle the answer is the same.
    {
        const Maintained again = maintain(wt);
        GG_CHECK(again.ok);
        GG_CHECK(again.changes.empty());
    }
    // The main worktree detached as well: its commit is not kept until it is named.
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(wt).ok);
    GG_CHECK(keepRefs(s, repo) == names({w}));
    GG_CHECK(maintain(wt, {x}).ok);
    GG_CHECK(keepRefs(s, repo) == names({w, x}));
    s.git(repo, {"checkout", "-q", "main"});
    // The linked worktree stops in a cherry-pick: a commit only its HEAD holds is not kept, stopped
    // or aborted.
    s.git(repo, {"update-ref", "-d", gg::keep::refName(w)});
    s.git(repo, {"update-ref", "-d", gg::keep::refName(x)});
    s.git(repo, {"branch", "side", "main~4"});
    s.git(repo, {"checkout", "-q", "side"});
    s.commitFile(repo, "f1.txt", "side\n", "Side change");
    s.git(repo, {"checkout", "-q", "main"});
    s.commitFile(wt, "f1.txt", "wt\n", "Conflicting in the worktree");
    s.gitMayFail(wt, {"cherry-pick", "side"});
    GG_REQUIRE(fs::exists(s.root() / "wt" / ".git"));
    GG_CHECK(!s.gitOut(wt, {"status", "--porcelain=v2"}).empty());
    const Maintained stopped = maintain(repo);
    GG_CHECK(stopped.ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(wt, {"cherry-pick", "--abort"});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
}

GG_TEST("keep", "an operation that creates commits and finishes while a rebase is stopped names nothing")
{
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "f.txt", "a\nb\nc\n", "Base");
    s.git(repo, {"branch", "theirs"});
    s.commitFile(repo, "g.txt", "g\n", "Clean pick");
    s.commitFile(repo, "f.txt", "a\nours\nc\n", "Conflicting pick");
    s.git(repo, {"checkout", "-q", "theirs"});
    s.commitFile(repo, "f.txt", "a\ntheirs\nc\n", "Theirs");
    s.git(repo, {"checkout", "-q", "main"});
    s.gitMayFail(repo, {"rebase", "theirs"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    // HEAD is detached on the replayed clean pick, which no branch reaches.
    const std::string head = s.head(repo);
    GG_REQUIRE(s.gitOut(repo, {"for-each-ref", "--contains", head, "refs/heads"}).empty());
    {
        gg::git2::Repository r = gg::git2::openRepository(repo);
        gg::OperationRecorder rec(r.get(), "test", "commit", false);
        rec.setCreatesCommits(true);
        rec.begin();
        s.git(repo, {"checkout", "-q", "--theirs", "f.txt"});
        s.git(repo, {"add", "f.txt"});
        s.git(repo, {"commit", "-q", "-m", "Mid-rebase commit"}); // HEAD moves, still mid-rebase
        rec.finish(true, false);
    }
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(repo, {"rebase", "--abort"});
    {
        const Maintained again = maintain(repo);
        GG_CHECK(again.ok);
        GG_CHECK(again.changes.empty());
    }
    // The fixtures stopped in a rebase, as the other tests have them.
    const fs::path mid = s.fixture(Recipe::MidRebaseApply);
    GG_CHECK(maintain(mid).ok);
    GG_CHECK(keepRefs(s, mid).empty());
}

GG_TEST("keep", "changes lists the refs created and deleted with their old and new values")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x1 = detachedCommit(s, repo, "main", "x1.txt");
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const std::string zero(gg::git2::hexSize(gg::git2::oidType(r.get())), '0');
    Maintained m = maintain(repo, {x1});
    GG_REQUIRE(m.changes.size() == 1u);
    GG_CHECK_STR_EQ(m.changes[0].ref, gg::keep::refName(x1));
    GG_CHECK_STR_EQ(m.changes[0].oldValue, zero);
    GG_CHECK_STR_EQ(m.changes[0].newValue, x1);
    s.commitFile(repo, "x2.txt", "x2\n", "Next");
    const std::string x2 = s.head(repo);
    m = maintain(repo, {x2});
    GG_REQUIRE(m.changes.size() == 2u);
    const auto deleted = std::find_if(m.changes.begin(), m.changes.end(), [&](const auto& c) { return c.newValue == zero; });
    const auto created = std::find_if(m.changes.begin(), m.changes.end(), [&](const auto& c) { return c.oldValue == zero; });
    GG_REQUIRE(deleted != m.changes.end() && created != m.changes.end());
    GG_CHECK_STR_EQ(deleted->ref, gg::keep::refName(x1));
    GG_CHECK_STR_EQ(deleted->oldValue, x1);
    GG_CHECK_STR_EQ(created->ref, gg::keep::refName(x2));
    GG_CHECK_STR_EQ(created->newValue, x2);
}

GG_TEST("keep", "legacy cleanup leaves the keep refs and removes every other refs/gg ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo, {x}).ok);
    s.git(repo, {"update-ref", "refs/gg/workspaces/x", "main"});
    s.git(repo, {"update-ref", "refs/gg/commit-aliases", "main"});
    s.git(repo, {"update-ref", "refs/gg/keeper", "main"}); // not under refs/gg/keep/
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const gg::LegacyMigration migration = gg::migrateLegacy(r.get());
    GG_CHECK(migration.error.empty());
    GG_CHECK_EQ(migration.refsDeleted, 3);
    GG_CHECK(gg::trim(s.gitOut(repo, {"for-each-ref", "--format=%(refname)", "refs/gg/"})) == gg::keep::refName(x));
    GG_CHECK(gg::keep::isKeepRef(gg::keep::refName(x)));
    GG_CHECK(!gg::keep::isKeepRef("refs/gg/keeper"));
}

GG_TEST("keep", "the journal tracks refs/gg/keep/* and ignores the rest of refs/gg/")
{
    GG_CHECK(gg::journal::tracked("refs/heads/main"));
    GG_CHECK(gg::journal::tracked("refs/gg/keep/" + std::string(40, 'a')));
    GG_CHECK(!gg::journal::tracked("refs/gg/other"));
    GG_CHECK(!gg::journal::tracked("refs/gg/keeper"));
    GG_CHECK(!gg::journal::tracked("HEAD"));
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    {
        gg::git2::Repository r = gg::git2::openRepository(repo);
        std::string error;
        gg::reconcile::run(r.get(), &error); // the baseline
        GG_CHECK(error.empty());
    }
    s.git(repo, {"update-ref", gg::keep::refName(x), x});
    s.git(repo, {"update-ref", "refs/gg/other", x});
    {
        gg::git2::Repository r = gg::git2::openRepository(repo);
        std::string error;
        gg::reconcile::run(r.get(), &error);
        GG_CHECK(error.empty());
    }
    bool keepRecorded = false;
    bool otherRecorded = false;
    gg::journal::Journal journal{repo / ".git"};
    for (const auto& op : journal.read(nullptr))
        for (const auto& c : op.refs) {
            keepRecorded = keepRecorded || (c.ref == gg::keep::refName(x) && c.newValue == x);
            otherRecorded = otherRecorded || c.ref == "refs/gg/other";
        }
    GG_CHECK(keepRecorded);
    GG_CHECK(!otherRecorded);
    s.git(repo, {"update-ref", "-d", "refs/gg/other"}); // not a keep ref: the transparency check would flag it
}

GG_TEST("keep", "a keep ref with a ref below it does not stop the keep ref the commit needs")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"update-ref", gg::keep::refName(x) + "/sub", x});
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(m.error.empty());
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(x)), x);
    GG_CHECK_EQ(m.changes.size(), 2u);
    const auto again = maintain(repo);
    GG_CHECK(again.ok);
    GG_CHECK(again.changes.empty());
}

GG_TEST("keep", "an unborn HEAD keeps nothing")
{
    const fs::path repo = s.fixture(Recipe::Empty);
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(m.changes.empty());
    GG_CHECK(keepRefs(s, repo).empty());
}

GG_TEST("keep", "a keep ref that points at a blob is deleted and nothing is kept")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string blob = s.revParse(repo, "main:" + s.gitOut(repo, {"ls-tree", "--name-only", "main"}).substr(0, s.gitOut(repo, {"ls-tree", "--name-only", "main"}).find('\n')));
    const std::string ref = "refs/gg/keep/" + blob;
    s.git(repo, {"update-ref", ref, blob});
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK_EQ(m.changes.size(), 1u);
}

GG_TEST("keep", "a linked worktree whose directory was removed does not matter: the named commit is kept, its HEAD is not")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("gone");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    std::error_code ec;
    fs::remove_all(wt, ec);
    GG_REQUIRE(!fs::exists(wt));
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    const Maintained m = maintain(repo, {x});
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
}

GG_TEST("keep", "a failed write reports the error, changes nothing and lists no changes")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    const std::string before = s.gitOut(repo, {"for-each-ref"});
    // A stale lock file on the ref the commit needs makes the create fail.
    const fs::path lock = repo / ".git" / (gg::keep::refName(x) + ".lock");
    fs::create_directories(lock.parent_path());
    { std::ofstream(lock) << "locked\n"; }
    const Maintained m = maintain(repo, {x});
    GG_CHECK(!m.ok);
    GG_CHECK(!m.error.empty());
    GG_CHECK(m.changes.empty());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"for-each-ref"}), before);
    fs::remove(lock);
    GG_CHECK(maintain(repo, {x}).ok);
    GG_CHECK(keepRefs(s, repo) == names({x}));
}

GG_TEST("keep", "commits on a detached HEAD through the app: the keep ref moves with the tip inside the operation; Undo and Redo restore it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(keepRefs(s, repo).empty());
    const std::string start = refState(s, repo);

    newDetachedOn(ctx, s, s.head(repo));
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo).size() == 1; }));
    s.settle();
    const std::string x1 = s.head(repo);
    GG_CHECK(keepRefs(s, repo) == names({x1}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(x1)), x1);
    const std::string afterFirst = refState(s, repo);
    {
        const auto made = opsChanging(repo, gg::keep::refName(x1), true);
        GG_REQUIRE(made.size() == 1);
        GG_CHECK(made[0].src != "git");
        GG_CHECK(made[0].ended);
    }

    newDetachedOn(ctx, s, x1);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != x1 && keepRefs(s, repo) == names({s.head(repo)}); }));
    s.settle();
    const std::string x2 = s.head(repo);
    GG_CHECK(keepRefs(s, repo) == names({x2}));
    const std::string afterSecond = refState(s, repo);
    {
        // The old ref's deletion and the new one's creation are in the one operation of the commit.
        const auto made = opsChanging(repo, gg::keep::refName(x2), true);
        const auto gone = opsChanging(repo, gg::keep::refName(x1), false);
        GG_REQUIRE(made.size() == 1);
        GG_REQUIRE(gone.size() == 1);
        GG_CHECK_STR_EQ(made[0].id, gone[0].id);
        GG_CHECK(made[0].src != "git");
        GG_CHECK(!made[0].keepOnly());
    }
    GG_CHECK_EQ(gitOpCount(repo), 0u); // nothing was reported as an external change
    GG_CHECK_EQ(housekeepingCount(repo), 0u); // the commit's keep refs are its own

    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, afterFirst));
    GG_CHECK(keepRefs(s, repo) == names({x1}));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, start));
    GG_CHECK(keepRefs(s, repo).empty());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(becomes(s, repo, afterFirst));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(becomes(s, repo, afterSecond));
    GG_CHECK(keepRefs(s, repo) == names({x2}));
    GG_CHECK_EQ(gitOpCount(repo), 0u);
    // And once more back: Undo deletes the keep ref of the second commit again.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, afterFirst));
    GG_CHECK(keepRefs(s, repo) == names({x1}));
}

GG_TEST("keep", "creating a branch at the kept tip removes its keep ref in that operation; Undo brings it back")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    newDetachedOn(ctx, s, s.head(repo));
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo).size() == 1; }));
    s.settle();
    const std::string x = s.head(repo);
    const std::string kept = refState(s, repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(x)) != nullptr; }));
    s.contextMenu(rowRef(x).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "graduated");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_REQUIRE(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/graduated"}).ok(); }));
    s.settle();
    GG_CHECK(keepRefs(s, repo).empty());
    const auto gone = opsChanging(repo, gg::keep::refName(x), false);
    GG_REQUIRE(gone.size() == 1);
    GG_CHECK(gone[0].src != "git");
    const bool hasBranch = std::any_of(gone[0].refs.begin(), gone[0].refs.end(),
        [](const gg::journal::RefChange& r) { return r.ref == "refs/heads/graduated"; });
    GG_CHECK(hasBranch);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, kept));
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(gitOpCount(repo), 0u);
}

GG_TEST("keep", "a plain git commit on a detached HEAD is not kept: its pass journals the commit and creates no keep ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(keepRefs(s, repo).empty());
    const size_t before = gitOpCount(repo);
    s.commitFile(repo, "x.txt", "x\n", "Detached x");
    const std::string x = s.head(repo);
    const gg::reconcile::Result pass = gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK(pass.appended > 0);
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK_EQ(gitOpCount(repo), before + 1); // one operation for the commit, with no keep ref in it
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK(opsChanging(repo, gg::keep::refName(x), true).empty());
    const gg::reconcile::Result again = gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK_EQ(again.appended, 0u);
    // A second commit is not kept either.
    s.commitFile(repo, "y.txt", "y\n", "Detached y");
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK_EQ(gitOpCount(repo), before + 2);
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

GG_TEST("keep", "a pass whose only change is a keep ref writes one operation of its own; the next pass writes nothing")
{
    // The journal's first pass takes the refs as they are (no operation for them), so only the
    // deletion of the keep ref of a commit a branch reaches is left to record.
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo, {x}).ok);
    s.git(repo, {"branch", "graduated", x});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    const gg::reconcile::Result first = gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK_EQ(first.appended, 1u);
    GG_CHECK(keepRefs(s, repo).empty());
    const auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    // No operation of this pass deleted it: housekeeping, not "external changes".
    GG_CHECK_STR_EQ(ops[0].src, "gg");
    GG_CHECK_STR_EQ(ops[0].label, "keep refs");
    GG_CHECK(ops[0].keepOnly());
    GG_CHECK(ops[0].ended);
    GG_REQUIRE(ops[0].refs.size() == 1);
    GG_CHECK_STR_EQ(ops[0].refs[0].ref, gg::keep::refName(x));
    GG_CHECK_STR_EQ(ops[0].refs[0].oldValue, x);
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    GG_CHECK_EQ(journalOps(repo).size(), 1u);
}

GG_TEST("keep", "after leaving the detached commit with a plain git checkout the commit survives reflog expiry and gc")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo, {x}).ok);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({x}));
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headDetached == false; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({x}));
    s.git(repo, {"reflog", "expire", "--expire=now", "--all"});
    s.git(repo, {"gc", "-q", "--prune=now"});
    GG_CHECK(commitReadable(s, repo, x));
}

GG_TEST("keep", "a stopped native rebase: a pass maintains the keep refs (a graduated one goes) but keeps no intermediate HEAD")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    gg::reconcile::run(r.get(), &error);
    s.commitFile(repo, "a1.txt", "a1\n", "A1");
    s.commitFile(repo, "a2.txt", "a2\n", "A2");
    const std::string a2 = s.head(repo);
    keepByName(repo, {a2});
    GG_REQUIRE(keepRefs(s, repo) == names({a2}));
    // Stops after the first replayed commit (the exec fails): HEAD is then in the middle of the rebase.
    s.gitMayFail(repo, {"rebase", "-x", "false", "--onto", "main~1", "main"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.head(repo) != a2);
    // A plain branch at the kept tip graduates it: only a pass that maintains the keep refs removes K(a2).
    s.git(repo, {"branch", "graduated", a2});
    const size_t known = journalOps(repo).size();
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo).empty()); // K(a2) gone, the intermediate HEAD not kept
    const auto dropped = opsChanging(repo, gg::keep::refName(a2), false);
    GG_REQUIRE(dropped.size() == 1);
    const auto ops = journalOps(repo);
    GG_CHECK(std::none_of(ops.begin(), ops.begin() + static_cast<std::ptrdiff_t>(known),
        [&](const gg::journal::Operation& op) { return op.id == dropped[0].id; })); // begun by this pass
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    for (const auto& op : journalOps(repo))
        for (const auto& c : op.refs)
            GG_CHECK(!gg::keep::isKeepRef(c.ref) || c.newValue == gg::zeroId(r.get()) || c.newValue == a2);
}

GG_TEST("keep", "a rebase stopped in a pass and finished in a terminal keeps nothing, and a keep ref deleted later is not appended to the operation the earlier pass began")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    gg::reconcile::run(r.get(), &error);
    s.commitFile(repo, "a.txt", "a\n", "A");
    const std::string a = s.head(repo);
    keepByName(repo, {a}); // K(a)
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    s.commitFile(repo, "d1.txt", "d1\n", "D1");
    s.commitFile(repo, "d.txt", "d\n", "D");
    const std::string d = s.head(repo);
    keepByName(repo, {d});
    GG_REQUIRE(keepRefs(s, repo) == names({a, d}));
    // D1 and D are replayed onto A, stopping after each (the exec fails): HEAD moves while the rebase is open.
    s.gitMayFail(repo, {"rebase", "-x", "false", "--onto", a, "main"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    gg::reconcile::run(r.get(), &error); // the pass that begins the rebase's operation
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo) == names({a, d})); // the intermediate HEAD is not kept
    std::string rebaseOp;
    for (const auto& op : journalOps(repo))
        if (!op.ended)
            rebaseOp = op.id;
    GG_REQUIRE(!rebaseOp.empty());
    // A commit of the linked worktree and a step of the stopped rebase (HEAD moves: the operation
    // that keeps w by name appends that to the open rebase operation, which is then the last one it
    // touched): the worktree's keep ref is housekeeping, not part of the rebase.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    s.gitMayFail(repo, {"rebase", "--continue"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    keepByName(repo, {w});
    GG_CHECK(keepRefs(s, repo) == names({a, d, w}));
    {
        const auto kept = opsChanging(repo, gg::keep::refName(w), true);
        GG_REQUIRE(kept.size() == 1);
        GG_CHECK(kept[0].id != rebaseOp);
        GG_CHECK(kept[0].keepOnly());
        GG_CHECK_STR_EQ(kept[0].label, "keep refs");
    }
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    {
        // A branch at A made through ggui while the rebase is stopped: the recorder joins the rebase's
        // operation, and its finish leaves the keep refs alone (mid-rebase), so K(a) stays for now.
        gg::OperationRecorder rec(r.get(), "test", "branch", false);
        rec.begin();
        s.git(repo, {"branch", "graduated", a});
        rec.finish(true, false);
    }
    GG_CHECK(keepRefs(s, repo) == names({a, d, w}));
    const size_t before = journalOps(repo).size();
    for (int i = 0; i < 4 && fs::exists(repo / ".git" / "rebase-merge"); ++i)
        s.gitMayFail(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-merge"));
    const std::string d2 = s.head(repo);
    GG_CHECK(d2 != d);
    // The pass that sees the rebase end (this pass begins nothing, and a detached rebase writes no
    // finish entry for it to read): D' is not kept (a rebase finished in a terminal names nothing)
    // and A's keep ref, which the branch made, goes in a "keep refs" operation of its own, never in
    // the rebase's, which an earlier pass began (for the journal it would be older than everything
    // written since).
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo) == names({d, w}));
    const auto gone = opsChanging(repo, gg::keep::refName(a), false);
    GG_REQUIRE(gone.size() == 1);
    GG_CHECK(gone[0].id != rebaseOp);
    GG_CHECK(gone[0].keepOnly());
    const auto ops = journalOps(repo);
    for (const auto& op : ops)
        if (op.id == rebaseOp)
            for (const auto& c : op.refs)
                GG_CHECK(!gg::keep::isKeepRef(c.ref));
    GG_CHECK(std::none_of(ops.begin(), ops.begin() + static_cast<std::ptrdiff_t>(before),
        [&](const gg::journal::Operation& op) { return op.id == gone[0].id; })); // written by the last pass
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    GG_CHECK_EQ(journalOps(repo).size(), ops.size());
}

GG_TEST("keep", "Undo and Redo pass over a keep-only operation: main's commit is undone and redone, the other worktree's keep ref stays")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string tip = s.head(repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); // main's commit, through the app
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();

    // A plain git commit in the detached worktree, kept by name (keepAsHousekeeping): its keep ref is
    // journaled as an operation of its own (nothing else changed in main's view), on top of the commit.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    keepAsHousekeeping(repo, {w});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({w}); }));
    const auto kept = opsChanging(repo, gg::keep::refName(w), true);
    GG_REQUIRE(kept.size() == 1);
    GG_CHECK_STR_EQ(kept[0].src, "gg"); // housekeeping: a pass in main made nothing of it
    GG_CHECK_STR_EQ(kept[0].label, "keep refs");
    GG_CHECK(kept[0].keepOnly());
    GG_CHECK_EQ(kept[0].refs.size(), 1u);
    GG_CHECK_STR_EQ(journalOps(repo).back().id, kept[0].id); // on top
    s.settle();
    const std::string withKeep = refState(s, repo);

    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) == tip; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({w})); // the worktree's keep ref was not touched
    GG_CHECK_STR_EQ(s.head(repo), tip);
    GG_CHECK_STR_EQ(s.head(wt), w);

    // A pass between the Undo and the Redo journals nothing new that stops it.
    gg::reconcile::run(r.get(), &error);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();
    GG_CHECK_STR_EQ(refState(s, repo), withKeep);
}

GG_TEST("keep", "the Operations panel lists a keep-only operation with its Restore item disabled; an ordinary row's is enabled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string tip = s.head(repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); // an ordinary, restorable operation
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();
    const auto ordinary = s.session()->operations().back().id;

    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    keepByName(repo, {w});
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({w}); }));
    const auto kept = opsChanging(repo, gg::keep::refName(w), true);
    GG_REQUIRE(kept.size() == 1);
    GG_REQUIRE(kept[0].keepOnly());
    GG_REQUIRE(s.waitUntil([&] {
        for (const auto& op : s.session()->operations())
            if (op.id == kept[0].id)
                return true;
        return false;
    }));
    s.settle();

    s.showPanel("Operations");
    const std::string table = s.child("//Operations", "##ops_table");
    // Opens the row's menu, checks whether the Restore item is disabled, closes the menu.
    auto restoreDisabled = [&](const std::string& id) {
        const std::string row = table + "/**/op_" + id + "/###row";
        if (!s.itemExists(row.c_str()))
            return std::optional<bool>();
        ctx->ItemClick(row.c_str(), ImGuiMouseButton_Right);
        ctx->Yield(2);
        const bool disabled =
            (ctx->ItemInfo("//$FOCUSED/Restore (undo this operation)").ItemFlags & ImGuiItemFlags_Disabled) != 0;
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        return std::optional<bool>(disabled);
    };
    const std::optional<bool> keepRow = restoreDisabled(kept[0].id);
    const std::optional<bool> plainRow = restoreDisabled(ordinary);
    GG_REQUIRE(keepRow.has_value() && plainRow.has_value());
    GG_CHECK(*keepRow);
    GG_CHECK(!*plainRow);
}

GG_TEST("keep", "Undo of a branch created at a kept tip with HEAD elsewhere brings the keep ref back; Redo deletes it again")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"checkout", "-q", "main"});
    keepByName(repo, {x}); // main's HEAD is elsewhere: the keep ref is housekeeping
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({x}); }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({x})); // HEAD is elsewhere: the keep ref is what holds x
    // K(x) was seeded by name (keepByName, before the repository was opened) and journaled as
    // housekeeping, as no operation of main made x; the operations below add none.
    const size_t housekeeping = housekeepingCount(repo);
    GG_CHECK_EQ(housekeeping, 1u);
    {
        // The branch at the kept tip, in an operation of its own (the History panel does not list
        // the kept commit yet, so there is no row to create it from).
        gg::git2::Repository r = gg::git2::openRepository(repo);
        gg::OperationRecorder rec(r.get(), "test", "create graduated", false);
        rec.begin();
        s.git(repo, {"branch", "graduated", x});
        rec.finish(true, false);
    }
    GG_CHECK(keepRefs(s, repo).empty());
    const std::string graduated = refState(s, repo);

    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_REQUIRE(s.waitUntil([&] { return !s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/graduated"}).ok(); }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({x}));
    {
        // The undo operation itself owns the creation of K(x) (its `extra`): no housekeeping.
        const auto made = opsChanging(repo, gg::keep::refName(x), true);
        GG_REQUIRE(made.size() >= 1);
        GG_CHECK(made.back().isUndo());
        GG_CHECK(!made.back().keepOnly());
        GG_CHECK_STR_EQ(journalOps(repo).back().id, made.back().id);
        GG_CHECK_EQ(housekeepingCount(repo), housekeeping);
    }
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(becomes(s, repo, graduated));
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK_EQ(housekeepingCount(repo), housekeeping);
}

// An operation made in the linked worktree `wt` that commits on its detached HEAD (what a native
// command run from there records): the HEAD of that worktree and the keep ref of the commit.
static std::string operationInWorktree(Scenario& s, const fs::path& wt, const std::string& file)
{
    gg::git2::Repository r = gg::git2::openRepository(wt);
    gg::OperationRecorder rec(r.get(), "test", "commit in " + file, false);
    rec.setCreatesCommits(true);
    rec.begin();
    s.commitFile(wt, file, file + "\n", "In " + file);
    rec.finish(true, false);
    return s.head(wt);
}

GG_TEST("keep", "two worktrees: Undo in main does not undo a detached commit made in a linked worktree; Undo there does")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string tip = s.head(repo);
    const std::string wtStart = s.head(wt);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); // main's own commit, through the app
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();

    const std::string w = operationInWorktree(s, wt, "w.txt");
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({w}); }));
    s.settle();
    {
        const auto made = opsChanging(repo, gg::keep::refName(w), true);
        GG_REQUIRE(made.size() == 1);
        GG_CHECK(!gg::journal::visibleFrom(made[0], "main"));
        GG_CHECK(gg::journal::visibleFrom(made[0], wt.filename().string()));
    }

    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) == tip; })); // main undid its own commit
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({w}));
    GG_CHECK_STR_EQ(s.head(wt), w);

    // From the worktree itself the operation is undone, the keep ref with it.
    gg::git2::Repository r = gg::git2::openRepository(wt);
    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(s.head(wt), wtStart);
    GG_CHECK(keepRefs(s, repo).empty());
    const gg::UndoResult redone = gg::undo(r.get(), true, "test");
    GG_CHECK(redone.ok);
    GG_CHECK_STR_EQ(s.head(wt), w);
    GG_CHECK(keepRefs(s, repo) == names({w}));
}

GG_TEST("keep", "a keep ref in the undone operation that has since moved does not make Undo refuse")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string tip = s.head(repo);
    // W commits with plain git while main's operation runs: main's operation is told to keep w3 (keepNamed
    // before its finish; the begin of an operation would otherwise have recorded the commit as an
    // external change first).
    std::string w3;
    {
        gg::OperationRecorder rec(r.get(), "test", "main commit", false);
        rec.begin();
        s.commitFile(wt, "w3.txt", "w3\n", "w3");
        w3 = s.head(wt);
        s.commitFile(repo, "m.txt", "m\n", "In main");
        keepNamed(r, {w3});
        rec.finish(true, false);
    }
    GG_REQUIRE(keepRefs(s, repo) == names({w3}));
    // K(w3) is W's commit, not main's: it is in a housekeeping operation of its own.
    GG_REQUIRE(opsChanging(repo, gg::keep::refName(w3), true).size() == 1);
    GG_CHECK_STR_EQ(opsChanging(repo, gg::keep::refName(w3), true)[0].label, "keep refs");
    // W commits again, w4 is kept by name instead (keepAsHousekeeping): K(w3) moved away from the value main's operation left.
    s.commitFile(wt, "w4.txt", "w4\n", "w4");
    const std::string w4 = s.head(wt);
    keepAsHousekeeping(repo, {w4});
    GG_REQUIRE(keepRefs(s, repo) == names({w4}));

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), tip);
    GG_CHECK(keepRefs(s, repo) == names({w4}));
    GG_CHECK_STR_EQ(s.head(wt), w4);
}

GG_TEST("keep", "a keep ref that another worktree's commit needs is journaled as housekeeping, not as part of the operation that created it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string tip = s.head(repo);
    std::string w3;
    std::string mainOp;
    {
        gg::OperationRecorder rec(r.get(), "test", "main commit", false);
        rec.begin();
        mainOp = rec.id();
        s.commitFile(wt, "w3.txt", "w3\n", "w3");
        w3 = s.head(wt);
        s.commitFile(repo, "m.txt", "m\n", "In main");
        keepNamed(r, {w3}); // kept by name, but not by main's operation: its keep ref is not its own
        rec.finish(true, false);
    }
    GG_REQUIRE(keepRefs(s, repo) == names({w3}));
    const auto ops = journalOps(repo);
    GG_REQUIRE(!ops.empty());
    for (const auto& op : ops)
        if (op.id == mainOp) {
            GG_CHECK(std::none_of(op.refs.begin(), op.refs.end(),
                [](const gg::journal::RefChange& c) { return gg::keep::isKeepRef(c.ref); }));
            GG_CHECK(!op.refs.empty()); // main's own HEAD and branch
        }
    const auto& top = ops.back();
    GG_CHECK(top.keepOnly());
    GG_CHECK(top.ended);
    GG_CHECK(top.ok);
    GG_CHECK_STR_EQ(top.src, "gg");
    GG_CHECK_STR_EQ(top.label, "keep refs");
    GG_CHECK_STR_EQ(top.wt, "main");
    GG_REQUIRE(top.refs.size() == 1);
    GG_CHECK_STR_EQ(top.refs[0].ref, gg::keep::refName(w3));
    GG_CHECK_STR_EQ(top.refs[0].newValue, w3);
    GG_CHECK_EQ(housekeepingCount(repo), 1u);

    // Nothing is left for the reconciler to report.
    const size_t before = journalOps(repo).size();
    const gg::reconcile::Result pass = gg::reconcile::run(r.get(), &error);
    GG_CHECK_EQ(pass.appended, 0u);
    GG_CHECK_EQ(journalOps(repo).size(), before);

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), tip);
    GG_CHECK(keepRefs(s, repo) == names({w3}));
    GG_CHECK_STR_EQ(s.head(wt), w3);
    GG_CHECK_EQ(housekeepingCount(repo), 1u);
}

GG_TEST("keep", "Redo passes over a keep-only operation that lies above the undo operation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string tip = s.head(repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();
    const std::string committed = s.head(repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) == tip; }));
    s.settle();

    // A plain commit in the detached worktree, kept by name (keepAsHousekeeping): a keep-only operation above the Undo.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    keepAsHousekeeping(repo, {w}); // no operation of its own on top of it
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({w}); }));
    s.settle();
    const auto ops = journalOps(repo);
    GG_REQUIRE(!ops.empty());
    GG_CHECK(ops.back().keepOnly());
    GG_CHECK(!ops.back().isUndo());

    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) == committed; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({w}));
    GG_CHECK_STR_EQ(s.head(wt), w);
}

GG_TEST("keep", "no adoption: the keep ref an app operation journaled as housekeeping stays with it; the operation a later pass begins for the commit has no keep entry, and Undo there keeps the commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    gg::git2::Repository rm = gg::git2::openRepository(repo);
    gg::git2::Repository rw = gg::git2::openRepository(wt);
    std::string error;
    gg::reconcile::run(rm.get(), &error); // the baselines
    gg::reconcile::run(rw.get(), &error);
    const std::string p = commitOperation(s, wt, "p.txt", "p\n", "Parent"); // W's own operation keeps p
    GG_REQUIRE(keepRefs(s, repo) == names({p}));
    std::string c;
    {
        // W commits with plain git while an operation of main is open and c is kept by name there:
        // K(c) is journaled as housekeeping.
        gg::OperationRecorder rec(rm.get(), "test", "main commit", false);
        rec.begin();
        s.commitFile(wt, "c.txt", "c\n", "Child");
        c = s.head(wt);
        s.commitFile(repo, "m.txt", "m\n", "In main");
        keepNamed(rm, {c});
        rec.finish(true, false);
    }
    GG_REQUIRE(keepRefs(s, repo) == names({c}));
    GG_CHECK_EQ(housekeepingCount(repo), 1u);
    const gg::reconcile::Result pass = gg::reconcile::run(rw.get(), &error); // W's own pass finds the commit
    GG_CHECK(error.empty());
    GG_CHECK(pass.appended > 0);
    const auto created = opsChanging(repo, gg::keep::refName(c), true);
    GG_REQUIRE(created.size() == 1); // only the housekeeping operation: W's commit operation takes nothing
    GG_CHECK(created[0].keepOnly());
    const auto ops = journalOps(repo);
    GG_REQUIRE(!ops.empty());
    GG_CHECK_STR_EQ(ops.back().label, "git commit");
    GG_CHECK(!ops.back().keepOnly());
    for (const auto& r : ops.back().refs)
        GG_CHECK(!gg::keep::isKeepRef(r.ref));
    GG_CHECK_EQ(housekeepingCount(repo), 1u);
    GG_CHECK_EQ(gg::reconcile::run(rw.get(), &error).appended, 0u);
    GG_CHECK_EQ(gg::reconcile::run(rm.get(), &error).appended, 0u);

    const gg::UndoResult undone = gg::undo(rw.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(wt), p);
    GG_CHECK(keepRefs(s, repo) == names({c})); // c stays kept; p, where HEAD is again, is its ancestor
    GG_CHECK_EQ(gg::reconcile::run(rw.get(), &error).appended, 0u);
    GG_CHECK_EQ(gg::reconcile::run(rm.get(), &error).appended, 0u);
}

GG_TEST("keep", "an operation that creates commits names the one on its detached HEAD: its keep ref is that operation's own change")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    std::string c;
    std::string id;
    {
        gg::OperationRecorder rec(r.get(), "test", "commit", false);
        rec.setCreatesCommits(true);
        rec.begin();
        s.commitFile(repo, "c.txt", "c\n", "Child");
        c = s.head(repo);
        id = rec.id();
        rec.finish(true, false);
    }
    GG_CHECK(keepRefs(s, repo) == names({c}));
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    const auto created = opsChanging(repo, gg::keep::refName(c), true);
    GG_REQUIRE(created.size() == 1);
    GG_CHECK_STR_EQ(created[0].id, id);
    GG_CHECK(!created[0].keepOnly());
}

GG_TEST("keep", "a rewrite names the commit it puts a detached HEAD on, not one it puts a branch on")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    gg::rewrite::Rewriter detachedRewriter(repo);
    const gg::rewrite::Result detached = detachedRewriter.compute(rewordPlan(r.get(), x));
    GG_REQUIRE(detached.ok);
    GG_CHECK(!detached.headAfter.empty() && detached.headAfter != detached.headBefore);
    GG_CHECK(std::find(detached.keepExtra.begin(), detached.keepExtra.end(), detached.headAfter) != detached.keepExtra.end());

    s.git(repo, {"checkout", "-q", "main"});
    gg::rewrite::Rewriter branchRewriter(repo);
    const gg::rewrite::Result onBranch = branchRewriter.compute(rewordPlan(r.get(), s.head(repo)));
    GG_REQUIRE(onBranch.ok);
    GG_CHECK(onBranch.headAfter != onBranch.headBefore);
    GG_CHECK(std::find(onBranch.keepExtra.begin(), onBranch.keepExtra.end(), onBranch.headAfter) == onBranch.keepExtra.end());
}

GG_TEST("keep", "a rebase that finishes without moving HEAD in its last step (edit, then Continue) keeps the tip")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    detachedCommit(s, repo, "main", "p.txt");
    s.commitFile(repo, "t.txt", "t\n", "Detached t");
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '2s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops at t
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(!gg::native::rebaseIdentity(r.get()).empty());
    std::string tip;
    {
        gg::OperationRecorder rec(r.get(), "test", "continue", false);
        rec.setCreatesCommits(true);
        rec.begin();
        s.git(repo, {"rebase", "--continue"});
        tip = s.head(repo);
        rec.finish(true, false);
    }
    GG_REQUIRE(gg::native::rebaseIdentity(r.get()).empty());
    GG_CHECK(keepRefs(s, repo) == names({tip}));
}

GG_TEST("keep", "a keep ref a resumed recorder creates again after an operation of a later pass deleted it is housekeeping, and no pass journals it twice")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string t = detachedOperation(s, repo, "main", "t.txt"); // K(t)
    GG_REQUIRE(keepRefs(s, repo) == names({t}));
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops on t, unchanged
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(!gg::native::rebaseIdentity(r.get()).empty());
    GG_REQUIRE(s.head(repo) == t);
    gg::reconcile::run(r.get(), &error); // begins the rebase's operation
    GG_CHECK(error.empty());
    // A branch at t in a terminal: the next pass begins its operation (later than the rebase's) and
    // deletes K(t) in it.
    s.git(repo, {"branch", "b", t});
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_REQUIRE(keepRefs(s, repo).empty());
    s.git(repo, {"branch", "-q", "-D", "b"});
    {
        gg::OperationRecorder rec(r.get(), "test", "continue", false); // joins the rebase's operation
        rec.setCreatesCommits(true);
        rec.begin();
        s.git(repo, {"rebase", "--continue"});
        rec.finish(true, false); // names t: K(t) comes back
    }
    GG_REQUIRE(gg::native::rebaseIdentity(r.get()).empty());
    GG_CHECK(keepRefs(s, repo) == names({t}));
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo) == names({t}));
    const auto made = opsChanging(repo, gg::keep::refName(t), true);
    GG_REQUIRE(made.size() == 2); // the one that named it first, and the housekeeping that names it again
    GG_CHECK(made.back().keepOnly());
}

GG_TEST("keep", "Undo of a rebase finished through ggui on a detached HEAD deletes the tip's keep ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string p = detachedOperation(s, repo, "main~1", "p.txt"); // the rebase below replays it onto main
    s.commitFile(repo, "t.txt", "t\n", "Detached t");
    const std::string before = s.head(repo);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '2s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops at t
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(!gg::native::rebaseIdentity(r.get()).empty());
    std::string tip;
    {
        gg::OperationRecorder rec(r.get(), "test", "continue", false); // joins the rebase begun above
        rec.setCreatesCommits(true);
        rec.begin();
        s.git(repo, {"rebase", "--continue"});
        tip = s.head(repo);
        rec.finish(true, false);
    }
    GG_REQUIRE(gg::native::rebaseIdentity(r.get()).empty());
    GG_REQUIRE(tip != before);
    GG_REQUIRE(keepRefs(s, repo) == names({p, tip}));

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), before);
    GG_CHECK(keepRefs(s, repo) == names({p})); // the tip's ref goes; p's was never touched
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

GG_TEST("keep", "a keep ref deleted in the step that finishes a rebase belongs to the rebase's operation: Undo keeps the commit again, Redo deletes it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string p = detachedOperation(s, repo, "main~1", "p.txt");
    s.commitFile(repo, "t.txt", "t\n", "Detached t");
    const std::string before = s.head(repo);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '2s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops at t
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(!gg::native::rebaseIdentity(r.get()).empty());
    std::string tip;
    {
        gg::OperationRecorder rec(r.get(), "test", "continue", false); // joins the rebase begun above
        rec.setCreatesCommits(true);
        rec.begin();
        s.git(repo, {"rebase", "--continue"});
        tip = s.head(repo);
        s.git(repo, {"update-ref", "-d", gg::keep::refName(p)}); // what the app's maintenance does for p
        rec.finish(true, false);
    }
    GG_REQUIRE(gg::native::rebaseIdentity(r.get()).empty());
    GG_REQUIRE(tip != before);
    GG_CHECK(keepRefs(s, repo) == names({tip}));
    const auto deleted = opsChanging(repo, gg::keep::refName(p), false);
    GG_REQUIRE(deleted.size() == 1);
    GG_CHECK(!deleted.front().keepOnly());
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), before);
    GG_CHECK(keepRefs(s, repo) == names({p}));

    const gg::UndoResult redone = gg::undo(r.get(), true, "test");
    GG_CHECK(redone.ok);
    GG_CHECK_STR_EQ(redone.error, "");
    GG_CHECK(keepRefs(s, repo) == names({tip}));
}

GG_TEST("keep", "Redo of an undone commit made on a detached HEAD keeps the commit again")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string main = s.head(repo);
    const std::string c = detachedOperation(s, repo, "main", "c.txt");
    GG_REQUIRE(keepRefs(s, repo) == names({c}));

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_REQUIRE(undone.ok);
    GG_CHECK_STR_EQ(s.head(repo), main);
    GG_CHECK(keepRefs(s, repo).empty());

    const gg::UndoResult redone = gg::undo(r.get(), true, "test");
    GG_CHECK(redone.ok);
    GG_CHECK_STR_EQ(redone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), c);
    GG_CHECK(keepRefs(s, repo) == names({c}));
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

GG_TEST("keep", "a checkout of a kept detached commit in a terminal takes no keep ref: Undo of it leaves the commit kept")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string main = s.head(repo);
    const std::string c = detachedOperation(s, repo, "main", "c.txt"); // its operation keeps c
    GG_REQUIRE(keepRefs(s, repo) == names({c}));
    s.git(repo, {"checkout", "-q", "main"});
    s.git(repo, {"checkout", "-q", c});
    GG_CHECK(gg::reconcile::run(r.get(), &error).appended > 0);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo) == names({c}));
    GG_CHECK_EQ(opsChanging(repo, gg::keep::refName(c), true).size(), 1u);
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(undone.error, "");
    GG_CHECK_STR_EQ(s.head(repo), main); // HEAD is on main again
    GG_CHECK(keepRefs(s, repo) == names({c}));
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

// The kept commit is not listed by the History panel while HEAD is elsewhere, so the rewrites below
// are driven at library level (the plan and the recorder the app builds), the way the app runs them.
GG_TEST("keep", "a reword of the kept tip keeps the new commit instead; Undo brings the old ref back, Redo swaps again")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const KeptChain c = keptChain(s, repo, r);
    GG_REQUIRE(keepRefs(s, repo) == names({c.t}));
    const fs::path linked = s.path("linked");
    s.git(repo, {"worktree", "add", "-q", "--detach", linked.string(), "main"});
    const std::string start = refState(s, repo);

    const auto mapping = rewriteOperation(r, repo, "reword", [&](git_repository* g) { return rewordPlan(g, c.t); });
    GG_REQUIRE(mapping.count(c.t) == 1);
    const std::string t2 = mapping.at(c.t);
    GG_CHECK(keepRefs(s, repo) == names({t2}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(t2)), t2);
    const std::string rewritten = refState(s, repo);
    {
        // The old ref's deletion and the new one's creation are the rewrite's own. It changed keep refs
        // only, so it is visible from the worktree it ran in and from no other.
        const auto made = opsChanging(repo, gg::keep::refName(t2), true);
        const auto gone = opsChanging(repo, gg::keep::refName(c.t), false);
        GG_REQUIRE(made.size() == 1);
        GG_REQUIRE(gone.size() >= 1);
        GG_CHECK_STR_EQ(made[0].id, gone.back().id);
        GG_CHECK(!made[0].keepOnly());
        GG_CHECK(gg::journal::visibleFrom(made[0], "main"));
        GG_CHECK(!gg::journal::visibleFrom(made[0], "linked"));
    }
    checkNothingElse(s, repo, r);

    const gg::UndoResult undone = gg::undo(r.get(), false, "test");
    GG_CHECK(undone.ok);
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK(keepRefs(s, repo) == names({c.t}));
    checkNothingElse(s, repo, r);

    const gg::UndoResult redone = gg::undo(r.get(), true, "test");
    GG_CHECK(redone.ok);
    GG_CHECK_STR_EQ(refState(s, repo), rewritten);
    GG_CHECK(keepRefs(s, repo) == names({t2}));
    checkNothingElse(s, repo, r);
}

GG_TEST("keep", "a reword of the commit below the kept tip rewrites the tip too: exactly one keep ref, on the new tip")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const KeptChain c = keptChain(s, repo, r);
    GG_REQUIRE(keepRefs(s, repo) == names({c.t}));
    const std::string start = refState(s, repo);

    const auto mapping = rewriteOperation(r, repo, "reword", [&](git_repository* g) { return rewordPlan(g, c.p); });
    GG_REQUIRE(mapping.count(c.p) == 1);
    GG_REQUIRE(mapping.count(c.t) == 1);
    const std::string t2 = mapping.at(c.t);
    GG_CHECK(t2 != c.t);
    GG_CHECK(keepRefs(s, repo) == names({t2}));
    GG_CHECK_STR_EQ(s.revParse(repo, t2 + "~1"), mapping.at(c.p));
    checkNothingElse(s, repo, r);

    GG_CHECK(gg::undo(r.get(), false, "test").ok);
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK(keepRefs(s, repo) == names({c.t}));
    checkNothingElse(s, repo, r);
}

GG_TEST("keep", "dropping the kept tip above another unreachable commit keeps its parent; Undo restores the tip's ref and drops the parent's")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    const KeptChain c = keptChain(s, repo, r);
    GG_REQUIRE(keepRefs(s, repo) == names({c.t}));
    const std::string start = refState(s, repo);

    const auto mapping = rewriteOperation(r, repo, "drop", [&](git_repository* g) { return abandonPlan(g, c.t); });
    GG_REQUIRE(mapping.count(c.t) == 1);
    GG_CHECK_STR_EQ(mapping.at(c.t), c.p); // what took the dropped commit's place
    GG_CHECK(keepRefs(s, repo) == names({c.p}));
    const std::string abandoned = refState(s, repo);
    checkNothingElse(s, repo, r);

    GG_CHECK(gg::undo(r.get(), false, "test").ok);
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK(keepRefs(s, repo) == names({c.t}));
    GG_CHECK(!s.gitMayFail(repo, {"rev-parse", "-q", "--verify", gg::keep::refName(c.p)}).ok());
    checkNothingElse(s, repo, r);

    GG_CHECK(gg::undo(r.get(), true, "test").ok);
    GG_CHECK_STR_EQ(refState(s, repo), abandoned);
    checkNothingElse(s, repo, r);
}

GG_TEST("keep", "dropping a kept tip whose parent is on a branch leaves no keep ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string t = detachedOperation(s, repo, "main", "t.txt"); // main's tip is its parent
    s.git(repo, {"checkout", "-q", "main"});
    gg::reconcile::run(r.get(), &error);
    GG_REQUIRE(keepRefs(s, repo) == names({t}));
    const std::string start = refState(s, repo);

    const auto mapping = rewriteOperation(r, repo, "drop", [&](git_repository* g) { return abandonPlan(g, t); });
    GG_REQUIRE(mapping.count(t) == 1);
    GG_CHECK_STR_EQ(mapping.at(t), s.revParse(repo, "main"));
    GG_CHECK(keepRefs(s, repo).empty());
    checkNothingElse(s, repo, r);

    GG_CHECK(gg::undo(r.get(), false, "test").ok);
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK(keepRefs(s, repo) == names({t}));
    checkNothingElse(s, repo, r);
}

GG_TEST("keep", "an operation with a keep ref entry and an index record is not keep-only")
{
    gg::journal::Operation op;
    op.id = "op";
    op.src = gg::journal::keepHousekeepingSrc;
    op.refs.push_back({gg::keep::refName(std::string(40, 'a')), std::string(40, '0'), std::string(40, 'a')});
    GG_CHECK(op.keepOnly());
    gg::journal::Operation user = op; // a user operation that changed only keep refs is not housekeeping
    user.src = "ggui";
    GG_CHECK(!user.keepOnly());
    gg::journal::Operation withIndex = op;
    withIndex.index.push_back(gg::journal::IndexChange{"", std::string(40, 'b'), std::string(40, 'c'), false});
    GG_CHECK(!withIndex.keepOnly());
    gg::journal::Operation withWorktree = op;
    withWorktree.worktrees.push_back(gg::journal::WorktreeChange{});
    GG_CHECK(!withWorktree.keepOnly());
    gg::journal::Operation undo = op;
    undo.undoes = "other";
    GG_CHECK(!undo.keepOnly());
}

GG_TEST("keep", "git gg new --before a kept tip moves the keep ref to the replayed commit; undo and redo move it back and forth")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string t = detachedOperation(s, repo, "main", "t.txt");
    s.git(repo, {"checkout", "-q", "main"});
    gg::reconcile::run(r.get(), &error);
    GG_REQUIRE(keepRefs(s, repo) == names({t}));
    const auto subject = [&](const std::string& rev) { return gg::trim(s.gitOut(repo, {"log", "-1", "--format=%s", rev})); };
    const std::string tSubject = subject(t);

    GG_REQUIRE(s.gitgg(repo, {"new", "--before", t, "-m", "inserted"}).ok());
    const std::vector<std::string> refs = keepRefs(s, repo);
    GG_REQUIRE(refs.size() == 1u);
    GG_CHECK(refs != names({t}));
    GG_CHECK(!s.gitMayFail(repo, {"rev-parse", "-q", "--verify", gg::keep::refName(t)}).ok());
    const std::string t2 = s.revParse(repo, refs[0]);
    GG_CHECK(refs == names({t2}));
    GG_CHECK(t2 != t);
    GG_CHECK_STR_EQ(subject(t2), tSubject);
    GG_CHECK_STR_EQ(subject(t2 + "~1"), "inserted");
    checkNothingElse(s, repo, r);

    GG_REQUIRE(s.gitgg(repo, {"undo"}).ok());
    GG_CHECK(keepRefs(s, repo) == names({t}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(t)), t);
    checkNothingElse(s, repo, r);

    GG_REQUIRE(s.gitgg(repo, {"redo"}).ok());
    GG_CHECK(keepRefs(s, repo) == names({t2}));
    GG_CHECK_STR_EQ(s.revParse(repo, gg::keep::refName(t2)), t2);
    checkNothingElse(s, repo, r);
}

// The kept commits of the open snapshot, as "<id> <summary>".
std::vector<std::string> keptOf(Scenario& s)
{
    std::vector<std::string> out;
    for (const auto& k : s.session()->snapshot()->kept)
        out.push_back(k.id.hex() + " " + k.summary);
    return out;
}

bool hasKeepBadge(Scenario& s, const std::string& id)
{
    const auto* row = s.session()->history().row(ggui::core::Oid::fromHex(id));
    return row && std::any_of(row->refs.begin(), row->refs.end(), [&](const ggui::core::RefBadge& b) {
        return b.kind == ggui::core::RefKind::Keep && b.name == id;
    });
}

GG_TEST("keep", "the snapshot lists the kept commits with their summaries, and none once a branch reaches them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(maintain(repo, {x}).ok);
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(keepRefs(s, repo) == names({x}));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return keptOf(s) == std::vector<std::string>{x + " Detached x.txt"}; }));
    s.git(repo, {"branch", "graduated", x});
    GG_CHECK(s.waitUntil([&] { return keepRefs(s, repo).empty() && keptOf(s).empty(); }));
}

GG_TEST("keep", "History: a kept commit carries a Keep badge with its short ID and loses it, and its row, with its ref hidden")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(maintain(repo, {x}).ok);
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(x)) != nullptr; }));
    GG_CHECK(hasKeepBadge(s, x));
    const std::string badge = "//History/**/" + x + "/###badge_" + x;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badge.c_str()); }));
    GG_CHECK(s.itemText(badge.c_str()).find(x.substr(0, 7)) != std::string::npos);
    GG_CHECK(s.itemText(badge.c_str()).find(x.substr(0, 8)) == std::string::npos);
    // Hidden like a branch: its commit leaves History unless another ref reaches it.
    s.session()->history().toggleRef(gg::keep::refName(x), false);
    GG_CHECK(s.waitUntil([&] {
        return !s.session()->history().loading() && s.session()->history().row(ggui::core::Oid::fromHex(x)) == nullptr;
    }));
    GG_CHECK(!s.itemExists(badge.c_str()));
    s.session()->history().toggleRef(gg::keep::refName(x), false);
    GG_CHECK(s.waitUntil([&] { return hasKeepBadge(s, x); }));
}

GG_TEST("keep", "History: the detached HEAD's own commit has the Head badge and no Keep badge")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(maintain(repo, {x}).ok);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(x)) != nullptr; }));
    GG_CHECK(keptOf(s).size() == 1);
    const auto* row = s.session()->history().row(ggui::core::Oid::fromHex(x));
    GG_REQUIRE(row != nullptr);
    GG_CHECK(std::any_of(row->refs.begin(), row->refs.end(),
        [](const ggui::core::RefBadge& b) { return b.kind == ggui::core::RefKind::Head; }));
    GG_CHECK(!hasKeepBadge(s, x));
}

GG_TEST("keep", "the Operations tooltip names a keep ref as detached <short ID>")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    keepByName(repo, {w});
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({w}); }));
    const auto kept = opsChanging(repo, gg::keep::refName(w), true);
    GG_REQUIRE(kept.size() == 1);
    GG_REQUIRE(s.waitUntil([&] {
        for (const auto& op : s.session()->operations())
            if (op.id == kept[0].id)
                return true;
        return false;
    }));
    s.settle();
    s.showPanel("Operations");
    const std::string row = s.child("//Operations", "##ops_table") + "/**/op_" + kept[0].id + "/###row";
    GG_REQUIRE(s.itemExists(row.c_str()));
    ctx->MouseMove(row.c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "detached " + w.substr(0, 10)));
    GG_CHECK(!s.textShown("//##Tooltip_00", gg::keep::kPrefix));
}

// A kept commit made on a detached HEAD at `from`, with HEAD back on main (the repository is not open yet).
std::string keptCommit(Scenario& s, const fs::path& repo, const std::string& from, const std::string& file)
{
    const std::string id = detachedCommit(s, repo, from, file);
    GG_CHECK(maintain(repo, {id}).ok);
    s.git(repo, {"checkout", "-q", "main"});
    return id;
}

// Branches panel: the Detached node and its rows.
std::string detachedRow(const std::string& hex) { return "//Branches/detached_group/detached_" + hex; }
const char* const kDetachedEye = "//Branches/detached_group/###eye";

GG_TEST("keep", "Branches: the Detached node and a row per kept commit appear with the kept commit and go with it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.settle();
    GG_CHECK(!s.itemExists(kDetachedEye));
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(kDetachedEye); }));
    const std::string row = detachedRow(x);
    GG_REQUIRE(s.itemExists((row + "/###detached_" + x).c_str()));
    GG_CHECK_STR_EQ(s.itemText((row + "/###detached_" + x).c_str()), x.substr(0, 7) + "  Detached x.txt");
    s.git(repo, {"branch", "graduated", x});
    GG_CHECK(s.waitUntil([&] { return !s.itemExists(kDetachedEye); }));
    GG_CHECK(!s.itemExists((row + "/###detached_" + x).c_str()));
}

GG_TEST("keep", "Branches: a Detached row's eye hides its commit in History; the node's eye and Hide all hide every kept commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    const std::string y = keptCommit(s, repo, "main~1", "y.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto& history = s.session()->history();
    const std::string kx = gg::keep::refName(x), ky = gg::keep::refName(y);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(kDetachedEye) && s.itemExists((detachedRow(y) + "/###eye").c_str()); }));
    GG_REQUIRE(s.waitUntil([&] { return history.row(ggui::core::Oid::fromHex(x)) && history.row(ggui::core::Oid::fromHex(y)); }));
    // The kept commits are listed in the snapshot's order.
    const ggui::core::SnapshotPtr snap = s.session()->snapshot();
    const auto& kept = snap->kept;
    GG_REQUIRE(kept.size() == 2u);
    GG_CHECK(ctx->ItemInfo((detachedRow(kept[0].id.hex()) + "/###eye").c_str()).RectFull.Min.y
        < ctx->ItemInfo((detachedRow(kept[1].id.hex()) + "/###eye").c_str()).RectFull.Min.y);
    // A row's eye.
    ctx->ItemClick((detachedRow(x) + "/###eye").c_str());
    GG_CHECK(!history.refVisible(kx) && history.refVisible(ky));
    GG_CHECK(s.waitUntil([&] { return !history.loading() && history.row(ggui::core::Oid::fromHex(x)) == nullptr; }));
    GG_CHECK(history.row(ggui::core::Oid::fromHex(y)) != nullptr);
    // The node's eye: mixed shows all, then hides all.
    ctx->ItemClick(kDetachedEye);
    GG_CHECK(history.refVisible(kx) && history.refVisible(ky));
    ctx->ItemClick(kDetachedEye);
    GG_CHECK(!history.refVisible(kx) && !history.refVisible(ky));
    GG_CHECK(history.refVisible("refs/heads/main"));
    // Show all and Hide all at the top reach the kept commits too.
    ctx->ItemClick("//Branches/###show_all_branches");
    GG_CHECK(history.refVisible(kx) && history.refVisible(ky));
    ctx->ItemClick("//Branches/###hide_all_branches");
    GG_CHECK(!history.refVisible(kx) && !history.refVisible(ky) && !history.refVisible("refs/heads/main"));
}

GG_TEST("keep", "Branches: the filter lists the kept commits whose ID or summary matches, and the node's eye acts on the listed ones")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    const std::string y = keptCommit(s, repo, "main~1", "y.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((detachedRow(y) + "/###eye").c_str()); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "DETACHED Y");
    ctx->Yield(2);
    GG_CHECK(s.itemExists((detachedRow(y) + "/###eye").c_str()));
    GG_CHECK(!s.itemExists((detachedRow(x) + "/###eye").c_str()));
    ctx->ItemClick(kDetachedEye);
    GG_CHECK(!history.refVisible(gg::keep::refName(y)) && history.refVisible(gg::keep::refName(x)));
    ctx->ItemInputValue("//Branches/##branch_filter", x.substr(2, 6).c_str());
    ctx->Yield(2);
    GG_CHECK(s.itemExists((detachedRow(x) + "/###eye").c_str()));
    GG_CHECK(!s.itemExists((detachedRow(y) + "/###eye").c_str()));
    ctx->ItemInputValue("//Branches/##branch_filter", "no such commit");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(kDetachedEye));
}

GG_TEST("keep", "Branches: a kept commit that is another worktree's detached HEAD names that worktree")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("detached-wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.commitFile(wt, "w.txt", "w\n", "wt");
    const std::string w = s.head(wt);
    keepByName(repo, {w});
    s.showPanel("Branches");
    const std::string label = detachedRow(w) + "/###detached_" + w;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(label.c_str()); }));
    GG_CHECK_STR_EQ(s.itemText(label.c_str()), w.substr(0, 7) + "  wt  [detached-wt]");
}

GG_TEST("keep", "Branches: a Detached row's hover Check out detaches HEAD on the commit and then goes; the row is the current one")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = detachedRow(x) + "/###detached_" + x;
    const std::string checkout = detachedRow(x) + "/###act_checkout";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->MouseMove("//Branches/##branch_filter");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(checkout.c_str()));
    ctx->MouseMove(row.c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists(checkout.c_str()));
    GG_CHECK(s.itemExists((detachedRow(x) + "/###act_branch").c_str()));
    ctx->ItemClick(checkout.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headDetached && s.session()->snapshot()->head.hex() == x; }));
    s.settle();
    ctx->MouseMove(row.c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists((detachedRow(x) + "/###act_branch").c_str()));
    GG_CHECK(!s.itemExists(checkout.c_str()));
    // The commit stays kept: HEAD is on it, no branch reaches it.
    GG_CHECK(s.itemExists(row.c_str()));
}

GG_TEST("keep", "Branches: a double click on a Detached row checks the commit out detached")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = detachedRow(x) + "/###detached_" + x;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    GG_REQUIRE(!s.session()->snapshot()->headDetached);
    ctx->MouseMove(row.c_str());
    ctx->Yield(3);
    ctx->ItemDoubleClick(row.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headDetached && s.session()->snapshot()->head.hex() == x; }));
}

GG_TEST("keep", "Branches: Create branch on a Detached row creates the branch at the kept commit and the row goes")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = detachedRow(x) + "/###detached_" + x;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->MouseMove(row.c_str());
    ctx->Yield(3);
    ctx->ItemClick((detachedRow(x) + "/###act_branch").c_str());
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "graduated");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_REQUIRE(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/graduated"}).ok(); }));
    GG_CHECK_STR_EQ(s.revParse(repo, "graduated"), x);
    GG_CHECK(s.waitUntil([&] { return keepRefs(s, repo).empty() && !s.itemExists(row.c_str()); }));
    // The menu item opens the same dialog at the commit.
    const std::string y = keptCommit(s, repo, "main~1", "y.txt");
    const std::string yRow = detachedRow(y) + "/###detached_" + y;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(yRow.c_str()); }));
    s.contextMenu(yRow.c_str(), "Create branch here...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogButton("Create branch", "Cancel");
}

GG_TEST("keep", "Branches: a Detached row's menu has Check out, Create branch, Reveal, Copy and Drop commit; Copy and Drop commit work")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = keptCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = detachedRow(x) + "/###detached_" + x;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->ItemClick(row.c_str(), ImGuiMouseButton_Right);
    for (const char* item : {"Check out", "Create branch here...", "Reveal", "Drop commit..."})
        GG_CHECK(s.itemExists(("//$FOCUSED/" + std::string(item)).c_str()));
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy " + x.substr(0, 7) + "###copy_id");
    ctx->ItemClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), x.substr(0, 7));
    // Shift held at the click: the full ID.
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    ctx->ItemClick(row.c_str(), ImGuiMouseButton_Right);
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy full ID###copy_id");
    ctx->ItemClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), x);
    // Reveal selects the commit in History.
    s.contextMenu(row.c_str(), "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == x; }));
    // Drop commit asks first; the commit is dropped and its row goes.
    s.contextMenu(row.c_str(), "Drop commit...");
    GG_REQUIRE(s.dialogOpen("Drop commit"));
    s.dialogButton("Drop commit", "Drop");
    GG_CHECK(s.waitUntil([&] { return keepRefs(s, repo).empty() && !s.itemExists(row.c_str()); }));
    GG_CHECK(keptOf(s).empty());
}

// App operations on a detached HEAD: only the commit an operation creates and names is kept.

// A detached HEAD on a kept new commit (made through the app) in a repository opened in the app; returns its id,
// or "" when the commit was not kept.
std::string openOnKeptCommit(ImGuiTestContext* ctx, Scenario& s, const fs::path& repo)
{
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    GG_CHECK(s.openRepository(repo));
    s.settle();
    newDetachedOn(ctx, s, s.head(repo));
    if (!s.waitUntil([&] { return keepRefs(s, repo).size() == 1; }))
        return {};
    s.settle();
    return s.head(repo);
}

GG_TEST("keep", "amending the kept tip through the app replaces its keep ref in its own operation; Undo restores the old one, Redo the new one")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = openOnKeptCommit(ctx, s, repo);
    GG_REQUIRE(!x.empty());
    const std::string kept = refState(s, repo);
    s.write(repo, "a.txt", "amended\n");
    s.git(repo, {"add", "a.txt"});
    ctx->MenuClick("//##MainMenuBar/Commit/Commit...");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogCheck("Commit", "amend", "Amend");
    s.dialogText("Commit", "message", "Amended");
    s.dialogButton("Commit", "Amend");
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != x; }));
    s.settle();
    const std::string x2 = s.head(repo);
    const std::string amended = refState(s, repo);
    GG_CHECK(keepRefs(s, repo) == names({x2})); // the amend deletes the keep ref of x it replaced
    {
        const auto made = opsChanging(repo, gg::keep::refName(x2), true);
        GG_REQUIRE(made.size() == 1);
        GG_CHECK(made[0].src != "git");
        GG_CHECK(!made[0].keepOnly());
    }
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK_EQ(gitOpCount(repo), 0u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, kept));
    GG_CHECK(keepRefs(s, repo) == names({x}));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(becomes(s, repo, amended));
    GG_CHECK(keepRefs(s, repo) == names({x2}));
    GG_CHECK_EQ(gitOpCount(repo), 0u);
}

GG_TEST("keep", "merging a branch into the kept detached HEAD through the app keeps the merge commit; Undo drops its keep ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "-b", "side", "main~2"});
    s.commitFile(repo, "s.txt", "s\n", "Side change");
    const std::string x = openOnKeptCommit(ctx, s, repo);
    GG_REQUIRE(!x.empty());
    const std::string kept = refState(s, repo);
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_side/###branch_side"); }));
    s.contextMenu("//Branches/branch_side/###branch_side", "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogButton("Merge into HEAD", "Merge");
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != x; }));
    s.settle();
    const std::string merge = s.head(repo);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^1"), x);
    GG_CHECK(keepRefs(s, repo) == names({merge})); // x is an ancestor of the merge: its ref went
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, kept));
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(gitOpCount(repo), 0u);
}

GG_TEST("keep", "cherry-picking onto the kept detached HEAD keeps the new commit, which replaces its parent's ref; Duplicate keeps its copy; Undo drops them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"checkout", "-q", "-b", "side", "main~2"});
    s.commitFile(repo, "s.txt", "s\n", "Side change");
    const std::string src = s.head(repo);
    const std::string srcParent = s.revParse(repo, "HEAD^");
    const std::string x = openOnKeptCommit(ctx, s, repo); // HEAD stays on x from here on
    GG_REQUIRE(!x.empty());
    const std::string kept = refState(s, repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(src)) != nullptr; }));
    ctx->ItemClick(rowRef(src).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/###cherry_pick");
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != x; }));
    s.settle();
    const std::string picked = s.head(repo);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^"), x);
    GG_CHECK(keepRefs(s, repo) == names({picked})); // x is an ancestor of the new commit: its ref went
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK_EQ(gitOpCount(repo), 0u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, kept));
    GG_CHECK(keepRefs(s, repo) == names({x}));
    // Duplicate of the side commit puts HEAD on a copy of it beside x (not on top of it): both are kept.
    ctx->ItemClick(rowRef(src).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/Duplicate");
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != x; }));
    s.settle();
    const std::string copy = s.head(repo);
    GG_CHECK(copy != x);
    GG_CHECK(copy != src);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^"), srcParent);
    GG_CHECK(keepRefs(s, repo) == names({x, copy}));
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, kept));
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(gitOpCount(repo), 0u);
}

GG_TEST("keep", "checking out an unreachable commit or a branch tip detached through the app keeps nothing")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string old = detachedCommit(s, repo, "main~1", "old.txt"); // plain git: no ref reaches it, no keep ref
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(keepRefs(s, repo).empty());
    s.session()->actions().checkout(old, true);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headDetached && s.head(repo) == old; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo).empty());
    s.session()->actions().checkout(s.revParse(repo, "main"), true);
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "main"); }));
    s.settle();
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK(keepRefs(s, repo).empty());
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK(commitReadable(s, repo, old));
}

GG_TEST("keep", "a rebase of a detached HEAD stopped in an edit and continued in the app keeps nothing while stopped and the commit it ends on after")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string p = detachedOperation(s, repo, "main~1", "p.txt"); // the rebase below replays it onto main
    s.commitFile(repo, "t.txt", "t\n", "Detached t");
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '2s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops at the replayed t
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    const std::string stopped = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::RebasingInteractive; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({p})); // neither the replayed commits nor the stopped HEAD
    ctx->ItemClick("//###Toolbar/Continue##tb_continue");
    GG_REQUIRE(s.waitUntil([&] { return !fs::exists(repo / ".git" / "rebase-merge"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), stopped); // the last step moved nothing: the tip is the stopped commit
    GG_CHECK(keepRefs(s, repo) == names({p, stopped}));
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
}

GG_TEST("keep", "Amend and continue in a rebase of a kept detached HEAD leaves its keep ref to the rebase's operation: Undo restores it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string p = detachedOperation(s, repo, "main~1", "p.txt");
    const std::string before = refState(s, repo);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '1s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "HEAD~1"}); // stops at p itself (fast-forwarded)
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_REQUIRE(s.head(repo) == p);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::RebasingInteractive; }));
    s.settle();
    s.write(repo, "p.txt", "amended\n");
    s.git(repo, {"add", "p.txt"});
    ctx->ItemClick("//###Toolbar/Amend and continue##tb_amend_continue");
    GG_REQUIRE(s.waitUntil([&] { return !fs::exists(repo / ".git" / "rebase-merge"); }));
    s.settle();
    GG_CHECK(keepRefs(s, repo).size() >= 1); // the rebase's own rule decides which others (GG-5 part 2)
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(becomes(s, repo, before));
    GG_CHECK(keepRefs(s, repo) == names({p}));
}

GG_TEST("keep", "a rebase of a branch stopped in a pass and finished in a terminal: the keep ref that goes with the finishing pass is housekeeping, not part of the rebase's operation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    const std::string k = detachedCommit(s, repo, "main", "k.txt");
    keepNamed(r, {k});
    s.git(repo, {"checkout", "-q", "-b", "topic", "main~2"});
    s.commitFile(repo, "t1.txt", "t1\n", "Topic 1");
    s.commitFile(repo, "t2.txt", "t2\n", "Topic 2");
    gg::reconcile::run(r.get(), &error); // the baseline
    GG_REQUIRE(keepRefs(s, repo) == names({k}));
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i '1s/^pick/edit/'");
    s.git(repo, {"rebase", "-i", "main"}); // stops at the replayed Topic 1
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    gg::reconcile::run(r.get(), &error); // the pass that begins the rebase's operation
    GG_CHECK(error.empty());
    std::string rebaseOp;
    for (const auto& op : journalOps(repo))
        if (!op.ended)
            rebaseOp = op.id;
    GG_REQUIRE(!rebaseOp.empty());
    {
        // A branch at k made through ggui while the rebase is stopped: the recorder joins the rebase's
        // operation and leaves the keep refs alone, so K(k) stays for now. The finishing pass finds
        // the branch journaled and begins no operation of its own.
        gg::OperationRecorder rec(r.get(), "test", "branch", false);
        rec.begin();
        s.git(repo, {"branch", "graduated", k});
        rec.finish(true, false);
    }
    GG_CHECK(keepRefs(s, repo) == names({k}));
    for (int i = 0; i < 4 && fs::exists(repo / ".git" / "rebase-merge"); ++i)
        s.gitMayFail(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-merge"));
    const size_t before = journalOps(repo).size();
    // This pass appends the rebase's finish to the operation an earlier pass began and deletes K(k):
    // for the journal that operation is older than everything written since, so the deletion is
    // housekeeping.
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo).empty());
    const auto gone = opsChanging(repo, gg::keep::refName(k), false);
    GG_REQUIRE(gone.size() == 1);
    GG_CHECK(gone[0].id != rebaseOp);
    GG_CHECK(gone[0].keepOnly());
    const auto ops = journalOps(repo);
    GG_CHECK_EQ(ops.size(), before + 1);
    for (const auto& op : ops)
        if (op.id == rebaseOp) {
            GG_CHECK(op.ended);
            for (const auto& c : op.refs)
                GG_CHECK(!gg::keep::isKeepRef(c.ref));
        }
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

} // namespace ggtest
