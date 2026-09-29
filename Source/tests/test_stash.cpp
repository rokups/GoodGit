// Stash (§4.9, §4.1 toolbar Stash/Pop).
#include "panels/ChangesPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + path + "/###file_" + path;
}

std::string stashRow(int index) { return "//Stashes/stash_" + std::to_string(index) + "/###row"; }

// Number of stashes; SIZE_MAX while refs/stash is being rewritten (git briefly fails then).
size_t stashCount(Scenario& s, const fs::path& repo)
{
    const auto r = s.gitMayFail(repo, {"stash", "list"});
    if (!r.ok())
        return SIZE_MAX;
    size_t n = 0;
    for (const auto& line : gg::splitLines(r.out))
        n += line.empty() ? 0 : 1;
    return n;
}

// f1 staged, f2 unstaged, u.txt untracked.
void dirty(Scenario& s, const fs::path& repo)
{
    s.write(repo, "f1.txt", "f1 staged\n");
    s.git(repo, {"add", "f1.txt"});
    s.write(repo, "f2.txt", "f2 unstaged\n");
    s.write(repo, "u.txt", "untracked\n");
}

void cleanUp(Scenario& s, const fs::path& repo)
{
    s.git(repo, {"reset", "-q", "--hard"});
    s.git(repo, {"clean", "-qfd"});
    s.git(repo, {"stash", "clear"});
}

} // namespace

GG_TEST("stash", "create: message, untracked, keep index, staged only, selected files")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    dirty(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 3; }));
    // Toolbar: everything including untracked files, with a message.
    ctx->ItemClick("//##Toolbar/###tb_stash");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogText("Stash changes", "message", "all of it");
    s.dialogCheck("Stash changes", "untracked", "Include untracked files");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return s.statusPorcelain(repo).empty(); }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"stash", "list"}).find("all of it") != std::string::npos);
    // Pop from the toolbar brings everything back (the index as well only with --index: f1 unstaged).
    ctx->ItemClick("//##Toolbar/###tb_pop");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 0; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "f1 staged\n");
    GG_CHECK_STR_EQ(s.read(repo, "u.txt"), "untracked\n");

    // Working tree menu, keep the index: staged f1 stays staged, f2 is stashed.
    cleanUp(s, repo);
    dirty(s, repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 3; }));
    s.contextMenu("//History/**/###row_wt", "Stash changes...");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogCheck("Stash changes", "keep_index", "Keep the index (--keep-index)");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 1; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "f1.txt");
    GG_CHECK_STR_EQ(s.read(repo, "f2.txt"), "line 2\n");

    // Staged only: f1 is stashed, f2 stays modified.
    cleanUp(s, repo);
    dirty(s, repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 3; }));
    ctx->ItemClick("//##Toolbar/###tb_stash");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogCheck("Stash changes", "staged_only", "Staged changes only (--staged)");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 1; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    GG_CHECK_STR_EQ(s.read(repo, "f2.txt"), "f2 unstaged\n");

    // Selected files only: f2 selected in Changes.
    cleanUp(s, repo);
    dirty(s, repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 3; }));
    ctx->ItemClick(fileRef(s, "Unstaged", "f2.txt").c_str());
    ctx->ItemClick("//##Toolbar/###tb_stash");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogCheck("Stash changes", "selected_only", "Selected files only");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 1; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f2.txt"), "line 2\n");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "f1.txt");
}

GG_TEST("stash", "apply, pop with the index, apply one file, branch, drop, undo, clear")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRow(2).c_str()); }));
    // Apply stash@{2} (a.txt changed): the stash stays.
    s.contextMenu(stashRow(2).c_str(), "Apply");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "a.txt") == "a changed\n"; }));
    s.settle();
    GG_CHECK_EQ(stashCount(s, repo), static_cast<size_t>(3));
    s.git(repo, {"checkout", "-q", "--", "."});

    // One file from stash@{0}: b.txt only.
    ctx->ItemClick(stashRow(0).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Working tree", "b.txt").c_str()); }));
    s.contextMenu(fileRef(s, "Working tree", "b.txt").c_str(), "Apply this file");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "b.txt") == "b again\n"; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / "new.txt"));
    s.git(repo, {"checkout", "-q", "--", "."});
    s.git(repo, {"reset", "-q"});

    // Pop stash@{1} restoring the index: a.txt staged, b.txt unstaged.
    s.contextMenu(stashRow(1).c_str(), "Pop (restore index)");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 2; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "a.txt");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--name-only"}), "b.txt");
    s.git(repo, {"reset", "-q", "--hard"});

    // Branch from stash@{0} (with untracked): a new branch with the changes.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRow(0).c_str()); }));
    s.contextMenu(stashRow(0).c_str(), "Branch from stash...");
    GG_REQUIRE(s.dialogOpen("Branch from stash"));
    s.dialogText("Branch from stash", "name", "from-stash");
    s.dialogButton("Branch from stash", "Create");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "--verify", "-q", "refs/heads/from-stash"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "new.txt"), "untracked\n");
    GG_CHECK_EQ(stashCount(s, repo), static_cast<size_t>(1));
    s.git(repo, {"reset", "-q", "--hard"});
    s.git(repo, {"clean", "-qfd"});
    s.git(repo, {"switch", "-q", "main"});

    // Drop with confirmation; Undo brings it back, Redo drops it again.
    const std::string kept = s.revParse(repo, "stash@{0}");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRow(0).c_str()); }));
    s.contextMenu(stashRow(0).c_str(), "Drop...");
    GG_REQUIRE(s.dialogOpen("Drop stash"));
    s.dialogButton("Drop stash", "Drop");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 0; }));
    s.settle();
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 1; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "stash@{0}"), kept);
    ctx->ItemClick("//##Toolbar/###tb_redo");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 0; }));
    s.settle();

    // Clear all.
    s.write(repo, "a.txt", "once\n");
    s.git(repo, {"stash", "push", "-q"});
    s.write(repo, "a.txt", "again\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRow(1).c_str()); }));
    ctx->ItemClick("//Stashes/Clear all...##clear_stashes");
    GG_REQUIRE(s.dialogOpen("Clear stashes"));
    s.dialogButton("Clear stashes", "Clear all");
    GG_CHECK(s.waitUntil([&] { return stashCount(s, repo) == 0; }));
    s.settle();
}

GG_TEST("stash", "a conflicting pop keeps the stash and leaves plain git conflicts")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    s.commitFile(repo, "a.txt", "a committed differently\n", "Diverge a.txt");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRow(2).c_str()); }));
    s.contextMenu(stashRow(2).c_str(), "Pop");
    GG_CHECK(s.waitUntil([&] { return !s.gitOut(repo, {"ls-files", "-u"}).empty(); }));
    s.settle();
    GG_CHECK_EQ(stashCount(s, repo), static_cast<size_t>(3));
    GG_CHECK(s.read(repo, "a.txt").find("<<<<<<<") != std::string::npos);
    // The conflict shows in Changes like any native conflict.
    GG_CHECK(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "a.txt").c_str()); }));
    if (s.app.dialogs().current() && s.app.dialogs().current()->icon)
        s.dismissError();
}

} // namespace ggtest
