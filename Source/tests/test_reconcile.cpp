// The reconciler: plain git (no hooks) becomes undoable journal operations (docs: reconciler design).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include "util/Env.hpp"

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

std::vector<gg::journal::Operation> gitOps(const fs::path& repo)
{
    std::vector<gg::journal::Operation> out;
    for (auto& op : journalOps(repo))
        if (op.src == "git")
            out.push_back(std::move(op));
    return out;
}

const gg::journal::RefChange* refChange(const gg::journal::Operation& op, const std::string& ref)
{
    for (const auto& r : op.refs)
        if (r.ref == ref && r.oldValue != r.newValue)
            return &r;
    return nullptr;
}

} // namespace

GG_TEST("reconcile", "plain git changes without hooks are journaled and Undo restores them")
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
    GG_CHECK_STR_EQ(ops.back().label, "git commit"); // read from HEAD's reflog
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
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git", "git commit"), 1u);
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 1u); // the tag has no reflog coverage
    for (int i = 0; i < 2; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.settle();
    }
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
    GG_CHECK_EQ(countOps(repo, "git", "git commit"), 1u);
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
    GG_CHECK(s.waitUntil([&] { return countOps(repo, "git", "git commit") == 1; }));
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

GG_TEST("reconcile", "one operation per plain git command, labelled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string mainRef = "refs/heads/main", featRef = "refs/heads/feat";
    // Each command: exactly one new operation with the expected label and ref changes, nothing
    // left over for an "external changes" operation.
    auto step = [&](std::vector<std::string> args, const std::string& label, std::vector<std::string> refs) {
        const size_t before = gitOps(repo).size();
        s.git(repo, args);
        GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= before + 1; }));
        s.settle();
        ctx->Yield(10);
        const auto ops = gitOps(repo);
        if (ops.size() != before + 1)
            ctx->LogError("'git %s' recorded %zu operations", args.front().c_str(), ops.size() - before);
        GG_REQUIRE(ops.size() == before + 1);
        GG_CHECK_STR_EQ(ops.back().label, label);
        GG_CHECK(!ops.back().cmd.empty());
        for (const auto& ref : refs)
            if (!refChange(ops.back(), ref))
                ctx->LogError("'%s': no change of %s", label.c_str(), ref.c_str());
        size_t changed = 0;
        for (const auto& r : ops.back().refs)
            changed += r.oldValue != r.newValue ? 1 : 0;
        GG_CHECK_EQ(changed, refs.size());
    };
    step({"commit", "-q", "--allow-empty", "-m", "c1"}, "git commit", {mainRef});
    step({"switch", "-q", "-c", "feat"}, "git checkout feat", {"HEAD", featRef});
    step({"commit", "-q", "--allow-empty", "-m", "c2"}, "git commit", {featRef});
    const std::string featTip = s.head(repo);
    step({"checkout", "-q", "main"}, "git checkout main", {"HEAD"});
    step({"merge", "-q", "--no-ff", "-m", "merge feat", "feat"}, "git merge feat", {mainRef});
    step({"reset", "-q", "--hard", "HEAD~1"}, "git reset HEAD~1", {mainRef});
    step({"cherry-pick", "--allow-empty", featTip}, "git cherry-pick", {mainRef});
    for (const auto& op : gitOps(repo))
        GG_CHECK(op.label != "external changes");
}

GG_TEST("reconcile", "switch -c records HEAD's branch change")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    const auto* head = refChange(ops.front(), "HEAD");
    GG_REQUIRE(head != nullptr);
    GG_CHECK_STR_EQ(head->oldValue, "ref:refs/heads/main");
    GG_CHECK_STR_EQ(head->newValue, "ref:refs/heads/feat");
    // Undo puts HEAD back on main (and removes the branch).
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "main"; }));
}

