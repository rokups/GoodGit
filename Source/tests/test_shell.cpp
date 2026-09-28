// Application shell, Welcome screen, menus, toolbar, layout and settings (§4.1; P1-12 … P1-14).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>

#include <imgui_internal.h>

namespace ggtest {

namespace {

ImGuiDockNode* dockOf(ImGuiTestContext* ctx, const char* window)
{
    ImGuiWindow* w = ctx->GetWindowByRef(window);
    return w ? w->DockNode : nullptr;
}

bool closed(Scenario& s) { return s.session() == nullptr; }

} // namespace

GG_TEST("shell", "open by typed path, default layout, close from the menu", "APP-WELCOME-OPEN-PATH",
    "LAYOUT-DEFAULT", "LAYOUT-HIDDEN-PANELS", "TB-BRANCH", "MENU-REPO-CLOSE", "APP-OPEN-STATES")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.session() && s.session()->opened());
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_branch"), "main");
    // Default dock layout: Branches|Tags, Worktrees|Remotes|Stashes, History, Changes,
    // Change information, Diff (Blame, Reflog and Operations join it when shown; hidden at first).
    ctx->Yield(3);
    for (const char* hidden : {"Blame", "Reflog", "Operations"}) {
        ImGuiWindow* w = ImGui::FindWindowByName(hidden);
        GG_CHECK(w == nullptr || !w->Active);
    }
    for (const char* hidden : {"Blame", "Reflog", "Operations"})
        s.showPanel(hidden);
    ctx->Yield(3);
    ImGuiDockNode* branches = dockOf(ctx, "//Branches");
    GG_CHECK(branches != nullptr);
    GG_CHECK(branches == dockOf(ctx, "//Tags"));
    GG_CHECK(dockOf(ctx, "//Worktrees") == dockOf(ctx, "//Remotes"));
    GG_CHECK(dockOf(ctx, "//Worktrees") == dockOf(ctx, "//Stashes"));
    GG_CHECK(dockOf(ctx, "//Diff") == dockOf(ctx, "//Blame"));
    GG_CHECK(dockOf(ctx, "//Diff") == dockOf(ctx, "//Reflog"));
    GG_CHECK(dockOf(ctx, "//Diff") == dockOf(ctx, "//Operations"));
    GG_CHECK(dockOf(ctx, "//History") != nullptr && dockOf(ctx, "//History") != branches);
    GG_CHECK(dockOf(ctx, "//Changes") != dockOf(ctx, "//Change information"));
    ImGuiWindow* history = ctx->GetWindowByRef("//History");
    ImGuiWindow* changes = ctx->GetWindowByRef("//Changes");
    ImGuiWindow* left = ctx->GetWindowByRef("//Branches");
    GG_REQUIRE(history && changes && left);
    GG_CHECK(left->Pos.x < history->Pos.x && history->Pos.x < changes->Pos.x);

    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    GG_CHECK(s.waitUntil([&] { return closed(s); }));
    GG_CHECK(s.itemExists("//Welcome/##welcome_path"));
}

GG_TEST("shell", "open with the picker: Welcome, menu, Ctrl+O, toolbar", "APP-WELCOME-OPEN", "MENU-REPO-OPEN",
    "MENU-REPO-OPEN-KEY", "MENU-REPO-CLOSE-KEY")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.track(repo);
    ggui::setEnv("GGUI_TEST_PICK_PATH", repo.string());
    auto reopen = [&](const std::function<void()>& action) {
        action();
        const bool ok = s.waitUntil([&] { return s.session() && s.session()->opened(); });
        GG_CHECK(ok);
        s.waitIdle();
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_W);
        GG_CHECK(s.waitUntil([&] { return closed(s); }));
    };
    reopen([&] { ctx->ItemClick("//Welcome/###welcome_open"); });
    reopen([&] { ctx->MenuClick("//##MainMenuBar/Repository/Open..."); });
    reopen([&] { ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_O); });
    // A cancelled picker opens nothing.
    ggui::unsetEnv("GGUI_TEST_PICK_PATH");
    ggui::setEnv("GGUI_TEST_PICK_CANCEL", "1");
    ctx->ItemClick("//Welcome/###welcome_open");
    ctx->Yield(3);
    GG_CHECK(closed(s));
}

