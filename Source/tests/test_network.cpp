// Clone, fetch, pull, push, askpass (§4.1 toolbar, §4.7, §4.8).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <algorithm>
#include <set>

#include "util/Env.hpp"
#include "util/Logging.hpp"

#include <IconsMaterialSymbols.h>
#include <libgg/GitRunner.hpp>
#include <libgg/LocalSocket.hpp>
#include "core/Services.hpp"
#include <libgg/Worktrees.hpp>

namespace ggtest {

namespace {

fs::path origin(Scenario& s, const fs::path& repo) { return s.root() / (repo.filename().string() + "-origin.git"); }
fs::path other(Scenario& s, const fs::path& repo) { return s.root() / (repo.filename().string() + "-other"); }
std::string fileUrl(const fs::path& p) { return "file://" + p.generic_string(); }

// Another developer pushes a commit to origin/main.
void remoteCommit(Scenario& s, const fs::path& repo, const std::string& file, const std::string& content)
{
    const fs::path o = other(s, repo);
    s.git(o, {"pull", "-q", "--no-rebase", "origin", "main"});
    s.commitFile(o, file, content, "Remote change " + file);
    s.git(o, {"push", "-q", "origin", "main"});
}

// rev-parse that is safe inside wait predicates: "" when the revision does not exist (yet).
std::string rev(Scenario& s, const fs::path& repo, const std::string& r)
{
    auto res = s.gitMayFail(repo, {"rev-parse", "--verify", "-q", r});
    return res.ok() ? gg::trim(res.out) : std::string();
}

// History row of `hex` loaded (after a commit made behind ggui's back).
bool rowLoaded(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
}

bool disabled(Scenario& s, const char* ref) { return (s.ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) != 0; }

void popupItem(Scenario& s, const char* button, const char* item)
{
    // The toolbar can redraw under a refresh (an outside commit) and swallow the click: open the
    // menu again until the item is there.
    const std::string path = std::string("//$FOCUSED/") + item;
    for (int attempt = 0; attempt < 3; ++attempt) {
        s.ctx->ItemClick(button);
        if (s.waitUntil([&] { return s.itemExists(path.c_str()); }, 2.0f))
            break;
    }
    s.ctx->ItemClick(path.c_str());
}

bool originHas(Scenario& s, const fs::path& repo, const std::string& ref, const std::string& id)
{
    auto r = s.gitMayFail(origin(s, repo), {"rev-parse", "--verify", "-q", ref});
    return r.ok() && gg::trim(r.out) == id;
}

// A superproject with a bare origin; its submodule "sub" has a bare origin too and is on the branch main.
// The submodule gets a commit that is on no remote, and the superproject records it.
fs::path superWithUnpushedSub(Scenario& s, fs::path& subRemote)
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    subRemote = s.root() / "sub-remote.git";
    s.git(s.root(), {"clone", "-q", "--bare", (s.root() / (repo.filename().string() + "-sub")).string(), subRemote.string()});
    s.track(subRemote);
    s.git(repo / "sub", {"remote", "set-url", "origin", fileUrl(subRemote)});
    s.git(repo / "sub", {"fetch", "-q", "origin"});
    s.git(repo / "sub", {"checkout", "-q", "-B", "main", "origin/main"});
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", origin(s, repo).string()});
    s.track(origin(s, repo));
    s.git(repo, {"remote", "add", "origin", fileUrl(origin(s, repo))});
    s.git(repo, {"push", "-q", "-u", "origin", "main"});
    s.commitFile(repo / "sub", "unpushed.txt", "u\n", "Sub unpushed");
    s.git(repo, {"add", "sub"});
    s.git(repo, {"commit", "-q", "-m", "Record unpushed submodule commit"});
    return repo;
}

} // namespace

GG_TEST("network", "clone from Welcome and the menu; unreachable remote fails cleanly")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path dest = s.path("cloned");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", fileUrl(origin(s, repo)));
    s.dialogText("Clone repository", "destination", dest.string());
    s.dialogButton("Clone repository", "Clone");
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->path() == dest && s.session()->snapshot(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(dest), s.revParse(repo, "origin/main"));
    s.track(dest);
    // From the menu, with a repository open: unreachable remote.
    const fs::path bad = s.path("never");
    ctx->MenuClick("//##MainMenuBar/Repository/Clone...");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", "git://127.0.0.1:1/nothing.git");
    s.dialogText("Clone repository", "destination", bad.string());
    s.dialogButton("Clone repository", "Clone");
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(s.dismissError());
    GG_CHECK(!fs::exists(bad));
    GG_CHECK(s.app.clone().state() == ggui::core::CloneService::State::Idle);
}

GG_TEST("network", "cancel a clone: no directory left behind")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.installSshShim();
    // A remote that never answers.
    const fs::path hang = s.root() / "hanging-ssh";
    // The stubs write their pids: the guard kills them when a failed check ends the test early.
    const fs::path stubPids = s.root() / "stub.pids";
    const StubGuard guard(stubPids, {"sleep", "stubborn-ssh"});
    s.write(s.root(), "hanging-ssh", "#!/bin/sh\necho $$ >> '" + stubPids.string() + "'\nexec sleep 60\n");
    fs::permissions(hang, fs::perms::owner_all);
    ggui::setEnv("GIT_SSH_COMMAND", hang.generic_string());
    const fs::path dest = s.path("cancelled");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", Scenario::sshUrl(origin(s, repo)));
    s.dialogText("Clone repository", "destination", dest.string());
    s.dialogButton("Clone repository", "Clone");
    GG_REQUIRE(s.waitUntil([&] { return fs::exists(dest); }));
    GG_CHECK(s.itemText("//Welcome/Cancel##clone") == "Cancel");
    ctx->ItemClick("//Welcome/Cancel##clone");
    GG_CHECK(s.waitUntil([&] { return s.app.clone().state() == ggui::core::CloneService::State::Idle; }, 10.0f));
    GG_CHECK(!fs::exists(dest));
    GG_CHECK(s.app.errorMessage().empty());
    // A remote helper that ignores SIGTERM is killed all the same.
    const fs::path stubborn = s.root() / "stubborn-ssh";
    s.write(s.root(), "stubborn-ssh", "#!/bin/sh\necho $$ >> '" + stubPids.string() + "'\ntrap '' TERM\nsleep 60 &\necho $! >> '" + stubPids.string() + "'\nwait\n");
    fs::permissions(stubborn, fs::perms::owner_all);
    ggui::setEnv("GIT_SSH_COMMAND", stubborn.generic_string());
    const fs::path dest2 = s.path("cancelled-stubborn");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", Scenario::sshUrl(origin(s, repo)));
    s.dialogText("Clone repository", "destination", dest2.string());
    s.dialogButton("Clone repository", "Clone");
    GG_REQUIRE(s.waitUntil([&] { return fs::exists(dest2); }));
    ctx->ItemClick("//Welcome/Cancel##clone");
    GG_CHECK(s.waitUntil([&] { return s.app.clone().state() == ggui::core::CloneService::State::Idle; }, 10.0f));
    GG_CHECK(!fs::exists(dest2));
}

