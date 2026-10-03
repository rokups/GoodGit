// Keep refs (decision K2): refs/gg/keep/<id> keeps the commits made on a detached HEAD alive.
// keep::maintain on repositories built with plain git, and the places that call it: every
// operation (OperationRecorder::finish) and every reconcile pass.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Keep.hpp>
#include <libgg/Legacy.hpp>
#include <libgg/Operation.hpp>
#include <libgg/Reconcile.hpp>
#include <libgg/Undo.hpp>

#include <algorithm>
#include <fstream>
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

} // namespace

GG_TEST("keep", "a detached HEAD on a commit no branch reaches is kept; the second run changes nothing")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    const Maintained first = maintain(repo);
    GG_CHECK(first.ok);
    GG_CHECK(first.error.empty());
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
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(repo, {"tag", "t1", "main~1"});
    s.git(repo, {"tag", "-a", "-m", "annotated", "t2", "main~2"});
    s.git(repo, {"checkout", "-q", "--detach", "t1"});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    s.git(repo, {"checkout", "-q", "--detach", "t2"}); // an annotated tag is peeled
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    s.git(repo, {"update-ref", "refs/remotes/origin/topic", x});
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(m.changes.empty());
    GG_CHECK(keepRefs(s, repo).empty());
    // Below a branch tip too.
    s.git(repo, {"checkout", "-q", "--detach", "main~3"});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo).empty());
}

GG_TEST("keep", "tips only: a commit on the detached HEAD replaces its parent's ref; unrelated lines keep one each")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x1 = detachedCommit(s, repo, "main", "x1.txt");
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({x1}));
    s.commitFile(repo, "x2.txt", "x2\n", "Second on the detached HEAD");
    const std::string x2 = s.head(repo);
    const Maintained m = maintain(repo);
    GG_CHECK(m.ok);
    GG_CHECK(keepRefs(s, repo) == names({x2}));
    GG_CHECK_EQ(m.changes.size(), 2u);
    // A second, unrelated line: both tips stay.
    const std::string y = detachedCommit(s, repo, "main~2", "y.txt");
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({x2, y}));
    GG_CHECK(gg::keep::read(gg::git2::openRepository(repo).get()).size() == 2u);
    // A merge of the two lines swallows both.
    s.git(repo, {"merge", "-q", "--no-edit", x2});
    const std::string merged = s.head(repo);
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({merged}));
}

GG_TEST("keep", "graduation: a branch or a tag at a kept tip or a descendant of it deletes its ref")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(repo).ok);
    const std::string y = detachedCommit(s, repo, "main~1", "y.txt");
    GG_CHECK(maintain(repo).ok);
    const std::string z = detachedCommit(s, repo, "main~2", "z.txt");
    GG_CHECK(maintain(repo).ok);
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
    GG_CHECK(maintain(repo).ok);
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

GG_TEST("keep", "worktrees: a linked detached HEAD is kept; one in the middle of an operation is not considered")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("wt");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    GG_CHECK(maintain(repo).ok);
    GG_CHECK(keepRefs(s, repo) == names({w}));
    // From the linked worktree's own handle the answer is the same.
    {
        const Maintained again = maintain(wt);
        GG_CHECK(again.ok);
        GG_CHECK(again.changes.empty());
    }
    // The main worktree detached as well: both.
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_CHECK(maintain(wt).ok);
    GG_CHECK(keepRefs(s, repo) == names({w, x}));
    s.git(repo, {"checkout", "-q", "main"});
    // The linked worktree stops in a cherry-pick: not considered, so a commit only its HEAD held
    // is not kept.
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
    // Aborted, it counts again.
    s.git(wt, {"cherry-pick", "--abort"});
    GG_CHECK(maintain(repo).ok);
    GG_CHECK_EQ(keepRefs(s, repo).size(), 1u);
}

GG_TEST("keep", "a worktree stopped in the middle of a rebase is not considered")
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
    Maintained m = maintain(repo);
    GG_REQUIRE(m.changes.size() == 1u);
    GG_CHECK_STR_EQ(m.changes[0].ref, gg::keep::refName(x1));
    GG_CHECK_STR_EQ(m.changes[0].oldValue, zero);
    GG_CHECK_STR_EQ(m.changes[0].newValue, x1);
    s.commitFile(repo, "x2.txt", "x2\n", "Next");
    const std::string x2 = s.head(repo);
    m = maintain(repo);
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
    GG_CHECK(maintain(repo).ok);
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

