// UI actions that had no test of their own (P3-20 UI-action audit, docs/ui-actions.md): dialog
// keys, menu variants, options inside dialogs and actions in less common repository states.
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }
std::string branchRow(const std::string& name) { return "//Branches/branch_" + name + "/###branch_" + name; }

bool rowShown(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.itemExists(rowRef(hex).c_str()); });
}

std::string symbolicHead(Scenario& s, const fs::path& repo)
{
    auto r = s.gitMayFail(repo, {"symbolic-ref", "-q", "--short", "HEAD"});
    return r.ok() ? gg::trim(r.out) : std::string("(detached)");
}

bool refExists(Scenario& s, const fs::path& repo, const std::string& ref)
{
    return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok();
}

void writeHook(Scenario& s, const fs::path& repo, const std::string& name, const std::string& body)
{
    s.write(repo / ".git" / "hooks", name, "#!/bin/sh\n" + body);
    fs::permissions(repo / ".git" / "hooks" / name, fs::perms::owner_all);
}

std::string settingsFile(Scenario& s) { return s.read(s.root() / "prefs", "settings.json"); }

// A bare repository next to `repo`, added to it as remote `name`.
fs::path addBareRemote(Scenario& s, const fs::path& repo, const std::string& name)
{
    const fs::path bare = s.path(name + ".git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", name, bare.string()});
    return bare;
}

} // namespace

GG_TEST("ui", "Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off", "SET-HOOKS-TAB",
    "APP-PROMPT-HOOKS")
{
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Linear, "second");
    const char* box = "//Settings/##settings_tabs/Hooks/Ask to install the ggui hooks when opening a repository##ask_hooks";
    GG_REQUIRE(!s.app.settings().data().askHooksOnOpen); // the test preferences start with it off
    ctx->MenuClick("//##MainMenuBar/Repository/Settings...");
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Hooks");
    ctx->ItemCheck(box);
    ctx->Yield(2);
    GG_CHECK(s.app.settings().data().askHooksOnOpen);
    GG_CHECK(settingsFile(s).find("\"askHooksOnOpen\": true") != std::string::npos);
    // On: opening a repository without the hooks asks.
    GG_REQUIRE(s.openRepository(first));
    GG_REQUIRE(s.dialogOpen("Install ggui hooks?"));
    s.dialogButton("Install ggui hooks?", "Not now");
    s.settle();
    // Not now installs nothing: no config-defined hooks, no wrapper scripts.
    GG_CHECK(s.gitMayFail(first, {"config", "--get-regexp", "^hook\\."}).out.empty());
    GG_CHECK(!fs::exists(first / ".git" / "hooks" / "reference-transaction"));
    // Off: the next repository opens without the prompt.
    ctx->ItemUncheck(box);
    ctx->Yield(2);
    GG_CHECK(!s.app.settings().data().askHooksOnOpen);
    GG_CHECK(settingsFile(s).find("\"askHooksOnOpen\": false") != std::string::npos);
    ctx->WindowClose("//Settings");
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(second));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->hooksStatus().has_value(); }));
    s.settle();
    ctx->Yield(5);
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("ui", "History: Ctrl-click drops a commit from the selection; New merge commit from the row menu",
    "ACT-NEW-MERGE", "HIST-CTX-NEW")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string main = s.head(repo);
    const std::string topic = s.revParse(repo, "topic");
    const std::string firstParent = s.revParse(repo, "main^1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowShown(s, topic) && rowShown(s, firstParent));
    auto& history = s.session()->history();
    ctx->ItemClick(rowRef(main).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(topic).c_str());
    ctx->ItemClick(rowRef(firstParent).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(history.extraSelection().size(), static_cast<size_t>(2));
    // Ctrl-click on a commit that is already selected takes it out again.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(firstParent).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_REQUIRE(history.extraSelection().size() == 1);
    GG_CHECK(history.extraSelection().front().hex() == topic);
    s.contextMenu(rowRef(main).c_str(), "New merge commit");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main^1") == main; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "main^2"), topic);
    GG_CHECK(!refExists(s, repo, "main^3"));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main");
}

GG_TEST("ui", "Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease",
    "HIST-CTX-PUSH", "BR-PUSH", "TB-PUSH-TO", "REMOTE-PUSH", "REMOTE-PUSH-SET-UPSTREAM", "TB-PUSH-FORCE-LEASE")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path backup = addBareRemote(s, repo, "backup");
    s.git(repo, {"switch", "-q", "-c", "topic"});
    s.commitFile(repo, "topic.txt", "1\n", "Topic one");
    std::string tip = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowShown(s, tip));
    // History ▸ Push on a branch without an upstream: Push to, --set-upstream checked. Push to
    // the second remote without setting the upstream.
    s.contextMenu(rowRef(tip).c_str(), "Push");
    GG_REQUIRE(s.dialogOpen("Push to"));
    GG_CHECK(s.app.dialogs().current()->checked("set_upstream"));
    s.comboSelect("//Push to/Remote##remote", "backup");
    s.dialogCheck("Push to", "set_upstream", "Set as upstream (--set-upstream)", false);
    s.dialogButton("Push to", "Push");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(backup, {"rev-parse", "topic"}).out.find(tip) == 0; }));
    s.settle();
    GG_CHECK(!s.gitMayFail(repo, {"config", "branch.topic.remote"}).ok());
    GG_CHECK(!refExists(s, s.path("with-remote-origin.git"), "refs/heads/topic"));
    // A rewritten branch: without Force with lease the push is rejected; with it, it goes through.
    s.git(repo, {"commit", "-q", "--amend", "-m", "Topic one, reworded"});
    tip = s.head(repo);
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->findBranch("topic")->target.hex() == tip; }));
    s.contextMenu(branchRow("topic").c_str(), "Push");
    GG_REQUIRE(s.dialogOpen("Push to"));
    s.comboSelect("//Push to/Remote##remote", "backup");
    s.dialogButton("Push to", "Push");
    GG_REQUIRE(s.dialogOpen("Push rejected", 30.0f));
    s.dialogButton("Push rejected", "Cancel");
    s.settle();
    GG_CHECK(s.gitOut(backup, {"rev-parse", "topic"}) != tip);
    s.contextMenu(branchRow("topic").c_str(), "Push to...");
    GG_REQUIRE(s.dialogOpen("Push to"));
    s.comboSelect("//Push to/Remote##remote", "backup");
    s.dialogCheck("Push to", "set_upstream", "Set as upstream (--set-upstream)", true);
    s.dialogCheck("Push to", "force", "Force with lease");
    s.dialogButton("Push to", "Push");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(backup, {"rev-parse", "topic"}).out.find(tip) == 0; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "--abbrev-ref", "topic@{upstream}"}), "backup/topic");
}

