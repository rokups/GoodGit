// New commit, check out / switch (§4.3).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

bool headIs(Scenario& s, const fs::path& repo, const std::string& id) { return s.head(repo) == id; }

std::string symbolicHead(Scenario& s, const fs::path& repo)
{
    auto r = s.gitMayFail(repo, {"symbolic-ref", "-q", "--short", "HEAD"});
    return r.ok() ? gg::trim(r.out) : std::string("(detached)");
}


} // namespace

GG_TEST("new", "new commit on HEAD advances the branch (toolbar, menu, keys)")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    std::string tip = s.head(repo);
    // HEAD's branch advanced by one empty commit.
    auto advanced = [&]() -> bool {
        const bool ok = s.waitUntil([&] { return s.revParse(repo, "main~1") == tip; });
        s.settle();
        const bool attached = symbolicHead(s, repo) == "main";
        const bool empty = s.gitOut(repo, {"diff", "--name-only", "HEAD~1", "HEAD"}).empty();
        tip = s.head(repo);
        return ok && attached && empty;
    };
    ctx->ItemClick("//###Toolbar/###tb_new");
    GG_CHECK(advanced());
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_CHECK(advanced());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(advanced());
    ctx->ItemClick(rowRef(tip).c_str());
    ctx->KeyPress(ImGuiKey_N);
    GG_CHECK(advanced());
    s.contextMenu(rowRef(tip).c_str(), "New");
    GG_CHECK(advanced());
    GG_CHECK_EQ(std::stoi(s.gitOut(repo, {"rev-list", "--count", "HEAD"})), 10);
}

GG_TEST("new", "new detached and new on another commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string main = s.head(repo);
    const std::string older = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick(rowRef(main).c_str());
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_N);
    GG_CHECK(ImGui::GetCurrentContext()->NavLayer == ImGuiNavLayer_Main); // Alt did not toggle the menu layer
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != main; }));
    s.settle();
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main); // branch untouched
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), main);
    // Holding Alt swaps "New" for "New detached" in the row menu.
    ctx->ItemClick(rowRef(older).c_str(), ImGuiMouseButton_Right);
    ctx->KeyDown(ImGuiMod_Alt);
    ctx->Yield(2);
    ctx->MenuClick("//$FOCUSED/New detached");
    ctx->KeyUp(ImGuiMod_Alt);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "HEAD~1") == older; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK(s.statusPorcelain(repo).empty()); // the worktree followed the detached HEAD
}

namespace {

// Right-clicks a row and reports whether the menu item `path` exists and is enabled; closes the menu.
struct MenuProbe {
    bool exists = false;
    bool enabled = false;
};

MenuProbe probe(Scenario& s, ImGuiTestContext* ctx, const std::string& row, const char* item)
{
    (void)s;
    auto copyShown = [&] { return ctx->ItemInfo("//$FOCUSED/Copy", ImGuiTestOpFlags_NoError).ID != 0; };
    ctx->ItemClick(rowRef(row).c_str(), ImGuiMouseButton_Right);
    for (int i = 0; i < 60 && !copyShown(); ++i)
        ctx->Yield(1);
    ctx->Yield(2);
    MenuProbe p;
    const ImGuiTestItemInfo info = ctx->ItemInfo((std::string("//$FOCUSED/") + item).c_str(), ImGuiTestOpFlags_NoError);
    p.exists = info.ID != 0;
    p.enabled = p.exists && !(info.ItemFlags & ImGuiItemFlags_Disabled);
    ctx->KeyPress(ImGuiKey_Escape);
    for (int i = 0; i < 60 && copyShown(); ++i)
        ctx->Yield(1);
    ctx->Yield(2);
    return p;
}

} // namespace