GG_TEST("network", "fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path second = s.path("second.git");
    s.git(s.root(), {"clone", "-q", "--bare", origin(s, repo).string(), second.string()});
    s.track(second);
    s.git(repo, {"remote", "add", "second", fileUrl(second)});
    // A remote named "origin/fork" owns refs/remotes/origin/fork/*: they are not branches of "origin".
    // (git refuses `remote add` for such a name: the config is written directly.)
    s.git(repo, {"config", "remote.origin/fork.url", fileUrl(second)});
    s.git(repo, {"config", "remote.origin/fork.fetch", "+refs/heads/*:refs/remotes/origin/fork/*"});
    s.git(repo, {"fetch", "-q", "origin/fork", "+refs/heads/main:refs/remotes/origin/fork/main"});
    // Tag on an old commit, only on origin (before any fetch sees it).
    s.git(other(s, repo), {"tag", "old-tag", "HEAD~2"});
    s.git(other(s, repo), {"push", "-q", "origin", "old-tag"});
    const std::string localMain = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    // Badges: 1 ahead, 1 behind.
    GG_CHECK(s.itemText("//###Toolbar/###tb_pull").find(ICON_MS_ARROW_DOWNWARD_ALT "1") != std::string::npos);
    GG_CHECK(s.itemText("//###Toolbar/###tb_push").find(ICON_MS_ARROW_UPWARD_ALT "1") != std::string::npos);

    // "second" has no remote-tracking branch yet: its tags come with a tags refspec, which never prunes
    // local tags, also with fetch.prune set.
    s.git(repo, {"tag", "local-only-tag"});
    s.git(repo, {"config", "fetch.prune", "true"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch tags");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/tags/old-tag"}).ok(); }));
    s.settle();
    GG_CHECK(s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/tags/local-only-tag"}).ok());
    s.git(repo, {"config", "--unset", "fetch.prune"});

    remoteCommit(s, repo, "r2.txt", "two\n");
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), localMain); // no automatic fast-forward / merge
    GG_CHECK(s.waitUntil([&] { return s.itemText("//###Toolbar/###tb_pull").find(ICON_MS_ARROW_DOWNWARD_ALT "2") != std::string::npos; }));

    // Tag on origin after the first fetch: a plain fetch does not take it, "Fetch tags" does.
    s.git(other(s, repo), {"tag", "new-tag", "HEAD"});
    s.git(other(s, repo), {"push", "-q", "origin", "new-tag"});
    remoteCommit(s, repo, "r2b.txt", "two b\n");
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    GG_CHECK(!s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/tags/new-tag"}).ok());
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch tags");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/tags/new-tag"}).ok(); }));
    s.settle();

    auto hasRef = [&](const std::string& ref) { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok(); };
    auto hasToast = [&](const std::string& text) {
        for (const auto& t : s.app.toasts())
            if (t.level == ggui::App::Notice::Warning && t.message == text)
                return true;
        return false;
    };

    // A branch only on "second": a plain fetch does not create its remote-tracking branch.
    s.git(second, {"branch", "only-second", "main~1"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return hasToast("No tracked branches on second. Use Fetch more."); }));
    s.settle();
    GG_CHECK(!hasRef("refs/remotes/second/only-second"));

    // With a remote-tracking branch, a plain fetch moves it, and creates no other.
    s.git(repo, {"fetch", "-q", "second", "+refs/heads/only-second:refs/remotes/second/only-second"});
    const std::string before = s.revParse(repo, "second/only-second");
    s.git(second, {"branch", "extra", "main~2"});
    s.git(second, {"branch", "-f", "only-second", "main"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "second/only-second") != before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "second/only-second"), s.gitOut(second, {"rev-parse", "main"}));
    GG_CHECK(!hasRef("refs/remotes/second/extra"));

    // The branch disappears on the remote: a plain fetch keeps it, "Fetch and prune" deletes it.
    // A second tracked branch that moves on the remote proves that the plain fetch ran,
    // so the kept ref is not a leftover.
    s.git(second, {"branch", "moves", "main~2"});
    s.git(repo, {"fetch", "-q", "second", "+refs/heads/moves:refs/remotes/second/moves"});
    s.git(second, {"branch", "-f", "moves", "main"});
    s.git(second, {"branch", "-D", "only-second"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "second/moves") == s.gitOut(second, {"rev-parse", "main"}); }));
    s.settle();
    GG_CHECK(hasRef("refs/remotes/second/only-second"));
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch and prune");
    GG_CHECK(s.waitUntil([&] { return !hasRef("refs/remotes/second/only-second"); }));
    s.settle();
    GG_CHECK(!hasRef("refs/remotes/second/extra"));

    // Prune from the git config: remote.<r>.prune makes a plain fetch prune.
    s.git(second, {"branch", "pruned", "main~1"});
    s.git(repo, {"fetch", "-q", "second", "+refs/heads/pruned:refs/remotes/second/pruned"});
    s.git(second, {"branch", "-D", "pruned"});
    s.git(repo, {"config", "remote.second.prune", "true"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return !hasRef("refs/remotes/second/pruned"); }));
    s.settle();
    s.git(repo, {"config", "--unset", "remote.second.prune"});

    // A name that only another refspec maps (refs/pull/*) is not a tracked branch: it is never pruned.
    s.git(repo, {"config", "--add", "remote.origin.fetch", "+refs/pull/*/head:refs/remotes/origin/pr/*"});
    s.git(repo, {"update-ref", "refs/remotes/origin/pr/12", "HEAD"});
    remoteCommit(s, repo, "r2c.txt", "two c\n");
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch and prune");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    GG_CHECK(hasRef("refs/remotes/origin/pr/12"));
    GG_CHECK(hasRef("refs/remotes/origin/fork/main"));
    GG_CHECK(!hasToast("No tracked branches on origin. Use Fetch more."));

    remoteCommit(s, repo, "r3.txt", "three\n");
    ctx->MenuClick("//##MainMenuBar/Repository/Fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();

    s.showPanel("Remotes");
    remoteCommit(s, repo, "r4.txt", "four\n");
    s.contextMenu("//Remotes/remote_origin/###row", "Fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    remoteCommit(s, repo, "r5.txt", "five\n");
    ctx->ItemClick("//Remotes/Fetch all##fetch_all");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    // In Branches, the remote and its remote-tracking branches have the Remotes panel's menu.
    s.showPanel("Branches");
    remoteCommit(s, repo, "r6.txt", "six\n");
    s.contextMenu("//Branches/remote_group_origin/origin", "Fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    s.contextMenu("//Branches/remote_group_origin/origin", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "origin");
    GG_CHECK_STR_EQ(s.head(repo), localMain);
}

GG_TEST("network", "fetch more: named branches and tags only; the remote branch list and the check list")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path second = s.path("second.git");
    s.git(s.root(), {"clone", "-q", "--bare", origin(s, repo).string(), second.string()});
    s.track(second);
    s.git(repo, {"remote", "add", "second", fileUrl(second)});
    s.git(second, {"branch", "only-second", "main~1"});
    s.git(second, {"branch", "other-second", "main~2"});
    s.git(second, {"tag", "second-tag", "main~1"});
    GG_REQUIRE(s.openRepository(repo));
    auto hasRef = [&](const std::string& ref) { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok(); };

    // The branches on the remote.
    s.session()->requestRemoteBranches("second");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->remoteBranches("second") != nullptr; }));
    const auto* listed = s.session()->remoteBranches("second");
    GG_CHECK(listed->ok);
    GG_CHECK(std::find(listed->names.begin(), listed->names.end(), "only-second") != listed->names.end());
    GG_CHECK(std::find(listed->names.begin(), listed->names.end(), "other-second") != listed->names.end());
    // A new request removes the old entry until the answer arrives.
    s.session()->requestRemoteBranches("second");
    GG_CHECK(s.session()->remoteBranches("second") == nullptr);
    GG_CHECK(s.waitUntil([&] { return s.session()->remoteBranches("second") != nullptr; }));

    // The check list keeps the state of the names that stay.
    ggui::Form form;
    ggui::Field& list = form.add(ggui::Field{ggui::Field::CheckList, "branches", "Branches"});
    list.setOptions({"a", "b", "c"});
    list.checks[0] = 1;
    list.checks[2] = 1;
    list.setOptions({"c", "d", "a"});
    GG_CHECK(form.checkedOptions("branches") == (std::vector<std::string>{"c", "a"}));
    GG_CHECK(form.checkedOptions("missing").empty());

    // Nothing to fetch: nothing happens.
    s.session()->actions().fetchRefs("second", {}, {});
    GG_CHECK(s.session()->actions().busy().empty());

    // One branch and one tag: no other branch comes with them.
    s.session()->actions().fetchRefs("second", {"only-second"}, {"second-tag"});
    GG_CHECK(s.waitUntil([&] { return hasRef("refs/remotes/second/only-second"); }));
    s.settle();
    GG_CHECK(hasRef("refs/tags/second-tag"));
    GG_CHECK(!hasRef("refs/remotes/second/other-second"));
    GG_CHECK(!hasRef("refs/remotes/second/main"));
    GG_CHECK_STR_EQ(s.revParse(repo, "second/only-second"), s.gitOut(second, {"rev-parse", "only-second"}));
}

