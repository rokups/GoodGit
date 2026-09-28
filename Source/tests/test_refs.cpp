// Branch, tag and remote management (§4.7; P2-13, P2-14, P2-15).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string branchRow(const std::string& name)
{
    std::string id = name;
    std::replace(id.begin(), id.end(), '/', ':');
    return "//Branches/branch_" + id + "/###branch_" + id;
}

std::string tagRow(const std::string& name) { return "//Tags/tag_" + name + "/###tag_" + name; }

bool refExists(Scenario& s, const fs::path& repo, const std::string& ref)
{
    return s.gitMayFail(repo, {"rev-parse", "--verify", "-q", ref}).ok();
}

std::string symbolicHead(Scenario& s, const fs::path& repo)
{
    auto r = s.gitMayFail(repo, {"symbolic-ref", "-q", "--short", "HEAD"});
    return r.ok() ? gg::trim(r.out) : std::string("(detached)");
}

fs::path origin(Scenario& s, const fs::path& repo) { return s.root() / (repo.filename().string() + "-origin.git"); }

} // namespace

GG_TEST("refs", "create, check out, rename and delete branches", "BR-CREATE", "BR-CREATE-CHECKOUT-DEFAULT", "BR-CHECKOUT", "BR-RENAME",
    "BR-DELETE-LOCAL", "HIST-CTX-CREATE-BRANCH", "HIST-CTX-DELETE-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    ctx->ItemClick("//Branches/###create_branch");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "feature-x");
    // Checked out after creating by default.
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->checked("checkout"));
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "feature-x"; }));
    s.settle();
    s.contextMenu(branchRow("feature-x").c_str(), "Rename...");
    GG_REQUIRE(s.dialogOpen("Rename branch"));
    s.dialogText("Rename branch", "name", "feature-y");
    s.dialogButton("Rename branch", "Rename");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "feature-y"; }));
    s.settle();
    s.contextMenu(branchRow("main").c_str(), "Check out");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "main"; }));
    s.settle();
    s.contextMenu(branchRow("feature-y").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/feature-y"); }));
    s.settle();
    // From History: create a branch at an older commit, then delete it from the row menu.
    const std::string older = s.revParse(repo, "HEAD~3");
    s.contextMenu(("//History/**/###row_" + older).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "from-history");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/from-history"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "from-history"), older);
    s.contextMenu(("//History/**/###row_" + older).c_str(), "Delete branch/from-history");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/from-history"); }));
    s.settle();
}

GG_TEST("refs", "Branches: a tree split on '/', groups named by their common prefix; double-click checks out",
    "BR-TREE", "BR-DOUBLE-CLICK-CHECKOUT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* b : {"feature/one", "feature/two", "user/rk/a", "user/rk/b", "solo/x"})
        s.git(repo, {"branch", b});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    // Groups: "feature" and "user/rk" (the longest shared prefix); "solo/x" alone is not split.
    GG_CHECK(s.itemExists("//Branches/###group_local:feature:"));
    GG_CHECK(s.itemExists("//Branches/###group_local:user:rk:"));
    GG_CHECK(!s.itemExists("//Branches/###group_local:user:"));
    GG_CHECK(!s.itemExists("//Branches/###group_local:solo:"));
    // Rows keep their IDs and show the name below their group.
    GG_CHECK_STR_EQ(s.itemText(branchRow("user/rk/a").c_str()), "a");
    GG_CHECK_STR_EQ(s.itemText(branchRow("feature/two").c_str()), "two");
    GG_CHECK_STR_EQ(s.itemText(branchRow("solo/x").c_str()), "solo/x");
    GG_CHECK(s.itemExists(branchRow("main").c_str()));
    // A collapsed group hides its rows; filtering opens it again.
    ctx->ItemClick("//Branches/###group_local:user:rk:");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(branchRow("user/rk/a").c_str()));
    ctx->ItemInputValue("//Branches/##branch_filter", "rk/b");
    ctx->Yield(2);
    GG_CHECK(s.itemExists(branchRow("user/rk/b").c_str()));
    GG_CHECK(!s.itemExists(branchRow("feature/one").c_str()));
    ctx->ItemInputValue("//Branches/##branch_filter", "");
    ctx->Yield(2);
    // Double-click checks a branch out.
    ctx->ItemDoubleClick(branchRow("feature/one").c_str());
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "feature/one"; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(repo).empty());
}