GG_TEST("shell", "opening shows progress and can be cancelled", "APP-WELCOME-PROGRESS", "APP-WELCOME-CANCEL")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::setSlowGitLatency(std::chrono::milliseconds(5000));
    // Type the path without Enter, then use the Open button (mouse path).
    ctx->ItemClick("//Welcome/##welcome_path");
    ctx->KeyCharsReplace(repo.string().c_str());
    ctx->ItemClick("//Welcome/Open");
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Welcome/Cancel##open"); }, 15.0f));
    GG_CHECK(s.session() && s.session()->opening());
    ctx->ItemClick("//Welcome/Cancel##open");
    GG_CHECK(s.waitUntil([&] { return closed(s); }, 5.0f));
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(s.app.errorMessage().empty());
    GG_CHECK(s.waitIdle());
}

GG_TEST("shell", "recent repositories: Welcome list, Recent menu, switcher", "APP-WELCOME-RECENT-OPEN",
    "APP-WELCOME-RECENT-DELETE", "APP-WELCOME-RECENT-INFO", "MENU-REPO-RECENT", "MENU-REPO-RECENT-FILTER",
    "MENU-REPO-RECENT-INFO", "TB-REPO-SWITCH")
{
    const fs::path remote = s.fixture(Recipe::WithRemote);
    const fs::path linear = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(remote));
    GG_REQUIRE(s.openRepository(linear));
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    GG_REQUIRE(s.waitUntil([&] { return closed(s); }));
    GG_REQUIRE(s.waitIdle());
    // Most recent first, with branch, upstream and ahead/behind.
    GG_CHECK_STR_EQ(s.app.recentRowText(0), linear.string() + "  \xe2\x80\x94  main");
    GG_CHECK_STR_EQ(s.app.recentRowText(1),
        remote.string() + "  \xe2\x80\x94  main \xe2\x86\x92 origin/main \xe2\x86\x91" "1 \xe2\x86\x93" "1");

    // Click a recent entry.
    ctx->ItemClick("//Welcome/recent_1/###row");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->path() == remote; }));
    s.waitIdle();

    // Toolbar switcher: back to the other repository.
    s.comboSelect("//##Toolbar/##tb_repo", "###switch_1");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->path() == linear; }));
    s.waitIdle();

    // Repository ▸ Recent ▸ with a filter.
    ctx->MenuClick("//##MainMenuBar/Repository/Recent");
    const std::string menu = "//###Menu_01"; // submenu windows are "<label>###Menu_<depth>"
    ctx->ItemInputValue((menu + "/##recent_filter").c_str(), "with-remote");
    ctx->Yield(2);
    GG_CHECK(!ctx->ItemExists((menu + "/###recent_menu_0").c_str()));
    ctx->ItemClick((menu + "/###recent_menu_1").c_str());
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->path() == remote; }));
    s.waitIdle();

    // Delete forgets the focused entry.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_W);
    GG_REQUIRE(s.waitUntil([&] { return closed(s); }));
    ctx->Yield(2);
    const size_t before = s.app.settings().data().recent.size();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->NavMoveTo("//Welcome/recent_1/###row");
    ctx->KeyPress(ImGuiKey_Delete);
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    GG_CHECK(s.waitUntil([&] { return s.app.settings().data().recent.size() == before - 1; }, 5.0f));
    // The context menu can forget an entry too.
    s.contextMenu("//Welcome/recent_0/###row", "Forget");
    GG_CHECK(s.waitUntil([&] { return s.app.settings().data().recent.size() == before - 2; }, 5.0f));
}