GG_TEST("network", "fetch more: dialog lists only what is new; Fetch takes the ticked branches and tags")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path second = s.path("second.git");
    s.git(s.root(), {"clone", "-q", "--bare", origin(s, repo).string(), second.string()});
    s.track(second);
    s.git(repo, {"remote", "add", "second", fileUrl(second)});
    s.git(second, {"branch", "only-second", "main~1"});
    s.git(second, {"tag", "t-second", "main~1"});
    GG_REQUIRE(s.openRepository(repo));
    auto hasRef = [&](const std::string& ref) { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok(); };

    // The default remote is origin: its main branch is known here, so the list does not show it.
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch more...");
    GG_REQUIRE(s.dialogOpen("Fetch more"));
    GG_CHECK(s.waitUntil([&] { return s.session()->remoteBranches("origin") != nullptr; }));
    s.settle();
    const std::string mainRow = "//Fetch more/**/main##branches_main";
    GG_CHECK(!s.itemExists(mainRow.c_str()));
    GG_CHECK(!s.itemExists("//Fetch more/**/only-second##branches_only-second"));

    // Pick "second": its new branch and tag are listed.
    ctx->ItemClick("//Fetch more/Remote##remote");
    ctx->ItemClick("//$FOCUSED/second");
    const std::string branchRow = "//Fetch more/**/only-second##branches_only-second";
    const std::string tagRow = "//Fetch more/**/t-second##tags_t-second";
    GG_CHECK(s.waitUntil([&] { return s.itemExists(branchRow.c_str()); }));
    GG_CHECK(s.waitUntil([&] { return s.itemExists(tagRow.c_str()); }));
    GG_CHECK(s.itemExists(mainRow.c_str())); // "second" has no remote-tracking branches here
    GG_CHECK(disabled(s, "//Fetch more/Fetch"));
    ctx->ItemCheck(branchRow.c_str());
    ctx->ItemCheck(tagRow.c_str());
    s.dialogButton("Fetch more", "Fetch");
    GG_CHECK(s.waitUntil([&] { return hasRef("refs/remotes/second/only-second") && hasRef("refs/tags/t-second"); }));
    s.settle();
    GG_CHECK(!hasRef("refs/remotes/second/main"));

    // The branch is tracked now: a plain "Fetch second" moves it.
    const std::string before = s.revParse(repo, "second/only-second");
    s.git(second, {"branch", "-f", "only-second", "main"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "second/only-second") != before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "second/only-second"), s.gitOut(second, {"rev-parse", "main"}));
}

GG_TEST("network", "a remote name that starts with a dash is not read as a git option")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const std::string marker = s.path("upload-pack-ran").string();
    // The name is relative to the repository and has no ':', '\\' or space: git rejects them in a remote name.
    const fs::path script = repo / "upload-pack.sh";
    s.write(repo, "upload-pack.sh", "#!/bin/sh\ntouch '" + marker + "'\n");
    fs::permissions(script, fs::perms::owner_all);
    const std::string name = "--upload-pack=./upload-pack.sh";
    s.git(repo, {"remote", "add", "--", name, "https://invalid.invalid/r.git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_CHECK(s.waitUntil([&] { return s.session()->remoteTags().count(name) != 0; }));
    s.settle();
    GG_CHECK(!fs::exists(marker));
}

GG_TEST("network", "a tag only on the remote is not in the Tags panel until Fetch more brings it")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(origin(s, repo), {"tag", "t-remote", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    auto hasRef = [&](const std::string& ref) { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok(); };
    const std::string row = "//Tags/tag_t-remote/###tag_t-remote";
    // The panel knows the tag of the remote, but lists only the local tags.
    GG_CHECK(s.waitUntil([&] {
        const auto& tags = s.session()->remoteTags();
        auto it = tags.find("origin");
        return it != tags.end() && it->second.ok && it->second.tags.count("t-remote") != 0;
    }));
    s.settle();
    GG_CHECK(!s.itemExists(row.c_str()));
    GG_CHECK(!hasRef("refs/tags/t-remote"));

    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch more...");
    GG_REQUIRE(s.dialogOpen("Fetch more"));
    const std::string tagRow = "//Fetch more/**/t-remote##tags_t-remote";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow.c_str()); }, 10.0f));
    ctx->ItemCheck(tagRow.c_str());
    s.dialogButton("Fetch more", "Fetch");
    GG_CHECK(s.waitUntil([&] { return hasRef("refs/tags/t-remote"); }));
    s.settle();
    GG_CHECK(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
}

