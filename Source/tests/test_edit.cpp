// History editing actions on the in-memory rewrite engine (§4.3).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

// c1 adds a.txt, c2 adds b.txt, c3 changes a.txt, c4 adds c.txt and d.txt (main = c4);
// "side" branches off c2 with s1 adding s.txt.
struct EditRepo {
    fs::path path;
    std::string c1, c2, c3, c4, s1;
};

EditRepo makeRepo(Scenario& s)
{
    EditRepo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    s.commitFile(p, "a.txt", "one\ntwo\nthree\n", "c1 add a");
    r.c1 = s.head(p);
    s.commitFile(p, "b.txt", "b\n", "c2 add b");
    r.c2 = s.head(p);
    s.git(p, {"branch", "side"});
    s.commitFile(p, "a.txt", "one\nTWO\nthree\n", "c3 change a");
    r.c3 = s.head(p);
    s.write(p, "c.txt", "c\n");
    s.write(p, "d.txt", "d\n");
    s.git(p, {"add", "c.txt", "d.txt"});
    s.git(p, {"commit", "-q", "-m", "c4 add c and d"});
    r.c4 = s.head(p);
    s.git(p, {"switch", "-q", "side"});
    s.commitFile(p, "s.txt", "s\n", "s1 add s");
    r.s1 = s.head(p);
    s.git(p, {"switch", "-q", "main"});
    return r;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

std::vector<std::string> subjects(Scenario& s, const fs::path& repo, const std::string& rev = "HEAD")
{
    std::vector<std::string> out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"log", "--format=%s", rev})))
        if (!l.empty())
            out.push_back(l);
    return out;
}

bool rowReady(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
}

// Waits for the history to change, then for ggui to be idle.
bool changed(Scenario& s, const fs::path& repo, const std::string& before, const char* rev = "HEAD")
{
    const bool ok = s.waitUntil([&] { return s.revParse(repo, rev) != before; });
    s.settle();
    return ok;
}

} // namespace

GG_TEST("edit", "duplicate a commit (D) and a branch (Shift+D) as detached copies", "ACT-DUPLICATE-COMMIT",
    "ACT-DUPLICATE-BRANCH", "HIST-KEY-D", "HIST-KEY-SHIFT-D")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiKey_D);
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK(s.head(r.path) != r.c3);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), r.c2);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), s.revParse(r.path, r.c3 + "^{tree}"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c4); // branches stay
    const std::string copy = s.head(r.path);
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_D);
    GG_CHECK(changed(s, r.path, copy));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), s.revParse(r.path, r.c4 + "^{tree}"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.c2);
    GG_CHECK(s.revParse(r.path, "HEAD~1") != r.c3);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c4);
}

GG_TEST("edit", "rebase one commit, and a commit with its descendants, onto another branch", "ACT-REBASE-COMMIT",
    "ACT-REBASE-BRANCH", "HIST-PUBLISHED-WARN")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    // c3 and its descendant c4 onto side.
    s.contextMenu(rowRef(r.c3).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "side");
    s.dialogButton("Rebase onto", "Rebase");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~2"), r.s1);
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c4 add c and d", "c3 change a", "s1 add s", "c2 add b", "c1 add a"}));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK_STR_EQ(s.read(r.path, "s.txt"), "s\n"); // the working tree followed
    // Only the tip commit (c4') onto c1: main follows it, c3 drops out of main.
    const std::string tip = s.head(r.path);
    GG_REQUIRE(rowReady(s, tip));
    s.contextMenu(rowRef(tip).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", r.c1);
    s.dialogCheck("Rebase onto", "with_descendants", "With its descendants");
    s.dialogButton("Rebase onto", "Rebase");
    GG_CHECK(changed(s, r.path, tip));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c4 add c and d", "c1 add a"}));
}

GG_TEST("edit", "squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)",
    "ACT-SQUASH-PARENT", "ACT-SQUASH-TARGET", "ACT-SQUASH-DESCENDANTS", "HIST-KEY-S", "HIST-KEY-SHIFT-S")
{
    const EditRepo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "main^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    ctx->ItemClick(rowRef(r.c4).c_str());
    ctx->KeyPress(ImGuiKey_S);
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogButton("Squash", "Squash");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
    GG_CHECK(subjects(s, r.path).size() == 3);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%B"}), "c3 change a\n\nc4 add c and d");
    // The tip into c1 (an ancestor), keeping c1's message (fixup).
    const std::string tip = s.head(r.path);
    GG_REQUIRE(rowReady(s, tip));
    s.contextMenu(rowRef(tip).c_str(), "Squash...");
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogText("Squash", "target", r.c1);
    s.dialogCheck("Squash", "combine", "Combine the messages (squash; otherwise keep the target's: fixup)");
    s.dialogButton("Squash", "Squash");
    GG_CHECK(changed(s, r.path, tip));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "HEAD~1:a.txt"}), "one\nTWO\nthree");
    // Everything after c1' into it.
    const std::string root = s.revParse(r.path, "HEAD~1");
    GG_REQUIRE(rowReady(s, root));
    ctx->ItemClick(rowRef(root).c_str());
    const std::string beforeAll = s.head(r.path);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_S);
    GG_CHECK(changed(s, r.path, beforeAll));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c1 add a"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
}