GG_TEST("shell", "errors open a popup; warnings are corner notifications", "APP-OPEN-ERROR", "APP-ERROR-POPUP",
    "APP-ERROR-DISMISS", "APP-NOTIFY-TOAST")
{
    const fs::path notRepo = s.path("not-a-repo");
    fs::create_directories(notRepo);
    ctx->ItemInputValue("//Welcome/##welcome_path", notRepo.string().c_str());
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(s.app.errorTitle().find("Cannot open") != std::string::npos);
    // An error popup with the message, Copy message and OK.
    const std::string title = s.app.errorTitle();
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK(s.app.dialogs().current()->message == s.app.errorMessage());
    s.dialogButton(title.c_str(), "Copy message");
    GG_CHECK_STR_EQ(s.clipboard(), s.app.errorMessage());
    ctx->ItemInputValue("//Welcome/##welcome_path", notRepo.string().c_str());
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK(s.waitIdle());

    // Warnings: a toast in the corner, closed with its button or fading out on its own.
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.app.clearError();
    s.app.notify(ggui::App::Notice::Warning, "Test warning", "Something minor happened");
    ctx->Yield(2);
    GG_REQUIRE(s.app.toasts().size() == 1);
    const std::string toast = "//##toast_" + std::to_string(s.app.toasts().front().id);
    GG_CHECK(s.itemExists((toast + "/###toast_close").c_str()));
    GG_CHECK_STR_EQ(s.app.errorMessage(), "Something minor happened");
    ctx->ItemClick((toast + "/###toast_close").c_str());
    ctx->Yield(2);
    GG_CHECK(s.app.toasts().empty());
    // Hovering keeps a notification up: move the mouse away from the corner first.
    ctx->MouseMoveToPos(ImGui::GetMainViewport()->GetCenter());
    s.app.notify(ggui::App::Notice::Info, "Test info", "Fades out");
    GG_CHECK(s.waitUntil([&] { return s.app.toasts().empty(); }, 15.0f));
}

GG_TEST("shell", "repository kinds: bare, unborn, linked worktree, SHA-256, detached", "APP-OPEN-STATES",
    "TB-DETACHED", "HIST-WT-ROW")
{
    const fs::path bare = s.fixture(Recipe::Bare);
    GG_REQUIRE(s.openRepository(bare));
    GG_CHECK(s.session()->snapshot()->bare);
    GG_CHECK(!s.itemExists("//History/**/###row_wt"));
    GG_CHECK(s.session()->history().rows().size() == 4);

    const fs::path unborn = s.fixture(Recipe::Unborn);
    GG_REQUIRE(s.openRepository(unborn));
    GG_CHECK(s.session()->snapshot()->headUnborn);
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_branch"), "main");
    GG_CHECK(s.session()->history().rows().empty());
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->untracked.size() == 1; }));

    const fs::path wts = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (wts.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(wt1));
    GG_CHECK_STR_EQ(s.session()->snapshot()->worktreeId, wt1.filename().string());
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_branch"), "wt1");

    const fs::path sha = s.fixture(Recipe::Sha256);
    GG_REQUIRE(s.openRepository(sha));
    GG_CHECK_STR_EQ(s.session()->snapshot()->objectFormat, "sha256");
    GG_CHECK_EQ(s.session()->snapshot()->head.hex(), s.head(sha));
    GG_CHECK(s.session()->history().rows().size() == 4);

    const fs::path linear = s.fixture(Recipe::Linear);
    s.git(linear, {"switch", "-q", "--detach", "HEAD~1"});
    GG_REQUIRE(s.openRepository(linear));
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_branch"), "detached");
}

GG_TEST("shell", "repository state badge", "APP-STATE-DETECT", "TB-STATE-BADGE", "CONF-NATIVE-DETECT")
{
    const std::pair<Recipe, const char*> cases[] = {{Recipe::MidMerge, "MERGING"}, {Recipe::MidRebase, "REBASING"},
        {Recipe::MidRebaseApply, "REBASING"}, {Recipe::MidCherryPick, "CHERRY-PICKING"},
        {Recipe::MidRevert, "REVERTING"}, {Recipe::Bisecting, "BISECTING"}};
    for (const auto& [recipe, badge] : cases) {
        const fs::path repo = s.fixture(recipe);
        GG_REQUIRE(s.openRepository(repo));
        const std::string text = s.itemText("//##Toolbar/###tb_state");
        if (text.rfind(badge, 0) != 0)
            ctx->LogError("%s: badge '%s', expected '%s'", recipeName(recipe), text.c_str(), badge);
        GG_CHECK(text.rfind(badge, 0) == 0);
    }
    const auto snap = s.session()->snapshot();
    GG_CHECK(snap->state == ggui::core::RepoState::Bisecting);
    const fs::path rebase = s.fixture(Recipe::MidRebase, "rebase-detail");
    GG_REQUIRE(s.openRepository(rebase));
    GG_CHECK(s.session()->snapshot()->state == ggui::core::RepoState::RebasingInteractive);
    GG_CHECK_STR_EQ(s.session()->snapshot()->stateDetail, "1/1");
    const fs::path apply = s.fixture(Recipe::MidRebaseApply, "apply-detail");
    GG_REQUIRE(s.openRepository(apply));
    GG_CHECK(s.session()->snapshot()->state == ggui::core::RepoState::Rebasing);
    const fs::path clean = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(clean));
    GG_CHECK(!s.itemExists("//##Toolbar/###tb_state"));
}

