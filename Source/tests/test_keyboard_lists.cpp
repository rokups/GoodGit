// Keyboard access to the side panels' lists: nav reaches the rows, the selection (where a list has one)
// follows the nav cursor, Alt+Space opens the cursor row's menu, Right/Left open and close tree nodes.
#include "panels/BlamePanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "shell/Settings.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

ImGuiID idOf(ImGuiTestContext* ctx, const std::string& ref) { return ctx->ItemInfo(ref.c_str(), ImGuiTestOpFlags_NoError).ID; }

// Alt pressed and released with nothing in between.
void altTap(ImGuiTestContext* ctx)
{
    ctx->KeyDown(ImGuiMod_Alt);
    ctx->Yield(2);
    ctx->KeyUp(ImGuiMod_Alt);
    ctx->Yield(3);
}

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
    // Items on one line (toolbar, the parents of a commit) are reached sideways.
    for (int i = 0; i < 20 && g.NavId != id; ++i)
        press(ctx, ImGuiKey_RightArrow);
    for (int i = 0; i < 40 && g.NavId != id; ++i)
        press(ctx, ImGuiKey_LeftArrow);
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

GG_TEST("keyboard", "change information: parent links by Right/Left, Enter reveals the parent")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    GG_REQUIRE(s.openRepository(repo));
    const std::string merge = s.gitOut(repo, {"rev-list", "--merges", "-1", "HEAD"});
    const std::string second = s.revParse(repo, merge + "^2");
    GG_REQUIRE(!merge.empty() && !second.empty());
    auto& session = *s.session();
    session.revealCommit(ggui::core::Oid::fromHex(merge));
    const std::string ref = "//Change information/**/###parent_";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((ref + "1").c_str()); }));
    GG_CHECK_STR_EQ(session.selection().id.hex(), merge);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    GG_REQUIRE(navThrough(s, "//Change information", {ref + "0"}));
    press(ctx, ImGuiKey_RightArrow);
    GG_CHECK(g.NavId == idOf(ctx, ref + "1"));
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return session.selection().id.hex() == second; }));
}

GG_TEST("keyboard", "change information: the conflict list is reachable, Enter opens Blame")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string conflicted = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->revealCommit(ggui::core::Oid::fromHex(conflicted));
    const std::string ref = "//Change information/**/###conflict_0";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(ref.c_str()); }, 30.0f));
    GG_REQUIRE(navThrough(s, "//Change information", {ref}));
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return s.session()->blame().blame() != nullptr; }, 30.0f));
}

GG_TEST("keyboard", "welcome: recent repositories by arrows, Alt+Space menu, Delete forgets, Enter opens")
{
    const fs::path a = s.fixture(Recipe::Linear, "r/a");
    const fs::path b = s.fixture(Recipe::Linear, "r/b");
    const fs::path c = s.fixture(Recipe::Linear, "r/c");
    for (const auto& r : {a, b, c}) {
        GG_REQUIRE(s.openRepository(r));
        s.settle();
    }
    s.app.closeRepository();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Welcome/recent_2/###row"); }));
    ggui::Settings& st = s.app.settings();
    GG_REQUIRE(st.data().recent.size() >= 3);
    GG_REQUIRE(navThrough(s, "//Welcome",
        {"//Welcome/recent_0/###row", "//Welcome/recent_1/###row", "//Welcome/recent_2/###row"}));
    altSpaceMenu(s, "Forget");
    // Delete forgets the cursor row (the oldest one: a).
    press(ctx, ImGuiKey_Delete);
    GG_CHECK(s.waitUntil([&] { return st.data().recent.size() == 2; }));
    for (const auto& r : st.data().recent)
        GG_CHECK(!fs::equivalent(r, a));
    // Enter opens the row under the cursor.
    GG_REQUIRE(navTo(ctx, "//Welcome/recent_1/###row"));
    const std::string target = st.data().recent[1];
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), target); }));
}

