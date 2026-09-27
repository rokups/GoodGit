// Undo/Redo and the Operations panel (§4.1 Edit, §4.7 Operations, §5 U1; P2-25).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

// Everything Undo restores: refs (with symbolic HEAD) and the index.
std::string repoState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    state += "\n" + s.gitOut(repo, {"ls-files", "-s"});
    return state;
}

size_t opsFrom(Scenario& s, const std::string& src)
{
    size_t n = 0;
    for (const auto& op : s.session()->operations())
        n += op.src == src ? 1 : 0;
    return n;
}

bool undone(Scenario& s, const fs::path& repo, const std::string& expected)
{
    const bool ok = s.waitUntil([&] { return repoState(s, repo) == expected; });
    s.settle();
    return ok;
}

} // namespace

GG_TEST("undo", "undo and redo from the menu, keys and toolbar; Operations lists sources and restores",
    "MENU-EDIT-UNDO", "MENU-EDIT-UNDO-KEY", "MENU-EDIT-REDO", "MENU-EDIT-REDO-KEY", "OPS-LIST", "OPS-SOURCE-LABEL",
    "OPS-RESTORE", "OPS-NO-HOOKS-NOTE")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Operations");
    const std::string before = repoState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return repoState(s, repo) != before; }));
    s.settle();
    const std::string after = repoState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(undone(s, repo, before));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(undone(s, repo, after));
    ctx->MenuClick("//##MainMenuBar/Edit/Undo");
    GG_CHECK(undone(s, repo, before));
    ctx->MenuClick("//##MainMenuBar/Edit/Redo");
    GG_CHECK(undone(s, repo, after));
    // The panel lists ggui's operations (new, undo, redo, ...) and git gg's.
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "ggui") >= 5; }));
    GG_REQUIRE(s.gitgg(repo, {"new", "-m", "from the command line"}).ok());
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "git-gg") == 1; }));
    const auto& ops = s.session()->operations();
    const std::string cliOp = ops.back().id;
    s.showPanel("Operations");
    GG_CHECK(s.textShown("//Operations", "git-gg"));
    GG_CHECK(s.textShown("//Operations", "ggui"));
    // Without managed hooks the panel says what Undo covers.
    GG_CHECK(!s.session()->hooksInstalled());
    GG_CHECK(s.textShown("//Operations", "Undo covers ggui and git gg only"));
    // Restore (undo) the command-line operation from its row.
    const std::string row = s.child("//Operations", "##ops_table") + "/**/op_" + cliOp + "/###row";
    GG_REQUIRE(s.itemExists(row.c_str()));
    s.contextMenu(row.c_str(), "Restore (undo this operation)");
    GG_CHECK(undone(s, repo, after));
}

GG_TEST("undo", "refusals: nothing to undo, refs moved outside the journal, local changes in the way",
    "TB-UNDO", "FAIL-LOCKED-REF")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("Nothing to undo") != std::string::npos);

    // A plain git commit (no hooks) moves main behind the journal's back: refused.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return opsFrom(s, "ggui") == 1; }));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "Plain git"});
    const std::string moved = repoState(s, repo);
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("moved outside the journal") != std::string::npos);
    GG_CHECK_STR_EQ(repoState(s, repo), moved);

    // Undoing a checkout must rewrite the working tree; a local edit there would be lost, so
    // ggui offers to stash it first.
    s.git(repo, {"switch", "-q", "-c", "other"});
    s.commitFile(repo, "f1.txt", "other content\n", "Other f1");
    s.git(repo, {"switch", "-q", "main"});
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_other/###branch_other"); }));
    s.contextMenu("//Branches/branch_other/###branch_other", "Check out");
    GG_REQUIRE(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "other"; }));
    s.settle();
    s.write(repo, "f1.txt", "edited after the checkout\n");
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_REQUIRE(s.dialogOpen("Undo would lose changes"));
    s.dialogButton("Undo would lose changes", "Stash and undo");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "main"; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"stash", "list"}).find("before undo") != std::string::npos);
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "line 1\n");

    // A locked ref: the mutation fails cleanly and leaves nothing half done.
    const std::string locked = repoState(s, repo);
    const fs::path lock = repo / ".git" / "refs" / "heads" / "main.lock";
    std::ofstream(lock) << "";
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("lock") != std::string::npos);
    fs::remove(lock);
    GG_CHECK_STR_EQ(repoState(s, repo), locked);
}