GG_TEST("edit", "split a commit by files (Alt+S)", "ACT-SPLIT", "HIST-KEY-ALT-S")
{
    const EditRepo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "main^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    ctx->ItemClick(rowRef(r.c4).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_S);
    GG_REQUIRE(s.dialogOpen("Split"));
    s.dialogCheck("Split", "file_0", "c.txt");
    s.dialogText("Split", "message", "c4a add c");
    s.dialogButton("Split", "Split");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c4 add c and d", "c4a add c", "c3 change a", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "--name-only", "--format=", "HEAD~1"}), "c.txt");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "--name-only", "--format=", "HEAD"}), "d.txt");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
}

GG_TEST("edit", "abandon a commit (A) and a branch (Shift+A)", "ACT-ABANDON", "ACT-ABANDON-BRANCH", "ACT-ABANDON-REMOTE",
    "HIST-KEY-A", "HIST-KEY-SHIFT-A")
{
    const EditRepo r = makeRepo(s);
    // side has an upstream on a bare remote.
    const fs::path bare = s.path("abandon-remote.git");
    s.git(s.root(), {"init", "-q", "--bare", bare.string()});
    s.track(bare);
    s.git(r.path, {"remote", "add", "origin", "file://" + bare.generic_string()});
    s.git(r.path, {"push", "-q", "-u", "origin", "side"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiKey_A);
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c4 add c and d", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "one\ntwo\nthree\n");
    GG_CHECK(s.statusPorcelain(r.path).empty());
    // The side branch: dropped, deleted locally and on its remote.
    GG_REQUIRE(rowReady(s, r.s1));
    ctx->ItemClick(rowRef(r.s1).c_str());
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_A);
    GG_REQUIRE(s.dialogOpen("Abandon branch"));
    s.dialogCheck("Abandon branch", "delete_remote", "Also delete them on their remote");
    s.dialogButton("Abandon branch", "Abandon");
    // side is on its remote: rewriting it asks first.
    GG_REQUIRE(s.dialogOpen("Rewrite published history?"));
    s.dialogButton("Rewrite published history?", "Rewrite");
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(r.path, {"rev-parse", "--verify", "-q", "refs/heads/side"}).ok(); }));
    GG_CHECK(s.waitUntil([&] { return s.gitOut(bare, {"branch", "--list", "side"}).empty(); }));
    s.settle();
}

GG_TEST("edit", "restore paths in a commit or the working tree; simplify parents", "ACT-RESTORE-COMMIT",
    "ACT-RESTORE-WORKTREE", "ACT-SIMPLIFY-PARENTS")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    // In c4, restore c.txt from c2 (where it does not exist): c4 no longer adds it.
    ctx->ItemClick(rowRef(r.c4).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    ctx->ItemClick((s.child("//Changes", "##files") + "/c.txt/###file_c.txt").c_str());
    s.contextMenu(rowRef(r.c4).c_str(), "Restore from...");
    GG_REQUIRE(s.dialogOpen("Restore"));
    s.dialogText("Restore", "from", r.c2);
    s.dialogButton("Restore", "Restore");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "--name-only", "--format=", "HEAD"}), "d.txt");
    GG_CHECK(!fs::exists(r.path / "c.txt"));
    // The working tree: a.txt as in c1 (staged too).
    const std::string tip = s.head(r.path);
    GG_REQUIRE(rowReady(s, tip));
    ctx->ItemClick(rowRef(r.c3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 1; }));
    ctx->ItemClick((s.child("//Changes", "##files") + "/a.txt/###file_a.txt").c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/Restore from...");
    GG_REQUIRE(s.dialogOpen("Restore"));
    s.dialogText("Restore", "from", r.c1);
    s.comboSelect("//Restore/Restore into##where", "The working tree (git restore)");
    s.dialogButton("Restore", "Restore");
    GG_CHECK(s.waitUntil([&] { return s.read(r.path, "a.txt") == "one\ntwo\nthree\n"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "a.txt");
    GG_CHECK_STR_EQ(s.head(r.path), tip);
    s.git(r.path, {"reset", "-q", "--hard"});
    // A merge whose second parent is already an ancestor of the first.
    const auto merge = s.gitgg(r.path, {"new", "-m", "Redundant merge", "HEAD", r.c2});
    GG_REQUIRE(merge.ok());
    const std::string m = gg::trim(merge.out);
    GG_REQUIRE(rowReady(s, m));
    s.contextMenu(rowRef(m).c_str(), "Simplify parents");
    GG_CHECK(changed(s, r.path, m));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%P"}), tip);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s"}), "Redundant merge");
}

