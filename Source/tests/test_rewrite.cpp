// In-memory rewrites (§4.3, §5 R1/R2, §8.4 rewrite invariants).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Rewrite.hpp>

#include <algorithm>
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

// Main is A-B-C; topic is B-D-E; side is D-S; mid is on D. HEAD ends on main.
struct ForkRepo {
    fs::path path;
    std::string c, d, e, s;
};

ForkRepo makeFork(Scenario& s)
{
    ForkRepo r;
    r.path = s.fixture(Recipe::Linear);
    r.c = s.revParse(r.path, "main");
    s.git(r.path, {"checkout", "-q", "-b", "topic", "main~1"});
    s.commitFile(r.path, "d.txt", "d\n", "D");
    r.d = s.head(r.path);
    s.git(r.path, {"branch", "mid", r.d});
    s.commitFile(r.path, "e.txt", "e\n", "E");
    r.e = s.head(r.path);
    s.git(r.path, {"checkout", "-q", "-b", "side", r.d});
    s.commitFile(r.path, "s.txt", "s\n", "S");
    r.s = s.head(r.path);
    s.git(r.path, {"checkout", "-q", "main"});
    return r;
}

// Starts the action and waits until the worker has finished.
void rebaseTip(Scenario& s, const std::string& tip, const std::string& dest)
{
    s.session()->actions().rebaseTipOnto(tip, dest);
    s.waitUntil([&] { return !s.session()->actions().busy().empty(); }, 1.0f);
    s.waitUntil([&] { return s.session()->actions().busy().empty(); });
    s.settle();
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
    // dest diverges from HEAD~3, so HEAD's 3 own commits have a new base.
    s.git(repo, {"switch", "-q", "-c", "dest", "HEAD~3"});
    s.commitFile(repo, "dest.txt", "d\n", "dest commit");
    s.git(repo, {"switch", "-q", "-"});
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
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "HEAD~3") == s.revParse(repo, "dest"); }));
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

GG_TEST("rewrite", "rangeToMove: dest..tip oldest first on a line, a fork, a merge, a detached tip and empty ranges")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    using Ids = std::vector<std::string>;
    const auto range = [&](const std::string& tip, const std::string& dest) {
        return gg::rewrite::rangeToMove(r.get(), tip, dest);
    };
    const auto at = [&](const std::string& rev) { return s.revParse(repo, rev); };
    // Linear: A-B-C are the last three commits of main.
    const std::string a = at("main~2");
    const std::string b = at("main~1");
    const std::string c = at("main");
    GG_CHECK(range(c, a) == (Ids{b, c}));
    // Fork: topic is B-D-E next to main's B-C.
    s.git(repo, {"checkout", "-q", "-b", "topic", b});
    s.commitFile(repo, "d.txt", "d\n", "D");
    const std::string d = s.head(repo);
    s.commitFile(repo, "e.txt", "e\n", "E");
    const std::string e = s.head(repo);
    GG_CHECK(range(e, c) == (Ids{d, e}));
    GG_CHECK(range(d, c) == (Ids{d}));
    GG_CHECK(range(e, a) == (Ids{b, d, e}));
    // Empty: the destination contains the tip, or is the tip.
    GG_CHECK(range(a, c).empty());
    GG_CHECK(range(d, e).empty());
    GG_CHECK(range(e, e).empty());
    // Merge: M merges the side F (from B) into main; parents come before children.
    s.git(repo, {"checkout", "-q", "-b", "side", b});
    s.commitFile(repo, "f.txt", "f\n", "F");
    const std::string f = s.head(repo);
    s.git(repo, {"checkout", "-q", "main"});
    s.git(repo, {"merge", "-q", "--no-ff", "-m", "M", "side"});
    const std::string m = s.head(repo);
    const Ids viaMerge = range(m, a);
    GG_REQUIRE(viaMerge.size() == 4u);
    GG_CHECK(std::find(viaMerge.begin(), viaMerge.end(), b) == viaMerge.begin());
    GG_CHECK(viaMerge.back() == m);
    GG_CHECK(std::find(viaMerge.begin(), viaMerge.end(), c) != viaMerge.end());
    GG_CHECK(std::find(viaMerge.begin(), viaMerge.end(), f) != viaMerge.end());
    GG_CHECK(range(m, c) == (Ids{f, m})); // the side F is not reachable from C
    // A detached tip with no ref on it.
    s.git(repo, {"checkout", "-q", "--detach", c});
    s.commitFile(repo, "g.txt", "g\n", "G");
    const std::string g = s.head(repo);
    s.commitFile(repo, "h.txt", "h\n", "H");
    const std::string h = s.head(repo);
    GG_CHECK(range(h, e) == (Ids{c, g, h}));
    GG_CHECK(range(h, c) == (Ids{g, h}));
}