GG_TEST("network", "forget: dialog lists the fetched branches and tags; Forget removes the ticked ones; Undo restores them")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path second = s.path("second.git");
    s.git(s.root(), {"clone", "-q", "--bare", origin(s, repo).string(), second.string()});
    s.track(second);
    s.git(repo, {"remote", "add", "second", fileUrl(second)});
    s.git(second, {"branch", "only-second", "main~1"});
    s.git(second, {"tag", "t-second", "main~1"});
    s.git(repo, {"fetch", "-q", "second", "+refs/heads/only-second:refs/remotes/second/only-second",
        "refs/tags/t-second:refs/tags/t-second"});
    s.git(repo, {"tag", "t-local"});
    s.git(repo, {"branch", "--track", "track", "second/only-second"});
    GG_REQUIRE(s.openRepository(repo));
    auto hasRef = [&](const std::string& ref) { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", ref}).ok(); };

    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    ctx->ItemClick("//Forget/Remote##remote");
    ctx->ItemClick("//$FOCUSED/second");
    const std::string branchRow = "//Forget/**/only-second##branches_only-second";
    const std::string tagRow = "//Forget/**/t-second##tags_t-second";
    GG_CHECK(s.waitUntil([&] { return s.itemExists(branchRow.c_str()); }));
    GG_CHECK(s.waitUntil([&] { return s.itemExists(tagRow.c_str()); })); // the tags need ls-remote
    s.settle();
    GG_CHECK(!s.itemExists("//Forget/**/t-local##tags_t-local"));
    GG_CHECK(!s.itemExists("//Forget/**/main##branches_main"));
    GG_CHECK(disabled(s, "//Forget/Forget"));
    ctx->ItemCheck(branchRow.c_str());
    ctx->ItemCheck(tagRow.c_str());
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] { return !hasRef("refs/remotes/second/only-second") && !hasRef("refs/tags/t-second"); }));
    s.settle();
    GG_CHECK(hasRef("refs/tags/t-local"));
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(repo, {"config", "branch.track.merge"})), "refs/heads/only-second");

    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return hasRef("refs/remotes/second/only-second") && hasRef("refs/tags/t-second"); }));
    s.settle();
    GG_CHECK(hasRef("refs/tags/t-local"));
}

GG_TEST("network", "fetch from an unreachable remote reports the failure")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "dead", "git://127.0.0.1:1/nothing.git"});
    // Fetch reads the remote only when a remote-tracking branch exists here.
    s.git(repo, {"update-ref", "refs/remotes/dead/main", "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch dead");
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK(s.session()->actions().busy().empty());
}

GG_TEST("network", "fetch all remotes goes on after an unreachable remote and then fails")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    // The dead remote comes first in the list by name and by config order.
    s.git(repo, {"remote", "remove", "origin"});
    s.git(repo, {"remote", "add", "dead", "git://127.0.0.1:1/nothing.git"});
    s.git(repo, {"remote", "add", "zgood", fileUrl(origin(s, repo))});
    s.git(repo, {"fetch", "-q", "zgood"});
    s.git(repo, {"update-ref", "refs/remotes/dead/main", "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    s.git(other(s, repo), {"pull", "-q", "--no-rebase", "origin", "main"});
    s.commitFile(other(s, repo), "late.txt", "late\n", "Late change");
    s.git(other(s, repo), {"push", "-q", "origin", "main"});
    const std::string tip = rev(s, other(s, repo), "HEAD");
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return rev(s, repo, "zgood/main") == tip; }));
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK(s.session()->actions().busy().empty());
}

GG_TEST("network", "prune keeps the tracking refs when the remote lists no branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(origin(s, repo), {"config", "uploadpack.hideRefs", "refs/heads"});
    s.git(repo, {"config", "fetch.prune", "true"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string before = rev(s, repo, "refs/remotes/origin/main");
    GG_REQUIRE(!before.empty());
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return s.session()->actions().busy().empty(); }));
    s.settle();
    // The fetch itself succeeds: the only notice is the prune warning.
    GG_CHECK(s.app.errorMessage().find("lists no branches") != std::string::npos);
    GG_CHECK_STR_EQ(rev(s, repo, "refs/remotes/origin/main"), before);
}

GG_TEST("network", "prune with a hidden remote branch deletes what plain git deletes")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path o = origin(s, repo);
    s.git(o, {"branch", "secret", "main"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"config", "fetch.prune", "true"});
    s.git(o, {"config", "uploadpack.hideRefs", "refs/heads/secret"});
    const auto refs = [&](const fs::path& r) {
        return s.gitOut(r, {"for-each-ref", "--format=%(refname)", "refs/remotes/origin/"});
    };
    GG_REQUIRE(!rev(s, repo, "refs/remotes/origin/secret").empty());
    // Plain git on a copy: the hidden branch looks deleted, so git prunes it and keeps main.
    const fs::path copy = s.path("plain-copy");
    fs::copy(repo, copy, fs::copy_options::recursive);
    s.git(copy, {"fetch", "-q", "--prune", "origin"});
    const std::string plain = refs(copy);
    GG_CHECK(plain.find("refs/remotes/origin/main") != std::string::npos);
    GG_CHECK(plain.find("origin/secret") == std::string::npos);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return rev(s, repo, "refs/remotes/origin/secret").empty(); }));
    s.settle();
    GG_CHECK(s.session()->actions().busy().empty());
    GG_CHECK_STR_EQ(refs(repo), plain);
}

GG_TEST("network", "pull follows pull.rebase; dropdown overrides; menu and panels")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"config", "pull.rebase", "true"});
    GG_REQUIRE(s.openRepository(repo));
    auto linearOnOrigin = [&] { return rev(s, repo, "HEAD~1") == rev(s, repo, "origin/main"); };
    ctx->ItemClick("//###Toolbar/###tb_pull");
    GG_CHECK(s.waitUntil(linearOnOrigin));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"log", "-1", "--format=%p"}).find(' ') == std::string::npos); // rebased, no merge

    remoteCommit(s, repo, "m.txt", "merge me\n");
    s.git(repo, {"fetch", "-q", "origin"});
    popupItem(s, "//###Toolbar/###tb_pull_menu", "Pull (merge)");
    GG_CHECK(s.waitUntil([&] { return rev(s, repo, "HEAD^2") == rev(s, repo, "origin/main"); }));
    s.settle();

    remoteCommit(s, repo, "r.txt", "rebase me\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"config", "pull.rebase", "false"});
    s.commitFile(repo, "local2.txt", "l2\n", "Local two");
    popupItem(s, "//###Toolbar/###tb_pull_menu", "Pull (rebase)");
    // The local commits (both of them) are replayed on origin/main: no merges on top of it.
    GG_CHECK(s.waitUntil([&] {
        return s.gitMayFail(repo, {"merge-base", "--is-ancestor", "origin/main", "HEAD"}).ok()
            && s.gitOut(repo, {"rev-list", "--merges", "origin/main..HEAD"}).empty()
            && s.gitOut(repo, {"rev-list", "--count", "origin/main..HEAD"}) == "2";
    }));
    s.settle();

    // Fast-forward only: succeeds when behind only, via the menu and the panels.
    s.git(repo, {"push", "-q", "origin", "main"});
    remoteCommit(s, repo, "ff1.txt", "ff\n");
    s.git(repo, {"fetch", "-q", "origin"});
    popupItem(s, "//###Toolbar/###tb_pull_menu", "Pull (fast-forward only)");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "origin/main"); }));
    s.settle();
    remoteCommit(s, repo, "ff2.txt", "ff\n");
    s.git(repo, {"fetch", "-q", "origin"});
    ctx->MenuClick("//##MainMenuBar/Repository/Pull");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "origin/main"); }));
    s.settle();
    remoteCommit(s, repo, "ff3.txt", "ff\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_main/###branch_main", "Pull");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "origin/main"); }));
    s.settle();
    remoteCommit(s, repo, "ff4.txt", "ff\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.showPanel("Remotes");
    s.contextMenu("//Remotes/remote_origin/###row", "Pull");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "origin/main"); }));
    s.settle();
}