GG_TEST("ui", "toolbar Push options ▸ Force with lease overwrites the upstream after confirming", "TB-PUSH-FORCE-LEASE")
{
    const fs::path repo = s.fixture(Recipe::WithRemote); // main is ahead 1, behind 1
    const fs::path origin = s.path("with-remote-origin.git");
    const std::string remoteOnly = s.revParse(repo, "origin/main");
    const std::string local = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//##Toolbar/###tb_push_menu");
    ctx->ItemClick("//$FOCUSED/Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    s.dialogButton("Force push", "Force push");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin, {"rev-parse", "main"}) == local; }));
    s.settle();
    // The remote-only commit is gone from the remote branch; the local branch did not move.
    GG_CHECK(!s.gitMayFail(origin, {"merge-base", "--is-ancestor", remoteOnly, "main"}).ok());
    GG_CHECK_STR_EQ(s.head(repo), local);
    GG_CHECK_STR_EQ(s.revParse(repo, "origin/main"), local);
}

GG_TEST("ui", "Reconcile by rebasing onto the upstream", "ACT-RECONCILE", "BR-RECONCILE")
{
    const fs::path repo = s.fixture(Recipe::WithRemote); // main is ahead 1, behind 1
    const std::string upstream = s.revParse(repo, "origin/main");
    const std::string mine = s.gitOut(repo, {"log", "-1", "--format=%s", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("main").c_str(), "Reconcile with remote or branch...");
    GG_REQUIRE(s.dialogOpen("Reconcile"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->text("with"), "origin/main");
    s.comboSelect("//Reconcile/How##how", "Rebase my commits onto it");
    s.dialogButton("Reconcile", "Reconcile");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "main^") == upstream; }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "main^2")); // rebased, not merged
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s", "main"}), mine);
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main");
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("ui", "Redo that would overwrite local changes offers Stash and redo", "TB-REDO")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"switch", "-q", "-c", "other"});
    s.commitFile(repo, "f1.txt", "other content\n", "Other f1");
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("other").c_str()); }));
    s.contextMenu(branchRow("other").c_str(), "Check out");
    GG_REQUIRE(s.waitUntil([&] { return symbolicHead(s, repo) == "other"; }));
    s.settle();
    ctx->ItemClick("//##Toolbar/###tb_undo");
    GG_REQUIRE(s.waitUntil([&] { return symbolicHead(s, repo) == "main"; }));
    s.settle();
    // Redo switches to other again, which would overwrite this edit.
    s.write(repo, "f1.txt", "edited before the redo\n");
    ctx->ItemClick("//##Toolbar/###tb_redo");
    GG_REQUIRE(s.dialogOpen("Undo would lose changes"));
    s.dialogButton("Undo would lose changes", "Stash and redo");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "other"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "other content\n");
    GG_CHECK(s.gitOut(repo, {"stash", "list"}).find("before redo") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "stash@{0}:f1.txt"}), "edited before the redo");
}