GG_TEST("edit", "insert a new commit before or after one", "ACT-NEW-INSERT-BEFORE", "ACT-NEW-INSERT-AFTER")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c2));
    s.contextMenu(rowRef(r.c2).c_str(), "New commit after");
    GG_CHECK(changed(s, r.path, r.c4));
    auto count = [&] { return std::stoi(s.gitOut(r.path, {"rev-list", "--count", "HEAD"})); };
    GG_CHECK_EQ(count(), 5);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2^"), r.c2);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2^{tree}"), s.revParse(r.path, r.c2 + "^{tree}"));
    // Every child of c2 now hangs on the new commit, side's too (like jj new --after).
    GG_CHECK_STR_EQ(s.revParse(r.path, "side^"), s.revParse(r.path, "HEAD~2"));
    const std::string tip = s.head(r.path);
    GG_REQUIRE(rowReady(s, r.c1));
    s.contextMenu(rowRef(r.c1).c_str(), "New commit before");
    GG_CHECK(changed(s, r.path, tip));
    GG_CHECK_EQ(count(), 6);
    GG_CHECK(s.gitMayFail(r.path, {"rev-parse", "--verify", "-q", "HEAD~5^"}).out.empty()); // the new root
    // After the tip: the branch advances onto it.
    const std::string newTip = s.head(r.path);
    GG_REQUIRE(rowReady(s, newTip));
    s.contextMenu(rowRef(newTip).c_str(), "New commit after");
    GG_CHECK(changed(s, r.path, newTip));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), newTip);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
}

GG_TEST("edit", "merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile", "ACT-MERGE-INTO-HEAD",
    "ACT-MERGE-NATIVE", "BR-MERGE-INTO-HEAD", "ACT-REBASE-HEAD-ONTO", "BR-REBASE-HEAD-ONTO", "ACT-RECONCILE",
    "BR-RECONCILE")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_side/###branch_side"); }));
    s.contextMenu("//Branches/branch_side/###branch_side", "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^1"), r.c4);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^2"), r.s1);
    GG_CHECK_STR_EQ(s.read(r.path, "s.txt"), "s\n");
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s"}), "Merge branch 'side'");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == r.c4; }));
    s.settle();
    // Natively.
    s.contextMenu("//Branches/branch_side/###branch_side", "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogCheck("Merge into HEAD", "native", "Use native git merge (stops with index conflicts)");
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(r.path, {"rev-parse", "-q", "--verify", "HEAD^2"}).ok(); }));
    s.settle();
    s.git(r.path, {"reset", "-q", "--hard", r.c4});
    // Rebase HEAD (main's own commits c3, c4) onto side.
    s.contextMenu("//Branches/branch_side/###branch_side", "Rebase HEAD onto branch");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c4 add c and d", "c3 change a", "s1 add s", "c2 add b", "c1 add a"}));
    // Reconcile: main diverged from its "upstream" side again; merge it in.
    s.git(r.path, {"switch", "-q", "side"});
    s.commitFile(r.path, "t.txt", "t\n", "s2 add t");
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_main/###branch_main"); }));
    s.contextMenu("//Branches/branch_main/###branch_main", "Reconcile with remote or branch...");
    GG_REQUIRE(s.dialogOpen("Reconcile"));
    s.dialogText("Reconcile", "with", "side");
    s.comboSelect("//Reconcile/How##how", "Merge it in");
    const std::string before = s.head(r.path);
    s.dialogButton("Reconcile", "Reconcile");
    GG_CHECK(changed(s, r.path, before));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^2"), s.revParse(r.path, "side"));
}