GG_TEST("keyboard", "main menu bar: an Alt tap focuses it, Right/Down walk menus, Recent forgets by Delete, Escape returns focus")
{
    const fs::path a = s.fixture(Recipe::Linear, "m/a");
    const fs::path b = s.fixture(Recipe::Linear, "m/b");
    const fs::path c = s.fixture(Recipe::Linear, "m/c");
    for (const auto& r : {a, b, c}) {
        GG_REQUIRE(s.openRepository(r));
        s.settle();
    }
    ggui::Settings& st = s.app.settings();
    GG_REQUIRE(st.data().recent.size() >= 3);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//History");
    ctx->Yield(2);
    ImGuiWindow* history = g.NavWindow;
    GG_REQUIRE(history != nullptr);
    ImGuiWindow* bar = ImGui::FindWindowByName("##MainMenuBar");
    GG_REQUIRE(bar != nullptr);
    // Alt+Space on the focused History row opens its menu and does not focus the menu bar.
    GG_REQUIRE(navTo(ctx, "//History/**/###row_" + s.revParse(c, "HEAD~1")));
    history = g.NavWindow; // the list's child window
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK(g.NavWindow != bar);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(g.NavWindow == history);
    // A tap on Alt: the menu bar's first menu has the nav cursor.
    altTap(ctx);
    GG_REQUIRE(g.NavWindow == bar);
    GG_CHECK(g.NavLayer == ImGuiNavLayer_Menu);
    GG_CHECK(g.NavId == idOf(ctx, "//##MainMenuBar/**/Repository"));
    press(ctx, ImGuiKey_RightArrow);
    GG_CHECK(g.NavId == idOf(ctx, "//##MainMenuBar/**/Commit"));
    press(ctx, ImGuiKey_LeftArrow);
    GG_CHECK(g.NavId == idOf(ctx, "//##MainMenuBar/**/Repository"));
    // Alt again leaves it, back to History.
    altTap(ctx);
    GG_CHECK(g.NavWindow == history);
    altTap(ctx);
    GG_REQUIRE(g.NavWindow == bar);
    // Activating an item by Enter hands focus back too (Copy path is harmless).
    press(ctx, ImGuiKey_DownArrow);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/Copy path"));
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(g.NavWindow == history);
    altTap(ctx);
    GG_REQUIRE(g.NavWindow == bar);
    // Down opens Repository, Recent opens by Right, Delete forgets an entry other than the current one.
    press(ctx, ImGuiKey_DownArrow);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    const std::string recent = "//$FOCUSED/Recent";
    GG_REQUIRE(navTo(ctx, recent));
    press(ctx, ImGuiKey_RightArrow);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    const std::string current = s.session()->path().string();
    int victim = -1, entries = 0;
    for (size_t i = 0; i < st.data().recent.size(); ++i) {
        if (idOf(ctx, "//$FOCUSED/###recent_menu_" + std::to_string(i)) != 0)
            ++entries;
        if (victim < 0 && !fs::equivalent(st.data().recent[i], current))
            victim = int(i);
    }
    GG_REQUIRE(victim >= 0);
    GG_REQUIRE(entries >= 3);
    const std::string victimPath = st.data().recent[size_t(victim)];
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###recent_menu_" + std::to_string(victim)));
    press(ctx, ImGuiKey_Delete);
    GG_CHECK(s.waitUntil([&] { return std::ranges::find(st.data().recent, victimPath) == st.data().recent.end(); }));
    GG_CHECK_EQ(st.data().recent.size(), size_t(2));
    // Escape closes Recent, Repository, then returns focus to History.
    for (int i = 0; i < 4 && g.NavWindow != history; ++i) {
        press(ctx, ImGuiKey_Escape);
    }
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(g.NavWindow == history);
}