GG_TEST("ui", "the drop chooser closes with Escape and changes nothing", "HIST-DND-CHOOSER")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string tip = s.head(repo);
    const std::string root = s.revParse(repo, "HEAD~4");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowShown(s, tip) && rowShown(s, root));
    ctx->ItemDragAndDrop(rowRef(tip).c_str(), rowRef(root).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Move before"); }));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//$FOCUSED/Move before"));
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), tip);
    // The next drop asks again (nothing was left pending).
    ctx->ItemDragAndDrop(rowRef(tip).c_str(), rowRef(root).c_str());
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Move before"); }));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), tip);
}

GG_TEST("ui", "dialogs: Escape cancels; Enter in a text field confirms when the button is enabled",
    "HIST-CTX-CREATE-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string tip = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowShown(s, tip));
    // Escape: the dialog closes and nothing is created.
    s.contextMenu(rowRef(tip).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "escaped");
    ctx->KeyPress(ImGuiKey_Escape);
    GG_CHECK(s.waitUntil([&] { return s.app.dialogs().current() == nullptr; }, 5.0f));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/heads/escaped"));
    // Enter with an empty name: Create is disabled, the dialog stays.
    s.contextMenu(rowRef(tip).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "");
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->title == "Create branch");
    // Enter with a name: created and checked out (the default).
    s.dialogText("Create branch", "name", "entered");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/entered"); }));
    s.settle();
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK_STR_EQ(s.revParse(repo, "entered"), tip);
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "entered"; }));
}

GG_TEST("ui", "Git required: Quit asks the app to quit; the refused open does not block later opens",
    "APP-PROMPT-GIT-OLD")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string path = ggui::getEnv("PATH");
    const fs::path oldGit = s.path("old-git");
    fs::create_directories(oldGit);
    Scenario::writeTool(oldGit, "git", "#!/bin/sh\necho 'git version 2.20.0'\n");
    ggui::setEnv("PATH", oldGit.string() + kPathSep + path);
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool shown = s.dialogOpen("Git required");
    ggui::setEnv("PATH", path);
    GG_REQUIRE(shown);
    const int quits = s.app.quitRequests();
    s.dialogButton("Git required", "Quit");
    GG_CHECK_EQ(s.app.quitRequests(), quits + 1);
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK(s.session() == nullptr || !s.session()->opened());
    // (The test run keeps the app alive.) The refused open is over: Welcome works again and the
    // repository opens with a good git.
    ctx->Yield(2);
    GG_CHECK((ctx->ItemInfo("//Welcome/###welcome_open").ItemFlags & ImGuiItemFlags_Disabled) == 0);
    GG_CHECK(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    s.settle();
}