GG_TEST("edit", "History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit", "ACT-MERGE-INTO-HEAD",
    "ACT-REBASE-HEAD-ONTO", "MENU-COMMIT-ACTIONS")
{
    const EditRepo r = makeRepo(s);
    // s1 is not a branch tip any more: it can only be picked as a commit.
    s.git(r.path, {"switch", "-q", "side"});
    s.commitFile(r.path, "t.txt", "t\n", "s2 add t");
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.s1));
    const std::string shortS1 = s.session()->shortId(ggui::core::Oid::fromHex(r.s1));
    s.contextMenu(rowRef(r.s1).c_str(), "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    GG_CHECK(s.textShown("//Merge into HEAD", "Merge commit " + shortS1 + " into HEAD."));
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^1"), r.c4);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^2"), r.s1);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s"}), "Merge commit '" + shortS1 + "'");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
    GG_CHECK(!fs::exists(r.path / "t.txt") && fs::exists(r.path / "s.txt"));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == r.c4; }));
    s.settle();
    // Not offered on HEAD itself.
    GG_REQUIRE(rowReady(s, r.c4));
    ctx->ItemClick(rowRef(r.c4).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Merge into HEAD...").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Rebase HEAD onto this").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->KeyPress(ImGuiKey_Escape);
    // The Commit menu: main's own commits (c3, c4) onto s1.
    ctx->ItemClick(rowRef(r.s1).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/Rebase HEAD onto this");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c4 add c and d", "c3 change a", "s1 add s", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.s1);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"branch", "--show-current"}), "main");
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

namespace {

// Drags commit `from` onto commit `to` in History with `mods` held (ui-spec §2.x: Ctrl+Shift = Move
// before, Shift = Move after); without modifiers the chooser's `choice` is clicked.
void dragCommit(Scenario& s, const std::string& from, const std::string& to, ImGuiKeyChord mods, const char* choice = nullptr)
{
    s.waitUntil([&] { return s.itemExists(rowRef(from).c_str()) && s.itemExists(rowRef(to).c_str()); });
    if (mods)
        s.ctx->KeyDown(mods);
    s.ctx->ItemDragAndDrop(rowRef(from).c_str(), rowRef(to).c_str());
    if (mods)
        s.ctx->KeyUp(mods);
    s.ctx->Yield(2);
    if (choice) {
        const std::string item = std::string("//$FOCUSED/") + choice;
        s.waitUntil([&] { return s.itemExists(item.c_str()); });
        s.ctx->ItemClick(item.c_str());
    }
}

} // namespace

GG_TEST("edit", "reorder: move a commit before another, and copy one", "ACT-REORDER", "ACT-REORDER-COPY")
{
    const EditRepo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "main^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    // Dragged in History: c4 before c3 (Ctrl+Shift).
    dragCommit(s, r.c4, r.c3, ImGuiMod_Ctrl | ImGuiMod_Shift);
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3 change a", "c4 add c and d", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), tree);
    const std::string tip = s.head(r.path);
    dragCommit(s, r.c2, tip, 0, "Copy after"); // the chooser
    GG_CHECK(changed(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c2 add b", "c3 change a", "c4 add c and d", "c2 add b", "c1 add a"}));
}

GG_TEST("edit", "text conflicts become first-class and never stop a rewrite; a later rewrite resolves them",
    "ACT-REWRITE-TEXT-CONFLICT", "ACT-REWRITE-COMPLETION-MSG", "ACT-REWRITE-AUTO-RESOLVE", "CONF-AUTO-RESOLVE",
    "ACT-REWRITE-INVARIANTS", "CONF-NO-NESTING")
{
    const EditRepo r = makeRepo(s);
    // c5 changes the line c3 changed: without c3, c5 conflicts.
    s.commitFile(r.path, "a.txt", "one\nTWO!\nthree\n", "c5 change a again");
    const std::string c5 = s.head(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiKey_A);
    GG_CHECK(changed(s, r.path, c5));
    GG_CHECK(s.waitUntil([&] { return !s.app.toasts().empty(); }));
    GG_CHECK(s.app.toasts().back().message.find("now have first-class conflicts") != std::string::npos);
    const std::string conflicted = s.gitOut(r.path, {"show", "HEAD:a.txt"});
    GG_CHECK(conflicted.find("<<<<<<<") != std::string::npos && conflicted.find("|||||||") != std::string::npos);
    GG_CHECK(s.read(r.path, "a.txt").find("<<<<<<<") != std::string::npos);
    GG_CHECK(s.statusPorcelain(r.path).empty()); // plain git status stays clean
    // Undo is exact.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == c5; }));
    s.settle();
    // Moving c5 before c3 conflicts; moving it back resolves (no nested markers).
    dragCommit(s, c5, r.c3, ImGuiMod_Ctrl | ImGuiMod_Shift);
    GG_CHECK(changed(s, r.path, c5));
    const std::string moved = s.head(r.path);
    // The moved c5 conflicts (c3 is not under it any more); at the tip the terms cancel out.
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD~2:a.txt"}).find("<<<<<<<") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "HEAD:a.txt"}), "one\nTWO!\nthree");
    const std::string c5moved = s.revParse(r.path, "HEAD~2");
    dragCommit(s, c5moved, s.head(r.path), ImGuiMod_Shift);
    GG_CHECK(changed(s, r.path, moved));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "HEAD:a.txt"}), "one\nTWO!\nthree");
    GG_CHECK(!s.gitMayFail(r.path, {"grep", "-q", "<<<<<<<", "HEAD"}).ok()); // no markers anywhere
}

