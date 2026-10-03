// Keep refs (decision K2): refs/gg/keep/<id> keeps the commits made on a detached HEAD alive.
// Library level: keep::maintain on repositories built with plain git (nothing calls it yet).
#include "tests/Harness.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Keep.hpp>
#include <libgg/Legacy.hpp>
#include <libgg/Reconcile.hpp>

#include <algorithm>
#include <fstream>

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

} // namespace ggtest