GG_TEST("reconcile", "commit then reset --hard while ggui is closed journals both")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    const std::string committed = refState(s, repo);
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 2);
    GG_CHECK_STR_EQ(ops[0].label, "git commit");
    GG_CHECK_STR_EQ(ops[1].label, "git reset HEAD~1");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // undoes the reset: the commit is back
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == committed; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // undoes the commit
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "stash push leaves no HEAD operation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.write(repo, "f1.txt", "changed for the stash\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    // "reset: moving to HEAD" in HEAD's reflog changes nothing; the stash ref has no HEAD reflog
    // coverage and is journaled as external changes (Inc 4 labels it).
    for (const auto& op : gitOps(repo)) {
        GG_CHECK(refChange(op, "HEAD") == nullptr);
        GG_CHECK(refChange(op, "refs/heads/main") == nullptr);
        GG_CHECK(refChange(op, "refs/stash") != nullptr);
    }
}

GG_TEST("reconcile", "expired reflog falls back to the snapshot and never duplicates")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    s.git(repo, {"reflog", "expire", "--expire=now", "--all"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ctx->Yield(10);
    GG_CHECK_EQ(gitOps(repo).size(), 1u);
    GG_CHECK_STR_EQ(gitOps(repo).front().label, "external changes");
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(gitOps(repo).size(), 1u);
    // The empty reflog is remembered: the next plain command is labelled again.
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the expiry"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    GG_CHECK_STR_EQ(gitOps(repo).back().label, "git commit");
}

GG_TEST("reconcile", "undo of a plain reset --hard carries the clean tree, of a plain commit keeps the changes")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string startHead = s.head(repo);
    s.write(repo, "added.txt", "added\n");
    s.git(repo, {"add", "added.txt"});
    s.git(repo, {"commit", "-q", "-m", "add a file"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / "added.txt"));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // the reset: the file comes back with the commit
    GG_CHECK(s.waitUntil([&] { return fs::exists(repo / "added.txt"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), "");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // the commit: HEAD moves back, the file stays (staged)
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == startHead; }));
    s.settle();
    GG_CHECK(fs::exists(repo / "added.txt"));
    GG_CHECK_STR_EQ(s.read(repo, "added.txt"), "added\n");
}

GG_TEST("reconcile", "a plain commit in a linked worktree is journaled from that worktree's own reflog")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(wt1));
    s.settle();
    s.git(wt1, {"commit", "-q", "--allow-empty", "-m", "in the worktree"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git commit");
    GG_CHECK(!ops.front().wt.empty() && ops.front().wt != "main");
}

GG_TEST("reconcile", "a finished plain rebase is one operation and Undo restores the branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f2"});
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "m1"});
    s.git(repo, {"switch", "-q", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 6; }));
    s.settle();
    const std::string before = refState(s, repo);
    s.git(repo, {"rebase", "-q", "main"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 7; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 7);
    GG_CHECK_STR_EQ(ops.back().label, "git rebase");
    GG_CHECK(refChange(ops.back(), "refs/heads/feat") != nullptr);
    GG_CHECK(refChange(ops.back(), "HEAD") == nullptr); // back on the branch it started on
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == before; }));
}

GG_TEST("reconcile", "a rebase started and aborted in a terminal, then a commit: the commit keeps its branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    s.git(repo, {"rebase", "--abort"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the abort"});
    const std::string committed = refState(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1); // no no-op "git rebase", no "external changes"
    GG_CHECK_STR_EQ(ops.front().label, "git commit");
    GG_CHECK(refChange(ops.front(), "refs/heads/main") != nullptr);
    GG_CHECK(refChange(ops.front(), "HEAD") == nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    GG_CHECK(committed != start);
}

GG_TEST("reconcile", "Undo refuses a plain git operation while a rebase is in progress")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    const std::string state = refState(s, repo);
    const size_t before = journalOps(repo).size();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    s.settle();
    ctx->Yield(10);
    GG_CHECK_STR_EQ(refState(s, repo), state);
    GG_CHECK_EQ(journalOps(repo).size(), before); // no undo operation was written
    GG_CHECK(fs::exists(repo / ".git" / "rebase-merge"));
    s.git(repo, {"rebase", "--abort"});
}

} // namespace ggtest