GG_TEST("edit", "no-op rewrites keep ids; the Commit menu carries the selected commit's actions", "MENU-COMMIT-ACTIONS")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/New commit after");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.c3); // everything up to c3 kept its id
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~3"), r.c2);
}

GG_TEST("edit", "refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged",
    "ACT-SQUASH-DESCENDANTS", "ACT-SQUASH-TARGET", "ACT-REBASE-COMMIT", "ACT-SIMPLIFY-PARENTS", "ACT-MOVE-CHANGES-PARENT",
    "ACT-MOVE-CHANGES-CHILD", "ACT-MERGE-INTO-HEAD", "ACT-REBASE-HEAD-ONTO")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    const std::string before = s.gitOut(r.path, {"for-each-ref"});
    // Each attempt is refused with `why` and changes nothing.
    auto refused = [&](const char* why) {
        GG_CHECK(s.dismissError());
        if (s.app.errorMessage().find(why) == std::string::npos)
            ctx->LogError("expected \"%s\", got \"%s\"", why, s.app.errorMessage().c_str());
        GG_CHECK(s.app.errorMessage().find(why) != std::string::npos);
        GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), before);
    };
    auto fileMenu = [&](const std::string& commit, const std::string& path, size_t files, const char* item) {
        GG_REQUIRE(rowReady(s, commit));
        ctx->ItemClick(rowRef(commit).c_str());
        GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == files; }));
        s.contextMenu((s.child("//Changes", "##files") + "/" + path + "/###file_" + path).c_str(), item);
    };
    // HEAD (c4) has no child to move changes to, nor descendants to squash.
    fileMenu(r.c4, "c.txt", 2, "Move to child");
    refused("the commit has no child on its line");
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_S);
    refused("the commit has no descendants");
    // The root commit has no parent to take changes.
    fileMenu(r.c1, "a.txt", 1, "Move to parent");
    refused("moving changes needs a commit with exactly one parent");
    // Squash into a commit that is not an ancestor.
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiKey_S);
    GG_REQUIRE(s.dialogOpen("Squash"));
    s.dialogText("Squash", "target", r.c4);
    s.dialogButton("Squash", "Squash");
    refused("the target must be an ancestor of the commit");
    // Rebase onto an unknown revision, or onto the commit's own descendant.
    s.contextMenu(rowRef(r.c2).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "no-such-branch");
    s.dialogButton("Rebase onto", "Rebase");
    refused("unknown revision 'no-such-branch'");
    s.contextMenu(rowRef(r.c2).c_str(), "Rebase onto...");
    GG_REQUIRE(s.dialogOpen("Rebase onto"));
    s.dialogText("Rebase onto", "destination", "main");
    s.dialogButton("Rebase onto", "Rebase");
    refused("cannot rebase onto the commit's own descendant");
    // HEAD already contains c2: merging it or rebasing HEAD onto it changes nothing.
    s.contextMenu(rowRef(r.c2).c_str(), "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogButton("Merge into HEAD", "Merge");
    refused("is already merged");
    s.contextMenu(rowRef(r.c2).c_str(), "Rebase HEAD onto this");
    GG_CHECK(s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.message == "Nothing to change.")
                return true;
        return false;
    }));
    // A commit ahead of HEAD: HEAD is already on its line.
    const std::string a1 = s.gitOut(r.path, {"commit-tree", "HEAD^{tree}", "-p", "HEAD", "-m", "Ahead"});
    s.git(r.path, {"branch", "ahead", a1});
    GG_REQUIRE(rowReady(s, a1));
    const std::string withAhead = s.gitOut(r.path, {"for-each-ref"});
    s.contextMenu(rowRef(a1).c_str(), "Rebase HEAD onto this");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("HEAD is already on") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), withAhead);

    // A real merge (plain git): its parents are not redundant; the line below it is not single.
    s.git(r.path, {"merge", "-q", "--no-ff", "-m", "Merge side", "side"});
    const std::string m = s.head(r.path);
    GG_REQUIRE(rowReady(s, m));
    const std::string withMerge = s.gitOut(r.path, {"for-each-ref"});
    s.contextMenu(rowRef(m).c_str(), "Simplify parents");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("no parent is redundant") != std::string::npos);
    ctx->ItemClick(rowRef(r.c3).c_str());
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_S);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("not a single line") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), withMerge);
}

