// The reconciler: plain git (no hooks) becomes undoable journal operations (docs: reconciler design).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/Journal.hpp>

#include <fstream>

namespace ggtest {

namespace {

std::vector<gg::journal::Operation> journalOps(const fs::path& repo)
{
    gg::journal::Journal journal{repo / ".git"};
    std::string error;
    return journal.read(&error);
}

size_t countOps(const fs::path& repo, const std::string& src, const std::string& label = {})
{
    size_t n = 0;
    for (const auto& op : journalOps(repo))
        n += op.src == src && (label.empty() || op.label == label) ? 1 : 0;
    return n;
}

size_t panelOps(Scenario& s, const std::string& src)
{
    size_t n = 0;
    for (const auto& op : s.session()->operations())
        n += op.src == src ? 1 : 0;
    return n;
}

// Everything Undo restores: refs (with symbolic HEAD).
std::string refState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    return state;
}

} // namespace

GG_TEST("reconcile", "plain git changes without hooks are journaled as external changes and Undo restores them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(!s.session()->hooksInstalled());
    const std::string start = refState(s, repo);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "x"});
    const std::string committed = refState(s, repo);
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    const auto& ops = s.session()->operations();
    GG_CHECK_STR_EQ(ops.back().label, "external changes");
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    s.settle();
    // Redo brings the commit back.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == committed; }));
    s.settle();

    // A new branch, a tag and a deleted branch: one operation each time the journal is looked at.
    s.git(repo, {"branch", "other", "main~1"});
    const std::string beforeMany = refState(s, repo);
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    s.git(repo, {"branch", "foo"});
    s.git(repo, {"tag", "t"});
    s.git(repo, {"branch", "-D", "other"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") >= 3; }));
    s.settle();
    const std::string afterMany = refState(s, repo);
    GG_CHECK(afterMany.find("refs/tags/t") != std::string::npos);
    GG_CHECK(afterMany.find("refs/heads/foo") != std::string::npos);
    GG_CHECK(afterMany.find("refs/heads/other") == std::string::npos);
    // Undo everything the plain git did (one or more external operations).
    for (int i = 0; i < 4 && refState(s, repo) != beforeMany; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.waitUntil([&] { return refState(s, repo) != afterMany; });
        s.settle();
    }
    GG_CHECK_STR_EQ(refState(s, repo), beforeMany);
    GG_CHECK(refState(s, repo).find("refs/heads/other") != std::string::npos);
}

GG_TEST("reconcile", "changes made while ggui is closed are journaled on open")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    s.git(repo, {"tag", "closed-tag"});
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 1u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "git gg undo in a terminal with ggui closed reconciles first")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok()); // establishes the baseline
    const std::string start = refState(s, repo);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(refState(s, repo) != start);
    const auto r = s.gitgg(repo, {"undo"});
    GG_CHECK(r.ok());
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 1u);
}

GG_TEST("reconcile", "ggui's own operations are never re-journaled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return refState(s, repo) != start; }));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "ggui") == 1; }));
    s.settle();
    ctx->Yield(30);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "ggui"), 1u);
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
    // A terminal git gg command is not re-journaled either.
    GG_REQUIRE(s.gitgg(repo, {"new", "-m", "from the command line"}).ok());
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok());
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
}

GG_TEST("reconcile", "state file deleted: no history replay, no duplicates")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    const size_t before = journalOps(repo).size();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    std::error_code ec;
    GG_REQUIRE(fs::remove(repo / ".git" / "gg" / "reconcile.json", ec));
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(journalOps(repo).size(), before);
    GG_CHECK(fs::exists(repo / ".git" / "gg" / "reconcile.json"));
}

GG_TEST("reconcile", "a deleted journal makes the baseline start over: no replay, later plain git still journaled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    std::error_code ec;
    GG_REQUIRE(fs::remove(repo / ".git" / "gg" / "journal", ec));
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 0u); // the old journal's ops are not replayed as one
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain again"});
    GG_CHECK(s.waitUntil([&] { return countOps(repo, "git", "external changes") == 1; }));
}

GG_TEST("reconcile", "Undo of a symbolic ref that is not HEAD restores that ref and leaves HEAD alone")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.gitMayFail(repo, {"remote", "set-head", "origin", "main"}).ok());
    const std::string symbolic = s.gitOut(repo, {"symbolic-ref", "refs/remotes/origin/HEAD"});
    GG_REQUIRE(!symbolic.empty());
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string head = s.gitOut(repo, {"symbolic-ref", "HEAD"});
    GG_REQUIRE(s.gitMayFail(repo, {"remote", "set-head", "origin", "-d"}).ok());
    GG_CHECK(!s.gitMayFail(repo, {"symbolic-ref", "-q", "refs/remotes/origin/HEAD"}).ok());
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"symbolic-ref", "-q", "refs/remotes/origin/HEAD"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "refs/remotes/origin/HEAD"}), symbolic);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "HEAD"}), head);
}

GG_TEST("reconcile", "managed hooks installed: the reconciler stays out")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "hooked"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 1u);
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 0u);
}

GG_TEST("reconcile", "another worktree's HEAD is never treated as deleted or created")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_REQUIRE(s.openRepository(wt1));
    s.settle();
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
}

} // namespace ggtest