GG_TEST("refs", "upstream: set, unset, fast-forward", "BR-SET-UPSTREAM", "BR-UNSET-UPSTREAM", "BR-FF-UPSTREAM",
    "BR-SET-UPSTREAM-FILTER")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "behind", "origin/main~1"});
    s.git(repo, {"branch", "--set-upstream-to=origin/main", "behind"});
    s.git(repo, {"branch", "loose", "HEAD"});
    s.git(repo, {"push", "-q", "origin", "HEAD~1:refs/heads/feature"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("behind").c_str(), "Fast-forward to upstream");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "behind") == s.revParse(repo, "origin/main"); }));
    s.settle();
    // The checked-out branch fast-forwards with its working tree.
    const std::string mainBefore = s.head(repo);
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->head.hex() == s.head(repo); }));
    s.contextMenu(branchRow("main").c_str(), "Fast-forward to upstream");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "origin/main"); }));
    s.settle();
    GG_CHECK(s.statusPorcelain(repo).empty());
    s.git(repo, {"reset", "-q", "--hard", mainBefore});
    s.contextMenu(branchRow("loose").c_str(), "Set upstream...");
    GG_REQUIRE(s.dialogOpen("Set upstream"));
    s.comboSelect("//Set upstream/Upstream of loose##upstream", "origin/main");
    s.dialogButton("Set upstream", "Set");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "branch.loose.merge"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "--abbrev-ref", "loose@{upstream}"}), "origin/main");
    // The branch list has a filter: typing narrows it, Enter picks the first match.
    s.contextMenu(branchRow("loose").c_str(), "Set upstream...");
    GG_REQUIRE(s.dialogOpen("Set upstream"));
    ctx->ItemClick("//Set upstream/Upstream of loose##upstream");
    ctx->Yield(2);
    ctx->KeyCharsAppend("feat");
    ctx->Yield(2);
    GG_CHECK(ctx->ItemExists(("//$FOCUSED/" + Scenario::escapeRef("origin/feature")).c_str()));
    GG_CHECK(!ctx->ItemExists(("//$FOCUSED/" + Scenario::escapeRef("origin/main")).c_str()));
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(2);
    s.dialogButton("Set upstream", "Set");
    GG_CHECK(s.waitUntil([&] {
        return gg::trim(s.gitMayFail(repo, {"rev-parse", "--abbrev-ref", "loose@{upstream}"}).out) == "origin/feature";
    }));
    s.settle();
    s.contextMenu(branchRow("loose").c_str(), "Unset upstream");
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(repo, {"config", "branch.loose.merge"}).ok(); }));
    s.settle();
}

GG_TEST("refs", "move a branch; warning for a branch checked out elsewhere", "BR-MOVE", "BR-MOVE-WORKTREE-WARN",
    "HIST-CTX-MOVE-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    s.git(repo, {"branch", "mover", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string target = s.revParse(repo, "HEAD~1");
    s.contextMenu(("//History/**/###row_" + target).c_str(), "Move branch/mover");
    GG_REQUIRE(s.dialogOpen("Move branch"));
    s.dialogButton("Move branch", "Move");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "mover") == target; }));
    s.settle();
    // wt1 is checked out in a linked worktree: the dialog warns.
    s.contextMenu(("//History/**/###row_" + target).c_str(), "Move branch/wt1");
    GG_REQUIRE(s.dialogOpen("Move branch"));
    GG_CHECK(s.app.dialogs().current()->message.find("checked out in worktree") != std::string::npos);
    s.dialogButton("Move branch", "Cancel");
    s.settle();
    // The checked-out branch moves with its working tree (a local change stays).
    s.write(repo, "untracked-note.txt", "kept\n");
    s.contextMenu(("//History/**/###row_" + target).c_str(), "Move branch/main");
    GG_REQUIRE(s.dialogOpen("Move branch"));
    s.dialogButton("Move branch", "Move");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    GG_CHECK_STR_EQ(s.read(repo, "untracked-note.txt"), "kept\n");
    fs::remove(repo / "untracked-note.txt");
}

GG_TEST("refs", "delete a branch on its remote, and everywhere", "BR-DELETE-REMOTE", "BR-DELETE-ALL",
    "REMOTE-PUSH-DELETE-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "remote-only", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "remote-only"});
    s.git(repo, {"branch", "both", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "both"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("remote-only").c_str(), "Delete/On its remote");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "remote-only"}).empty(); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/remote-only")); // local stays
    s.contextMenu(branchRow("both").c_str(), "Delete/Local and all remotes");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogCheck("Delete branch", "force", "Delete even if not merged (-D)");
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/both"); }));
    s.settle();
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "both"}).empty());
}

