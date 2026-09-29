// Initialize, git presence/version prompt, old gg refs cleanup (§4.1, §5 C3).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "shell/Settings.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>

namespace ggtest {

namespace {

bool refExists(Scenario& s, const fs::path& repo, const std::string& ref)
{
    return s.gitMayFail(repo, {"rev-parse", "--verify", "-q", ref}).ok();
}

std::vector<std::string> ggRefs(Scenario& s, const fs::path& repo)
{
    std::vector<std::string> out;
    for (const auto& line : gg::splitLines(s.gitOut(repo, {"for-each-ref", "--format=%(refname)", "refs/gg/"})))
        if (!line.empty())
            out.push_back(line);
    return out;
}

} // namespace

GG_TEST("setup", "initialize a repository from Welcome and the menu")
{
    const fs::path first = s.path("fresh-one");
    ggui::setEnv("GGUI_TEST_PICK_PATH", first.string());
    ctx->ItemClick("//Welcome/###welcome_init");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->path() == first; }));
    GG_CHECK(fs::exists(first / ".git" / "HEAD"));
    GG_CHECK(s.session()->snapshot()->headUnborn);
    s.track(first);
    const fs::path second = s.path("fresh-two");
    ggui::setEnv("GGUI_TEST_PICK_PATH", second.string());
    ctx->MenuClick("//##MainMenuBar/Repository/Initialize...");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->path() == second; }));
    s.track(second);
    ggui::unsetEnv("GGUI_TEST_PICK_PATH");
}

GG_TEST("setup", "git missing or too old: a blocking prompt with Retry")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string path = ggui::getEnv("PATH");
    // Too old.
    const fs::path oldGit = s.path("old-git");
    fs::create_directories(oldGit);
    Scenario::writeTool(oldGit, "git", "#!/bin/sh\necho 'git version 2.30.1'\n");
    ggui::setEnv("PATH", oldGit.string() + kPathSep + path);
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool oldShown = s.dialogOpen("Git required");
    ggui::setEnv("PATH", path);
    GG_REQUIRE(oldShown);
    GG_CHECK(s.app.dialogs().current()->message.find("Found git 2.30.1") != std::string::npos);
    GG_CHECK(s.session() == nullptr || !s.session()->opened());
    // Retry with a good git: the repository opens.
    s.dialogButton("Git required", "Retry");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    // Missing.
    const fs::path empty = s.path("no-git");
    fs::create_directories(empty);
    ggui::setEnv("PATH", empty.string());
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool missingShown = s.dialogOpen("Git required");
    ggui::setEnv("PATH", path);
    GG_REQUIRE(missingShown);
    GG_CHECK(s.app.dialogs().current()->message.find("not found") != std::string::npos);
    s.dialogButton("Git required", "Retry");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    s.settle();
}

