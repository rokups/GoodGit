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

GG_TEST("refs", "create, check out, rename and delete branches", "BR-CREATE", "BR-CHECKOUT", "BR-RENAME",
    "BR-DELETE-LOCAL", "HIST-CTX-CREATE-BRANCH", "HIST-CTX-DELETE-BRANCH")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    ctx->ItemClick("//Branches/###create_branch");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "feature-x");
    s.dialogCheck("Create branch", "checkout", "Check out after creating");
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
        return s.gitOut(repo, {"rev-parse", "--abbrev-ref", "loose@{upstream}"}) == "origin/feature";
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

GG_TEST("refs", "tags: lightweight, annotated, delete, push, delete on remote", "TAG-CREATE", "TAG-ANNOTATED", "TAG-DELETE",
    "TAG-PUSH", "TAG-DELETE-REMOTE")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
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
    s.contextMenu(tagRow("v10").c_str(), "Delete on remote/origin");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, origin(s, repo), "refs/tags/v10"); }));
    s.settle();
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
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/rescued"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "rescued"), old);
}

} // namespace ggtest