GG_TEST("ui", "Interactive rebase: Cancel while the commits are being read", "IR-ENTRY-COMMIT-MENU",
    "IR-MEMORY-CANCEL")
{
    // A long range (built with git fast-import) takes a while to read.
    const fs::path repo = s.fixture(Recipe::Empty);
    constexpr int kCommits = 20000;
    std::string stream;
    for (int i = 1; i <= kCommits; ++i) {
        const std::string msg = "c" + std::to_string(i) + "\n";
        const std::string content = std::to_string(i) + "\n";
        stream += "commit refs/heads/main\nmark :" + std::to_string(i) + "\ncommitter T <t@example.com> "
            + std::to_string(1700000000 + i) + " +0000\ndata " + std::to_string(msg.size()) + "\n" + msg;
        if (i > 1)
            stream += "from :" + std::to_string(i - 1) + "\n";
        stream += "M 644 inline f.txt\ndata " + std::to_string(content.size()) + "\n" + content + "\n";
    }
    s.git(repo, {"fast-import", "--quiet"}, stream);
    s.git(repo, {"reset", "-q", "--hard", "main"});
    const std::string root = s.gitOut(repo, {"rev-list", "--max-parents=0", "main"});
    const auto before = s.gitDirBytes(repo);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Commit/Interactive rebase...");
    GG_REQUIRE(s.dialogOpen("Interactive rebase onto"));
    s.dialogText("Interactive rebase onto", "base", root);
    s.dialogButton("Interactive rebase onto", "Open");
    auto& editor = s.session()->rebase();
    GG_REQUIRE(s.waitUntil([&] { return editor.loading() && s.itemExists("//Interactive rebase/Cancel###ir_cancel"); }));
    ctx->ItemClick("//Interactive rebase/Cancel###ir_cancel");
    GG_CHECK(!editor.isOpen());
    // The read finishes later and is dropped: the panel stays closed, nothing was written.
    GG_CHECK(s.settle());
    ctx->Yield(3);
    GG_CHECK(!editor.isOpen());
    GG_CHECK(!s.itemExists("//Interactive rebase/Cancel###ir_cancel"));
    GG_CHECK(s.gitDirBytes(repo) == before);
}

GG_TEST("ui", "View menu: every panel hides and shows again; the choice is saved", "MENU-VIEW-TOGGLE-PANEL")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    auto shown = [&](const char* name) {
        ImGuiWindow* w = ctx->GetWindowByRef((std::string("//") + name).c_str());
        return w != nullptr && w->WasActive;
    };
    for (const char* name : ggui::panel::All) {
        const auto& panels = s.app.settings().data().panels;
        const auto it = panels.find(name);
        const bool before = it == panels.end() ? ggui::panel::defaultVisible(name) : it->second;
        for (const bool expected : {!before, before}) {
            ctx->MenuClick((std::string("//##MainMenuBar/View/") + name).c_str());
            ctx->Yield(3);
            GG_CHECK_EQ(s.app.settings().data().panels.at(name), expected);
            GG_CHECK_EQ(shown(name), expected);
            const std::string saved = std::string("\"") + name + "\": " + (expected ? "true" : "false");
            GG_CHECK(settingsFile(s).find(saved) != std::string::npos);
        }
    }
}

GG_TEST("ui", "old gg data: Not now asks again next time; an unchecked commit is not kept", "APP-PROMPT-GGREFS",
    "GGREFS-DELETE", "GGREFS-LIST")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string tree = s.gitOut(repo, {"rev-parse", "HEAD^{tree}"});
    const std::string kept = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD~1", "-m", "Kept"});
    const std::string dropped = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD~2", "-m", "Dropped"});
    s.git(repo, {"update-ref", "refs/gg/heads/kept", kept});
    s.git(repo, {"update-ref", "refs/gg/heads/dropped", dropped});
    auto ggRefs = [&] { return s.gitOut(repo, {"for-each-ref", "--format=%(refname)", "refs/gg/"}); };
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.dialogOpen("Old gg data found"));
    s.dialogButton("Old gg data found", "Not now");
    s.settle();
    GG_CHECK(!ggRefs().empty());
    GG_CHECK(!s.app.settings().repo(repo.string()).ignoreOldGgRefs);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.dialogOpen("Old gg data found"));
    const std::string shortDropped = dropped.substr(0, 10);
    s.dialogCheck("Old gg data found", ("keep_" + shortDropped).c_str(), ("Keep " + shortDropped + " Dropped").c_str(),
        false);
    s.dialogButton("Old gg data found", "Clean up");
    GG_CHECK(s.waitUntil([&] { return ggRefs().empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "gg-backup/" + kept.substr(0, 10)), kept);
    GG_CHECK(!refExists(s, repo, "refs/heads/gg-backup/" + shortDropped));
    GG_CHECK(s.gitOut(repo, {"for-each-ref", "--contains", dropped, "--format=%(refname)"}).empty());
}

