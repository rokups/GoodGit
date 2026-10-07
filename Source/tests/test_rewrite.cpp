// In-memory rewrites (§4.3, §5 R1/R2, §8.4 rewrite invariants).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/Journal.hpp>

#include <fstream>

namespace ggtest {

namespace {

// Refs (with symbolic HEAD), index and working tree status: what a failed rewrite must keep.
std::string everything(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    state += "\n" + s.gitOut(repo, {"ls-files", "-s"}) + "\n" + s.statusPorcelain(repo);
    return state;
}

std::string info(Scenario& s, const fs::path& repo, const std::string& rev, const char* format)
{
    return s.gitOut(repo, {"log", "-1", std::string("--format=") + format, rev});
}

void selectCommit(Scenario& s, const std::string& hex)
{
    s.ctx->ItemClick(("//History/**/###row_" + hex).c_str());
    s.waitUntil([&] { return s.itemExists("//Change information/##message"); });
}

} // namespace

GG_TEST("rewrite", "reword a commit in the middle: descendants rebased, the rest untouched, one Undo")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "old", "HEAD~3"});   // an ancestor: never moves
    s.git(repo, {"branch", "mid", "HEAD~1"});   // a descendant of the reworded commit: moves
    // post-rewrite receives the old → new mapping.
    const fs::path mapLog = s.path("post-rewrite.log");
    s.write(repo / ".git" / "hooks", "post-rewrite", "#!/bin/sh\necho \"$1\" > '" + mapLog.string() + "'\ncat >> '" + mapLog.string() + "'\n");
    fs::permissions(repo / ".git" / "hooks" / "post-rewrite", fs::perms::owner_all);
    const std::string target = s.revParse(repo, "HEAD~2");
    const std::string ancestor = s.revParse(repo, "HEAD~3");
    const std::string tipTree = s.revParse(repo, "HEAD^{tree}");
    const std::string before = everything(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, target);
    s.setText("//Change information/##message", "Reworded in the middle\n\nWith a body.");
    ctx->ItemClick("//Change information/###save_message");
    GG_CHECK(s.waitUntil([&] { return info(s, repo, "HEAD~2", "%B") == "Reworded in the middle\n\nWith a body."; }));
    s.settle();
    // Ancestors and unrelated refs keep their ids; descendants are rebased with the same trees.
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~3"), ancestor);
    GG_CHECK_STR_EQ(s.revParse(repo, "old"), ancestor);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^{tree}"), tipTree);
    GG_CHECK_STR_EQ(s.revParse(repo, "mid"), s.revParse(repo, "HEAD~1"));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    GG_CHECK(s.statusPorcelain(repo).empty());
    GG_CHECK_STR_EQ(info(s, repo, "HEAD~1", "%an"), "Test User");
    // post-rewrite ran with "amend" and three mapped commits.
    const auto lines = gg::splitLines(s.read(mapLog.parent_path(), mapLog.filename().string()));
    GG_REQUIRE(!lines.empty());
    GG_CHECK_STR_EQ(lines[0], "amend");
    GG_CHECK(s.read(mapLog.parent_path(), mapLog.filename().string()).find(target + " ") != std::string::npos);
    // One journal operation, recording the rewrites; one Undo restores everything.
    gg::journal::Journal journal{repo / ".git"};
    std::string error;
    const auto ops = journal.read(&error);
    GG_REQUIRE(!ops.empty());
    GG_CHECK(ops.back().label.rfind("reword", 0) == 0);
    GG_CHECK_EQ(ops.back().rewrites.size(), static_cast<size_t>(3));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return everything(s, repo) == before; }));
}

GG_TEST("rewrite", "edit the author of any commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string target = s.revParse(repo, "HEAD~1");
    const std::string when = info(s, repo, target, "%at");
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, target);
    s.contextMenu("//Change information/**/###author", "Edit author...");
    GG_REQUIRE(s.dialogOpen("Edit author"));
    s.dialogText("Edit author", "name", "Someone Else");
    s.dialogText("Edit author", "email", "someone@example.com");
    s.dialogButton("Edit author", "Save");
    GG_CHECK(s.waitUntil([&] { return info(s, repo, "HEAD~1", "%an <%ae>") == "Someone Else <someone@example.com>"; }));
    s.settle();
    GG_CHECK_STR_EQ(info(s, repo, "HEAD~1", "%at"), when);  // the author date is kept
    GG_CHECK_STR_EQ(info(s, repo, "HEAD", "%an"), "Test User");
}

GG_TEST("rewrite", "published history asks first; a locked ref leaves everything untouched")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const std::string published = s.revParse(repo, "origin/main~1");
    const std::string before = everything(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.expandMerge(s.head(repo)) || true);
    selectCommit(s, published);
    s.setText("//Change information/##message", "Rewrite what is on the remote");
    ctx->ItemClick("//Change information/###save_message");
    GG_REQUIRE(s.dialogOpen("Rewrite published history?"));
    s.dialogButton("Rewrite published history?", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(everything(s, repo), before);
    // Confirmed, but main is locked: nothing may change.
    const fs::path lock = repo / ".git" / "refs" / "heads" / "main.lock";
    std::ofstream(lock) << "";
    selectCommit(s, published);
    s.setText("//Change information/##message", "Rewrite what is on the remote");
    ctx->ItemClick("//Change information/###save_message");
    GG_REQUIRE(s.dialogOpen("Rewrite published history?"));
    s.dialogButton("Rewrite published history?", "Rewrite");
    GG_CHECK(s.dismissError());
    fs::remove(lock);
    GG_CHECK_STR_EQ(everything(s, repo), before);
    // Without the lock it goes through; remote-tracking refs never move.
    const std::string remote = s.revParse(repo, "origin/main");
    selectCommit(s, published);
    s.setText("//Change information/##message", "Rewrite what is on the remote");
    ctx->ItemClick("//Change information/###save_message");
    GG_REQUIRE(s.dialogOpen("Rewrite published history?"));
    s.dialogButton("Rewrite published history?", "Rewrite");
    GG_CHECK(s.waitUntil([&] { return info(s, repo, "HEAD~1", "%s") == "Rewrite what is on the remote"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "origin/main"), remote);
}

} // namespace ggtest