GG_TEST("shell", "Repository menu: copy path, refresh, working directory, settings, quit", "MENU-REPO-COPY-PATH",
    "MENU-REPO-REFRESH", "MENU-REPO-REFRESH-KEY", "TB-REFRESH", "MENU-REPO-OPEN-WORKDIR", "TB-OPEN-FOLDER", "MENU-REPO-SETTINGS",
    "MENU-REPO-QUIT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path opened = s.fakeTool("xdg-open");
    GG_REQUIRE(s.openRepository(repo));
    ctx->MenuClick("//##MainMenuBar/Repository/Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), repo.string());

    auto generation = [&] { return s.session()->snapshot()->generation; };
    std::uint64_t g = generation();
    ctx->MenuClick("//##MainMenuBar/Repository/Refresh");
    GG_CHECK(s.waitUntil([&] { return generation() > g; }));
    g = generation();
    ctx->KeyPress(ImGuiKey_F5);
    GG_CHECK(s.waitUntil([&] { return generation() > g; }));
    g = generation();
    ctx->ItemClick("//##Toolbar/###tb_refresh");
    GG_CHECK(s.waitUntil([&] { return generation() > g; }));

    ctx->MenuClick("//##MainMenuBar/Repository/Open working directory");
    GG_CHECK(s.waitUntil([&] {
        return s.read(opened.parent_path(), opened.filename().string()).find(repo.string()) != std::string::npos;
    }));
    // The toolbar folder button opens it too.
    s.write(opened.parent_path(), opened.filename().string(), "");
    ctx->ItemClick("//##Toolbar/###tb_open");
    GG_CHECK(s.waitUntil([&] {
        return s.read(opened.parent_path(), opened.filename().string()).find(repo.string()) != std::string::npos;
    }));

    ctx->MenuClick("//##MainMenuBar/Repository/Settings...");
    ctx->Yield(2);
    GG_CHECK(s.app.settingsOpen());
    GG_CHECK(ctx->GetWindowByRef("//Settings") != nullptr);

    const int quits = s.app.quitRequests();
    ctx->MenuClick("//##MainMenuBar/Repository/Quit");
    GG_CHECK_EQ(s.app.quitRequests(), quits + 1);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Q);
    GG_CHECK_EQ(s.app.quitRequests(), quits + 2);
    GG_CHECK(s.waitIdle());
}

