// Undo/Redo and the Operations panel (§4.1 Edit, §4.7 Operations, §5 U1; P2-25).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <chrono>
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

    // Undo of a checkout while HEAD is locked: the ref update fails and the working tree and
    // index it had already carried back are put back too.
    s.contextMenu("//Branches/branch_other/###branch_other", "Check out");
    GG_REQUIRE(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "other"; }));
    s.settle();
    const std::string onOther = repoState(s, repo);
    const fs::path headLock = repo / ".git" / "HEAD.lock";
    std::ofstream(headLock) << "";
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.dismissError());
    fs::remove(headLock);
    GG_CHECK_STR_EQ(repoState(s, repo), onOther);
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "other content\n");
    GG_CHECK(s.statusPorcelain(repo).empty());

    // An operation whose HEAD had no known earlier value (another writer's record): refused.
    s.git(repo, {"switch", "-q", "--detach"});
    s.settle();
    std::ofstream(repo / ".git" / "gg" / "journal", std::ios::app | std::ios::binary)
        << "{\"v\":1,\"t\":\"begin\",\"op\":\"z-1\",\"src\":\"ggui\",\"label\":\"HEAD from nowhere\",\"wt\":\"main\"}\n"
        << "{\"v\":1,\"t\":\"refs\",\"op\":\"z-1\",\"u\":[[\"HEAD\",\"" << std::string(40, '0') << "\",\"" << s.head(repo) << "\"]]}\n"
        << "{\"v\":1,\"t\":\"end\",\"op\":\"z-1\"}\n";
    ctx->KeyPress(ImGuiKey_F5);
    GG_REQUIRE(s.waitUntil([&] {
        const auto& ops = s.session()->operations();
        return !ops.empty() && ops.back().id == "z-1";
    }));
    const std::string detached = repoState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("the previous value of HEAD was not recorded") != std::string::npos);
    GG_CHECK_STR_EQ(repoState(s, repo), detached);
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
    check("move HEAD to parent", [&] { ctx->MenuClick("//##MainMenuBar/Commit/Move HEAD to parent"); });
}

