// Screenshot gallery for reviewing the look (theme, icons, layout): menus, popups, dialogs and
// panels in both themes and at two UI scales, written to <artifacts>/screens/gallery-*.png.
// Manual: not part of the suite, run it with --test=gallery.
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/BlamePanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

void shot(Scenario& s, const std::string& name)
{
    s.ctx->Yield(3);
    s.screenshot("gallery-" + name);
}

bool openWorking(Scenario& s, fs::path* out = nullptr)
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    if (out)
        *out = repo;
    if (!s.openRepository(repo))
        return false;
    if (!s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }))
        return false;
    // Select the working tree row so the Changes panel shows the staged/unstaged groups.
    if (!s.waitUntil([&] { return s.itemExists("//History/**/###row_wt"); }))
        return false;
    s.ctx->Yield(10);
    for (int attempt = 0; attempt < 3; ++attempt) {
        s.ctx->ItemClick("//History/**/###row_wt");
        if (s.waitUntil([&] { return s.session()->selection().kind == ggui::SelKind::WorkingTree; }, 3.0f))
            break;
    }
    s.ctx->Yield(3);
    return s.session()->selection().kind == ggui::SelKind::WorkingTree;
}

void closePopups(Scenario& s)
{
    s.ctx->PopupCloseAll();
    s.ctx->Yield(3);
}

void menuShot(Scenario& s, const char* menu)
{
    s.ctx->MenuAction(ImGuiTestAction_Open, (std::string("//##MainMenuBar/") + menu).c_str());
    s.ctx->Yield(3);
    shot(s, std::string("menu-") + menu);
    closePopups(s);
}

void setLook(Scenario& s, const char* theme, int scale)
{
    s.app.openSettings();
    s.ctx->Yield(2);
    s.ctx->ItemClick("//Settings/##settings_tabs/General");
    s.ctx->SetRef("Settings");
    s.comboSelect("//Settings/##settings_tabs/General/Theme##theme", theme);
    s.ctx->SetRef("");
    s.ctx->ItemInputValue("//Settings/##settings_tabs/General/UI scale##scale", scale);
    s.ctx->Yield(2);
}

} // namespace

GG_MANUAL_TEST("gallery", "welcome, settings tabs")
{
    s.ctx->Yield(5);
    shot(s, "welcome");
    s.app.openSettings();
    ctx->Yield(2);
    for (const char* tab : {"General", "Git"}) {
        ctx->ItemClick((std::string("//Settings/##settings_tabs/") + tab).c_str());
        shot(s, std::string("settings-") + tab);
    }
}

GG_MANUAL_TEST("gallery", "main menus and context menus")
{
    fs::path repo;
    GG_REQUIRE(openWorking(s, &repo));
    s.settle();
    shot(s, "main-100");
    for (const char* m : {"Repository", "Commit", "Edit", "View"})
        menuShot(s, m);
    // A submenu.
    ctx->MenuAction(ImGuiTestAction_Open, "//##MainMenuBar/Commit");
    ctx->Yield(2);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Selected commit");
    ctx->Yield(3);
    shot(s, "menu-submenu-commit-selected");
    closePopups(s);
    ctx->MenuAction(ImGuiTestAction_Open, "//##MainMenuBar/Repository");
    ctx->Yield(2);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Recent");
    ctx->Yield(3);
    shot(s, "menu-submenu-repository-recent");
    closePopups(s);

    // History row menu (the row of the HEAD commit) with a submenu.
    const std::string head = s.head(repo);
    const std::string row = "//History/**/###row_" + head;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->ItemClick(row.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "ctx-history-row");
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Copy");
    ctx->Yield(3);
    shot(s, "ctx-history-row-submenu");
    closePopups(s);

    // Branches panel.
    s.showPanel("Branches");
    ctx->ItemClick("//Branches/branch_main/###branch_main", ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "ctx-branch");
    closePopups(s);

    // Changes file (the history menu selected the HEAD commit: select the working tree again).
    ctx->ItemClick("//History/**/###row_wt");
    ctx->Yield(5);
    const std::string file = "//Changes/**/###file_b.txt";
    ctx->ItemClick(file.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "ctx-changes-file");
    closePopups(s);

    // Diff line.
    ctx->ItemClick(file.c_str());
    s.showPanel("Diff");
    s.settle();
    GG_REQUIRE(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == "b.txt";
    }));
    const std::string body = s.child("//Diff", "##diff_body");
    ctx->LogInfo("diff body ref: %s", body.c_str());
    ctx->ItemClick((body + "/###line_2").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "ctx-diff-line");
    closePopups(s);
}

