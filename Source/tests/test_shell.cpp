// Application shell, Welcome screen, menus, toolbar, layout and settings (§4.1).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>

#include <SDL3/SDL_events.h>
#include <imgui_internal.h>

#include <fstream>

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
    "LAYOUT-DEFAULT", "LAYOUT-HIDDEN-PANELS", "LAYOUT-TAB-ORDER", "TB-BRANCH", "MENU-REPO-CLOSE", "APP-OPEN-STATES")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.session() && s.session()->opened());
    GG_CHECK_STR_EQ(s.itemText("//##Toolbar/###tb_branch"), "main");
    // Default dock layout: Branches|Tags, Remotes|Stashes|Worktrees, History, Changes,
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
    // Tabs in that node: Remotes, Stashes, Worktrees.
    if (ImGuiDockNode* node = dockOf(ctx, "//Remotes"); node && node->TabBar) {
        std::vector<std::string> tabs;
        for (const ImGuiTabItem& tab : node->TabBar->Tabs)
            if (tab.Window)
                tabs.emplace_back(tab.Window->Name);
        GG_CHECK(tabs == (std::vector<std::string>{"Remotes", "Stashes", "Worktrees"}));
    } else {
        GG_CHECK(false && "no tab bar for Remotes");
    }
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

GG_TEST("shell", "folders dropped on the window: the first opens, the repositories among them join the recent list",
    "APP-DROP-FOLDER", "APP-DROP-FOLDERS")
{
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    const fs::path plain = s.path("plain-folder");
    fs::create_directories(plain);
    s.write(s.root(), "a-file.txt", "not a folder\n");
    s.track(first);
    s.track(second);
    // One drop as SDL delivers it: BEGIN, a FILE per path, COMPLETE. The paths must outlive the
    // frames that poll the events.
    std::vector<std::string> paths;
    auto drop = [&](std::vector<std::string> dropped) {
        paths = std::move(dropped);
        SDL_Event ev{};
        ev.type = SDL_EVENT_DROP_BEGIN;
        SDL_PushEvent(&ev);
        for (const auto& p : paths) {
            SDL_Event file{};
            file.type = SDL_EVENT_DROP_FILE;
            file.drop.data = p.c_str();
            SDL_PushEvent(&file);
        }
        SDL_Event done{};
        done.type = SDL_EVENT_DROP_COMPLETE;
        SDL_PushEvent(&done);
        ctx->Yield(3);
    };
    drop({first.string(), plain.string(), second.string(), (s.root() / "a-file.txt").string()});
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    GG_CHECK(fs::equivalent(s.session()->path(), first));
    s.settle();
    const auto& recent = s.app.settings().data().recent;
    GG_REQUIRE(recent.size() >= 2);
    GG_CHECK(fs::equivalent(recent[0], first));
    GG_CHECK(fs::equivalent(recent[1], second));
    for (const auto& r : recent)
        GG_CHECK(!fs::equivalent(r, plain));
    // One folder: it opens.
    drop({second.string()});
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), second); }));
    s.settle();
    GG_CHECK(fs::equivalent(s.app.settings().data().recent.front(), second));
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
    const fs::path opened = s.fakeTool(kFileManager);
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
    // A file replaced by a symlink (a type change). Windows makes symlinks only with Developer
    // Mode (or as administrator), and git there keeps them as files unless core.symlinks is set.
    std::error_code symlinkError;
    fs::remove(repo / "a.txt");
    fs::create_symlink("s.txt", repo / "a.txt", symlinkError);
#ifdef _WIN32
    const bool typeChange = false;
#else
    const bool typeChange = !symlinkError;
#endif
    if (!typeChange)
        s.git(repo, {"checkout", "a.txt"});
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
    if (typeChange)
        GG_CHECK(s.waitUntil([&] {
            const auto st = s.session()->status();
            return st && !st->unstaged.empty() && st->unstaged[0].kind == ggui::core::ChangeKind::TypeChanged;
        }));
    fs::remove(repo / "a.txt");
    s.git(repo, {"checkout", "a.txt"});
    s.git(repo, {"update-ref", "-d", "refs/stash"});
    s.git(repo, {"worktree", "remove", "--force", orphan.string()});
}