GG_TEST("new", "New is offered on HEAD's branch or a commit with one branch; elsewhere only New detached")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string twoBranches = s.revParse(repo, "HEAD~1");
    const std::string noBranch = s.revParse(repo, "HEAD~2");
    const std::string oneBranch = s.revParse(repo, "HEAD~3");
    s.git(repo, {"branch", "a", twoBranches});
    s.git(repo, {"branch", "b", twoBranches});
    s.git(repo, {"branch", "only", oneBranch});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(oneBranch).c_str()); }));
    // HEAD's commit (main checked out) and a commit with exactly one branch: New.
    for (const std::string& id : {s.head(repo), oneBranch}) {
        GG_CHECK(probe(s, ctx, id, "New").enabled);
        GG_CHECK(!probe(s, ctx, id, "New detached").exists);
    }
    // Two branches, or none: New detached takes New's place.
    for (const std::string& id : {twoBranches, noBranch}) {
        GG_CHECK(!probe(s, ctx, id, "New").exists);
        GG_CHECK(probe(s, ctx, id, "New detached").enabled);
    }
    // Holding Alt offers New detached where New is possible.
    ctx->ItemClick(rowRef(oneBranch).c_str(), ImGuiMouseButton_Right);
    ctx->KeyDown(ImGuiMod_Alt);
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//$FOCUSED/New detached") && !s.itemExists("//$FOCUSED/New"));
    ctx->KeyUp(ImGuiMod_Alt);
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("new", "New on a commit with one other branch advances that branch and checks it out; with none it detaches")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string main = s.head(repo);
    const std::string older = s.revParse(repo, "HEAD~3");
    const std::string bare = s.revParse(repo, "HEAD~2");
    s.git(repo, {"branch", "only", older});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(older).c_str()); }));
    s.contextMenu(rowRef(older).c_str(), "New");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "only"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "only~1"), older);
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main); // the other branch is untouched
    GG_CHECK(s.statusPorcelain(repo).empty());
    // A commit without a branch: detached.
    const std::string advanced = s.head(repo);
    s.contextMenu(rowRef(bare).c_str(), "New detached");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != advanced; }));
    s.settle();
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), bare);
    GG_CHECK_STR_EQ(s.revParse(repo, "only"), advanced);
}

GG_TEST("new", "menu items follow the selection: single-commit items need one commit, ranges need adjacent ones")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string c0 = s.head(repo);
    const std::string c1 = s.revParse(repo, "HEAD~1");
    const std::string c2 = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c2).c_str()); }));
    // One commit: its items are enabled, the range item is not.
    ctx->ItemClick(rowRef(c0).c_str());
    GG_CHECK(probe(s, ctx, c0, "Create tag...").enabled);
    GG_CHECK(probe(s, ctx, c0, "Abandon").enabled);
    GG_CHECK(!probe(s, ctx, c0, "Interactive rebase selection...").enabled);
    // c0 and c2 (a gap at c1): single-commit items and the range item are disabled.
    ctx->ItemClick(rowRef(c0).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(c2).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(s.session()->history().extraSelection().size(), static_cast<size_t>(1));
    for (const char* item : {"New detached", "Create tag...", "Abandon", "Duplicate", "Check out"})
        GG_CHECK(!probe(s, ctx, c0, item).enabled);
    GG_CHECK(!probe(s, ctx, c0, "Interactive rebase selection...").enabled);
    // Right-clicking a selected row keeps the selection.
    GG_CHECK_EQ(s.session()->history().extraSelection().size(), static_cast<size_t>(1));
    // c1 and c2 are adjacent: the range item is enabled, single-commit items still are not.
    ctx->ItemClick(rowRef(c1).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(c2).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(probe(s, ctx, c1, "Interactive rebase selection...").enabled);
    GG_CHECK(!probe(s, ctx, c1, "Create tag...").enabled);
    // A plain right-click on a row outside the selection selects just that row.
    GG_CHECK(probe(s, ctx, c0, "Create tag...").enabled);
    GG_CHECK(s.session()->history().extraSelection().empty());
}

GG_TEST("checkout", "switch to a branch, detach")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string topic = s.revParse(repo, "topic");
    const std::string featureParent = s.revParse(repo, "feature~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.expandMerge(s.revParse(repo, "main")));
    s.contextMenu(rowRef(topic).c_str(), "Check out/topic");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "topic"; }));
    s.settle();
    s.contextMenu(rowRef(featureParent).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, featureParent) && symbolicHead(s, repo) == "(detached)"; }));
    s.settle();
    s.contextMenu(rowRef(s.revParse(repo, "main")).c_str(), "Check out/main");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "main"; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("checkout", "local changes block a switch: Stash and switch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "older", "HEAD~1"});
    s.write(repo, "f5.txt", "local edit that older does not have\n");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_older/###branch_older", "Check out");
    GG_REQUIRE(s.dialogOpen("Stash and switch"));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main"); // refused: nothing changed
    GG_CHECK_STR_EQ(s.read(repo, "f5.txt"), "local edit that older does not have\n");
    s.dialogButton("Stash and switch", "Stash and switch");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "older"; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"stash", "list"}).find("before switching to older") != std::string::npos);
}

} // namespace ggtest
