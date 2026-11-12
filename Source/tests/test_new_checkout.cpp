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

// Waits until HEAD leaves `before`, then lets the app settle.
bool headMoved(Scenario& s, const fs::path& repo, const std::string& before)
{
    const bool ok = s.waitUntil([&] { return s.head(repo) != before; });
    s.settle();
    return ok;
}

} // namespace

GG_TEST("new", "New detaches HEAD at an empty commit on HEAD and leaves the branch alone (toolbar, menu, keys)")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string main = s.head(repo);
    std::string tip = main;
    // HEAD detached on a new empty commit whose parent is the previous HEAD; main did not move.
    auto detached = [&]() -> bool {
        const bool ok = headMoved(s, repo, tip);
        const bool det = symbolicHead(s, repo) == "(detached)";
        const bool parent = s.revParse(repo, "HEAD~1") == tip;
        const bool empty = s.gitOut(repo, {"diff", "--name-only", "HEAD~1", "HEAD"}).empty();
        const bool branch = s.revParse(repo, "main") == main;
        tip = s.head(repo);
        return ok && det && parent && empty && branch;
    };
    ctx->ItemClick("//###Toolbar/###tb_new");
    GG_CHECK(detached());
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_CHECK(detached());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(detached());
    ctx->ItemClick(rowRef(tip).c_str());
    ctx->KeyPress(ImGuiKey_N);
    GG_CHECK(detached());
    s.contextMenu(rowRef(tip).c_str(), "New");
    GG_CHECK(detached());
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK_EQ(std::stoi(s.gitOut(repo, {"rev-list", "--count", "HEAD"})), 10);
}

GG_TEST("new", "New is offered on every single commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string twoBranches = s.revParse(repo, "HEAD~1");
    const std::string noBranch = s.revParse(repo, "HEAD~2");
    s.git(repo, {"branch", "a", twoBranches});
    s.git(repo, {"branch", "b", twoBranches});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(noBranch).c_str()); }));
    for (const std::string& id : {s.head(repo), twoBranches, noBranch}) {
        GG_CHECK(probe(s, ctx, id, "New").enabled);
        GG_CHECK(!probe(s, ctx, id, "New detached").exists);
    }
}

GG_TEST("new", "New on an older commit detaches at its child and keeps every branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string main = s.head(repo);
    const std::string older = s.revParse(repo, "HEAD~3");
    s.git(repo, {"branch", "only", older});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(older).c_str()); }));
    s.contextMenu(rowRef(older).c_str(), "New");
    GG_CHECK(headMoved(s, repo, main));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), older); // the parent rule is unchanged: the selected commit
    GG_CHECK_STR_EQ(s.revParse(repo, "only"), older);
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("new", "New commit takes only the staged files when some are staged")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string main = s.head(repo);
    s.write(repo, "staged.txt", "staged\n");
    s.write(repo, "loose.txt", "loose\n");
    s.git(repo, {"add", "staged.txt"});
    s.session()->refresh();
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_CHECK(headMoved(s, repo, main));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(repo, {"diff", "--name-only", "HEAD~1", "HEAD"})), "staged.txt");
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(repo, {"status", "--porcelain"})), "?? loose.txt");
}

GG_TEST("new", "New commit takes all files, untracked ones too, when nothing is staged")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string main = s.head(repo);
    const std::string tracked = gg::trim(s.gitOut(repo, {"ls-files"}).substr(0, s.gitOut(repo, {"ls-files"}).find('\n')));
    GG_REQUIRE(!tracked.empty());
    s.write(repo, tracked, "changed\n");
    s.write(repo, "fresh.txt", "fresh\n");
    s.session()->refresh();
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_CHECK(headMoved(s, repo, main));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    const std::string names = s.gitOut(repo, {"diff", "--name-only", "HEAD~1", "HEAD"});
    GG_CHECK(names.find(tracked) != std::string::npos);
    GG_CHECK(names.find("fresh.txt") != std::string::npos);
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("new", "New commit on a clean tree is empty")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string main = s.head(repo);
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_CHECK(headMoved(s, repo, main));
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), main);
    GG_CHECK(s.gitOut(repo, {"diff", "--name-only", "HEAD~1", "HEAD"}).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("new", "Undo after New (staged files) stages the staged file again and keeps the other unstaged")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string main = s.head(repo);
    const std::string tracked = gg::trim(s.gitOut(repo, {"ls-files"}).substr(0, s.gitOut(repo, {"ls-files"}).find('\n')));
    GG_REQUIRE(!tracked.empty());
    s.write(repo, "staged.txt", "staged\n");
    s.git(repo, {"add", "staged.txt"});
    s.write(repo, tracked, "changed\n");
    s.session()->refresh();
    s.settle();
    const std::string status = s.gitOut(repo, {"status", "--porcelain"});
    GG_REQUIRE(status.find("A  staged.txt") != std::string::npos);
    ctx->MenuClick("//##MainMenuBar/Commit/New commit");
    GG_REQUIRE(headMoved(s, repo, main));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, main); }));
    s.settle();
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), status);
    GG_CHECK_STR_EQ(s.read(repo, tracked), "changed\n");
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
    GG_CHECK(probe(s, ctx, c0, "Drop commit...").enabled);
    GG_CHECK(!probe(s, ctx, c0, "Interactive rebase selection...").enabled);
    // c0 and c2 (a gap at c1): single-commit items and the range item are disabled.
    ctx->ItemClick(rowRef(c0).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(c2).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(s.session()->history().extraSelection().size(), static_cast<size_t>(1));
    for (const char* item : {"New detached", "Create tag...", "Drop commit...", "Duplicate", "Check out"})
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

GG_TEST("checkout", "Check out lists the branches at the commit and Detached; Edit commit is in the editing group")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string tip = s.revParse(repo, "topic");
    const std::string noBranch = s.revParse(repo, "feature~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.expandMerge(s.revParse(repo, "main")));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(noBranch).c_str()); }));
    GG_REQUIRE(gg::splitLines(s.gitOut(repo, {"branch", "--points-at", noBranch})).empty());
    GG_CHECK(probe(s, ctx, noBranch, "Check out").enabled);
    GG_CHECK(probe(s, ctx, tip, "Check out").enabled);
    GG_CHECK(probe(s, ctx, noBranch, "Edit commit (checkout detached)").enabled);
    // Edit commit is in the history editing group, after Create branch.
    ctx->ItemClick(rowRef(tip).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_REQUIRE(s.itemExists("//$FOCUSED/Edit commit (checkout detached)"));
    const float checkOutY = ctx->ItemInfo("//$FOCUSED/Check out", ImGuiTestOpFlags_NoError).RectFull.Min.y;
    const float editY = ctx->ItemInfo("//$FOCUSED/Edit commit (checkout detached)", ImGuiTestOpFlags_NoError).RectFull.Min.y;
    const float createY = ctx->ItemInfo("//$FOCUSED/Create branch...", ImGuiTestOpFlags_NoError).RectFull.Min.y;
    GG_CHECK(checkOutY < createY && createY < editY);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);

    // The submenu lists the branches and, last, Detached.
    ctx->ItemClick(rowRef(tip).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Check out/topic");
    GG_CHECK(s.itemExists("//Check out###Menu_00/topic"));
    GG_CHECK(s.itemExists("//Check out###Menu_00/Detached"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
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
    s.contextMenu(rowRef(featureParent).c_str(), "Edit commit (checkout detached)");
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