GG_TEST("shell", "while a mutation runs every menu disables what would conflict; browsing still works",
    "TB-REMOTE-BUSY")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "other", "HEAD~1"});
    s.git(repo, {"tag", "-a", "-m", "v1", "v1"});
    s.write(repo, "stashed.txt", "stash me\n");
    s.git(repo, {"add", "stashed.txt"});
    s.git(repo, {"stash", "push", "-q", "-m", "a stash"});
    s.write(repo, "staged.txt", "staged\n");
    s.git(repo, {"add", "staged.txt"});
    s.write(repo, "untracked.txt", "untracked\n");
    const std::string tracked = gg::splitLines(s.gitOut(repo, {"ls-files"})).front();
    s.write(repo, tracked, s.read(repo, tracked) + "unstaged\n");
    // The commit waits in pre-commit until the test lets it go.
    const fs::path go = s.root() / "go";
    s.write(repo / ".git" / "hooks", "pre-commit", "#!/bin/sh\nwhile [ ! -f '" + go.string() + "' ]; do sleep 0.05; done\n");
    fs::permissions(repo / ".git" / "hooks" / "pre-commit", fs::perms::owner_all);
    GG_REQUIRE(s.openRepository(repo));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); // an operation for the Operations panel
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->operations().empty(); }));
    s.settle();
    const std::string head = s.head(repo);
    const std::string refsBefore = s.gitOut(repo, {"for-each-ref"});

    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Waits for the hook");
    s.dialogButton("Commit", "Commit");
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->actions().busy().empty(); }, 10.0f));

    // Opens the context menu of `ref`, checks that `item` is disabled, closes it.
    auto menu = [&](const std::string& ref, const char* item) {
        ctx->ItemClick(ref.c_str(), ImGuiMouseButton_Right);
        ctx->Yield(2);
        if (item) {
            const ImGuiTestItemInfo info = ctx->ItemInfo((std::string("//$FOCUSED/") + item).c_str());
            if (!(info.ItemFlags & ImGuiItemFlags_Disabled))
                ctx->LogError("'%s' in the menu of %s is enabled while busy", item, ref.c_str());
            GG_CHECK((info.ItemFlags & ImGuiItemFlags_Disabled) != 0);
        }
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
    };
    const std::string files = s.child("//Changes", "##files");
    auto fileRef = [&](const char* group, const std::string& path) { return files + "/" + group + "/" + path + "/###file_" + path; };
    menu("//History/**/###row_wt", "Stage all");
    menu(fileRef("Staged", "staged.txt"), "Unstage");
    menu(fileRef("Unstaged", tracked), "Stage");
    menu(fileRef("Untracked", "untracked.txt"), "Intent to add");
    // Space (stage/unstage) waits too.
    ctx->ItemClick(fileRef("Unstaged", tracked).c_str());
    ctx->KeyPress(ImGuiKey_Space);
    ctx->ItemClick(fileRef("Unstaged", tracked).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((s.child("//Diff", "##diff_body") + "/###line_1").c_str()); }));
    menu(s.child("//Diff", "##diff_body") + "/###line_1", "Stage line(s)");
    ctx->ItemClick(fileRef("Staged", "staged.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == "staged.txt" && s.itemExists((s.child("//Diff", "##diff_body") + "/###line_1").c_str());
    }));
    menu(s.child("//Diff", "##diff_body") + "/###line_1", "Unstage line(s)");
    // A commit: its row, its files, its lines, its author; the Commit menu.
    const std::string older = s.revParse(repo, "HEAD~1");
    ctx->ItemClick(("//History/**/###row_" + older).c_str());
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), older); // browsing works
    menu("//History/**/###row_" + older, "Duplicate");
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    const std::string first = s.session()->changes().rows().front().path;
    menu(files + "/" + first + "/###file_" + first, "Move to parent");
    ctx->ItemClick((files + "/" + first + "/###file_" + first).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((s.child("//Diff", "##diff_body") + "/###line_1").c_str()); }));
    menu(s.child("//Diff", "##diff_body") + "/###line_1", "Revert line(s)");
    menu("//Change information/**/###author", "Edit author...");
    ctx->MenuAction(ImGuiTestAction_Hover, "//##MainMenuBar/Commit/Commit...");
    GG_CHECK((ctx->ItemInfo("//$FOCUSED/Commit...").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->KeyPress(ImGuiKey_Escape);
    // Keys do nothing either.
    ctx->ItemClick(("//History/**/###row_" + older).c_str());
    ctx->KeyPress(ImGuiKey_D);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    // The side panels.
    s.showPanel("Branches");
    menu("//Branches/branch_main/###branch_main", "Push");
    menu("//Branches/branch_other/###branch_other", "Check out");
    s.showPanel("Tags");
    menu("//Tags/tag_v1/###tag_v1", nullptr);
    s.showPanel("Stashes");
    menu("//Stashes/stash_0/###row", "Apply (restore index)");
    s.showPanel("Remotes");
    menu("//Remotes/remote_origin/###row", nullptr);
    s.showPanel("Reflog");
    menu("//Reflog/##reflog_table/r0/###reflog_0", "Create branch from new...");
    s.showPanel("Operations");
    const std::string opRow = s.child("//Operations", "##ops_table") + "/**/op_" + s.session()->operations().back().id + "/###row";
    menu(opRow, "Restore (undo this operation)");

    // Let the hook go: the commit lands; nothing else happened meanwhile.
    s.write(s.root(), "go", "");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != head; }, 30.0f));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s"}), "Waits for the hook");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--name-only"}), tracked); // Space did not stage it
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), head);
    GG_CHECK_EQ(gg::splitLines(s.gitOut(repo, {"for-each-ref"})).size(), gg::splitLines(refsBefore).size());
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("shell", "settings files from elsewhere: wrong types, not an object, not JSON; out-of-range values are clamped",
    "SET-SCALE", "SET-THEME", "APP-LOG-FILE")
{
    // A fresh ggui (a restart) reads each settings.json; what it cannot use falls back to defaults.
    auto start = [&](const std::string& name, const std::string& json) {
        const fs::path dir = s.path("prefs-" + name);
        s.write(dir, "settings.json", json);
        const fs::path log = s.path(name + ".log");
        auto r = s.runGgui({"--smoke"}, {{"GGUI_PREF_PATH", dir.string()}, {"GGUI_LOG_FILE", log.string()}});
        GG_CHECK(r.ok());
        return s.read(log.parent_path(), log.filename().string());
    };
    GG_CHECK(start("array", "[1, 2]").find("scale=1.00 theme=dark recent=0") != std::string::npos);
    GG_CHECK(start("broken", "{\"uiScale\": ").find("ignoring unreadable settings.json") != std::string::npos);
    const std::string mixed = start("mixed",
        R"({"uiScale": 9, "theme": "light", "recent": [1, "/nowhere", null], "repos": {"/r": {"hooks": "not-now"},
            "/s": {"hooks": "never"}, "/t": {"hooks": "installed", "ignoreOldGgRefs": true}},
            "panels": {"History": true, "Diff": "yes"}, "diff": {"sideBySide": true, "context": 500, "whitespace": 9},
            "nothingStaged": "stage-selected", "window": {"x": 10, "y": 20, "w": 800, "h": 600, "maximized": true}})");
    GG_CHECK(mixed.find("scale=3.00 theme=light recent=1") != std::string::npos);
    const std::string types = start("types", R"({"recent": 5, "repos": [], "panels": 1, "diff": "x", "window": 2,
        "nothingStaged": "stage-all"})");
    GG_CHECK(types.find("scale=1.00 theme=dark recent=0") != std::string::npos);
}