GG_TEST("shell", "View menu: panels, next/previous changed file, reset layout", "MENU-VIEW-TOGGLE-PANEL",
    "MENU-VIEW-RESET-LAYOUT", "MENU-VIEW-NEXT-FILE", "MENU-VIEW-NEXT-FILE-KEY", "MENU-VIEW-PREV-FILE",
    "MENU-VIEW-PREV-FILE-KEY")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    auto& panels = s.app.settings().data().panels;
    ctx->MenuClick("//##MainMenuBar/View/History");
    ctx->Yield(2);
    GG_CHECK(!panels["History"]);
    GG_CHECK(!ctx->GetWindowByRef("//History")->Active);
    ctx->MenuClick("//##MainMenuBar/View/History");
    ctx->Yield(2);
    GG_CHECK(panels["History"]);

    // Next / previous changed file (menu and keys).
    auto current = [&]() -> std::string {
        const auto* r = s.session()->changes().current();
        return r ? r->path : std::string();
    };
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    const auto& rows = s.session()->changes().rows();
    const std::string first = rows[0].path;
    const std::string second = rows[1].path;
    ctx->MenuClick("//##MainMenuBar/View/Next changed file");
    GG_CHECK_STR_EQ(current(), first);
    ctx->KeyPress(ImGuiKey_F6);
    GG_CHECK_STR_EQ(current(), second);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F6);
    GG_CHECK_STR_EQ(current(), first);
    ctx->KeyPress(ImGuiKey_F6);
    ctx->MenuClick("//##MainMenuBar/View/Previous changed file");
    GG_CHECK_STR_EQ(current(), first);

    // Hide Diff, show and undock Blame, then reset the layout: Diff back, Blame hidden again.
    ctx->MenuClick("//##MainMenuBar/View/Diff");
    ctx->Yield(2);
    s.showPanel("Blame");
    ctx->DockClear("Blame", nullptr);
    ctx->Yield(2);
    GG_CHECK(dockOf(ctx, "//Blame") == nullptr);
    ctx->MenuClick("//##MainMenuBar/View/Reset layout");
    ctx->Yield(4);
    GG_CHECK(panels["Diff"]);
    GG_CHECK(!panels["Blame"]);
    s.showPanel("Blame");
    ctx->Yield(2);
    GG_CHECK(dockOf(ctx, "//Diff") != nullptr && dockOf(ctx, "//Blame") == dockOf(ctx, "//Diff"));
}

GG_TEST("shell", "settings persist across restarts", "SET-SCALE", "SET-THEME", "APP-LOG-FILE")
{
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/UI scale##scale", 150);
    ctx->Yield(2);
    s.comboSelect("//Settings/##settings_tabs/General/Theme##theme", "Light");
    GG_REQUIRE(s.waitIdle());
    GG_CHECK(s.app.settings().data().uiScale > 1.49f && s.app.settings().data().uiScale < 1.51f);
    // A fresh ggui process (a restart) reads them back.
    const fs::path log = s.path("restart.log");
    auto r = s.runGgui({"--smoke"}, {{"GGUI_LOG_FILE", log.string()}});
    GG_CHECK(r.ok());
    const std::string text = s.read(log.parent_path(), log.filename().string());
    GG_CHECK(text.find("scale=1.50 theme=light") != std::string::npos);
}

GG_TEST("shell", "auto-open argv[1], else the most recent existing repository", "APP-AUTOOPEN-ARG",
    "APP-AUTOOPEN-RECENT", "APP-LOG-FILE", "HARNESS-SMOKE")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path log1 = s.path("first.log");
    auto r = s.runGgui({"--smoke", repo.string()}, {{"GGUI_LOG_FILE", log1.string()}});
    GG_CHECK(r.ok());
    GG_CHECK(s.read(s.root(), "first.log").find("opening " + repo.string()) != std::string::npos);
    // Now the settings list it as recent; a missing repository in front is skipped.
    const fs::path prefs = s.root() / "prefs";
    std::string json = s.read(prefs, "settings.json");
    const auto pos = json.find("\"recent\": [");
    GG_REQUIRE(pos != std::string::npos);
    json.insert(pos + 11, "\"" + (s.root() / "gone").generic_string() + "\", ");
    s.write(prefs, "settings.json", json);
    const fs::path log2 = s.path("second.log");
    r = s.runGgui({"--smoke"}, {{"GGUI_LOG_FILE", log2.string()}});
    if (!r.ok())
        ctx->LogError("second run: %s\n%s", r.message().c_str(), r.err.c_str());
    GG_CHECK(r.ok());
    const std::string text = s.read(s.root(), "second.log");
    GG_CHECK(text.find("opening " + repo.string()) != std::string::npos);
    GG_CHECK(text.find("opening " + (s.root() / "gone").string()) == std::string::npos);
}

