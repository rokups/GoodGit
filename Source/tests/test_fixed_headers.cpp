// Controls above a list stay visible: only the list scrolls, never the whole window.
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Settings.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

// With more rows than fit: scroll the list (the "##list" child of `window`) to the bottom and check that
// the header control `header` has not moved, is fully inside the window, the window itself does not
// scroll and the list does.
void checkFixedHeader(Scenario& s, const char* window, const std::string& header)
{
    ImGuiTestContext* ctx = s.ctx;
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ImGuiWindow* win = ctx->WindowInfo((std::string("//") + window).c_str(), ImGuiTestOpFlags_NoError).Window;
    GG_REQUIRE(win != nullptr);
    const std::string listRef = s.child((std::string("//") + window).c_str(), "##list");
    ImGuiWindow* list = ctx->WindowInfo(listRef.c_str(), ImGuiTestOpFlags_NoError).Window;
    GG_REQUIRE(list != nullptr);
    ctx->Yield(2);
    GG_REQUIRE(list->ScrollMax.y > 0.0f); // the fixture has more rows than fit
    const ImRect before = ctx->ItemInfo(header.c_str()).RectFull;
    ctx->ScrollToBottom(listRef.c_str());
    ctx->Yield(3);
    GG_CHECK(list->Scroll.y > 0.0f);
    GG_CHECK_EQ(win->Scroll.y, 0.0f);
    GG_CHECK_EQ(win->ScrollMax.y, 0.0f);
    const ImRect after = ctx->ItemInfo(header.c_str()).RectFull;
    GG_CHECK(after.Min.x == before.Min.x && after.Min.y == before.Min.y);
    GG_CHECK(win->InnerClipRect.Contains(after));
    // A wheel turn over the list scrolls the list too.
    ctx->ScrollToTop(listRef.c_str());
    ctx->Yield(2);
    ctx->MouseMoveToPos(list->InnerClipRect.GetCenter());
    ctx->MouseWheelY(-5.0f);
    ctx->Yield(3);
    GG_CHECK(list->Scroll.y > 0.0f);
    GG_CHECK_EQ(win->Scroll.y, 0.0f);
}

} // namespace

GG_TEST("fixed headers", "branches: the buttons and filter stay while the list scrolls")
{
    GG_REQUIRE(s.openRepository(s.fixture(Recipe::ManyRefs)));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/##branch_filter"); }));
    checkFixedHeader(s, "Branches", "//Branches/##branch_filter");
    checkFixedHeader(s, "Branches", "//Branches/###create_branch");
}

GG_TEST("fixed headers", "tags: the add button and filter stay while the list scrolls")
{
    GG_REQUIRE(s.openRepository(s.fixture(Recipe::ManyRefs)));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/##tag_filter"); }));
    checkFixedHeader(s, "Tags", "//Tags/##tag_filter");
    checkFixedHeader(s, "Tags", "//Tags/###create_tag");
}

GG_TEST("fixed headers", "remotes: the buttons stay while the list scrolls")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 60; ++i)
        s.git(repo, {"remote", "add", "r" + std::to_string(i), "file:///nonexistent/r" + std::to_string(i) + ".git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Remotes/remote_r59/###row"); }));
    checkFixedHeader(s, "Remotes", "//Remotes/###add_remote");
    checkFixedHeader(s, "Remotes", "//Remotes/Fetch all##fetch_all");
}

GG_TEST("fixed headers", "stashes: the buttons stay while the list scrolls")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 40; ++i) {
        std::ofstream(repo / "stash.txt") << i << "\n";
        s.git(repo, {"stash", "push", "-q", "-u", "-m", "s" + std::to_string(i)});
    }
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_39/###row"); }));
    checkFixedHeader(s, "Stashes", "//Stashes/Push##stash_push");
    checkFixedHeader(s, "Stashes", "//Stashes/Clear all...##clear_stashes");
}

GG_TEST("fixed headers", "worktrees: the add button stays while the list scrolls")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 30; ++i)
        s.git(repo, {"worktree", "add", "-q", "--detach", repo.string() + "-wt" + std::to_string(i), "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->worktrees.size() >= 31; }));
    checkFixedHeader(s, "Worktrees", "//Worktrees/###add_worktree");
}

GG_TEST("fixed headers", "welcome: the buttons and the path field stay while the recent list scrolls")
{
    for (int i = 0; i < 20; ++i) // the most the list keeps
        s.app.settings().addRecent("/nonexistent/recent/repo" + std::to_string(i));
    // Twenty is all the list keeps: a large UI scale makes them overflow the window.
    struct Restore {
        ggui::Theme theme = ggui::theme().theme();
        float scale = ggui::theme().scale();
        ~Restore() { ggui::theme().apply(theme, scale); }
    } restore;
    ggui::theme().apply(restore.theme, 2.0f);
    s.ctx->Yield(3);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Welcome/recent_19/###row"); }));
    checkFixedHeader(s, "Welcome", "//Welcome/###welcome_open");
    checkFixedHeader(s, "Welcome", "//Welcome/##welcome_path");
}

} // namespace ggtest