GG_TEST("shell", "command line: --list-tests, --headless, unknown options and a second path are reported", "APP-AUTOOPEN-ARG",
    "HARNESS-SMOKE")
{
    auto r = s.runGgui({"--list-tests"});
    GG_CHECK(r.ok());
    GG_CHECK(r.out.find("command line: --list-tests") != std::string::npos);
    const fs::path repo = s.fixture(Recipe::Linear);
    r = s.runGgui({"--headless", "--bogus", repo.string(), "second-path", "--smoke"});
    GG_CHECK(r.ok());
    GG_CHECK(r.err.find("ggui: unknown option --bogus") != std::string::npos);
    GG_CHECK(r.err.find("ggui: unknown option second-path") != std::string::npos);
    // No usable display, or no usable GPU driver: ggui says why and exits.
    r = s.runGgui({"--smoke"}, {{"SDL_VIDEO_DRIVER", "no-such-driver"}});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.err.find("SDL_Init failed") != std::string::npos);
    r = s.runGgui({"--smoke"}, {{"SDL_GPU_DRIVER", "no-such-driver"}});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.err.find("SDL_CreateGPUDevice failed") != std::string::npos);
}

GG_TEST("shell", "toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress",
    "TB-PUSH", "TB-HEAD-PLAIN", "ACT-NEW-MERGE", "CONF-NATIVE-PROGRESS")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    // Force with lease against the upstream asks first; Push tags goes to the upstream's remote.
    ctx->ItemClick("//##Toolbar/###tb_push_menu");
    ctx->ItemClick("//$FOCUSED/Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    GG_CHECK(s.app.dialogs().current()->message.find("(--force-with-lease)") != std::string::npos);
    s.dialogButton("Force push", "Cancel");
    s.git(repo, {"tag", "pushed-tag"});
    ctx->ItemClick("//##Toolbar/###tb_push_menu");
    ctx->ItemClick("//$FOCUSED/Push tags");
    const fs::path remote = fs::path(s.gitOut(repo, {"remote", "get-url", "origin"}).substr(7));
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(remote, {"rev-parse", "-q", "--verify", "refs/tags/pushed-tag"}).ok(); }));
    s.settle();
    // Without an upstream, Force with lease is a Push to.
    s.git(repo, {"switch", "-q", "-c", "local-only"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "local-only"; }));
    ctx->ItemClick("//##Toolbar/###tb_push_menu");
    ctx->ItemClick("//$FOCUSED/Force with lease...");
    GG_REQUIRE(s.dialogOpen("Push to"));
    s.dialogButton("Push to", "Cancel");
    // HEAD's full ID on hover.
    ctx->MouseMove("//##Toolbar/###tb_head");
    ctx->SleepNoSkip(1.0f, 0.1f);
    // Two commits selected (Ctrl+click): New makes a merge of both.
    const std::string a = s.head(repo), b = s.revParse(repo, "origin/main");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + b).c_str()); }));
    ctx->ItemClick(("//History/**/###row_" + a).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(("//History/**/###row_" + b).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "HEAD^2"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^1"), a);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^2"), b);
    // A rebase of a detached HEAD, stopped: the progress view names it.
    s.git(repo, {"switch", "-q", "--detach", "HEAD~1"});
    const fs::path list = s.root() / "todo.txt";
    std::ofstream(list) << "edit " << s.head(repo) << "\n";
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "cp '" + list.generic_string() + "'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~1"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//##Toolbar/Progress##tb_rebase_progress"); }));
    ctx->ItemClick("//##Toolbar/Progress##tb_rebase_progress");
    ctx->Yield(2);
    GG_CHECK(s.textShown("//$FOCUSED", "Rebasing detached"));
    ctx->KeyPress(ImGuiKey_Escape);
    s.git(repo, {"rebase", "--abort"});
}