GG_TEST("setup", "old gg refs: listed, kept as branches, deleted at once, undoable, ignorable")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // Two commits only refs/gg keeps alive (tips of two lines), plus a ref on a reachable commit.
    const std::string tree = s.gitOut(repo, {"rev-parse", "HEAD^{tree}"});
    const std::string lost1 = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD~1", "-m", "Lost one"});
    const std::string lost2 = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD~2", "-m", "Lost two"});
    s.git(repo, {"update-ref", "refs/gg/heads/a", lost1});
    s.git(repo, {"update-ref", "refs/gg/heads/b", lost2});
    s.git(repo, {"update-ref", "refs/gg/op/reachable", "HEAD"});
    // Under refs/gg/cache too: the journal records it like any other ref, so Undo restores it.
    s.git(repo, {"update-ref", "refs/gg/cache/heads", "HEAD"});
    const std::string before = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.dialogOpen("Old gg data found"));
    const ggui::Form* f = s.app.dialogs().current();
    GG_CHECK(f->message.find("4 refs under refs/gg/") != std::string::npos);
    // Only the two lost commits are listed (the reachable one is not).
    int listed = 0;
    for (const auto& field : f->fields)
        listed += field.id.rfind("keep_", 0) == 0 ? 1 : 0;
    GG_CHECK_EQ(listed, 2);
    // lost1 under a name of our choice, lost2 under the default backup name.
    s.dialogText("Old gg data found", ("branch_" + lost1.substr(0, 10)).c_str(), "rescued");
    s.dialogButton("Old gg data found", "Clean up");
    GG_CHECK(s.waitUntil([&] { return ggRefs(s, repo).empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "rescued"), lost1);
    GG_CHECK_STR_EQ(s.revParse(repo, "gg-backup/" + lost2.substr(0, 10)), lost2);
    // All refs went in one update-ref --stdin.
    size_t updates = 0;
    for (const auto& e : gg::commandLog())
        updates += (e.args.size() > 2 && e.args[1] == "update-ref" && e.args[2] == "--stdin") ? 1 : 0;
    GG_CHECK(updates >= 1);
    // Undo brings every refs/gg ref back and removes the branches.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"}) == before; }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/heads/rescued"));
    // Ignore: remembered for this repository.
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.dialogOpen("Old gg data found"));
    s.dialogButton("Old gg data found", "Ignore");
    GG_CHECK(s.app.settings().repo(repo.string()).ignoreOldGgRefs);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK_EQ(ggRefs(s, repo).size(), static_cast<size_t>(4));
}

} // namespace ggtest