GG_TEST("network", "pull disabled when detached or without upstream; Stash and pull")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.git(repo, {"switch", "-q", "--detach"});
    GG_CHECK(s.waitUntil([&] { return disabled(s, "//###Toolbar/###tb_pull"); }));
    std::string reason;
    GG_CHECK(!s.session()->pullAvailable(&reason));
    GG_CHECK(reason.find("detached") != std::string::npos);
    s.git(repo, {"switch", "-q", "-c", "no-upstream"});
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "no-upstream"; }));
    GG_CHECK(disabled(s, "//###Toolbar/###tb_pull"));
    GG_CHECK(!s.session()->pullAvailable(&reason));
    GG_CHECK(reason.find("upstream") != std::string::npos);
    s.git(repo, {"switch", "-q", "main"});
    GG_CHECK(s.waitUntil([&] { return !disabled(s, "//###Toolbar/###tb_pull"); }));

    // Incoming change to o1.txt, local uncommitted edit of o1.txt.
    remoteCommit(s, repo, "o1.txt", "remote edit\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.write(repo, "local-only.txt", "local uncommitted\n");
    s.write(repo, "o1.txt", "local uncommitted\n");
    s.git(repo, {"config", "pull.rebase", "false"});
    ctx->ItemClick("//###Toolbar/###tb_pull");
    GG_REQUIRE(s.dialogOpen("Stash and pull"));
    s.dialogButton("Stash and pull", "Stash and pull");
    GG_CHECK(s.waitUntil([&] { return rev(s, repo, "HEAD^2") == rev(s, repo, "origin/main"); }));
    s.settle();
    // Both sides replace the one line of o1.txt, so the re-applied stash conflicts there and git keeps it.
    GG_CHECK(s.read(repo, "local-only.txt") == "local uncommitted\n");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"diff", "--name-only", "--diff-filter=U"}) == "o1.txt"; }));
    const std::string o1 = s.read(repo, "o1.txt");
    GG_CHECK(o1.find("<<<<<<<") != std::string::npos);
    GG_CHECK(o1.find("remote edit") != std::string::npos);
    GG_CHECK(o1.find("local uncommitted") != std::string::npos);
    GG_CHECK(!s.gitOut(repo, {"stash", "list"}).empty());
}

GG_TEST("network", "pull on a branch without upstream is refused and creates no tracking ref")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.git(repo, {"switch", "-q", "-c", "no-upstream"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "no-upstream"; }));
    // A branch that the clone never fetched: a bare pull would create its tracking ref.
    s.git(other(s, repo), {"push", "-q", "origin", "main:extra"});
    GG_REQUIRE(rev(s, repo, "origin/extra").empty());
    s.session()->actions().pull(ggui::PullMode::Merge);
    GG_CHECK(s.waitUntil([&] { return s.app.errorMessage().find("no upstream") != std::string::npos; }));
    GG_CHECK(s.waitUntil([&] { return s.session()->actions().busy().empty(); }));
    GG_CHECK(rev(s, repo, "origin/extra").empty());
}

GG_TEST("network", "pull blocked by an untracked file does not offer Stash and pull")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    remoteCommit(s, repo, "clash.txt", "remote\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.write(repo, "clash.txt", "local untracked\n");
    s.git(repo, {"config", "pull.rebase", "false"});
    s.session()->actions().pull(ggui::PullMode::Merge);
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(!s.dialogOpen("Stash and pull", 1.0f));
    GG_CHECK(s.read(repo, "clash.txt") == "local untracked\n");
}

GG_TEST("network", "push: toolbar, menu, History and Branches; no upstream prefills Push to")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"pull", "-q", "--rebase", "origin", "main"});
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();
    s.commitFile(repo, "p2.txt", "2\n", "Push two");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1; }));
    ctx->MenuClick("//##MainMenuBar/Repository/Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();
    s.commitFile(repo, "p3.txt", "3\n", "Push three");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1; }));
    GG_REQUIRE(rowLoaded(s, s.head(repo)));
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_main/###branch_main", "Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();

    // No upstream: toolbar Push opens Push to with --set-upstream checked.
    s.git(repo, {"switch", "-q", "-c", "topic"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "topic"; }));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push to"));
    GG_CHECK(s.app.dialogs().current()->checked("set_upstream"));
    s.dialogButton("Push to", "Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "topic", s.head(repo)); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "--abbrev-ref", "topic@{upstream}"}), "origin/topic");

    // Push to... from the dropdown and Branches, under other remote branch names.
    popupItem(s, "//###Toolbar/###tb_push_menu", "Push to...");
    GG_REQUIRE(s.dialogOpen("Push to"));
    s.dialogText("Push to", "branch", "renamed-a");
    s.dialogButton("Push to", "Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "renamed-a", s.head(repo)); }));
    s.settle();
    s.contextMenu("//Branches/branch_main/###branch_main", "Push to...");
    GG_REQUIRE(s.dialogOpen("Push to"));
    s.dialogText("Push to", "branch", "renamed-c");
    s.dialogButton("Push to", "Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "renamed-c", s.revParse(repo, "main")); }));
    s.settle();
    s.commitFile(repo, "t2.txt", "t\n", "Topic two");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1; }));
    s.contextMenu("//Branches/branch_topic/###branch_topic", "Push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "topic", s.head(repo)); }));
    s.settle();
}

GG_TEST("network", "rejected push: Pull then push, Force with lease; push tags")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push rejected"));
    s.dialogButton("Push rejected", "Pull then push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();
    GG_CHECK(s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "HEAD^2"}).ok());

    // Diverge again and overwrite the remote with the confirmation dialog.
    remoteCommit(s, repo, "lost.txt", "lost\n");
    s.git(repo, {"fetch", "-q", "origin"});
    s.commitFile(repo, "mine.txt", "mine\n", "Mine");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1 && s.session()->incoming() == 1; }));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push rejected"));
    s.dialogButton("Push rejected", "Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    s.dialogButton("Force push", "Force push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();

    // Force with lease from the dropdown.
    s.git(repo, {"commit", "-q", "--amend", "-m", "Mine, amended"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1 && s.session()->incoming() == 1; }));
    s.settle();
    popupItem(s, "//###Toolbar/###tb_push_menu", "Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    s.dialogButton("Force push", "Force push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();

    s.git(repo, {"tag", "v1"});
    popupItem(s, "//###Toolbar/###tb_push_menu", "Push tags");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "refs/tags/v1", s.head(repo)); }));
    s.settle();
}