GG_TEST("edit", "dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults",
    "ACT-SPLIT", "ACT-RESTORE-COMMIT", "TAG-CREATE", "REM-ADD", "BR-SET-UPSTREAM")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string head = s.head(repo);
    GG_REQUIRE(rowReady(s, head));
    auto toast = [&](const char* title) {
        return s.waitUntil([&] {
            for (const auto& t : s.app.toasts())
                if (t.title == title)
                    return true;
            return false;
        });
    };
    // Split needs two files; HEAD has one.
    ctx->ItemClick(rowRef(head).c_str());
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_S);
    GG_CHECK(toast("Split"));
    // Restore without files selected in Changes: the dialog says so and cannot run.
    ctx->ItemClick(rowRef(head).c_str());
    s.contextMenu(rowRef(head).c_str(), "Restore from...");
    GG_REQUIRE(s.dialogOpen("Restore"));
    GG_CHECK(s.app.dialogs().current()->message == "Select files in Changes first.");
    s.dialogText("Restore", "from", "HEAD~1");
    GG_CHECK((ctx->ItemInfo("//Restore/Restore").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    s.dialogButton("Restore", "Cancel");
    // No remotes: Push explains where to add one; Set upstream has nothing to offer.
    ctx->MenuClick("//##MainMenuBar/Repository/Push");
    GG_CHECK(toast("Push"));
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_main/###branch_main", "Set upstream...");
    GG_CHECK(toast("Set upstream"));
    // An annotated tag without a message takes its name as the message.
    s.showPanel("Tags");
    ctx->ItemClick("//Tags/###create_tag");
    GG_REQUIRE(s.dialogOpen("Create tag"));
    s.dialogText("Create tag", "name", "v-annotated");
    s.dialogCheck("Create tag", "annotated", "Annotated (with a message)");
    s.dialogButton("Create tag", "Create");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"cat-file", "-t", "v-annotated"}).out == "tag\n"; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"tag", "-l", "--format=%(contents:subject)", "v-annotated"}), "v-annotated");
    // The first remote is suggested as "origin".
    s.showPanel("Remotes");
    ctx->ItemClick("//Remotes/###add_remote");
    GG_REQUIRE(s.dialogOpen("Add remote"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->text("name"), "origin");
    s.dialogButton("Add remote", "Cancel");
}

