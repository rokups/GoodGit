// Keyboard access to the side panels' lists: nav reaches the rows, the selection (where a list has one)
// follows the nav cursor, Alt+Space opens the cursor row's menu, Right/Left open and close tree nodes.
#include "panels/HistoryPanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

ImGuiID idOf(ImGuiTestContext* ctx, const std::string& ref) { return ctx->ItemInfo(ref.c_str(), ImGuiTestOpFlags_NoError).ID; }

void press(ImGuiTestContext* ctx, ImGuiKeyChord chord)
{
    ctx->KeyPress(chord);
    ctx->Yield(2);
}

// Presses Down (then Up, when the item is above the starting point) until the nav cursor is on `ref`; bounded.
bool navTo(ImGuiTestContext* ctx, const std::string& ref)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiID id = idOf(ctx, ref);
    if (id == 0)
        return false;
    for (int i = 0; i < 40 && g.NavId != id; ++i)
        press(ctx, ImGuiKey_DownArrow);
    for (int i = 0; i < 80 && g.NavId != id; ++i)
        press(ctx, ImGuiKey_UpArrow);
    return g.NavId == id;
}

// Without clicking: focus the window, walk the nav cursor onto each of `rows` in turn (>= 3 rows, the
// later ones reached by Down from the earlier).
bool navThrough(Scenario& s, const char* window, const std::vector<std::string>& rows)
{
    ImGuiTestContext* ctx = s.ctx;
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus(window);
    ctx->Yield(2);
    for (const auto& r : rows)
        if (!navTo(ctx, r))
            return false;
    return true;
}

// Alt+Space on the cursor row opens its menu (`item` is specific to that row's menu); Escape closes it.
void altSpaceMenu(Scenario& s, const std::string& item)
{
    ImGuiTestContext* ctx = s.ctx;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiID nav = g.NavId;
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK(ctx->ItemExists(("//$FOCUSED/" + item).c_str()));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(g.NavId == nav);
}

std::string branchRef(const std::string& name)
{
    std::string id = name;
    std::replace(id.begin(), id.end(), '/', ':');
    return "//Branches/**/###branch_" + id;
}

// A hidden panel opened by a test is hidden again.
bool panelHidden(Scenario& s, const char* name)
{
    const auto& panels = s.app.settings().data().panels;
    const auto it = panels.find(name);
    return it == panels.end() ? !ggui::panel::defaultVisible(name) : !it->second;
}

void hideAgain(Scenario& s, const char* name)
{
    s.ctx->MenuClick((std::string("//##MainMenuBar/View/") + name).c_str());
    s.ctx->Yield(3);
}

} // namespace

GG_TEST("keyboard", "branches: tree rows, Alt+Space menus, groups open and close by Right/Left")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* b : {"alpha", "beta", "feat/one", "feat/two"})
        s.git(repo, {"branch", b});
    s.git(repo, {"push", "-q", "origin", "alpha"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRef("feat/two").c_str()); }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // alpha, beta (rows 1 and 2), then past the group to main.
    GG_REQUIRE(navThrough(s, "//Branches", {branchRef("alpha"), branchRef("beta")}));
    altSpaceMenu(s, "Rename...");
    // The local group node: Left closes it, Right opens it again.
    const std::string group = "//Branches/**/###group_local:feat:";
    GG_REQUIRE(navTo(ctx, group));
    GG_CHECK(s.itemExists(branchRef("feat/one").c_str()));
    press(ctx, ImGuiKey_LeftArrow);
    GG_CHECK(!s.itemExists(branchRef("feat/one").c_str()));
    GG_CHECK(g.NavId == idOf(ctx, group));
    press(ctx, ImGuiKey_RightArrow);
    GG_CHECK(s.itemExists(branchRef("feat/one").c_str()));
    // Down enters the group, then on to main (the rows below).
    GG_CHECK(navTo(ctx, branchRef("feat/two")));
    GG_CHECK(navTo(ctx, branchRef("main")));
    // The remote node and a remote-tracking branch under it.
    const std::string remote = "//Branches/remote_group_origin/origin";
    GG_REQUIRE(navTo(ctx, remote));
    altSpaceMenu(s, "Show all branches in History");
    press(ctx, ImGuiKey_LeftArrow);
    GG_CHECK(!s.itemExists("//Branches/**/###rbranch_origin:alpha"));
    press(ctx, ImGuiKey_RightArrow);
    GG_REQUIRE(navTo(ctx, "//Branches/**/###rbranch_origin:alpha"));
    GG_REQUIRE(navTo(ctx, "//Branches/**/###rbranch_origin:main"));
    altSpaceMenu(s, "Reveal");
}

GG_TEST("keyboard", "tags: rows by arrows, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* t : {"v1", "v2", "v3"})
        s.git(repo, {"tag", t});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/**/###tag_v3"); }));
    GG_REQUIRE(navThrough(s, "//Tags", {"//Tags/**/###tag_v1", "//Tags/**/###tag_v2", "//Tags/**/###tag_v3"}));
    altSpaceMenu(s, "Push tag");
    // Space toggles the row's visibility in History (the keyboard side of the eye icon).
    auto& history = s.session()->history();
    GG_CHECK(history.refVisible("refs/tags/v3"));
    press(ctx, ImGuiKey_Space);
    GG_CHECK(!history.refVisible("refs/tags/v3"));
    press(ctx, ImGuiKey_Space);
    GG_CHECK(history.refVisible("refs/tags/v3"));
}