GG_TEST("network", "push of a tag that differs on the remote is refused and the remote tag stays")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    // Origin has v1 on one commit. The local v1 is on another one.
    const std::string remoteTarget = s.revParse(repo, "HEAD");
    s.git(repo, {"tag", "v1"});
    s.git(repo, {"push", "-q", "origin", "refs/tags/v1"});
    s.commitFile(repo, "next.txt", "n\n", "Next");
    s.git(repo, {"tag", "-f", "v1"});
    GG_REQUIRE(s.revParse(repo, "v1") != remoteTarget);
    GG_REQUIRE(originHas(s, repo, "refs/tags/v1", remoteTarget));

    // Plain git refuses the same push: the reason is "already exists".
    const auto plain = s.gitMayFail(repo, {"push", "origin", "refs/tags/v1"});
    GG_REQUIRE(!plain.ok());
    GG_REQUIRE(plain.err.find("already exists") != std::string::npos);

    // The single tag push shows the reason and leaves the remote tag.
    s.session()->actions().pushTag("origin", "v1");
    GG_REQUIRE(s.dialogOpen("push tag v1"));
    GG_CHECK(s.app.dialogs().current()->message.find("already exists") != std::string::npos);
    s.dialogButton("push tag v1", "OK");
    GG_CHECK(originHas(s, repo, "refs/tags/v1", remoteTarget));
    s.settle();

    // Push tags (all tags) is refused in the same way.
    popupItem(s, "//###Toolbar/###tb_push_menu", "Push tags");
    GG_REQUIRE(s.waitUntil([&] { return s.app.dialogs().current() != nullptr; }));
    GG_CHECK(s.app.dialogs().current()->message.find("already exists") != std::string::npos);
    GG_CHECK(originHas(s, repo, "refs/tags/v1", remoteTarget));
}

GG_TEST("network", "push sends the checked oid when the branch moves after the guard")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"checkout", "-q", "-b", "topic"}); // origin has no topic: the push cannot be rejected
    GG_REQUIRE(s.openRepository(repo));
    s.commitFile(repo, "checked.txt", "c\n", "Checked");
    const std::string checked = s.head(repo);
    s.commitFile(repo, "moved.txt", "m\n", "Moved after the check");
    GG_REQUIRE(s.head(repo) != checked);
    s.session()->actions().push("origin", "topic", "topic", false, false, false, checked);
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "topic", checked); }));
    s.settle();
    GG_CHECK(!originHas(s, repo, "topic", s.head(repo)));
}

GG_TEST("network", "push --set-upstream still sets the upstream with an oid source")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.git(repo, {"branch", "topic"});
    s.session()->actions().push("origin", "topic", "topic", true, false);
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "topic", s.revParse(repo, "topic")); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "branch.topic.merge"}), "refs/heads/topic");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "branch.topic.remote"}), "origin");
}

GG_TEST("network", "force push keeps the remote commit that a fetch brought after the dialog opened")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.commitFile(repo, "mine.txt", "mine\n", "Mine");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1; }));
    s.settle();
    popupItem(s, "//###Toolbar/###tb_push_menu", "Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    remoteCommit(s, repo, "unseen.txt", "unseen\n");
    const std::string remoteHead = s.gitOut(origin(s, repo), {"rev-parse", "main"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.dialogButton("Force push", "Force push");
    s.settle();
    GG_CHECK(originHas(s, repo, "main", remoteHead));
}

GG_TEST("network", "force with lease is refused when the remote moves after the last fetch")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.commitFile(repo, "mine.txt", "mine\n", "Mine");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->outgoing() == 1; }));
    s.settle();
    popupItem(s, "//###Toolbar/###tb_push_menu", "Force with lease...");
    GG_REQUIRE(s.dialogOpen("Force push"));
    remoteCommit(s, repo, "unseen.txt", "unseen\n"); // no fetch: the tracking ref is stale
    const std::string remoteHead = s.gitOut(origin(s, repo), {"rev-parse", "main"});
    s.dialogButton("Force push", "Force push");
    s.settle();
    GG_CHECK(originHas(s, repo, "main", remoteHead));
    GG_CHECK(!originHas(s, repo, "main", s.head(repo)));
}

GG_TEST("network", "a force push whose dialog saw no tracking ref is rejected when a fetch creates it")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.commitFile(repo, "mine.txt", "mine\n", "Mine");
    s.git(repo, {"update-ref", "-d", "refs/remotes/origin/main"});
    const std::string before = s.gitOut(origin(s, repo), {"rev-parse", "main"});
    s.git(repo, {"fetch", "-q", "origin"}); // the ref appears after the dialog "saw" none
    s.session()->actions().push("origin", "main", "main", false, true, false, {}, std::string());
    s.settle();
    GG_CHECK(originHas(s, repo, "main", before));
}

GG_TEST("network", "a rejected push of a branch that is not checked out does not offer Pull then push")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"switch", "-q", "-c", "topic"});
    s.commitFile(repo, "t.txt", "topic\n", "Topic one");
    s.git(repo, {"switch", "-q", "main"});
    s.commitFile(repo, "x.txt", "x\n", "Remote topic");
    s.git(repo, {"push", "-q", "origin", "main:topic"});
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"branch", "-q", "--set-upstream-to=origin/topic", "topic"});
    const std::string mainBefore = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_topic/###branch_topic"); }));
    s.contextMenu("//Branches/branch_topic/###branch_topic", "Push");
    GG_REQUIRE(s.dialogOpen("Push rejected", 30.0f));
    bool offered = false;
    for (const auto& b : s.app.dialogs().current()->buttons)
        offered = offered || b.label == "Pull then push";
    GG_CHECK(!offered);
    s.dialogButton("Push rejected", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), mainBefore);
    GG_CHECK(!s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "HEAD^2"}).ok());
}