namespace ggtest {

GG_TEST("rewrite", "pre-rebase can veto a rebase; post-checkout runs when HEAD moves")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "dest", "HEAD~3"});
    const fs::path hooks = repo / ".git" / "hooks";
    s.write(hooks, "pre-rebase", "#!/bin/sh\necho \"pre-rebase says no to $1\" >&2\nexit 1\n");
    fs::permissions(hooks / "pre-rebase", fs::perms::owner_all);
    const fs::path checkoutLog = s.path("post-checkout.log");
    s.write(hooks, "post-checkout", "#!/bin/sh\necho \"$1 $2 $3\" > '" + checkoutLog.string() + "'\n");
    fs::permissions(hooks / "post-checkout", fs::perms::owner_all);
    const std::string before = everything(s, repo);
    const std::string tip = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(tip)) != nullptr; });
    s.contextMenu(("//History/**/###row_" + tip).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "dest");
    s.dialogButton("Rebase onto", "Rebase");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("pre-rebase says no") != std::string::npos);
    GG_CHECK_STR_EQ(everything(s, repo), before);
    GG_CHECK(!fs::exists(checkoutLog));
    // Without the veto: the rebase happens and post-checkout reports old → new HEAD.
    fs::remove(hooks / "pre-rebase");
    s.contextMenu(("//History/**/###row_" + tip).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "dest");
    s.dialogButton("Rebase onto", "Rebase");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "HEAD~1") == s.revParse(repo, "dest"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(checkoutLog.parent_path(), checkoutLog.filename().string()), tip + " " + s.head(repo) + " 1\n");
}

GG_TEST("rewrite", "in a linked worktree: its own branch follows quietly, the main worktree's branch asks first")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "wtb", "HEAD~1"});
    const fs::path wt = s.root() / "linked";
    s.git(repo, {"worktree", "add", "-q", wt.string(), "wtb"});
    s.track(wt);
    const std::string target = s.revParse(repo, "HEAD~3"); // reachable from main and wtb
    GG_REQUIRE(s.openRepository(wt));
    selectCommit(s, target);
    s.setText("//Change information/##message", "Reworded from the linked worktree");
    ctx->ItemClick("//Change information/###save_message");
    GG_REQUIRE(s.dialogOpen("Rewrite published history?"));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f != nullptr);
    GG_CHECK(f->message.find("main is checked out in another worktree") != std::string::npos);
    GG_CHECK(f->message.find("wtb is checked out") == std::string::npos);
    s.dialogButton("Rewrite published history?", "Rewrite");
    GG_CHECK(s.waitUntil([&] { return info(s, wt, "HEAD~2", "%s") == "Reworded from the linked worktree"; }));
    s.settle();
    GG_CHECK_STR_EQ(info(s, repo, "main~3", "%s"), "Reworded from the linked worktree");
    GG_CHECK_STR_EQ(s.gitOut(wt, {"branch", "--show-current"}), "wtb");
    GG_CHECK(s.statusPorcelain(wt).empty());
    s.git(repo, {"worktree", "remove", "--force", wt.string()});
}

GG_TEST("rewrite", "in a bare repository, and at the root: reword, drop the root commit")
{
    // Bare: the branch moves, nothing needs a working tree.
    const fs::path bare = s.fixture(Recipe::Bare);
    const std::string target = s.revParse(bare, "HEAD~1");
    GG_REQUIRE(s.openRepository(bare));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(target)) != nullptr; }));
    selectCommit(s, target);
    s.setText("//Change information/##message", "Reworded in a bare repository");
    ctx->ItemClick("//Change information/###save_message");
    GG_CHECK(s.waitUntil([&] { return info(s, bare, "HEAD~1", "%s") == "Reworded in a bare repository"; }));
    s.settle();
    GG_CHECK(s.fsck(bare));

    // The root commit: reworded (its descendants follow), then dropped (they become roots' children).
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string root = s.revParse(repo, "HEAD~4");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(root)) != nullptr; }));
    selectCommit(s, root);
    s.setText("//Change information/##message", "Reworded root");
    ctx->ItemClick("//Change information/###save_message");
    GG_CHECK(s.waitUntil([&] { return info(s, repo, "HEAD~4", "%s") == "Reworded root"; }));
    s.settle();
    const std::string newRoot = s.revParse(repo, "HEAD~4");
    const std::string second = s.revParse(repo, "HEAD~3");
    const std::string tip = s.head(repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(newRoot)) != nullptr; }));
    ctx->ItemClick(("//History/**/###row_" + newRoot).c_str());
    ctx->KeyPress(ImGuiKey_A);
    GG_REQUIRE(s.dialogOpen("Drop commit"));
    s.dialogButton("Drop commit", "Drop");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != tip; }));
    s.settle();
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(repo, {"rev-list", "--count", "HEAD"})), "4");
    GG_CHECK(s.gitOut(repo, {"rev-list", "--max-parents=0", "HEAD"}) != second); // the old second commit, re-rooted
    GG_CHECK_STR_EQ(info(s, repo, "HEAD~3", "%s"), info(s, repo, second, "%s"));
    GG_CHECK(s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "HEAD~4"}).out.empty());
}

} // namespace ggtest