GG_TEST("keep", "a linked worktree whose directory was removed is skipped")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.path("gone");
    s.git(repo, {"worktree", "add", "-q", "--detach", wt.string(), "main"});
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    std::error_code ec;
    fs::remove_all(wt, ec);
    GG_REQUIRE(!fs::exists(wt));
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    const Maintained m = maintain(repo);
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
    const Maintained m = maintain(repo);
    GG_CHECK(!m.ok);
    GG_CHECK(!m.error.empty());
    GG_CHECK(m.changes.empty());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"for-each-ref"}), before);
    fs::remove(lock);
    GG_CHECK(maintain(repo).ok);
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

GG_TEST("keep", "a plain git commit on a detached HEAD: the pass keeps it inside the commit's operation and the next pass adds nothing")
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
    GG_CHECK(keepRefs(s, repo) == names({x}));
    GG_CHECK_EQ(gitOpCount(repo), before + 1); // one operation for the commit, keep ref included
    GG_CHECK_EQ(housekeepingCount(repo), 0u); // the commit's operation owns the keep ref: no "keep refs"
    const auto made = opsChanging(repo, gg::keep::refName(x), true);
    GG_REQUIRE(made.size() == 1);
    GG_CHECK_STR_EQ(made[0].label, "git commit");
    GG_CHECK(made[0].ended);
    GG_CHECK(!made[0].keepOnly());
    const gg::reconcile::Result again = gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK_EQ(again.appended, 0u);
    GG_CHECK_EQ(gitOpCount(repo), before + 1);
    // A second commit moves the ref, again in the commit's own operation.
    s.commitFile(repo, "y.txt", "y\n", "Detached y");
    const std::string y = s.head(repo);
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(keepRefs(s, repo) == names({y}));
    GG_CHECK_EQ(gitOpCount(repo), before + 2);
    const auto moved = opsChanging(repo, gg::keep::refName(y), true);
    const auto dropped = opsChanging(repo, gg::keep::refName(x), false);
    GG_REQUIRE(moved.size() == 1);
    GG_REQUIRE(dropped.size() == 1);
    GG_CHECK_STR_EQ(moved[0].id, dropped[0].id);
    GG_CHECK_STR_EQ(moved[0].label, "git commit");
    GG_CHECK_EQ(housekeepingCount(repo), 0u);
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
}

GG_TEST("keep", "a pass whose only change is a keep ref writes one operation of its own; the next pass writes nothing")
{
    // The journal's first pass takes HEAD as it is (no operation for it), so only the keep ref of
    // the unanchored commit HEAD is on is left to record.
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    const gg::reconcile::Result first = gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK_EQ(first.appended, 1u);
    GG_CHECK(keepRefs(s, repo) == names({x}));
    const auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    // No operation of this pass made x: housekeeping, not "external changes".
    GG_CHECK_STR_EQ(ops[0].src, "gg");
    GG_CHECK_STR_EQ(ops[0].label, "keep refs");
    GG_CHECK(ops[0].keepOnly());
    GG_CHECK(ops[0].ended);
    GG_REQUIRE(ops[0].refs.size() == 1);
    GG_CHECK_STR_EQ(ops[0].refs[0].ref, gg::keep::refName(x));
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    GG_CHECK_EQ(journalOps(repo).size(), 1u);
}

GG_TEST("keep", "after leaving the detached commit with a plain git checkout the commit survives reflog expiry and gc")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string x = detachedCommit(s, repo, "main", "x.txt");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({x}); }));
    s.settle();
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
    gg::reconcile::run(r.get(), &error);
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