GG_TEST("network", "Pull then push works with a submodule")
{
    fs::path subRemote;
    const fs::path repo = superWithUnpushedSub(s, subRemote);
    const fs::path o = other(s, repo);
    s.git(s.root(), {"clone", "-q", fileUrl(origin(s, repo)), o.string()});
    s.commitFile(o, "theirs.txt", "t\n", "Theirs");
    s.git(o, {"push", "-q", "origin", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().push("origin", "main", "main", false, false);
    GG_REQUIRE(s.dialogOpen("Push rejected"));
    s.dialogButton("Push rejected", "Pull then push");
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();
}

GG_TEST("network", "a push pushes the submodule branch first and then the superproject")
{
    fs::path subRemote;
    const fs::path repo = superWithUnpushedSub(s, subRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().push("origin", "main", "main", false, false);
    GG_CHECK(s.waitUntil([&] { return originHas(s, repo, "main", s.head(repo)); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(subRemote, "main"), s.head(repo / "sub"));
    GG_CHECK(s.app.errorMessage().empty());
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("network", "a push shows the error when the submodule push fails")
{
    fs::path subRemote;
    const fs::path repo = superWithUnpushedSub(s, subRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.git(repo / "sub", {"remote", "set-url", "origin", fileUrl(subRemote.parent_path() / "gone.git")});
    s.session()->actions().push("origin", "main", "main", false, false);
    GG_REQUIRE(s.dialogOpen("push main to origin"));
    GG_CHECK(s.app.errorMessage().find("submodule") != std::string::npos);
    s.dialogButton("push main to origin", "OK");
    s.settle();
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK(!originHas(s, repo, "main", s.head(repo)));
}

GG_TEST("network", "push is refused when outgoing commits hold first-class conflicts")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const fs::path bare = s.path("conflicted-origin.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", fileUrl(bare)});
    s.git(repo, {"push", "-q", "origin", "main~2:refs/heads/main"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"branch", "--set-upstream-to=origin/main", "main"});
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push refused"));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f != nullptr);
    bool listsFile = false;
    for (const auto& row : f->revealRows)
        listsFile |= row.first.find("conflict.txt") != std::string::npos;
    GG_CHECK(listsFile);
    GG_CHECK(!originHas(s, repo, "main", s.head(repo)));
    // Reveal selects the commit of the first listed row. The walk lists the newest first, so that is HEAD.
    GG_REQUIRE(!f->revealRows.empty());
    GG_CHECK_STR_EQ(f->revealRows[0].second, s.head(repo));
    ctx->ItemClick("//Push refused/reveal_0/Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.head(repo); }));
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main~2"));
}

GG_TEST("network", "push tags is refused when a new tag points at a conflicted commit")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const fs::path bare = s.path("tags-origin.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", fileUrl(bare)});
    s.git(repo, {"push", "-q", "origin", "main~2:refs/heads/main"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"tag", "v1", "main~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().push("origin", "main", "main", false, false, true);
    GG_REQUIRE(s.dialogOpen("Push refused"));
    GG_CHECK(s.app.dialogs().current()->message.find("first-class conflicts") != std::string::npos);
    GG_CHECK(!originHas(s, repo, "refs/tags/v1", s.revParse(repo, "main~1")));
}

GG_TEST("network", "push is refused when outgoing commits left broken conflict markers")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const fs::path bare = s.path("broken-origin.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", fileUrl(bare)});
    // Push the conflicted history itself first, so the only outgoing commit below is the one
    // that breaks the markers, not the (already-pushed) first-class conflict.
    s.git(repo, {"push", "-q", "origin", "main"});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"branch", "--set-upstream-to=origin/main", "main"});
    // HEAD (main) still holds conflict.txt's conflict unchanged; break its markers instead of
    // resolving it.
    s.write(repo, "conflict.txt", "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\nx=2\n>>>>>>> side 2\nbottom\n");
    s.git(repo, {"add", "conflict.txt"});
    s.git(repo, {"commit", "-q", "-m", "Break the region"});
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//###Toolbar/###tb_push");
    GG_REQUIRE(s.dialogOpen("Push refused"));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f != nullptr);
    GG_CHECK(f->message.find("broken conflict markers") != std::string::npos);
    bool listsFile = false;
    for (const auto& row : f->revealRows)
        listsFile |= row.first.find("conflict.txt line 2, 7") != std::string::npos;
    GG_CHECK(listsFile);
    GG_CHECK(!originHas(s, repo, "main", s.head(repo)));
}

GG_TEST("network", "askpass: answer and cancel a credentials prompt")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.installSshShim("s3cret");
    const fs::path cancelled = s.path("askpass-cancelled");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", Scenario::sshUrl(origin(s, repo)));
    s.dialogText("Clone repository", "destination", cancelled.string());
    s.dialogButton("Clone repository", "Clone");
    GG_REQUIRE(s.dialogOpen("Credentials"));
    GG_CHECK(s.app.dialogs().current()->message.find("password") != std::string::npos);
    s.dialogButton("Credentials", "Cancel");
    GG_CHECK(s.waitUntil([&] { return s.app.clone().state() == ggui::core::CloneService::State::Idle; }));
    GG_CHECK(!fs::exists(cancelled));
    GG_CHECK(s.dismissError());
    s.app.clearError();

    const fs::path dest = s.path("askpass-cloned");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", Scenario::sshUrl(origin(s, repo)));
    s.dialogText("Clone repository", "destination", dest.string());
    s.dialogButton("Clone repository", "Clone");
    GG_REQUIRE(s.dialogOpen("Credentials"));
    s.dialogText("Credentials", "answer", "s3cret");
    s.dialogButton("Credentials", "OK");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->path() == dest; }));
    s.settle();
    s.track(dest);
    GG_CHECK_STR_EQ(s.head(dest), s.revParse(repo, "origin/main"));
}

// Sends one askpass request to ggui's server the way git-gg does. Empty `token`: the right one.
static gg::net::Socket askpassClient(const std::string& token, const std::string& prompt, bool closeNow)
{
    const std::string endpoint = gg::askpassEndpoint();
    const auto s = gg::net::connectLoopback(std::stoi(endpoint.substr(0, endpoint.find(':'))));
    if (s == gg::net::kInvalid)
        return s;
    gg::net::sendAll(s, (token.empty() ? endpoint.substr(endpoint.find(':') + 1) : token) + "\n" + prompt + "\n");
    if (closeNow) {
        gg::net::closeSocket(s);
        return gg::net::kInvalid;
    }
    return s;
}

GG_TEST("network", "askpass: a client with a wrong token gets no prompt and the server keeps serving")
{
    askpassClient("wrong-token", "Password for 'https://x':", true);
    ctx->Yield(10);
    GG_CHECK(!s.dialogOpen("Credentials"));
    askpassClient("", "Password for 'https://x':", true);
    GG_CHECK(s.waitUntil([&] { return s.dialogOpen("Credentials"); }));
}

GG_TEST("network", "askpass: a client without a token gets no prompt and the server keeps serving")
{
    const std::string endpoint = gg::askpassEndpoint();
    const auto bad = gg::net::connectLoopback(std::stoi(endpoint.substr(0, endpoint.find(':'))));
    GG_REQUIRE(bad != gg::net::kInvalid);
    gg::net::sendAll(bad, "\nPassword for 'https://x':\n"); // an empty token line
    std::string reply;
    const auto sent = std::chrono::steady_clock::now();
    GG_CHECK(!gg::net::recvLine(bad, reply, 5000) || reply.empty()); // the server closes: no credential
    // A close comes at once; a hung server would only end at its own 2 s read timeout.
    GG_CHECK(std::chrono::steady_clock::now() - sent < std::chrono::milliseconds(1500));
    gg::net::closeSocket(bad);
    ctx->Yield(10);
    GG_CHECK(!s.dialogOpen("Credentials"));
    askpassClient("", "Password for 'https://x':", true);
    GG_CHECK(s.waitUntil([&] { return s.dialogOpen("Credentials"); }));
}

GG_TEST("network", "askpass: the dialog closes when git goes away, then the next prompt shows")
{
    askpassClient("", "Password for 'https://x':", true);
    GG_CHECK(s.waitUntil([&] { return s.dialogOpen("Credentials"); }));
    GG_CHECK(s.waitUntil([&] { return !s.dialogOpen("Credentials"); }, static_cast<float>(timeBudgetMs(1000) / 1000.0)));
    const auto second = askpassClient("", "Are you sure you want to continue connecting (yes/no/[fingerprint])?", false);
    GG_REQUIRE(s.waitUntil([&] { return s.dialogOpen("Credentials"); }));
    const ggui::Form* f = s.app.dialogs().current();
    GG_REQUIRE(f != nullptr && f->field("answer") != nullptr);
    GG_CHECK(f->field("answer")->kind == ggui::Field::Text);
    s.dialogButton("Credentials", "Cancel");
    gg::net::closeSocket(second);
}

