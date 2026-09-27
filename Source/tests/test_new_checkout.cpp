// New commit, check out / switch, Move HEAD (§4.3; P2-11, P2-12).
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

GG_TEST("new", "new commit on HEAD advances the branch (toolbar, menu, keys)", "ACT-NEW", "TB-NEW", "MENU-COMMIT-NEW",
    "MENU-COMMIT-NEW-KEY", "HIST-KEY-N", "HIST-CTX-NEW")
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
    ctx->ItemClick("//##Toolbar/###tb_new");
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

GG_TEST("new", "new detached and new on another commit", "ACT-NEW-DETACHED", "HIST-KEY-ALT-N", "HIST-CTX-NEW-DETACHED")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string main = s.head(repo);
    const std::string older = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick(rowRef(main).c_str());
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != main; }));
    s.settle();
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main); // branch untouched
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), main);
    s.contextMenu(rowRef(older).c_str(), "New detached");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "HEAD~1") == older; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), main);
    GG_CHECK(s.statusPorcelain(repo).empty()); // the worktree followed the detached HEAD
}

GG_TEST("new", "several parents make a merge commit", "ACT-NEW-MERGE")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string main = s.head(repo);
    const std::string topic = s.revParse(repo, "topic");
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick(rowRef(main).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(topic).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(s.session()->history().extraSelection().size(), static_cast<size_t>(1));
    ctx->KeyPress(ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main^1") == main; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "main^2"), topic);
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main");
}

GG_TEST("checkout", "switch to a branch, detach, E key", "ACT-CHECKOUT-BRANCH", "ACT-CHECKOUT-DETACH", "HIST-KEY-E",
    "HIST-CTX-CHECKOUT")
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
    // E on a branch tip switches to the branch.
    ctx->ItemClick(rowRef(s.revParse(repo, "main")).c_str());
    ctx->KeyPress(ImGuiKey_E);
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "main"; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("checkout", "local changes block a switch: Stash and switch", "ACT-CHECKOUT-REFUSE", "ACT-CHECKOUT-STASH",
    "STASH-SWITCH-HELPER")
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

GG_TEST("checkout", "move HEAD to parent and child", "ACT-MOVE-HEAD-PARENT", "ACT-MOVE-HEAD-CHILD", "MENU-COMMIT-PREV",
    "MENU-COMMIT-NEXT", "TB-NO-PREV-NEXT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string main = s.head(repo);
    const std::string p1 = s.revParse(repo, "HEAD~1");
    const std::string p2 = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    // The toolbar has no Previous / Next buttons (the Commit menu has the actions).
    GG_CHECK(!s.itemExists("//##Toolbar/###tb_prev") && !s.itemExists("//##Toolbar/###tb_next"));
    ctx->MenuClick("//##MainMenuBar/Commit/Move HEAD to parent");
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, p1); }));
    s.settle();
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "(detached)");
    ctx->MenuClick("//##MainMenuBar/Commit/Move HEAD to parent");
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, p2); }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Commit/Move HEAD to child");
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, p1); }));
    s.settle();
    // The child that is a branch tip switches to the branch again.
    ctx->MenuClick("//##MainMenuBar/Commit/Move HEAD to child");
    GG_CHECK(s.waitUntil([&] { return headIs(s, repo, main) && symbolicHead(s, repo) == "main"; }));
    s.settle();
}

} // namespace ggtest