GG_TEST("undo", "a corrupt journal line is skipped, not fatal", "HOOK-JOURNAL-CORRUPT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string before = repoState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return opsFrom(s, "ggui") == 1; }));
    s.settle();
    std::ofstream(repo / ".git" / "gg" / "journal", std::ios::app) << "{ this is not json\n\x01\x02 garbage\n";
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return opsFrom(s, "ggui") == 2; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->Yield(2);
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "ggui") == 3; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(undone(s, repo, before));
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("undo", "every everyday mutation can be undone", "UNDO-ALL-MUTATIONS")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    // Runs `act` (a UI action), waits until the repository changed, then Ctrl+Z must restore it.
    auto check = [&](const char* what, const std::function<void()>& act) {
        s.settle();
        const std::string before = repoState(s, repo);
        act();
        const bool changed = s.waitUntil([&] { return repoState(s, repo) != before; });
        s.settle();
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        const bool restored = undone(s, repo, before);
        if (!changed || !restored)
            ctx->LogError("undo of '%s': changed=%d restored=%d", what, changed, restored);
        GG_CHECK(changed && restored);
    };
    check("new commit", [&] { ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); });
    check("create branch", [&] {
        ctx->ItemClick("//Branches/###create_branch");
        s.dialogOpen("Create branch");
        s.dialogText("Create branch", "name", "undo-me");
        s.dialogButton("Create branch", "Create");
    });
    s.git(repo, {"branch", "victim"});
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_victim/###branch_victim"); }));
    check("rename branch", [&] {
        s.contextMenu("//Branches/branch_victim/###branch_victim", "Rename...");
        s.dialogOpen("Rename branch");
        s.dialogText("Rename branch", "name", "renamed");
        s.dialogButton("Rename branch", "Rename");
    });
    check("delete branch", [&] {
        s.contextMenu("//Branches/branch_victim/###branch_victim", "Delete/Local");
        s.dialogOpen("Delete branch");
        s.dialogButton("Delete branch", "Delete");
    });
    check("check out", [&] { s.contextMenu("//Branches/branch_victim/###branch_victim", "Check out"); });
    check("tag", [&] {
        s.showPanel("Tags");
        ctx->ItemClick("//Tags/###create_tag");
        s.dialogOpen("Create tag");
        s.dialogText("Create tag", "name", "v-undo");
        s.dialogButton("Create tag", "Create");
    });
    s.write(repo, "f.txt", "staged\n");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((s.child("//Changes", "##files") + "/Untracked/f.txt/###file_f.txt").c_str()); }));
    check("stage", [&] { s.contextMenu((s.child("//Changes", "##files") + "/Untracked/f.txt/###file_f.txt").c_str(), "Stage"); });
    s.git(repo, {"add", "f.txt"});
    check("commit", [&] {
        ctx->ItemClick("//##Toolbar/###tb_commit");
        s.dialogOpen("Commit");
        s.dialogText("Commit", "message", "Undo me");
        s.dialogButton("Commit", "Commit");
    });
    check("amend message", [&] {
        ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
        s.dialogOpen("Amend");
        s.dialogText("Amend", "message", "Amended");
        s.dialogCheck("Amend", "message_only", "Change the message only (keep the index out)");
        s.dialogButton("Amend", "Amend");
    });
    check("stash", [&] {
        ctx->ItemClick("//##Toolbar/###tb_stash");
        s.dialogOpen("Stash changes");
        s.dialogButton("Stash changes", "Stash");
    });
    const fs::path other = s.root() / (repo.filename().string() + "-other");
    s.git(other, {"pull", "-q", "--no-rebase", "origin", "main"});
    s.commitFile(other, "fetched.txt", "x\n", "To fetch");
    s.git(other, {"push", "-q", "origin", "main"});
    check("fetch", [&] { ctx->ItemClick("//##Toolbar/###tb_fetch"); });
    check("move HEAD to parent", [&] { ctx->ItemClick("//##Toolbar/###tb_prev"); });
}

} // namespace ggtest
