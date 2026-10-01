// Clone, fetch, pull, push, askpass (§4.1 toolbar, §4.7, §4.8).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <set>

#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>

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
    s.write(s.root(), "hanging-ssh", "#!/bin/sh\nexec sleep 60\n");
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
    s.write(s.root(), "stubborn-ssh", "#!/bin/sh\ntrap '' TERM\nsleep 60 &\nwait\n");
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
    // Tag on an old commit, only on origin (before any fetch sees it).
    s.git(other(s, repo), {"tag", "old-tag", "HEAD~2"});
    s.git(other(s, repo), {"push", "-q", "origin", "old-tag"});
    const std::string localMain = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    // Badges: 1 ahead, 1 behind.
    GG_CHECK(s.itemText("//###Toolbar/###tb_pull").find("\xe2\x86\x93" "1") != std::string::npos);
    GG_CHECK(s.itemText("//###Toolbar/###tb_push").find("\xe2\x86\x91" "1") != std::string::npos);

    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch tags");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/tags/old-tag"}).ok(); }));
    s.settle();

    remoteCommit(s, repo, "r2.txt", "two\n");
    ctx->ItemClick("//###Toolbar/###tb_fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == s.head(other(s, repo)); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), localMain); // no automatic fast-forward / merge
    GG_CHECK(s.waitUntil([&] { return s.itemText("//###Toolbar/###tb_pull").find("\xe2\x86\x93" "2") != std::string::npos; }));

    // A branch only on "second": fetch that remote from the dropdown.
    s.git(second, {"branch", "only-second", "main~1"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch second");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "second/only-second"}).ok(); }));
    s.settle();

    // Prune: the branch disappears on the remote.
    s.git(second, {"branch", "-D", "only-second"});
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch and prune");
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "second/only-second"}).ok(); }));
    s.settle();

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

GG_TEST("network", "fetch from an unreachable remote reports the failure")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "dead", "git://127.0.0.1:1/nothing.git"});
    GG_REQUIRE(s.openRepository(repo));
    popupItem(s, "//###Toolbar/###tb_fetch_menu", "Fetch dead");
    GG_CHECK(s.waitUntil([&] { return !s.app.errorMessage().empty(); }));
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK(s.session()->actions().busy().empty());
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
    // The stash was re-applied: the local edit is back (in conflict with the incoming one, or merged).
    GG_CHECK(s.read(repo, "local-only.txt") == "local uncommitted\n");
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
    const std::string conflicted = s.revParse(repo, "main~1");
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
    // Reveal jumps to the conflicted commit (the first listed row is the oldest or newest; either is conflicted).
    ctx->ItemClick("//Push refused/reveal_0/Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == conflicted || s.session()->selection().id.hex() == s.head(repo); }));
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main~2"));
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
    GG_CHECK(!phases.count("remote"));
    GG_CHECK_STR_EQ(s.head(dest), s.revParse(src, "main"));
}

} // namespace ggtest