GG_TEST("edit", "more refusals and edges: reorder across branches, reorder on a detached HEAD, fold onto another branch or a deletion, a native merge git refuses",
    "ACT-REORDER", "HIST-DND-FILES", "ACT-MERGE-NATIVE")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    auto drag = [&](const std::string& from, const std::string& to, ImGuiKeyChord mods) {
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(from.c_str()) && s.itemExists(to.c_str()); }));
        ctx->KeyDown(mods);
        ctx->ItemDragAndDrop(from.c_str(), to.c_str());
        ctx->KeyUp(mods);
        ctx->Yield(2);
    };
    // s1 (on side) cannot move next to c4 (on main): different lines of history.
    const std::string refs = s.gitOut(r.path, {"for-each-ref"});
    drag(rowRef(r.s1), rowRef(r.c4), ImGuiMod_Shift);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("same line of history") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), refs);
    // A working tree file cannot be folded into a commit that is not HEAD or before it.
    s.write(r.path, "b.txt", "b changed\n");
    ctx->ItemClick("//History/**/###row_wt");
    const std::string bRow = s.child("//Changes", "##files") + "/Unstaged/b.txt/###file_b.txt";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(bRow.c_str()); }));
    drag(bRow, rowRef(r.s1), 0);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("the checked-out one or one of its ancestors") != std::string::npos);
    s.git(r.path, {"checkout", "--", "b.txt"});
    // A deleted file folded into c2: c2 no longer adds it.
    fs::remove(r.path / "b.txt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(bRow.c_str()); }));
    const std::string tip = s.head(r.path);
    drag(bRow, rowRef(r.c2), 0);
    GG_CHECK(changed(s, r.path, tip));
    GG_CHECK(!s.gitMayFail(r.path, {"cat-file", "-e", "HEAD:b.txt"}).ok());
    GG_CHECK(s.statusPorcelain(r.path).empty());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == tip; }));
    s.settle();
    s.git(r.path, {"checkout", "--", "b.txt"});
    // A native merge git refuses (a local change in the way): an error, no merge in progress.
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_side/###branch_side"); }));
    s.git(r.path, {"switch", "-q", "side"});
    s.commitFile(r.path, "a.txt", "side's a\n", "s2 change a");
    s.git(r.path, {"switch", "-q", "main"});
    s.write(r.path, "a.txt", "local edit\n");
    // The menu item is disabled while the app still has side checked out.
    GG_REQUIRE(s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return snap && snap->headBranch == "main" && snap->head.hex() == s.head(r.path);
    }));
    s.settle();
    s.contextMenu("//Branches/branch_side/###branch_side", "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogCheck("Merge into HEAD", "native", "Use native git merge (stops with index conflicts)");
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(s.dismissError());
    GG_CHECK(!fs::exists(r.path / ".git" / "MERGE_HEAD"));
    s.git(r.path, {"checkout", "--", "a.txt"});
    s.settle();
    // On a detached HEAD a reorder moves HEAD along.
    s.git(r.path, {"switch", "-q", "--detach", "main"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headDetached; }));
    const std::string detached = s.head(r.path);
    drag(rowRef(r.c4), rowRef(r.c3), ImGuiMod_Ctrl | ImGuiMod_Shift);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) != detached; }));
    s.settle();
    GG_CHECK(subjects(s, r.path, "HEAD") == (std::vector<std::string>{"c3 change a", "c4 add c and d", "c2 add b", "c1 add a"}));
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), s.head(r.path)); // the branch at the old tip follows
}