GG_MANUAL_TEST("gallery", "toolbar dropdowns and tooltip")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    for (const char* n : {"fetch", "pull", "push"}) {
        const std::string b = std::string("//###Toolbar/###tb_") + n + "_menu";
        ctx->ItemClick(b.c_str());
        ctx->Yield(3);
        shot(s, std::string("toolbar-") + n + "-menu");
        closePopups(s);
    }
    ctx->MouseMove("//###Toolbar/###tb_commit");
    ctx->SleepNoSkip(1.2f, 0.1f);
    shot(s, "tooltip-toolbar-commit");
    ctx->MouseMove("//###Toolbar/###tb_push");
    ctx->SleepNoSkip(1.2f, 0.1f);
    shot(s, "tooltip-toolbar-push");
}

GG_MANUAL_TEST("gallery", "dialogs")
{
    fs::path repo;
    GG_REQUIRE(openWorking(s, &repo));
    s.git(repo, {"branch", "old-topic"});
    s.settle();
    auto dialog = [&](const char* title, const char* name, const std::function<void()>& open) {
        open();
        if (!s.dialogOpen(title)) {
            ctx->LogError("gallery: dialog %s did not open", title);
            return;
        }
        shot(s, name);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
        if (s.itemExists((std::string("//") + title + "/Cancel").c_str()))
            s.dialogButton(title, "Cancel");
    };
    dialog("Commit", "dialog-commit", [&] { ctx->ItemClick("//###Toolbar/###tb_commit"); });
    dialog("Stash changes", "dialog-stash", [&] { ctx->ItemClick("//###Toolbar/###tb_stash"); });
    s.showPanel("Branches");
    dialog("Create branch", "dialog-create-branch", [&] { ctx->ItemClick("//Branches/###create_branch"); });
    s.showPanel("Tags");
    dialog("Create tag", "dialog-create-tag", [&] { ctx->ItemClick("//Tags/###create_tag"); });
    s.showPanel("Branches");
    dialog("Delete branch", "dialog-delete-branch", [&] {
        s.contextMenu("//Branches/branch_old-topic/###branch_old-topic", "Delete/Local");
    });
    s.showPanel("Worktrees");
    dialog("Add worktree", "dialog-add-worktree", [&] { ctx->ItemClick("//Worktrees/###add_worktree"); });
    dialog("Clone repository", "dialog-clone", [&] { ctx->MenuClick("//##MainMenuBar/Repository/Clone..."); });
}

GG_MANUAL_TEST("gallery", "push dialog")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.git(repo, {"switch", "-q", "-c", "topic"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "topic"; }));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push to"));
    shot(s, "dialog-push");
}

GG_MANUAL_TEST("gallery", "clone dialog from Welcome")
{
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    shot(s, "dialog-clone-welcome");
}

GG_MANUAL_TEST("gallery", "conflicts view")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    std::string ref;
    GG_REQUIRE(s.waitUntil([&] {
        ref = "//Changes/**/###file_f.txt";
        return s.itemExists(ref.c_str());
    }));
    ctx->ItemClick(ref.c_str());
    s.showPanel("Diff");
    s.settle();
    shot(s, "conflicts-view");
    ctx->ItemClick(ref.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "conflicts-file-menu");
    closePopups(s);
}