GG_TEST("shell", "recent repositories whose state changed: upstream gone, unborn branch with an upstream, no longer a repository",
    "APP-WELCOME-RECENT-INFO")
{
    const fs::path gone = s.fixture(Recipe::WithRemote, "upstream-gone");
    const fs::path unborn = s.fixture(Recipe::Empty, "unborn-upstream");
    const fs::path notRepo = s.path("was-a-repo"); // not tracked: it stops being a repository
    s.git(s.root(), {"init", "-q", "-b", "main", notRepo.string()});
    s.commitFile(notRepo, "a.txt", "a\n", "first");
    for (const fs::path& p : {gone, unborn, notRepo})
        GG_REQUIRE(s.openRepository(p));
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    GG_REQUIRE(s.waitUntil([&] { return closed(s); }));
    s.git(gone, {"remote", "set-head", "origin", "-d"});
    s.git(gone, {"update-ref", "-d", "refs/remotes/origin/main"});
    s.git(unborn, {"config", "branch.main.remote", "origin"});
    s.git(unborn, {"config", "branch.main.merge", "refs/heads/main"});
    fs::rename(notRepo / ".git", s.path("was-a-repo.git")); // back at the end
    // Opening Welcome again reads the summaries anew.
    GG_REQUIRE(s.openRepository(gone));
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    GG_REQUIRE(s.waitUntil([&] { return closed(s); }));
    GG_REQUIRE(s.waitIdle());
    GG_CHECK(s.waitUntil([&] { return s.app.recentRowText(0).find("main \xe2\x86\x92 origin/main") != std::string::npos; }));
    GG_CHECK(s.app.recentRowText(0).find("\xe2\x86\x91") == std::string::npos); // no ahead/behind without the ref
    GG_CHECK(s.app.recentRowText(1).rfind(notRepo.string(), 0) == 0);
    GG_CHECK(s.app.recentRowText(2).find("  \xe2\x80\x94  main") != std::string::npos);
    fs::rename(s.path("was-a-repo.git"), notRepo / ".git");
}

} // namespace ggtest