GG_TEST("shell", "toolbar HEAD: plain text, copy short or full ID", "TB-HEAD-PLAIN", "TB-HEAD-COPY",
    "APP-COPY-ID-SHIFT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string shortHead = s.gitOut(repo, {"rev-parse", "--short", "HEAD"});
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_head"), shortHead);
    // Plain text: clicking it neither selects anything nor highlights it.
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    ctx->ItemClick("//##Toolbar/###tb_head");
    ctx->ItemClick("//##Toolbar/###tb_branch");
    ctx->Yield(3);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    GG_CHECK(ImGui::GetActiveID() == 0);
    ctx->MouseMove("//##Toolbar/###tb_head");
    GG_CHECK(!s.itemDrawsBackground("//##Toolbar/###tb_head") && !s.itemDrawsBackground("//##Toolbar/###tb_branch"));
    // Right-click still offers Copy ID: short by default, full with Shift.
    ctx->ItemClick("//##Toolbar/###tb_head", ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/Copy ID");
    GG_CHECK_STR_EQ(s.clipboard(), shortHead);
    ctx->ItemClick("//##Toolbar/###tb_head", ImGuiMouseButton_Right);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->MenuClick("//$FOCUSED/Copy ID");
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.clipboard(), s.head(repo));
}

GG_TEST("shell", "activity spinner, task tooltip and Cancel", "TB-SPINNER", "TB-CANCEL", "TB-TASK-TOOLTIP",
    "APP-CANCEL-LONG-OPS")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    gg::setSlowGitLatency(std::chrono::milliseconds(10000));
    ctx->KeyPress(ImGuiKey_F6); // selects a file: the diff request is now slow
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//##Toolbar/##tb_activity"); }, 5.0f));
    ctx->MouseMove("//##Toolbar/##tb_activity");
    ctx->Yield(3);
    ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_CHECK(tip != nullptr && tip->Active);
    GG_CHECK(!s.session()->activities().empty());
    ctx->ItemClick("//##Toolbar/Cancel##tb_cancel");
    GG_CHECK(s.waitUntil([&] { return s.session()->activities().empty(); }, 5.0f));
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(s.waitUntil([&] { return !s.itemExists("//##Toolbar/##tb_activity"); }, 5.0f));
}