GG_TEST("edit", "by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch",
    "ACT-DUPLICATE-COMMIT", "ACT-DUPLICATE-BRANCH", "ACT-SQUASH-DESCENDANTS", "ACT-SPLIT", "ACT-ABANDON", "ACT-ABANDON-BRANCH",
    "TAG-CREATE", "ACT-NEW-DETACHED", "INFO-CONFLICTED-FILES", "BLAME-SELECT-BLOCK", "CONF-NATIVE-TAKE-SIDE",
    "STASH-APPLY", "REFLOG-BRANCH")
{
    const EditRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    const std::string c4Tree = s.revParse(r.path, r.c4 + "^{tree}");
    // Duplicate: a detached copy of c4 on its parent; the branches stay.
    s.contextMenu(rowRef(r.c4).c_str(), "Duplicate");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), r.c3);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), c4Tree);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c4);
    // Duplicate branch: c3 and c4 copied onto c2.
    const std::string copy = s.head(r.path);
    s.contextMenu(rowRef(r.c3).c_str(), "Duplicate branch");
    GG_CHECK(changed(s, r.path, copy));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.c2);
    GG_CHECK(s.revParse(r.path, "HEAD~1") != r.c3);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), c4Tree);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c4);
    const std::string branchCopy = s.head(r.path);
    // Squash descendants into this: c4 folded into c3 on main.
    s.contextMenu(rowRef(r.c3).c_str(), "Squash descendants into this");
    GG_CHECK(changed(s, r.path, r.c4, "main"));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c3 change a", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), c4Tree);
    // Split by mouse: a.txt goes into a new first commit.
    const std::string squashed = s.revParse(r.path, "main");
    GG_REQUIRE(rowReady(s, squashed));
    ctx->ItemClick(rowRef(squashed).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 3; }));
    s.contextMenu(rowRef(squashed).c_str(), "Split...");
    GG_REQUIRE(s.dialogOpen("Split"));
    for (const auto& field : s.app.dialogs().current()->fields)
        if (field.kind == ggui::Field::Check && field.label == "a.txt")
            s.dialogCheck("Split", field.id.c_str(), "a.txt");
    s.dialogText("Split", "message", "c3a first part");
    s.dialogButton("Split", "Split");
    GG_CHECK(changed(s, r.path, squashed, "main"));
    GG_CHECK(subjects(s, r.path, "main") == (std::vector<std::string>{"c3 change a", "c3a first part", "c2 add b", "c1 add a"}));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--name-only", "main~2", "main~1"}), "a.txt");
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^{tree}"), c4Tree);
    // Abandon branch: s1 dropped; side (which only pointed into it) is kept at its parent.
    s.contextMenu(rowRef(r.s1).c_str(), "Abandon branch...");
    GG_REQUIRE(s.dialogOpen("Abandon branch"));
    s.dialogCheck("Abandon branch", "delete_branches", "Delete the branches that only point into it", false);
    s.dialogButton("Abandon branch", "Abandon");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c2; }));
    s.settle();
    // Abandon the detached copy's tip: HEAD moves to its parent.
    GG_REQUIRE(rowReady(s, branchCopy));
    const std::string copyParent = s.revParse(r.path, branchCopy + "^");
    s.contextMenu(rowRef(branchCopy).c_str(), "Abandon");
    GG_CHECK(changed(s, r.path, branchCopy));
    GG_CHECK_STR_EQ(s.head(r.path), copyParent);
    // Create tag... on a commit other than HEAD tags that commit.
    GG_REQUIRE(rowReady(s, r.c1));
    s.contextMenu(rowRef(r.c1).c_str(), "Create tag...");
    GG_REQUIRE(s.dialogOpen("Create tag"));
    s.dialogText("Create tag", "name", "v-row");
    s.dialogButton("Create tag", "Create");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(r.path, {"rev-parse", "-q", "--verify", "refs/tags/v-row"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "v-row"), r.c1);
    const std::string beforeDetached = s.head(r.path);
    ctx->MenuClick("//##MainMenuBar/Commit/New detached commit");
    GG_CHECK(changed(s, r.path, beforeDetached));
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), r.c1); // on the selected commit
    // Reflog: a branch from an entry's new commit.
    s.showPanel("Reflog");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Reflog/##reflog_table/r0/###reflog_0"); }));
    const std::string newest = s.revParse(r.path, "HEAD@{0}");
    s.contextMenu("//Reflog/##reflog_table/r0/###reflog_0", "Create branch from new...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "from-reflog");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(r.path, {"rev-parse", "-q", "--verify", "refs/heads/from-reflog"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "from-reflog"), newest);

    // A conflicted commit: its file in Change information opens the blame; lines select by click.
    const fs::path conflicted = s.fixture(Recipe::Conflicted2);
    const std::string cc = s.revParse(conflicted, "HEAD~1");
    GG_REQUIRE(s.openRepository(conflicted));
    GG_REQUIRE(rowReady(s, cc));
    ctx->ItemClick(rowRef(cc).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/**/###conflict_0"); }));
    ctx->ItemClick("//Change information/**/###conflict_0");
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] {
        const auto& b = s.session()->blame().blame();
        return b && b->query.path == "conflict.txt" && b->lines.size() > 3;
    }));
    ctx->Yield(3);
    auto clickLine = [&](int n, ImGuiKeyChord mods) {
        const ImGuiTestItemInfo row = ctx->ItemInfo(("//Blame/##blame_table/l" + std::to_string(n) + "/###blame_line_" + std::to_string(n)).c_str());
        ctx->MouseMoveToPos(ImVec2(row.RectFull.Min.x + 10.0f, row.RectFull.GetCenter().y));
        if (mods)
            ctx->KeyDown(mods);
        ctx->MouseClick(ImGuiMouseButton_Left);
        if (mods)
            ctx->KeyUp(mods);
        ctx->Yield(2);
    };
    clickLine(1, 0);
    clickLine(3, ImGuiMod_Shift);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 0);
    GG_CHECK_EQ(s.session()->blame().selectionLast(), 2);

    // A native conflict resolved with their side.
    const fs::path merge = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(merge));
    const std::string f = s.child("//Changes", "##files") + "/Conflicted/f.txt/###file_f.txt";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(f.c_str()); }));
    s.contextMenu(f.c_str(), "Take theirs");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(merge, {"ls-files", "-u"}).empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(merge, "f.txt"), s.gitOut(merge, {"show", "MERGE_HEAD:f.txt"}) + "\n");

    // A stash applied with its index.
    const fs::path stashes = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(stashes));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    // stash@{1} has a staged a.txt and an unstaged b.txt: both come back where they were.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_1/###row"); }));
    s.contextMenu("//Stashes/stash_1/###row", "Apply (restore index)");
    GG_CHECK(s.waitUntil([&] { return !s.statusPorcelain(stashes).empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(stashes, {"diff", "--cached", "--name-only"}), "a.txt");
    GG_CHECK_STR_EQ(s.gitOut(stashes, {"diff", "--name-only"}), "b.txt");
    GG_CHECK_STR_EQ(s.gitOut(stashes, {"show", ":a.txt"}), "a staged");
    GG_CHECK_EQ(s.gitOut(stashes, {"stash", "list"}).find("index and worktree") != std::string::npos, true);
}

} // namespace ggtest