GG_TEST("keyboard", "toolbar: the repository switcher opens by Enter, Down walks the entries, Delete forgets, Enter switches")
{
    const fs::path a = s.fixture(Recipe::Linear, "left/proj");
    const fs::path b = s.fixture(Recipe::Linear, "right/proj");
    const fs::path c = s.fixture(Recipe::Linear, "alpha");
    for (const auto& r : {a, b, c}) {
        GG_REQUIRE(s.openRepository(r));
        s.settle();
    }
    ggui::Settings& st = s.app.settings();
    GG_REQUIRE(st.data().recent.size() >= 3);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const std::string combo = "//###Toolbar/##tb_repo";
    // Most recent first: c (current), b, a.
    GG_REQUIRE(navThrough(s, "//###Toolbar", {combo}));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_0"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_1"));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_2"));
    // Delete on the current repository does nothing.
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_0"));
    press(ctx, ImGuiKey_Delete);
    GG_CHECK_EQ(st.data().recent.size(), size_t(3));
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_2"));
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), a); }));
    s.settle();
    // Delete on another entry forgets it; Escape closes the combo.
    ctx->WindowFocus("//###Toolbar");
    GG_REQUIRE(navTo(ctx, combo));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/###switch_1"));
    press(ctx, ImGuiKey_Delete);
    GG_CHECK(s.waitUntil([&] { return st.data().recent.size() == 2; }));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(fs::equivalent(s.session()->path(), a));
}

GG_TEST("keyboard", "dialogs: combos open by Enter, rows by Down, the filter picks by Enter; settings combo")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "second", "file:///nonexistent/second.git"});
    for (const char* b : {"alpha", "beta", "gamma"})
        s.git(repo, {"update-ref", (std::string("refs/remotes/origin/") + b).c_str(), "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->remoteBranches.size() >= 4; }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    auto form = [&] { return s.app.dialogs().current(); };

    // Set upstream: a filterable combo.
    s.session()->showSetUpstreamDialog("main");
    GG_REQUIRE(s.dialogOpen("Set upstream"));
    GG_REQUIRE(navTo(ctx, "//Set upstream/Upstream of main##upstream"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    GG_REQUIRE(form()->fields[0].options.size() >= 4);
    const int before = form()->fields[0].choice;
    // Escape order: the filter field first, then the combo; the dialog stays until a third Escape.
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 2);
    GG_CHECK(form() && form()->title == "Set upstream");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK(form() && form()->title == "Set upstream");
    GG_REQUIRE(navTo(ctx, "//Set upstream/Upstream of main##upstream"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    // Down from the filter reaches the rows; Enter chooses.
    press(ctx, ImGuiKey_DownArrow);
    press(ctx, ImGuiKey_DownArrow);
    press(ctx, ImGuiKey_DownArrow);
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK(form()->fields[0].choice != before);
    // The filter: type, Enter picks the first match.
    GG_REQUIRE(navTo(ctx, "//Set upstream/Upstream of main##upstream"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    ctx->KeyChars("gam");
    ctx->Yield(2);
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK_STR_EQ(form()->fields[0].options[static_cast<size_t>(form()->fields[0].choice)], "origin/gamma");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!form());

    // Push to: the remote combo.
    s.session()->showPushToDialog("main");
    GG_REQUIRE(s.dialogOpen("Push to"));
    GG_REQUIRE(navTo(ctx, "//Push to/Remote##remote"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    press(ctx, ImGuiKey_DownArrow);
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK_EQ(form()->fields[0].choice, 1);
    // Escape closes an open combo only, not the dialog under it.
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK(form() && form()->title == "Push to");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!form());

    // Add worktree: "Check out" shows the branch combo; both by keyboard.
    s.session()->showAddWorktreeDialog(0, "");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    GG_REQUIRE(navTo(ctx, "//Add worktree/Check out##checkout"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    press(ctx, ImGuiKey_DownArrow);
    press(ctx, ImGuiKey_Enter);
    GG_CHECK_EQ(form()->fields[1].choice, 1);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Add worktree/Branch##existing"); }));
    GG_REQUIRE(navTo(ctx, "//Add worktree/Branch##existing"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 2);
    ctx->KeyChars("main");
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!form());

    // A plain combo: Settings > Git > Pull method.
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    const std::string tab = "//Settings/##settings_tabs/Git/##config_scope/User/";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + "Pull method##pull_method").c_str()); }));
    GG_REQUIRE(navTo(ctx, tab + "Pull method##pull_method"));
    press(ctx, ImGuiKey_Enter);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_REQUIRE(navTo(ctx, "//$FOCUSED/Rebase, keeping merges"));
    press(ctx, ImGuiKey_Enter);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK(s.waitUntil([&] { return gg::trim(s.gitMayFail(repo, {"config", "--global", "--get", "pull.rebase"}).out) == "merges"; }));
}

} // namespace ggtest