GG_MANUAL_TEST("gallery", "interactive rebase and blame")
{
    const fs::path repo = s.fixture(Recipe::Empty, "ir");
    std::vector<std::string> c(1);
    const char* files[] = {"", "a", "b", "c", "d", "e"};
    for (int i = 1; i <= 5; ++i) {
        s.commitFile(repo, std::string(files[i]) + ".txt", std::string(files[i]) + "\n", "c" + std::to_string(i) + " add " + files[i]);
        c.push_back(s.head(repo));
        if (i == 3)
            s.git(repo, {"branch", "part1"});
    }
    GG_REQUIRE(s.openRepository(repo));
    const std::string row = "//History/**/###row_" + c[3];
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->ItemClick(row.c_str());
    ctx->KeyPress(ImGuiKey_I);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->rebase().isOpen() && s.session()->rebase().context() != nullptr; }));
    ctx->Yield(3);
    shot(s, "rebase-i-panel");
    const std::string act = "//Interactive rebase/**/###ir_action_" + c[4];
    ctx->ItemClick(act.c_str());
    ctx->Yield(3);
    shot(s, "rebase-i-combo-popup");
    closePopups(s);
    ctx->ItemClick("//Interactive rebase/###ir_cancel");
    ctx->Yield(3);

    // Blame with content on a file with history and a working-tree edit.
    // A source file, so the shots show the editor's syntax highlighting.
    auto story = [](const char* text, const char* format) {
        return std::string("#include <cstdio>\n\n// Tells the story.\nint main()\n{\n    const char* line = \"") + text
            + "\";\n    std::printf(\"" + format + "\", line);\n    return 0;\n}\n";
    };
    s.commitFile(repo, "story.cpp", story("Once", "%s"), "Write the story");
    s.write(repo, "story.cpp", story("Once upon a time", "%s"));
    s.git(repo, {"add", "story.cpp"});
    s.git(repo, {"commit", "-q", "--author=Other Author <other@example.com>", "-m", "Tell more of it"});
    s.write(repo, "story.cpp", story("Once upon a time", "%s\\n"));
    ctx->Yield(10);
    ctx->ItemClick("//History/**/###row_wt");
    ctx->Yield(3);
    const std::string file = "//Changes/**/###file_story.cpp";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(file.c_str()); }));
    s.contextMenu(file.c_str(), "Blame file");
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return bool(s.session()->blame().blame()); }));
    s.settle();
    shot(s, "blame-panel");
    const std::string blameLine = s.child("//Blame", "##blame_editor") + "/###blame_line_3";
    ctx->MouseMove(blameLine.c_str());
    ctx->SleepNoSkip(1.2f, 0.1f);
    shot(s, "blame-tooltip");
    ctx->MouseMove(blameLine.c_str());
    ctx->ItemClick(blameLine.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    shot(s, "blame-line-menu");
    closePopups(s);
}

GG_MANUAL_TEST("gallery", "light theme and 150 percent")
{
    fs::path repo;
    GG_REQUIRE(openWorking(s, &repo));
    setLook(s, "Dark", 150);
    ctx->WindowClose("//Settings");
    s.settle();
    shot(s, "main-150");
    setLook(s, "Dark", 100);
    ctx->WindowClose("//Settings");
    setLook(s, "Light", 100);
    ctx->Yield(3);
    shot(s, "settings-light-General");
    for (const char* tab : {"Git"}) {
        ctx->ItemClick((std::string("//Settings/##settings_tabs/") + tab).c_str());
        shot(s, std::string("settings-light-") + tab);
    }
    ctx->WindowClose("//Settings");
    s.settle();
    shot(s, "main-light");
    const std::string file = "//Changes/**/###file_b.txt";
    ctx->ItemClick(file.c_str());
    s.showPanel("Diff");
    s.settle();
    shot(s, "main-light-diff");
    ctx->MenuAction(ImGuiTestAction_Open, "//##MainMenuBar/Repository");
    ctx->Yield(3);
    shot(s, "menu-light-Repository");
    closePopups(s);
    ctx->ItemClick("//###Toolbar/###tb_commit");
    if (s.dialogOpen("Commit"))
        shot(s, "dialog-light-commit");
}

} // namespace ggtest