GG_TEST("keyboard", "worktrees: rows by arrows, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->worktrees.size() >= 3; }));
    std::vector<std::string> rows;
    for (const auto& w : s.session()->snapshot()->worktrees)
        rows.push_back("//Worktrees/worktree_" + w.name + "/###row");
    GG_REQUIRE(rows.size() >= 3);
    GG_REQUIRE(navThrough(s, "//Worktrees", rows));
    altSpaceMenu(s, "Copy path");
}

GG_TEST("keyboard", "remotes: rows by arrows, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "second", "file:///nonexistent/second.git"});
    s.git(repo, {"remote", "add", "third", "file:///nonexistent/third.git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Remotes/remote_third/###row"); }));
    GG_REQUIRE(navThrough(s, "//Remotes",
        {"//Remotes/remote_origin/###row", "//Remotes/remote_second/###row", "//Remotes/remote_third/###row"}));
    altSpaceMenu(s, "Edit URL...");
}

GG_TEST("keyboard", "stashes: rows by arrows select the stash, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_2/###row"); }));
    GG_REQUIRE(navThrough(s, "//Stashes", {"//Stashes/stash_0/###row"}));
    auto& session = *s.session();
    GG_CHECK(session.selection().kind == ggui::SelKind::Stash);
    GG_CHECK_EQ(session.selection().stashIndex, 0);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // The selection follows the cursor.
    GG_REQUIRE(navTo(ctx, "//Stashes/stash_1/###row"));
    GG_CHECK(session.selection().kind == ggui::SelKind::Stash);
    GG_CHECK_EQ(session.selection().stashIndex, 1);
    GG_REQUIRE(navTo(ctx, "//Stashes/stash_2/###row"));
    GG_CHECK_EQ(session.selection().stashIndex, 2);
    GG_CHECK(g.NavId == idOf(ctx, "//Stashes/stash_2/###row"));
    altSpaceMenu(s, "Branch from stash...");
    GG_CHECK_EQ(session.selection().stashIndex, 2);
}

GG_TEST("keyboard", "reflog: ref picker and entries by keyboard, Alt+Space entry menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "alpha"});
    GG_REQUIRE(s.openRepository(repo));
    const bool hidden = panelHidden(s, "Reflog");
    s.showPanel("Reflog");
    auto& reflog = s.session()->reflog();
    GG_REQUIRE(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->entries.size() >= 3; }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // The picker: Enter opens it, Down walks its rows, Enter chooses.
    GG_REQUIRE(navThrough(s, "//Reflog", {"//Reflog/##reflog_ref"}));
    press(ctx, ImGuiKey_Enter);
    ctx->Yield(2);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_REQUIRE(s.itemExists("//$FOCUSED/###ref_HEAD"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###ref_HEAD"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###ref_refs:heads:alpha"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###ref_refs:heads:main"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###ref_refs:heads:alpha"));
    press(ctx, ImGuiKey_Enter);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK_STR_EQ(reflog.ref(), "refs/heads/alpha");
    // Back to HEAD by the picker, then the entries.
    GG_REQUIRE(navTo(ctx, "//Reflog/##reflog_ref"));
    press(ctx, ImGuiKey_Enter);
    ctx->Yield(2);
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###ref_HEAD"));
    press(ctx, ImGuiKey_Enter);
    ctx->Yield(3);
    GG_REQUIRE(s.waitUntil([&] { return reflog.ref() == "HEAD" && s.itemExists("//Reflog/##reflog_table/r2/###reflog_2"); }));
    GG_REQUIRE(navThrough(s, "//Reflog",
        {"//Reflog/##reflog_table/r0/###reflog_0", "//Reflog/##reflog_table/r1/###reflog_1", "//Reflog/##reflog_table/r2/###reflog_2"}));
    altSpaceMenu(s, "Reveal new commit");
    if (hidden)
        hideAgain(s, "Reflog");
}

GG_TEST("keyboard", "operations: rows by arrows, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    for (const char* b : {"o1", "o2", "o3"}) {
        s.session()->actions().createBranch(b, "HEAD", false);
        s.settle();
    }
    const bool hidden = panelHidden(s, "Operations");
    s.showPanel("Operations");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->operations().size() >= 3; }));
    std::vector<std::string> rows;
    const auto ops = s.session()->operations(); // a copy: the list is reloaded while the UI runs
    for (auto it = ops.rbegin(); it != ops.rend() && rows.size() < 3; ++it)
        rows.push_back(s.child("//Operations", "##ops_table") + "/**/op_" + it->id + "/###row");
    GG_REQUIRE(rows.size() == 3);
    GG_REQUIRE(navThrough(s, "//Operations", rows));
    altSpaceMenu(s, "Copy operation ID");
    if (hidden)
        hideAgain(s, "Operations");
}

} // namespace ggtest