GG_TEST("rewrite", "rebase tip onto: the commits from the divergence point move, other branches on them restack")
{
    const ForkRepo r = makeFork(s);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "topic", "main");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c);
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic~2"), r.c);
    GG_CHECK_STR_EQ(info(s, r.path, "topic~1", "%s"), "D");
    GG_CHECK_STR_EQ(info(s, r.path, "topic", "%s"), "E");
    GG_CHECK(s.revParse(r.path, "topic~1") != r.d);
    GG_CHECK_STR_EQ(s.revParse(r.path, "mid"), s.revParse(r.path, "topic~1"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "side~1"), s.revParse(r.path, "topic~1"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "side~2"), r.c);
    GG_CHECK_STR_EQ(info(s, r.path, "side", "%s"), "S");
    GG_CHECK(s.revParse(r.path, "side") != r.s);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rewrite", "rebase tip onto: a tip in the middle of a branch moves, its children restack")
{
    const ForkRepo r = makeFork(s);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, r.d, "main");
    const std::string d2 = s.revParse(r.path, "mid");
    GG_CHECK(d2 != r.d);
    GG_CHECK_STR_EQ(s.revParse(r.path, d2 + "^"), r.c);
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic~1"), d2);
    GG_CHECK_STR_EQ(s.revParse(r.path, "side~1"), d2);
    GG_CHECK_STR_EQ(info(s, r.path, "topic", "%s"), "E");
    GG_CHECK_STR_EQ(info(s, r.path, "side", "%s"), "S");
    GG_CHECK(s.revParse(r.path, "topic") != r.e);
    GG_CHECK(s.revParse(r.path, "side") != r.s);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c);
}

GG_TEST("rewrite", "rebase tip onto: a branch that is not checked out leaves the working tree and HEAD alone")
{
    const ForkRepo r = makeFork(s);
    const auto tracked = gg::splitLines(s.gitOut(r.path, {"ls-files"}));
    GG_REQUIRE(!tracked.empty());
    const std::string file = tracked[0];
    s.write(r.path, file, "unstaged change\n");
    const std::string head = s.head(r.path);
    const std::string status = s.statusPorcelain(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "topic", "main");
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic~2"), r.c);
    GG_CHECK_STR_EQ(s.head(r.path), head);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
    GG_CHECK_STR_EQ(s.read(r.path, file), "unstaged change\n");
    GG_CHECK_STR_EQ(s.statusPorcelain(r.path), status);
}

GG_TEST("rewrite", "rebase tip onto: refuses a branch that is checked out in a different worktree")
{
    const ForkRepo r = makeFork(s);
    const fs::path wt = s.root() / "linked";
    s.git(r.path, {"worktree", "add", "-q", wt.string(), "topic"});
    s.track(wt);
    const std::string before = everything(s, r.path);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "topic", "main");
    GG_CHECK(s.app.errorMessage().find("is checked out in the worktree") != std::string::npos);
    GG_CHECK(everything(s, r.path) == before);
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic"), r.e);
    s.dismissError();
    rebaseTip(s, "refs/heads/topic", "main"); // the full ref name is refused too
    GG_CHECK(s.app.errorMessage().find("refs/heads/topic is checked out in the worktree") != std::string::npos);
    GG_CHECK(everything(s, r.path) == before);
    s.git(r.path, {"worktree", "remove", "--force", wt.string()});
}