GG_TEST("network", "askpass: a closed peer closes only its own dialog, not the queued ones")
{
    askpassClient("", "Password for 'https://x':", true);
    GG_REQUIRE(s.waitUntil([&] { return s.dialogOpen("Credentials"); }));
    ggui::Form other;
    other.title = "Other dialog";
    other.buttons.push_back({"Cancel", {}});
    s.app.dialogs().open(std::move(other));
    GG_CHECK(s.waitUntil([&] { return s.dialogOpen("Other dialog"); }, static_cast<float>(timeBudgetMs(1000) / 1000.0)));
    GG_CHECK(!s.dialogOpen("Credentials"));
    s.dialogButton("Other dialog", "Cancel");
}

GG_TEST("network", "askpass: every prompt is secret except a known non-secret one")
{
    using ggui::core::AskpassServer;
    GG_CHECK(AskpassServer::isSecretPrompt("Password for 'https://x':"));
    GG_CHECK(AskpassServer::isSecretPrompt("user@host's password: "));
    GG_CHECK(AskpassServer::isSecretPrompt("Enter passphrase for key '/home/u/.ssh/id_ed25519': "));
    GG_CHECK(AskpassServer::isSecretPrompt("Enter PIN for 'PIV_II': "));
    GG_CHECK(AskpassServer::isSecretPrompt("Password for 'https://username@example.com':"));
    GG_CHECK(AskpassServer::isSecretPrompt("Password for 'https://fingerprint.example.com':"));
    GG_CHECK(AskpassServer::isSecretPrompt("Enter passphrase for key '/home/u/.ssh/fingerprint_key':"));
    GG_CHECK(AskpassServer::isSecretPrompt("Passcode:"));
    GG_CHECK(AskpassServer::isSecretPrompt("Verification code:"));
    GG_CHECK(AskpassServer::isSecretPrompt("PASSWORD:"));
    GG_CHECK(!AskpassServer::isSecretPrompt("Are you sure you want to continue connecting (yes/no/[fingerprint])?"));
    GG_CHECK(!AskpassServer::isSecretPrompt("Username for 'https://x':"));
}

GG_TEST("network", "remote actions are disabled while a mutation runs; browsing still works")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    gg::setSlowGitLatency(std::chrono::milliseconds(1500));
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    const bool busy = s.waitUntil([&] { return !s.session()->actions().busy().empty(); }, 5.0f);
    GG_CHECK(busy);
    ctx->Yield(2);
    GG_CHECK(disabled(s, "//###Toolbar/###tb_fetch"));
    GG_CHECK(disabled(s, "//###Toolbar/###tb_push"));
    GG_CHECK(!s.session()->activities().empty());
    // Browsing: selecting another commit still works.
    const std::string older = s.revParse(repo, "HEAD~1");
    ctx->ItemClick(("//History/**/###row_" + older).c_str());
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), older);
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(s.waitUntil([&] { return s.session()->actions().busy().empty(); }));
    s.settle();
    GG_CHECK(!disabled(s, "//###Toolbar/###tb_fetch"));
}

GG_TEST("network", "clone over git://: the server's progress shows its phase, not \"remote\"")
{
    // A repository with a few thousand objects, served by git daemon.
    const fs::path base = s.path("served");
    const fs::path src = base / "big.git";
    s.git(s.root(), {"init", "-q", "--bare", src.string()});
    std::string input;
    for (int i = 0; i < 3000; ++i)
        input += "M 100644 inline f" + std::to_string(i) + ".txt\ndata <<EOF\nfile " + std::to_string(i) + "\nEOF\n";
    s.git(src, {"fast-import", "--quiet"},
        "commit refs/heads/main\ncommitter T <t@example.com> 0 +0000\ndata <<EOF\nmany files\nEOF\n" + input + "\n");
    s.git(src, {"symbolic-ref", "HEAD", "refs/heads/main"});
    const std::string url = s.startGitDaemon(base) + "big.git";
    const fs::path dest = s.path("big-clone");
    ctx->ItemClick("//Welcome/###welcome_clone");
    GG_REQUIRE(s.dialogOpen("Clone repository"));
    s.dialogText("Clone repository", "url", url);
    s.dialogText("Clone repository", "destination", dest.string());
    s.dialogButton("Clone repository", "Clone");
    std::set<std::string> phases;
    GG_REQUIRE(s.waitUntil([&] {
        if (const std::string ph = s.app.clone().phase(); !ph.empty())
            phases.insert(ph);
        return s.session() && s.session()->path() == dest && s.session()->snapshot();
    }, 60.0f));
    s.settle();
    s.track(dest);
    // The set can be empty: a fast clone ends between two frames, and pumpClone clears the phase.
    GG_CHECK(!phases.count("remote"));
    GG_CHECK_STR_EQ(s.head(dest), s.revParse(src, "main"));
}

GG_TEST("network", "a password in a remote URL stays out of the log, the command log, the journal, the errors and the UI (GG-140)")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const std::string url = "https://alice:s3cret@invalid.invalid/r.git";
    s.git(repo, {"remote", "add", "backup", url});
    s.git(repo, {"remote", "set-url", "--push", "backup", "https://bob:pushs3cret@invalid.invalid/p.git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    s.commitFile(repo, "pw.txt", "p\n", "Needs a push");
    s.settle();
    gg::clearCommandLog();
    s.session()->actions().fetch("backup", false, false);
    s.settle();
    s.session()->actions().push("backup", "main", "main", false, false);
    s.settle();
    const std::string secret = "s3cret";
    auto has = [&](const std::string& text) { return text.find(secret) != std::string::npos; };
    // Not vacuous: the failed fetch and push reach the log with the host.
    bool hostInLog = false;
    for (const auto& line : ggui::recentLogLines()) {
        hostInLog = hostInLog || line.find("invalid.invalid") != std::string::npos;
        GG_CHECK(!has(line));
    }
    GG_CHECK(hostInLog);
    GG_CHECK(!gg::commandLog().empty());
    for (const auto& e : gg::commandLog()) {
        GG_CHECK(!has(e.err));
        for (const auto& a : e.args)
            GG_CHECK(!has(a));
    }
    for (const auto& [file, bytes] : s.gitDirBytes(repo))
        if (file != ".git/config") // the remote URL lives in the config: git owns that file
            GG_CHECK(!has(bytes));
    for (const auto& t : s.app.toasts()) {
        GG_CHECK(!has(t.title));
        GG_CHECK(!has(t.message));
    }
    if (s.app.dialogs().current()) {
        GG_CHECK(!has(s.app.dialogs().current()->title));
        GG_CHECK(!has(s.app.dialogs().current()->message));
        s.app.dialogs().closeCurrent(); // the dialog covers the Remotes panel
        s.settle();
    }
    ctx->MouseMove("//Remotes/remote_backup/###row");
    ctx->SleepNoSkip(kTooltipWait, 0.1f);
    for (const char* window : {"//Remotes", "//##Tooltip_00"})
        for (const auto& text : s.drawnText(window))
            GG_CHECK(!has(text));
    GG_CHECK(s.textShown("//Remotes", "backup@invalid.invalid"));
    GG_CHECK(s.textShown("//##Tooltip_00", "alice"));
    GG_CHECK(s.textShown("//##Tooltip_00", "bob")); // the push URL is hidden too
}

} // namespace ggtest