namespace ggtest {

GG_TEST("setup", "Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    const std::string tabs = "//Settings/##settings_tabs/Git/##config_scope/";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tabs + "User").c_str()); }));
    auto scope = [&](const char* label) {
        ctx->ItemClick((tabs + label).c_str());
        ctx->Yield(2);
        return tabs + label + "/";
    };
    auto config = [&](const char* flag, const char* key) {
        return gg::trim(s.gitMayFail(repo, {"config", flag, "--get", key}).out);
    };
    auto edit = [&](const std::string& tab, const std::string& key, const std::string& value) {
        s.setText(tab + key + "##" + key, value);
        ctx->KeyPress(ImGuiKey_Enter);
        s.settle();
    };
    // User scope: identity, editor, pull method.
    std::string tab = scope("User");
    edit(tab, "user.name", "Ui User");
    GG_CHECK(s.waitUntil([&] { return config("--global", "user.name") == "Ui User"; }));
    edit(tab, "user.email", "ui@example.com");
    GG_CHECK(s.waitUntil([&] { return config("--global", "user.email") == "ui@example.com"; }));
    edit(tab, "core.editor", "editor-for-user");
    GG_CHECK(s.waitUntil([&] { return config("--global", "core.editor") == "editor-for-user"; }));
    s.comboSelect((tab + "Pull method##pull_method").c_str(), "Rebase, keeping merges");
    GG_CHECK(s.waitUntil([&] { return config("--global", "pull.rebase") == "merges"; }));
    s.comboSelect((tab + "Same-change resolution##same_change").c_str(), "Keep");
    GG_CHECK(s.waitUntil([&] { return config("--global", "gg.sameChange") == "keep"; }));
    // Repository scope: the user values show as hints until overridden.
    tab = scope("Repository");
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "Ui User  (User)"); }));
    GG_CHECK(!s.itemExists((tab + "Inherit##user.name").c_str()));
    edit(tab, "user.name", "Repo User");
    GG_CHECK(s.waitUntil([&] { return config("--local", "user.name") == "Repo User"; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "user.name"}), "Repo User");
    // Overriding offers "Inherit", which clears the override again.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + "Inherit##user.name").c_str()); }));
    ctx->ItemClick((tab + "Inherit##user.name").c_str());
    GG_CHECK(s.waitUntil([&] { return config("--local", "user.name").empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "user.name"}), "Ui User");
    edit(tab, "core.editor", "editor-for-repo");
    GG_CHECK(s.waitUntil([&] { return config("--local", "core.editor") == "editor-for-repo"; }));
    edit(tab, "merge.tool", "meld");
    GG_CHECK(s.waitUntil([&] { return config("--local", "merge.tool") == "meld"; }));
    edit(tab, "diff.tool", "kdiff3");
    GG_CHECK(s.waitUntil([&] { return config("--local", "diff.tool") == "kdiff3"; }));
    // Pull method: fast-forward only is pull.ff; rebase is pull.rebase (and drops ff-only here).
    s.comboSelect((tab + "Pull method##pull_method").c_str(), "Fast-forward only");
    GG_CHECK(s.waitUntil([&] { return config("--local", "pull.ff") == "only" && config("--local", "pull.rebase").empty(); }));
    s.settle();
    s.comboSelect((tab + "Pull method##pull_method").c_str(), "Rebase");
    GG_CHECK(s.waitUntil([&] { return config("--local", "pull.rebase") == "true" && config("--local", "pull.ff").empty(); }));
    s.settle();
    s.comboSelect((tab + "Pull method##pull_method").c_str(), "Merge");
    GG_CHECK(s.waitUntil([&] { return config("--local", "pull.rebase") == "false"; }));
    s.settle();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + "Inherit##pull_method").c_str()); }));
    ctx->ItemClick((tab + "Inherit##pull_method").c_str());
    GG_CHECK(s.waitUntil([&] { return config("--local", "pull.rebase").empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "pull.rebase"}), "merges");
    // Emptying a field unsets the value at that scope.
    edit(tab, "merge.tool", "");
    GG_CHECK(s.waitUntil([&] { return config("--local", "merge.tool").empty(); }));
    // Same-change resolution: overriding, then Inherit falls back to the user scope's "Keep".
    s.comboSelect((tab + "Same-change resolution##same_change").c_str(), "Accept");
    GG_CHECK(s.waitUntil([&] { return config("--local", "gg.sameChange") == "accept"; }));
    s.settle();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + "Inherit##same_change").c_str()); }));
    ctx->ItemClick((tab + "Inherit##same_change").c_str());
    GG_CHECK(s.waitUntil([&] { return config("--local", "gg.sameChange").empty(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "gg.sameChange"}), "keep");
    // Worktree scope: its tab exists only while "Worktree settings" (extensions.worktreeConfig) is
    // on (git would write the repository's config otherwise).
    const std::string worktreeSettings = "//Settings/##settings_tabs/Git/Worktree settings##worktree_config";
    GG_CHECK(!s.itemExists("//Settings/##settings_tabs/Git/##config_scope/Worktree"));
    ctx->ItemCheck(worktreeSettings.c_str());
    GG_CHECK(s.waitUntil([&] { return config("--local", "extensions.worktreeConfig") == "true"; }));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Settings/##settings_tabs/Git/##config_scope/Worktree"); }));
    tab = scope("Worktree");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tab + "core.editor##core.editor").c_str()); }));
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "editor-for-repo  (Repository)"); }));
    edit(tab, "core.editor", "editor-for-worktree");
    GG_CHECK(s.waitUntil([&] { return config("--worktree", "core.editor") == "editor-for-worktree"; }));
    GG_CHECK_STR_EQ(config("--local", "core.editor"), "editor-for-repo");
    // A change made with plain git shows up in the field.
    s.git(repo, {"config", "--worktree", "core.editor", "changed-outside"});
    ctx->ItemClick("//Settings/##settings_tabs/General");
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "changed-outside"); }));
    // Off again: the extension is unset and the tab goes away.
    ctx->ItemUncheck(worktreeSettings.c_str());
    GG_CHECK(s.waitUntil([&] { return config("--local", "extensions.worktreeConfig").empty(); }));
    GG_CHECK(s.waitUntil([&] { return !s.itemExists("//Settings/##settings_tabs/Git/##config_scope/Worktree"); }));
    ctx->WindowClose("//Settings");
}