GG_TEST("shell", "unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees",
    "APP-STATE-DETECT", "TB-STATE-BADGE", "APP-OPEN-STATES", "REM-LIST", "TAG-FILTER", "WT-LIST")
{
    // a.txt: c1 "1", c2 "2", c3 "3"; "side" changes it to "s" (s1) and adds s.txt (s2).
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "a.txt", "1\n", "c1");
    const std::string c1 = s.head(repo);
    s.commitFile(repo, "a.txt", "2\n", "c2");
    const std::string c2 = s.head(repo);
    s.commitFile(repo, "a.txt", "3\n", "c3");
    s.git(repo, {"switch", "-q", "-c", "side", c1});
    s.commitFile(repo, "a.txt", "s\n", "s1");
    const std::string s1 = s.head(repo);
    s.commitFile(repo, "s.txt", "s\n", "s2");
    const std::string s2 = s.head(repo);
    s.git(repo, {"switch", "-q", "main"});
    auto snap = [&] { return s.session()->snapshot(); };

    // A cherry-pick of two commits, the conflicted first one committed by hand: git is between
    // commits (no CHERRY_PICK_HEAD, the sequence remains).
    GG_CHECK(!s.gitMayFail(repo, {"cherry-pick", s1, s2}).ok());
    s.write(repo, "a.txt", "3s\n");
    s.git(repo, {"add", "a.txt"});
    s.git(repo, {"commit", "-q", "--no-edit"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "CHERRY_PICK_HEAD"));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return snap()->state == ggui::core::RepoState::CherryPicking; }));
    s.git(repo, {"cherry-pick", "--continue"});
    GG_CHECK(s.waitUntil([&] { return snap()->state == ggui::core::RepoState::None; }));

    // The same for a revert of two commits.
    GG_CHECK(!s.gitMayFail(repo, {"revert", "--no-edit", c2, s2}).ok());
    s.write(repo, "a.txt", "reverted\n");
    s.git(repo, {"add", "a.txt"});
    s.git(repo, {"commit", "-q", "--no-edit"});
    GG_CHECK(s.waitUntil([&] { return snap()->state == ggui::core::RepoState::Reverting; }));
    s.git(repo, {"revert", "--quit"});

    // git rebase (not -i) of a detached HEAD stopped by a conflict.
    s.git(repo, {"switch", "-q", "--detach", c1});
    s.commitFile(repo, "a.txt", "detached\n", "d1");
    GG_CHECK(!s.gitMayFail(repo, {"rebase", "main"}).ok());
    GG_CHECK(s.waitUntil([&] { return snap()->state == ggui::core::RepoState::RebasingInteractive; }));
    GG_CHECK_STR_EQ(snap()->stateOnto, "detached HEAD");
    GG_CHECK(s.itemText("//##Toolbar/###tb_state").rfind("REBASING", 0) == 0);
    s.git(repo, {"rebase", "--abort"});
    s.git(repo, {"switch", "-q", "main"});

    // Remotes: one with only a push URL, one with no URL at all (not listed).
    s.git(repo, {"config", "remote.pushonly.pushurl", (s.root() / "nowhere.git").string()});
    s.git(repo, {"config", "remote.nourl.fetch", "+refs/heads/*:refs/remotes/nourl/*"});
    // Tags: an annotated tag of a tree, one without a message.
    s.git(repo, {"tag", "-a", "-m", "a tree", "treetag", "HEAD^{tree}"});
    const std::string tagId = s.git(repo, {"mktag"},
        "object " + c1 + "\ntype commit\ntag nomsg\ntagger T <t@example.com> 0 +0000\n").out;
    s.git(repo, {"update-ref", "refs/tags/nomsg", gg::trim(tagId)});
    // A refs/stash written by hand: a plain commit, not git stash's merge.
    s.git(repo, {"update-ref", "--create-reflog", "-m", "hand-made stash", "refs/stash", c2});
    // A linked worktree on an orphan branch (its HEAD names a branch with no commits yet).
    const fs::path orphan = s.root() / "orphan-wt";
    s.git(repo, {"worktree", "add", "-q", "--detach", orphan.string()});
    s.git(orphan, {"checkout", "-q", "--orphan", "fresh"});
    // A file replaced by a symlink (a type change).
    fs::remove(repo / "a.txt");
    fs::create_symlink("s.txt", repo / "a.txt");
    ctx->KeyPress(ImGuiKey_F5);
    GG_REQUIRE(s.waitUntil([&] { return snap()->tags.size() == 2 && snap()->worktrees.size() == 2; }));
    std::vector<std::string> remotes;
    for (const auto& r : snap()->remotes)
        remotes.push_back(r.name + " [" + r.url + "] [" + r.pushUrl + "]");
    GG_CHECK(remotes == (std::vector<std::string>{"pushonly [] [" + (s.root() / "nowhere.git").string() + "]"}));
    for (const auto& t : snap()->tags) {
        GG_CHECK(t.annotated);
        if (t.name == "treetag")
            GG_CHECK_STR_EQ(t.target.hex(), s.revParse(repo, "HEAD^{tree}"));
        else
            GG_CHECK(t.message.empty());
    }
    GG_REQUIRE(snap()->stashes.size() == 1u);
    GG_CHECK_STR_EQ(snap()->stashes[0].base.hex(), c1);
    GG_CHECK(!snap()->stashes[0].hasIndexChanges);
    GG_CHECK_STR_EQ(snap()->worktrees[1].branch, "fresh");
    GG_CHECK(snap()->worktrees[1].head.isNull());
    s.showPanel("Remotes");
    GG_CHECK(s.textShown("//Remotes", "pushonly"));
    s.showPanel("Worktrees");
    GG_CHECK(s.textShown("//Worktrees", "fresh"));
    GG_CHECK(s.waitUntil([&] {
        const auto st = s.session()->status();
        return st && !st->unstaged.empty() && st->unstaged[0].kind == ggui::core::ChangeKind::TypeChanged;
    }));
    fs::remove(repo / "a.txt");
    s.git(repo, {"checkout", "a.txt"});
    s.git(repo, {"update-ref", "-d", "refs/stash"});
    s.git(repo, {"worktree", "remove", "--force", orphan.string()});
}

} // namespace ggtest