GG_TEST("rewrite", "rebase tip onto: the working tree follows when HEAD is on a restacked branch")
{
    const ForkRepo r = makeFork(s);
    s.git(r.path, {"checkout", "-q", "side"});
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "topic", "main");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "side");
    GG_CHECK_STR_EQ(info(s, r.path, "side~1", "%s"), "D");
    GG_CHECK(s.revParse(r.path, "side~1") != r.d);
    GG_CHECK_STR_EQ(s.revParse(r.path, "side~2"), r.c);
    GG_CHECK_STR_EQ(s.read(r.path, "d.txt"), "d\n");
    GG_CHECK_STR_EQ(s.read(r.path, "s.txt"), "s\n");
    const auto fromC = gg::splitLines(s.gitOut(r.path, {"diff", "--name-only", "main~1", "main"}));
    GG_REQUIRE(!fromC.empty());
    GG_CHECK(fs::exists(r.path / fromC[0]));
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("rewrite", "rebase tip onto: refuses a tip that no local branch, HEAD or keep ref reaches")
{
    const ForkRepo r = makeFork(s);
    // A commit only a remote-tracking ref and a tag hold.
    const std::string lone = gg::trim(s.gitOut(r.path, {"commit-tree", "main^{tree}", "-p", "main~1", "-m", "Lone"}));
    s.git(r.path, {"update-ref", "refs/remotes/origin/lone", lone});
    s.git(r.path, {"tag", "lone-tag", lone});
    const std::string before = everything(s, r.path);
    GG_REQUIRE(s.openRepository(r.path));
    for (const char* name : {"origin/lone", "lone-tag", lone.c_str()}) {
        rebaseTip(s, name, "main");
        GG_CHECK(s.app.errorMessage().find(std::string(name) + " is on no local branch") != std::string::npos);
        GG_CHECK(everything(s, r.path) == before);
        s.dismissError();
    }
}

GG_TEST("rewrite", "rebase tip onto: a branch behind the destination fast-forwards")
{
    const ForkRepo r = makeFork(s);
    const std::string refsBefore = s.gitOut(r.path, {"for-each-ref", "--format=%(refname) %(objectname)"});
    const std::string head = s.head(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "mid", "topic");
    GG_CHECK_STR_EQ(s.revParse(r.path, "mid"), r.e);
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic"), r.e);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c);
    GG_CHECK_STR_EQ(s.head(r.path), head);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
    GG_CHECK(s.statusPorcelain(r.path).empty());
    // No other ref changed: only mid differs from before.
    std::string expected = refsBefore;
    const std::string was = "refs/heads/mid " + r.d;
    const size_t at = expected.find(was);
    GG_REQUIRE(at != std::string::npos);
    expected.replace(at, was.size(), "refs/heads/mid " + r.e);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref", "--format=%(refname) %(objectname)"}), expected);
}

GG_TEST("rewrite", "rebase tip onto: HEAD behind the destination fast-forwards with the working tree")
{
    const ForkRepo r = makeFork(s);
    s.git(r.path, {"checkout", "-q", "mid"});
    const std::string before = everything(s, r.path);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "HEAD", "topic");
    GG_CHECK_STR_EQ(s.revParse(r.path, "mid"), r.e);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "mid");
    GG_CHECK(fs::exists(r.path / "e.txt"));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return everything(s, r.path) == before; }));
    GG_CHECK_STR_EQ(s.revParse(r.path, "mid"), r.d);
    GG_CHECK(!fs::exists(r.path / "e.txt"));
}

GG_TEST("rewrite", "rebase tip onto: a detached HEAD behind the destination fast-forwards")
{
    const ForkRepo r = makeFork(s);
    s.git(r.path, {"checkout", "-q", "--detach", r.d});
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "HEAD", "topic");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "");
    GG_CHECK_STR_EQ(s.head(r.path), r.e);
    GG_CHECK(fs::exists(r.path / "e.txt"));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK_STR_EQ(s.revParse(r.path, "mid"), r.d);
    GG_CHECK_STR_EQ(s.revParse(r.path, "topic"), r.e);
}

GG_TEST("rewrite", "rebase tip onto: a tip on the destination, and a commit id behind it, are refused")
{
    const ForkRepo r = makeFork(s);
    const std::string before = everything(s, r.path);
    GG_REQUIRE(s.openRepository(r.path));
    rebaseTip(s, "mid", "mid");
    GG_CHECK(s.app.errorMessage().find("mid is already on mid") != std::string::npos);
    GG_CHECK(everything(s, r.path) == before);
    s.dismissError();
    rebaseTip(s, r.d, "topic");
    GG_CHECK(s.app.errorMessage().find("topic already contains " + r.d) != std::string::npos);
    GG_CHECK(everything(s, r.path) == before);
}

} // namespace ggtest