GG_TEST("refs", "tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote", "TAG-CREATE",
    "TAG-ANNOTATED", "TAG-DELETE", "TAG-PUSH", "TAG-DELETE-REMOTE", "TAG-DELETE-MENU", "TAG-REMOTE-ONLY")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    // A tag only on origin.
    s.git(origin(s, repo), {"tag", "remote-only", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    auto remoteHas = [&](const std::string& tag) {
        const auto& tags = s.session()->remoteTags();
        auto it = tags.find("origin");
        return it != tags.end() && it->second.ok && it->second.tags.count(tag) != 0;
    };
    // Listed (read with git ls-remote while the panel is shown); Delete ▸ Local is disabled for it.
    GG_REQUIRE(s.waitUntil([&] { return remoteHas("remote-only"); }));
    const std::string remoteOnly = "//Tags/rtag_remote-only/###rtag_remote-only";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(remoteOnly.c_str()); }));
    GG_CHECK(s.itemText(remoteOnly.c_str()).rfind("remote-only  (origin)", 0) == 0);
    s.contextMenu(remoteOnly.c_str(), "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "remote-only");
    ctx->ItemClick(remoteOnly.c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Delete");
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Local").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(remoteOnly.c_str(), "Delete/origin");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, origin(s, repo), "refs/tags/remote-only"); }));
    GG_CHECK(s.waitUntil([&] { return !s.itemExists(remoteOnly.c_str()); }));
    s.settle();
    ctx->ItemClick("//Tags/###create_tag");
    GG_REQUIRE(s.dialogOpen("Create tag"));
    s.dialogText("Create tag", "name", "v9");
    s.dialogButton("Create tag", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/tags/v9"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"cat-file", "-t", "v9"}), "commit");
    ctx->ItemClick("//Tags/###create_tag");
    GG_REQUIRE(s.dialogOpen("Create tag"));
    s.dialogText("Create tag", "name", "v10");
    s.dialogCheck("Create tag", "annotated", "Annotated (with a message)");
    s.dialogText("Create tag", "message", "Release ten");
    s.dialogButton("Create tag", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/tags/v10"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"cat-file", "-t", "v10"}), "tag");
    GG_CHECK(s.gitOut(repo, {"tag", "-n1", "v10"}).find("Release ten") != std::string::npos);
    s.contextMenu(tagRow("v10").c_str(), "Push tag/origin");
    GG_CHECK(s.waitUntil([&] { return refExists(s, origin(s, repo), "refs/tags/v10"); }));
    s.settle();
    // On origin now (read again after the push): Delete is a submenu, Local first.
    GG_REQUIRE(s.waitUntil([&] { return remoteHas("v10"); }));
    s.contextMenu(tagRow("v10").c_str(), "Delete/Local");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/tags/v10"); }));
    s.settle();
    GG_CHECK(refExists(s, origin(s, repo), "refs/tags/v10"));
    // Now only on origin: deleted there from its row.
    const std::string v10 = "//Tags/rtag_v10/###rtag_v10";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(v10.c_str()); }));
    s.contextMenu(v10.c_str(), "Delete/origin");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, origin(s, repo), "refs/tags/v10"); }));
    s.settle();
    GG_REQUIRE(s.waitUntil([&] { return !remoteHas("v10"); }));
    // Only here: a single Delete item.
    s.contextMenu(tagRow("v9").c_str(), "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/tags/v9"); }));
    s.settle();
}

GG_TEST("refs", "remotes: add, edit URL, prune on fetch, delete", "REM-ADD", "REM-EDIT-URL", "REM-PRUNE-ON-FETCH",
    "REM-DELETE")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    ctx->ItemClick("//Remotes/###add_remote");
    GG_REQUIRE(s.dialogOpen("Add remote"));
    s.dialogText("Add remote", "name", "backup");
    s.dialogText("Add remote", "url", "file://" + origin(s, repo).generic_string());
    s.dialogButton("Add remote", "Add");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"remote", "get-url", "backup"}).ok(); }));
    s.settle();
    s.contextMenu("//Remotes/remote_backup/###row", "Edit URL...");
    GG_REQUIRE(s.dialogOpen("Edit remote URL"));
    s.dialogText("Edit remote URL", "url", "https://example.invalid/backup.git");
    s.dialogButton("Edit remote URL", "Save");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"remote", "get-url", "backup"}) == "https://example.invalid/backup.git"; }));
    s.settle();
    s.contextMenu("//Remotes/remote_backup/###row", "Prune on fetch");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "remote.backup.prune"}).out == "true\n"; }));
    s.settle();
    s.contextMenu("//Remotes/remote_backup/###row", "Prune on fetch");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "remote.backup.prune"}).out == "false\n"; }));
    s.settle();
    s.contextMenu("//Remotes/remote_backup/###row", "Delete");
    GG_REQUIRE(s.dialogOpen("Delete remote"));
    s.dialogButton("Delete remote", "Delete");
    GG_CHECK(s.waitUntil([&] { return !s.gitMayFail(repo, {"remote", "get-url", "backup"}).ok(); }));
    s.settle();
}

GG_TEST("refs", "create a branch from a reflog entry", "REFLOG-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string old = s.head(repo);
    s.git(repo, {"reset", "-q", "--hard", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Reflog");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Reflog/##reflog_table/r0/###reflog_0"); }));
    s.contextMenu("//Reflog/##reflog_table/r0/###reflog_0", "Create branch from old...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "rescued");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/rescued"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "rescued"), old);
}

} // namespace ggtest