GG_TEST("keep", "a rebase stopped in a pass and finished in a terminal: its keep refs are not appended to the operation the earlier pass began")
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
    gg::reconcile::run(r.get(), &error); // K(a)
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    s.commitFile(repo, "d1.txt", "d1\n", "D1");
    s.commitFile(repo, "d.txt", "d\n", "D");
    const std::string d = s.head(repo);
    gg::reconcile::run(r.get(), &error);
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
    // A commit of the linked worktree and a step of the stopped rebase (HEAD moves: the pass appends
    // that to the open operation, which is then the last one it touched): the worktree's keep ref
    // is housekeeping, not part of the rebase.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    s.gitMayFail(repo, {"rebase", "--continue"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    GG_CHECK(keepRefs(s, repo) == names({a, d, w}));
    {
        const auto kept = opsChanging(repo, gg::keep::refName(w), true);
        GG_REQUIRE(kept.size() == 1);
        GG_CHECK(kept[0].id != rebaseOp);
        GG_CHECK(kept[0].keepOnly());
        GG_CHECK_STR_EQ(kept[0].label, "keep refs");
    }
    GG_CHECK_EQ(gg::reconcile::run(r.get(), &error).appended, 0u);
    const size_t before = journalOps(repo).size();
    for (int i = 0; i < 4 && fs::exists(repo / ".git" / "rebase-merge"); ++i)
        s.gitMayFail(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-merge"));
    const std::string d2 = s.head(repo);
    GG_CHECK(d2 != d);
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    // D' is a child of A: A's keep ref goes, D' gets one, D keeps its own.
    GG_CHECK(keepRefs(s, repo) == names({d, d2, w}));
    // Those changes are in an operation of their own, never in the rebase's, which an earlier pass
    // began (for the journal it would be older than everything written since).
    const auto made = opsChanging(repo, gg::keep::refName(d2), true);
    const auto gone = opsChanging(repo, gg::keep::refName(a), false);
    GG_REQUIRE(made.size() == 1);
    GG_REQUIRE(gone.size() == 1);
    GG_CHECK(made[0].id != rebaseOp);
    GG_CHECK(gone[0].id != rebaseOp);
    const auto ops = journalOps(repo);
    for (const auto& op : ops)
        if (op.id == rebaseOp)
            for (const auto& c : op.refs)
                GG_CHECK(!gg::keep::isKeepRef(c.ref));
    for (const auto& id : {made[0].id, gone[0].id})
        GG_CHECK(std::none_of(ops.begin(), ops.begin() + static_cast<std::ptrdiff_t>(before),
            [&](const gg::journal::Operation& op) { return op.id == id; })); // written by the last pass
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

    // A plain git commit in the detached worktree, then a pass in main: its keep ref is journaled
    // as an operation of its own (nothing else changed in main's view), on top of the commit.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error);
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
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error);
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
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return keepRefs(s, repo) == names({x}); }));
    s.settle();
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headDetached == false; }));
    s.settle();
    GG_CHECK(keepRefs(s, repo) == names({x})); // HEAD is elsewhere: the keep ref is what holds x
    // The pass that opened the repository journaled K(x) as housekeeping (no operation of this
    // pass made x); the operations below add none.
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
    // W commits with plain git while main's operation runs: its finish keeps w3 (the begin of an
    // operation would otherwise have recorded the commit as an external change first).
    std::string w3;
    {
        gg::OperationRecorder rec(r.get(), "test", "main commit", false);
        rec.begin();
        s.commitFile(wt, "w3.txt", "w3\n", "w3");
        w3 = s.head(wt);
        s.commitFile(repo, "m.txt", "m\n", "In main");
        rec.finish(true, false);
    }
    GG_REQUIRE(keepRefs(s, repo) == names({w3}));
    // K(w3) is W's commit, not main's: it is in a housekeeping operation of its own.
    GG_REQUIRE(opsChanging(repo, gg::keep::refName(w3), true).size() == 1);
    GG_CHECK_STR_EQ(opsChanging(repo, gg::keep::refName(w3), true)[0].label, "keep refs");
    // W commits again, a pass keeps w4 instead: K(w3) moved away from the value main's operation left.
    s.commitFile(wt, "w4.txt", "w4\n", "w4");
    const std::string w4 = s.head(wt);
    gg::reconcile::run(r.get(), &error);
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

    // A plain commit in the detached worktree and a pass: a keep-only operation above the Undo.
    s.commitFile(wt, "w.txt", "w\n", "In the worktree");
    const std::string w = s.head(wt);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error);
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
    s.commitFile(wt, "p.txt", "p\n", "Parent");
    const std::string p = s.head(wt);
    gg::reconcile::run(rw.get(), &error);
    GG_REQUIRE(keepRefs(s, repo) == names({p}));
    std::string c;
    {
        // W commits with plain git while an operation of main is open: main's finish journals K(c)
        // as housekeeping.
        gg::OperationRecorder rec(rm.get(), "test", "main commit", false);
        rec.begin();
        s.commitFile(wt, "c.txt", "c\n", "Child");
        c = s.head(wt);
        s.commitFile(repo, "m.txt", "m\n", "In main");
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

GG_TEST("keep", "a checkout of a kept detached commit in a terminal takes no keep ref: Undo of it leaves the commit kept")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error); // the baseline
    const std::string main = s.head(repo);
    s.git(repo, {"checkout", "-q", "--detach", "main"});
    s.commitFile(repo, "c.txt", "c\n", "Unreachable");
    const std::string c = s.head(repo);
    gg::reconcile::run(r.get(), &error); // K(c) journaled
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

GG_TEST("keep", "an operation with a keep ref entry and an index record is not keep-only")
{
    gg::journal::Operation op;
    op.id = "op";
    op.refs.push_back({gg::keep::refName(std::string(40, 'a')), std::string(40, '0'), std::string(40, 'a')});
    GG_CHECK(op.keepOnly());
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

} // namespace ggtest