GG_TEST("undo", "journal variants: foreign, torn and future records are skipped; busy and stale locks; a newer format is refused",
    "HOOK-JOURNAL-CORRUPT", "OPS-LIST")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string before = repoState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return opsFrom(s, "ggui") == 1; }));
    s.settle();
    const fs::path journal = repo / ".git" / "gg" / "journal";
    const std::string now = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    // Written by something else: lines that are not records, records with missing or mistyped
    // fields, a newer record version, records whose begin was lost, refs/map payloads of the
    // wrong shape, an unknown record type, operations opened by git processes that are gone
    // (one never closed, one too old), and a torn last line.
    std::ofstream(journal, std::ios::app | std::ios::binary)
        << "\n[1, 2]\n"
        << "{\"op\":\"x\"}\n{\"t\":\"begin\"}\n{\"t\":1,\"op\":\"x\"}\n{\"t\":\"begin\",\"op\":2}\n"
        << "{\"v\":2,\"t\":\"begin\",\"op\":\"from-the-future\"}\n"
        << "{\"v\":1,\"t\":\"end\",\"op\":\"lost\"}\n"
        << "{\"v\":1,\"t\":\"begin\",\"op\":\"t-1\",\"src\":\"git\",\"label\":\"typed by hand\",\"wt\":\"elsewhere\",\"time\":" << now << "}\n"
        << "{\"v\":1,\"t\":\"refs\",\"op\":\"t-1\",\"u\":5}\n"
        << "{\"v\":1,\"t\":\"refs\",\"op\":\"t-1\",\"u\":[7,[\"HEAD\"],[1,\"a\",\"b\"],[\"a\",1,\"b\"],[\"a\",\"b\",1]]}\n"
        << "{\"v\":1,\"t\":\"map\",\"op\":\"t-1\",\"m\":3}\n"
        << "{\"v\":1,\"t\":\"map\",\"op\":\"t-1\",\"m\":[7,[\"a\"],[1,\"b\"],[\"a\",2]]}\n"
        << "{\"v\":1,\"t\":\"index\",\"op\":\"t-1\",\"wt\":\"elsewhere\",\"before\":\"\",\"after\":\"\"}\n"
        << "{\"v\":1,\"t\":\"index\",\"op\":\"t-1\",\"wt\":\"elsewhere\",\"before\":\"\",\"after\":\"\",\"worktree\":true}\n"
        << "{\"v\":1,\"t\":\"future-kind\",\"op\":\"t-1\"}\n"
        << "{\"v\":1,\"t\":\"begin\",\"op\":\"git-999999-1\",\"src\":\"git\",\"label\":\"git gone\",\"time\":" << now << "}\n"
        << "{\"v\":1,\"t\":\"begin\",\"op\":\"git-999998-1\",\"src\":\"git\",\"label\":\"git old\",\"time\":1}\n"
        << "{\"v\":1,\"t\":\"beg";
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return opsFrom(s, "ggui") == 2; }));
    s.settle();
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "git") == 3; }));
    for (const auto& op : s.session()->operations()) {
        if (op.id == "t-1") {
            GG_CHECK(op.refs.empty());
            GG_CHECK(op.rewrites.empty());
            GG_CHECK(op.index.size() == 1u && op.index[0].worktree);
        }
        if (op.src == "git")
            GG_CHECK(op.ended); // their git processes are gone
    }
    // The torn line stays on its own line: the new record after it is intact.
    GG_CHECK(s.read(repo, ".git/gg/journal").find("{\"v\":1,\"t\":\"beg\n{") != std::string::npos);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "ggui") == 3; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(undone(s, repo, before));
    GG_CHECK(s.app.dialogs().current() == nullptr);

    // Another writer holds the journal lock: the mutation still runs, with a warning that Undo
    // will not know it. A lock left behind long ago is stale and removed.
    const fs::path lock = repo / ".git" / "gg" / "journal.lock";
    std::ofstream(lock) << "";
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.title == "Undo journal" && t.message.find("journal busy") != std::string::npos)
                return true;
        return false;
    }, 30.0f));
    s.settle();
    fs::last_write_time(lock, fs::file_time_type::clock::now() - std::chrono::hours(1));
    const size_t ggOps = opsFrom(s, "ggui");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] { return opsFrom(s, "ggui") == ggOps + 1; }));
    s.settle();
    GG_CHECK(!fs::exists(lock));

    // A journal from a newer ggui is not read (nor guessed at): an error, and nothing to undo.
    s.write(repo, ".git/gg/journal", "{\"gg-journal\":99}\n");
    ctx->KeyPress(ImGuiKey_F5);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("journal written by a newer ggui (format 99)") != std::string::npos);
    // A header whose version is not a number counts as the oldest format.
    s.write(repo, ".git/gg/journal", "{\"gg-journal\":\"one\"}\n");
    ctx->KeyPress(ImGuiKey_F5);
    GG_CHECK(s.waitUntil([&] { return s.session()->operations().empty(); }));
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("undo", "in a linked worktree: its HEAD and branch are undone; the main worktree's HEAD is left to it",
    "MENU-EDIT-UNDO-KEY", "OPS-LIST")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    const std::string mainBefore = s.revParse(repo, "main");
    const std::string wt1Before = s.revParse(repo, "wt1");
    // A commit in the main worktree, then one in wt1.
    GG_REQUIRE(s.openRepository(repo));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(repo, "main") != mainBefore; }));
    s.settle();
    GG_REQUIRE(s.openRepository(wt1));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(repo, "wt1") != wt1Before; }));
    s.settle();
    // Undo in wt1: its own commit first (its HEAD and branch)...
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "wt1") == wt1Before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(wt1), wt1Before);
    // ... then the main worktree's commit: its branch goes back; its HEAD is not wt1's to restore.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main") == mainBefore; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "HEAD"}), "refs/heads/main");
    GG_CHECK_STR_EQ(s.gitOut(wt1, {"symbolic-ref", "HEAD"}), "refs/heads/wt1");
    // Redo from wt1 brings the main worktree's commit back.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main") != mainBefore; }));
    s.settle();
    // A checkout in wt1 moves only wt1's HEAD: undone there; from the main worktree it is not
    // visible (the main worktree's newest own operation is undone instead).
    s.git(repo, {"branch", "side", "main~1"});
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_side/###branch_side"); }));
    s.contextMenu("//Branches/branch_side/###branch_side", "Check out");
    GG_REQUIRE(s.waitUntil([&] { return s.gitOut(wt1, {"branch", "--show-current"}) == "side"; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitOut(wt1, {"branch", "--show-current"}) == "wt1"; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(s.waitUntil([&] { return s.gitOut(wt1, {"branch", "--show-current"}) == "side"; }));
    s.settle();
    const std::string mainNow = s.revParse(repo, "main");
    GG_REQUIRE(s.openRepository(repo));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main") != mainNow; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(wt1, {"branch", "--show-current"}), "side");
}

} // namespace ggtest