GG_TEST("ui", "toolbar Amend with HEAD selected; Skip hooks on Amend", "TB-COMMIT", "TB-COMMIT-LABEL", "ACT-AMEND",
    "HOOK-SKIP", "ACT-COMMIT-SKIP-HOOKS")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    writeHook(s, repo, "pre-commit", "echo 'pre-commit hook says no' >&2\nexit 1\n");
    const std::string head = s.head(repo);
    const std::string parent = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowShown(s, head));
    ctx->ItemClick(rowRef(head).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemText("//##Toolbar/###tb_commit").find("Amend") != std::string::npos; }));
    // The hook refuses a plain amend.
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogText("Amend", "message", "Amended past the hook");
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("pre-commit hook says no") != std::string::npos);
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), head);
    // Skip hooks: git commit --amend --no-verify.
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogText("Amend", "message", "Amended past the hook");
    s.dialogCheck("Amend", "skip_hooks", "Skip hooks (--no-verify)");
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != head; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s"}), "Amended past the hook");
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), parent);
    GG_CHECK_STR_EQ(symbolicHead(s, repo), "main");
}

GG_TEST("ui", "Stashes ▸ Pop applies an older stash and drops only that one", "STASH-POP")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    const std::string newest = s.revParse(repo, "stash@{0}");
    const std::string oldest = s.revParse(repo, "stash@{2}");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    // stash@{1}: a staged a.txt and an unstaged b.txt; Pop brings both back as unstaged changes.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_1/###row"); }));
    s.contextMenu("//Stashes/stash_1/###row", "Pop");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"stash", "list"}).find("index and worktree") == std::string::npos; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "a.txt"), "a staged\n");
    GG_CHECK_STR_EQ(s.read(repo, "b.txt"), "b unstaged\n");
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    GG_CHECK_STR_EQ(s.revParse(repo, "stash@{0}"), newest);
    GG_CHECK_STR_EQ(s.revParse(repo, "stash@{1}"), oldest);
    GG_CHECK(!refExists(s, repo, "stash@{2}"));
}

GG_TEST("ui", "a stopped cherry-pick: Skip, and Commit with conflicts", "TB-STATE-SKIP", "CONF-COMMIT-WITH-CONFLICTS")
{
    auto picking = [&](const fs::path& repo) { return fs::exists(repo / ".git" / "CHERRY_PICK_HEAD"); };
    // Skip: the commit is left out, HEAD stays, the working tree is clean again.
    const fs::path skipped = s.fixture(Recipe::MidCherryPick, "skip");
    GG_REQUIRE(picking(skipped));
    const std::string head = s.head(skipped);
    GG_REQUIRE(s.openRepository(skipped));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//##Toolbar/Skip##tb_skip"); }));
    ctx->ItemClick("//##Toolbar/Skip##tb_skip");
    GG_CHECK(s.waitUntil([&] { return !picking(skipped); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(skipped), head);
    GG_CHECK(s.statusPorcelain(skipped).empty());
    GG_CHECK_STR_EQ(s.read(skipped, "f.txt"), "a\nours\nc\n");
    // Commit with conflicts: the pick is committed with the conflict as diff3 regions.
    const fs::path committed = s.fixture(Recipe::MidCherryPick, "commit");
    const std::string before = s.head(committed);
    GG_REQUIRE(s.openRepository(committed));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//##Toolbar/Commit with conflicts##tb_commit_conflicts"); }));
    ctx->ItemClick("//##Toolbar/Commit with conflicts##tb_commit_conflicts");
    GG_CHECK(s.waitUntil([&] { return !picking(committed); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(committed, "HEAD~1"), before);
    GG_CHECK_STR_EQ(s.gitOut(committed, {"log", "-1", "--format=%s"}), "Theirs");
    const std::string text = s.gitOut(committed, {"show", "HEAD:f.txt"});
    GG_CHECK(text.find("<<<<<<<") != std::string::npos && text.find(">>>>>>>") != std::string::npos);
}

} // namespace ggtest