GG_TEST("setup", "Settings ▸ Git: the user config in $XDG_CONFIG_HOME and the system config show their values")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // git config --global reads $XDG_CONFIG_HOME/git/config too (below ~/.gitconfig).
    const fs::path xdg = fs::path(ggui::getEnv("XDG_CONFIG_HOME")) / "git";
    fs::create_directories(xdg);
    s.write(xdg, "config", "[core]\n\teditor = xdg-editor\n");
    // The system configuration ggui reads in tests (TestRunner points libgit2 at this directory).
    const fs::path system = s.root().parent_path() / "empty-system-config";
    fs::create_directories(system);
    s.write(system, "gitconfig", "[diff]\n\ttool = system-difftool\n");
    GG_REQUIRE(s.openRepository(repo));
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    const std::string tabs = "//Settings/##settings_tabs/Git/##config_scope/";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((tabs + "User").c_str()); }));
    ctx->ItemClick((tabs + "User").c_str());
    // The User tab shows the XDG file's value, and the system value as a hint.
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "xdg-editor"); }));
    GG_CHECK(s.textShown("//Settings", "system-difftool  (System)"));
    // The Repository tab inherits both.
    ctx->ItemClick((tabs + "Repository").c_str());
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "xdg-editor  (User)"); }));
    GG_CHECK(s.textShown("//Settings", "system-difftool  (System)"));
    ctx->WindowClose("//Settings");
    fs::remove(system / "gitconfig");
}

GG_TEST("setup", "ahead/behind badges follow ref changes made outside ggui")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    auto badge = [&](const char* id) { return s.itemText((std::string("//##Toolbar/###") + id).c_str()); };
    GG_CHECK(badge("tb_pull").find("\xe2\x86\x93" "1") != std::string::npos);
    GG_CHECK(badge("tb_push").find("\xe2\x86\x91" "1") != std::string::npos);
    s.git(repo, {"pull", "-q", "--rebase", "origin", "main"});
    GG_CHECK(s.waitUntil([&] { return badge("tb_pull").find("\xe2\x86\x93") == std::string::npos; }));
    s.git(repo, {"push", "-q", "origin", "main"});
    GG_CHECK(s.waitUntil([&] { return badge("tb_push").find("\xe2\x86\x91") == std::string::npos; }));
}

GG_TEST("setup", "git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string path = ggui::getEnv("PATH");
    const fs::path realGit = gg::findInPath("git");
    // A stand-in git that reports `version` and otherwise runs the real one.
    auto withGit = [&](const std::string& name, const std::string& version) {
        const fs::path dir = s.path(name);
        fs::create_directories(dir);
        Scenario::writeTool(dir, "git", "#!/bin/sh\nif [ \"$1\" = --version ]; then echo '" + version + "'; exit 0; fi\nexec '"
                + realGit.generic_string() + "' \"$@\"\n");
        ggui::setEnv("PATH", dir.string() + kPathSep + path);
    };
    // git 3.x is newer than the minimum; a vendor suffix is ignored. The path ends with a slash.
    withGit("git3", "git version 3.1.0.vendor.2");
    ctx->ItemInputValue("//Welcome/##welcome_path", (repo.string() + "/").c_str());
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    ggui::setEnv("PATH", path);
    GG_CHECK_STR_EQ(s.session()->path().string(), repo.string());
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    // No version number at all: not supported.
    withGit("git-odd", "git version unknown");
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool shown = s.dialogOpen("Git required");
    ggui::setEnv("PATH", path);
    GG_REQUIRE(shown);
    GG_CHECK(s.app.dialogs().current()->message.find("git version unknown") != std::string::npos);
    s.dialogButton("Git required", "Retry");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    s.settle();
    // A notice's text can be copied from its menu.
    s.app.notify(ggui::App::Notice::Warning, "Copy me", "the message");
    GG_REQUIRE(!s.app.toasts().empty());
    const std::string toast = "//##toast_" + std::to_string(s.app.toasts().back().id);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((toast + "/###toast_close").c_str()); }));
    ctx->MouseMove((toast + "/###toast_close").c_str());
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->Yield(2);
    ctx->ItemClick("//$FOCUSED/Copy message");
    GG_CHECK_STR_EQ(s.clipboard(), "Copy me: the message");
}

} // namespace ggtest
