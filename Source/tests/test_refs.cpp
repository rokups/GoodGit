// Branch, tag and remote management (§4.7).
#include "panels/HistoryPanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include <libgg/GitRunner.hpp>

namespace ggtest {

namespace {

std::string branchRow(const std::string& name)
{
    std::string id = name;
    std::replace(id.begin(), id.end(), '/', ':');
    return "//Branches/branch_" + id + "/###branch_" + id;
}

// The row of the remote-tracking branch `name` ("origin/x") in the Branches panel.
std::string remoteRow(const std::string& name)
{
    std::string id = name;
    std::replace(id.begin(), id.end(), '/', ':');
    return "//Branches/remote_group_origin/origin/rbranch_" + id + "/###rbranch_" + id;
}

// Selects the remote-tracking branch rows `names`: a click on the first, Ctrl+click on the others.
void selectRemoteRows(Scenario& s, const std::vector<std::string>& names)
{
    ImGuiTestContext* ctx = s.ctx;
    ctx->ItemClick(remoteRow(names.front()).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    for (size_t i = 1; i < names.size(); ++i)
        ctx->ItemClick(remoteRow(names[i]).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
}

// A repository where the branches `names` are on origin and fetched, with no local branch of these names.
fs::path repoWithRemoteBranches(Scenario& s, const std::vector<std::string>& names)
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const auto& name : names) {
        s.git(repo, {"branch", name, "HEAD~1"});
        s.git(repo, {"push", "-q", "origin", name});
        s.git(repo, {"branch", "-D", name});
    }
    s.git(repo, {"fetch", "-q", "origin"});
    return repo;
}

std::string tagRow(std::string name)
{
    std::replace(name.begin(), name.end(), '/', ':'); // row IDs have ':' for '/'
    return "//Tags/tag_" + name + "/###tag_" + name;
}

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

// The Delete branch dialog's warning ("" for none), and whether its "Merged into" row is shown: both
// follow the answer of the merged check, which arrives a moment after the dialog opens.
std::string deleteWarning(Scenario& s)
{
    const ggui::Form* form = s.app.dialogs().current();
    const ggui::Field* w = form ? form->field("unmerged_warning") : nullptr;
    return w && w->live ? w->live() : std::string();
}

bool mergedRowShown(Scenario& s)
{
    const ggui::Form* form = s.app.dialogs().current();
    if (form)
        for (const auto& f : form->fields)
            if (f.text.find("Merged into:") == 0)
                return !f.visible || f.visible(*form);
    return false;
}

} // namespace

GG_TEST("refs", "delete two selected branches from the row menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* name : {"a", "b", "c"})
        s.git(repo, {"branch", name, "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("c").c_str()); }));
    ctx->ItemClick(branchRow("a").c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(branchRow("b").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    s.contextMenu(branchRow("b").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    GG_CHECK(s.waitUntil([&] {
        const ggui::Form* form = s.app.dialogs().current();
        return form && form->buttons[0].enabled(*form);
    }, 5.0f));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/a") && !refExists(s, repo, "refs/heads/b"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/c"));
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/a") && refExists(s, repo, "refs/heads/b"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/c"));
}

GG_TEST("refs", "the row menu of an unselected branch stays the single menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* name : {"a", "b", "c"})
        s.git(repo, {"branch", name, "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("c").c_str()); }));
    ctx->ItemClick(branchRow("a").c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(branchRow("b").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    // Rename is an item of the single menu only. The right-click does not change the selection.
    s.contextMenu(branchRow("c").c_str(), "Rename...");
    GG_CHECK(s.dialogOpen("Rename branch"));
    GG_CHECK(s.session()->branches().selection().names.size() == 2);
    s.dialogButton("Rename branch", "Cancel");
}

namespace {

// Selects the local branches `names` in the Branches panel: a click on the first, Ctrl+click on the others.
void selectBranches(Scenario& s, const std::vector<std::string>& names)
{
    ImGuiTestContext* ctx = s.ctx;
    ctx->ItemClick(branchRow(names.front()).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    for (size_t i = 1; i < names.size(); ++i)
        ctx->ItemClick(branchRow(names[i]).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
}

// A repository with the branches a and b (with an upstream) and c and d (without one).
fs::path repoWithUpstreams(Scenario& s)
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"a", "b", "c", "d"})
        s.git(repo, {"branch", name, "HEAD~1"});
    s.git(repo, {"branch", "--set-upstream-to=origin/main", "a"});
    s.git(repo, {"push", "-q", "-u", "origin", "b"});
    return repo;
}

} // namespace

GG_TEST("refs", "the multi menu deletes on the remote only the selected branches that have an upstream")
{
    const fs::path repo = repoWithUpstreams(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("d").c_str()); }));
    selectBranches(s, {"b", "c"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    s.contextMenu(branchRow("c").c_str(), "Delete/On its remote");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    // Only the branch with an upstream is in the list.
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    GG_CHECK_STR_EQ(form->message, "Delete 1 branch on their remotes?");
    const auto checked = form->checkedOptions("branches");
    GG_CHECK(checked.size() == 1 && checked.front() == "b");
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "b"}).empty(); }));
    s.settle();
    // The local branches stay.
    GG_CHECK(refExists(s, repo, "refs/heads/b"));
    GG_CHECK(refExists(s, repo, "refs/heads/c"));
}

GG_TEST("refs", "the multi menu item On its remote is disabled when no selected branch has an upstream")
{
    const fs::path repo = repoWithUpstreams(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("d").c_str()); }));
    const auto disabled = [&](const char* item) -> bool {
        const ImGuiTestItemInfo info = ctx->ItemInfo((std::string("//$FOCUSED/") + item).c_str(), ImGuiTestOpFlags_NoError);
        GG_CHECK(info.ID != 0);
        return (info.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    const auto openDelete = [&](const char* row) {
        ctx->ItemClick(row, ImGuiMouseButton_Right);
        ctx->MenuAction(ImGuiTestAction_Open, "//$FOCUSED/Delete");
        ctx->Yield(2);
    };
    const auto closeMenus = [&] {
        ctx->PopupCloseAll();
        ctx->Yield(2);
    };
    // No upstream: the item is disabled and the other two are not.
    selectBranches(s, {"c", "d"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    openDelete(branchRow("d").c_str());
    GG_CHECK(disabled("On its remote"));
    GG_CHECK(!disabled("Local"));
    GG_CHECK(!disabled("Local and all remotes"));
    closeMenus();
    GG_CHECK(!s.dialogOpen("Delete branches", 0.5f));
    // One selected branch with an upstream enables the item.
    selectBranches(s, {"a", "d"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    openDelete(branchRow("d").c_str());
    GG_CHECK(!disabled("On its remote"));
    closeMenus();
}

GG_TEST("refs", "the multi menu deletes the selected branches locally and on all remotes")
{
    const fs::path repo = repoWithUpstreams(s);
    s.git(repo, {"push", "-q", "origin", "c"}); // c has a remote branch of its name, without an upstream
    s.git(repo, {"fetch", "-q", "origin"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("d").c_str()); }));
    selectBranches(s, {"b", "c"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    s.contextMenu(branchRow("c").c_str(), "Delete/Local and all remotes");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    GG_CHECK_STR_EQ(form->message, "Delete 2 branches and every remote branch with the same name?");
    GG_CHECK(form->checkedOptions("branches").size() == 2);
    GG_CHECK(s.waitUntil([&] {
        const ggui::Form* f = s.app.dialogs().current();
        return f && f->buttons[0].enabled(*f);
    }, 5.0f));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/b") && !refExists(s, repo, "refs/heads/c"); }));
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "b", "c"}).empty(); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/a"));
    GG_CHECK(refExists(s, repo, "refs/heads/d"));
}

GG_TEST("refs", "the multi menu shows or hides all selected branches in History")
{
    const fs::path repo = repoWithUpstreams(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("d").c_str()); }));
    const auto& history = s.session()->history();
    const auto visible = [&](const char* name) { return history.refVisible(std::string("refs/heads/") + name); };
    GG_CHECK(visible("a") && visible("b") && visible("c"));
    selectBranches(s, {"a", "b"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    // Hide: both selected branches and the remote-tracking ref of b go. The others stay.
    s.toggleVisibility(branchRow("b").c_str());
    GG_CHECK(s.waitUntil([&] { return !visible("a") && !visible("b"); }));
    GG_CHECK(!history.refVisible("refs/remotes/origin/b"));
    GG_CHECK(visible("c") && visible("d") && visible("main"));
    // Show: both come back.
    s.toggleVisibility(branchRow("b").c_str());
    GG_CHECK(s.waitUntil([&] { return visible("a") && visible("b"); }));
    GG_CHECK(history.refVisible("refs/remotes/origin/b"));
}

GG_TEST("refs", "create, check out, rename and delete branches")
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
    // From History: create a branch at an older commit, then delete it from the Branches panel.
    const std::string older = s.revParse(repo, "HEAD~3");
    s.contextMenu(("//History/**/###row_" + older).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogText("Create branch", "name", "from-history");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/from-history"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "from-history"), older);
    s.contextMenu(branchRow("from-history").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/from-history"); }));
    s.settle();
}

GG_TEST("refs", "Branches: a tree split on '/', groups named by their common prefix; double-click checks out")
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

GG_TEST("refs", "upstream: set, unset, fast-forward")
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

GG_TEST("refs", "move a branch; warning for a branch checked out elsewhere")
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

GG_TEST("refs", "Move branch names the branch and its tip, prefills the destination commit with a live preview, and moves to what the field says")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    s.git(repo, {"branch", "mover", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string target = s.revParse(repo, "HEAD~1");
    auto subjectOf = [&](const std::string& rev) { return gg::trim(s.gitOut(repo, {"log", "-1", "--format=%s", rev})); };
    auto form = [&]() -> const ggui::Form* { return s.app.dialogs().current(); };
    s.contextMenu(("//History/**/###row_" + target).c_str(), "Move branch/mover");
    GG_REQUIRE(s.dialogOpen("Move branch"));
    GG_REQUIRE(form() && form()->field("to"));
    // The source: the branch and its current tip; the destination: the clicked commit, previewed.
    GG_CHECK(form()->fields.front().kind == ggui::Field::Info);
    GG_CHECK(form()->fields.front().text.find("Branch: mover") == 0);
    GG_CHECK(form()->fields.front().text.find(subjectOf("mover")) != std::string::npos);
    GG_CHECK_STR_EQ(form()->text("to"), target);
    GG_CHECK(s.waitUntil([&] { return form()->field("to")->preview.line().find(subjectOf(target)) != std::string::npos; }, 3.0f));
    // Retyping the destination updates the preview, and Move goes where the field says.
    s.dialogText("Move branch", "to", "HEAD");
    GG_CHECK(s.waitUntil([&] { return form()->field("to")->preview.line().find(subjectOf("HEAD")) != std::string::npos; }, 3.0f));
    s.dialogText("Move branch", "to", "no-such-ref");
    GG_CHECK(s.waitUntil([&] { return form()->field("to")->preview.warning; }, 3.0f));
    s.dialogText("Move branch", "to", "HEAD");
    s.dialogButton("Move branch", "Move");
    const std::string head = s.revParse(repo, "HEAD");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "mover") == head; }));
    s.settle();
}

GG_TEST("refs", "Create branch and Delete branch name their commits: the start commit is an input prefilled with the clicked one")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    const std::string older = s.revParse(repo, "HEAD~2");
    auto subjectOf = [&](const std::string& rev) { return gg::trim(s.gitOut(repo, {"log", "-1", "--format=%s", rev})); };
    auto form = [&]() -> const ggui::Form* { return s.app.dialogs().current(); };
    s.contextMenu(("//History/**/###row_" + older).c_str(), "Create branch...");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    GG_REQUIRE(form() && form()->field("at"));
    GG_CHECK_STR_EQ(form()->text("at"), older);
    GG_CHECK(s.waitUntil([&] { return form()->field("at")->preview.line().find(subjectOf(older)) != std::string::npos; }, 3.0f));
    // The typed start commit is used, not the clicked one.
    s.dialogText("Create branch", "at", "HEAD~1");
    s.dialogText("Create branch", "name", "at-field");
    s.dialogCheck("Create branch", "checkout", "Check out after creating", false);
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/at-field"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "at-field"), s.revParse(repo, "HEAD~1"));
    // Delete branch: the branch's tip is named, and what a safe delete checks it against.
    s.contextMenu(branchRow("at-field").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    GG_REQUIRE(form() && form()->fields.size() >= 2);
    GG_CHECK(form()->fields[0].text.find("Branch: at-field") == 0);
    GG_CHECK(form()->fields[0].text.find(subjectOf("at-field")) != std::string::npos);
    GG_CHECK(form()->fields[1].text.find("Merged into: HEAD") == 0);
    s.dialogButton("Delete branch", "Cancel");
    s.settle();
}

GG_TEST("refs", "Move branch is disabled on a row where every branch already points")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string head = s.head(repo);
    const std::string older = s.revParse(repo, "HEAD~1");
    const std::string hereRef = "//History/**/###row_" + head;
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(hereRef.c_str()); }));
    ctx->ItemClick(hereRef.c_str(), ImGuiMouseButton_Right);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Move branch").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->KeyPress(ImGuiKey_Escape);
    // Another row still offers main.
    const std::string olderRef = "//History/**/###row_" + older;
    ctx->ItemClick(olderRef.c_str(), ImGuiMouseButton_Right);
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/Move branch").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("refs", "Delete branch warns about an unmerged branch and deletes it anyway; a merged branch gets no warning")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "merged-one", "HEAD~1"});
    s.git(repo, {"switch", "-q", "-c", "unmerged-one", "HEAD~1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "only here"});
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto form = [&]() -> const ggui::Form* { return s.app.dialogs().current(); };
    auto warning = [&] { return deleteWarning(s); };
    auto mergedShown = [&] { return mergedRowShown(s); };
    // Merged: the row names what it is merged into, and there is no warning.
    s.contextMenu(branchRow("merged-one").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    GG_CHECK(s.waitUntil([&] { return mergedShown(); }, 5.0f));
    GG_CHECK(warning().empty());
    GG_CHECK(!form()->field("force"));
    s.dialogButton("Delete branch", "Cancel");
    s.settle();
    // Not merged: the warning shows, the row is gone, and Delete removes the branch.
    s.contextMenu(branchRow("unmerged-one").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    GG_CHECK(s.waitUntil([&] { return !warning().empty(); }, 5.0f));
    GG_CHECK(warning().find("'unmerged-one' is not merged into HEAD") != std::string::npos);
    GG_CHECK(!mergedShown());
    GG_CHECK(!form()->field("force"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/unmerged-one"); }));
    s.settle();
}

GG_TEST("refs", "delete branches dialog deletes the checked branches in one undo step")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* name : {"a", "b", "c"})
        s.git(repo, {"branch", name, "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteBranchesDialog({"a", "b", "c"}, 0);
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    // Delete waits for the merged check of every checked branch.
    GG_CHECK(s.waitUntil([&] {
        const ggui::Form* form = s.app.dialogs().current();
        return form && form->buttons[0].enabled(*form);
    }, 5.0f));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete 3 local branches?");
    GG_CHECK(deleteWarning(s).empty());
    ctx->ItemUncheck("//Delete branches/**/c##branches_c");
    GG_CHECK(s.app.dialogs().current()->checkedOptions("branches").size() == 2);
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/a") && !refExists(s, repo, "refs/heads/b"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/c"));
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/heads/a") && refExists(s, repo, "refs/heads/b"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/c"));
}

GG_TEST("refs", "delete branches dialog leaves out the current branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "a", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteBranchesDialog({"main", "a"}, 0);
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    const ggui::Field* list = form->field("branches");
    GG_REQUIRE(list != nullptr);
    GG_CHECK(list->options == std::vector<std::string>{"a"});
    const ggui::Field* note = form->field("head_note");
    GG_REQUIRE(note != nullptr);
    GG_CHECK_STR_EQ(note->text, "The current branch 'main' is not in the list.");
    s.dialogButton("Delete branches", "Cancel");
    s.settle();
    // Only the current branch: no dialog.
    s.session()->showDeleteBranchesDialog({"main"}, 2);
    s.settle();
    GG_CHECK(!s.dialogOpen("Delete branches"));
}

GG_TEST("refs", "delete branches dialog: an unmerged branch shows the warning and is deleted with force")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "merged-b", "HEAD~1"});
    s.git(repo, {"switch", "-q", "-c", "unmerged-b", "HEAD~1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "only here"});
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteBranchesDialog({"merged-b", "unmerged-b"}, 0);
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    GG_CHECK(s.waitUntil([&] { return deleteWarning(s).find("unmerged-b") != std::string::npos; }, 5.0f));
    const std::string warning = deleteWarning(s);
    GG_CHECK(warning.find("Not merged into HEAD") != std::string::npos);
    GG_CHECK(warning.find("merged-b,") == std::string::npos);
    // A branch that is not checked leaves the warning.
    ctx->ItemUncheck("//Delete branches/**/unmerged-b##branches_unmerged-b");
    GG_CHECK(s.waitUntil([&] { return deleteWarning(s).empty(); }, 5.0f));
    ctx->ItemCheck("//Delete branches/**/unmerged-b##branches_unmerged-b");
    GG_CHECK(s.waitUntil([&] { return !deleteWarning(s).empty(); }, 5.0f));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/unmerged-b") && !refExists(s, repo, "refs/heads/merged-b"); }));
    s.settle();
}

GG_TEST("refs", "delete branches dialog: mode 2 deletes the local branches and their remote branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"both1", "both2"}) {
        s.git(repo, {"branch", name, "HEAD~1"});
        s.git(repo, {"push", "-q", "-u", "origin", name});
    }
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteBranchesDialog({"both1", "both2"}, 2);
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    GG_CHECK(s.waitUntil([&] {
        const ggui::Form* form = s.app.dialogs().current();
        return form && form->buttons[0].enabled(*form);
    }, 5.0f));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/both1") && !refExists(s, repo, "refs/heads/both2"); }));
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "both*"}).empty(); }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/both1"));
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/both2"));
}

GG_TEST("refs", "delete remote branches dialog deletes the checked branches on the remote")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"r1", "r2", "r3"}) {
        s.git(repo, {"branch", name, "HEAD~1"});
        s.git(repo, {"push", "-q", "origin", name});
    }
    s.git(repo, {"fetch", "-q", "origin"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteRemoteBranchesDialog({"origin/r1", "origin/r2", "origin/r3"});
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these branches on their remotes?");
    GG_CHECK(s.app.dialogs().current()->checkedOptions("branches").size() == 3);
    ctx->ItemUncheck((s.child("//Delete branches", "##branches_list") + "/origin\\/r3##branches_origin\\/r3").c_str());
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"ls-remote", "--heads", "."}).find("refs/heads/r1") == std::string::npos; }));
    s.settle();
    const std::string heads = s.gitOut(origin(s, repo), {"ls-remote", "--heads", "."});
    GG_CHECK(heads.find("refs/heads/r2") == std::string::npos);
    GG_CHECK(heads.find("refs/heads/r3") != std::string::npos);
    GG_CHECK(refExists(s, repo, "refs/heads/r1")); // the local branch stays
}

GG_TEST("refs", "forget refs dialog removes the checked remote-tracking branches; one undo restores them")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"f1", "f2", "f3"}) {
        s.git(repo, {"branch", name, "HEAD~1"});
        s.git(repo, {"push", "-q", "origin", name});
    }
    s.git(repo, {"fetch", "-q", "origin"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showForgetRefsDialog({"origin/f1", "origin/f2", "origin/f3"});
    GG_REQUIRE(s.dialogOpen("Forget"));
    ctx->ItemUncheck((s.child("//Forget", "##branches_list") + "/origin\\/f3##branches_origin\\/f3").c_str());
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/f1") && !refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f3"));
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "f1"}).find("f1") != std::string::npos); // the remote keeps it
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/remotes/origin/f1") && refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f3"));
}

GG_TEST("refs", "the menu of a remote-tracking row forgets only the local ref; one undo restores it")
{
    const fs::path repo = repoWithRemoteBranches(s, {"f1", "f2"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.itemExists(remoteRow("origin/f1").c_str()));
    ctx->ItemClick(remoteRow("origin/f1").c_str(), ImGuiMouseButton_Right);
    for (const char* item : {"Delete on remote...", "Forget..."})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(remoteRow("origin/f1").c_str(), "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    const auto checked = s.app.dialogs().current()->checkedOptions("branches");
    GG_REQUIRE(checked.size() == 1);
    GG_CHECK_STR_EQ(checked.front(), "origin/f1");
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/f1"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f2"));
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "f1"}).find("f1") != std::string::npos); // the remote keeps it
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/remotes/origin/f1"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f2"));
}

GG_TEST("refs", "the multi menu of remote-tracking rows forgets them in one undo step")
{
    const fs::path repo = repoWithRemoteBranches(s, {"f1", "f2", "f3"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.itemExists(remoteRow("origin/f3").c_str()));
    selectRemoteRows(s, {"origin/f1", "origin/f2"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    ctx->ItemClick(remoteRow("origin/f2").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    for (const char* item : {"###history_visibility", "Delete on remote...", "Forget..."})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(remoteRow("origin/f2").c_str(), "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    GG_CHECK(s.app.dialogs().current()->checkedOptions("branches") == (std::vector<std::string>{"origin/f1", "origin/f2"}));
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/f1") && !refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f3"));
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/remotes/origin/f1") && refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/f3"));
}

GG_TEST("refs", "the multi menu of remote-tracking rows deletes the selected branches on the remote")
{
    const fs::path repo = repoWithRemoteBranches(s, {"f1", "f2", "f3"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.itemExists(remoteRow("origin/f3").c_str()));
    selectRemoteRows(s, {"origin/f1", "origin/f3"});
    GG_REQUIRE(s.session()->branches().selection().names.size() == 2);
    s.contextMenu(remoteRow("origin/f1").c_str(), "Delete on remote...");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    GG_CHECK(s.app.dialogs().current()->checkedOptions("branches") == (std::vector<std::string>{"origin/f1", "origin/f3"}));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "f1", "f3"}).empty(); }));
    s.settle();
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "f2"}).find("f2") != std::string::npos);
}

GG_TEST("refs", "a plain fetch does not bring forgotten refs back; Fetch more lists them; one undo restores them")
{
    const fs::path repo = repoWithRemoteBranches(s, {"f1", "f2"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().forgetRefs({"refs/remotes/origin/f1", "refs/remotes/origin/f2"});
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/f1") && !refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    // A plain fetch updates only the refs that exist here.
    s.session()->actions().fetch("origin", false, false);
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/f1"));
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/f2"));
    // Fetch more lists the branches of the remote again.
    s.session()->showFetchMoreDialog("origin");
    GG_REQUIRE(s.dialogOpen("Fetch more"));
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Fetch more/**/f1##branches_f1"); }, 10.0f));
    GG_CHECK(s.itemExists("//Fetch more/**/f2##branches_f2"));
    s.dialogButton("Fetch more", "Cancel");
    // One undo restores both.
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/remotes/origin/f1") && refExists(s, repo, "refs/remotes/origin/f2"); }));
}

GG_TEST("refs", "forgetting many remote-tracking branches uses one update-ref call; one undo restores them")
{
    std::vector<std::string> names;
    std::vector<std::string> refs;
    for (int i = 0; i < 300; ++i) {
        names.push_back("bulk" + std::to_string(i));
        refs.push_back("refs/remotes/origin/" + names.back());
    }
    const fs::path repo = repoWithRemoteBranches(s, {"keep"});
    {
        std::vector<std::string> args{"push", "-q", "origin"};
        for (const auto& n : names)
            args.push_back("HEAD~1:refs/heads/" + n);
        s.git(repo, args);
        s.git(repo, {"fetch", "-q", "origin"});
    }
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.settle());
    GG_REQUIRE(refExists(s, repo, refs.back()));
    gg::clearCommandLog();
    s.session()->actions().forgetRefs(refs);
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, refs.front()) && !refExists(s, repo, refs.back()); }, 30.0f));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"for-each-ref", "refs/remotes/origin/bulk*"}).empty());
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/keep"));
    int updateRefCalls = 0;
    for (const auto& e : gg::commandLog())
        if (std::find(e.args.begin(), e.args.end(), "update-ref") != e.args.end())
            ++updateRefCalls;
    GG_CHECK(updateRefCalls == 1);
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, refs.front()) && refExists(s, repo, refs.back()); }, 30.0f));
}

GG_TEST("refs", "Pull does not bring forgotten refs back and still merges new upstream commits")
{
    const fs::path repo = repoWithRemoteBranches(s, {"f1", "f2"});
    const std::string branch = s.gitOut(repo, {"symbolic-ref", "--short", "HEAD"});
    const fs::path other = s.path("other-clone");
    s.git(s.root(), {"clone", "-q", origin(s, repo).string(), other.string()});
    s.commitFile(other, "pulled.txt", "pulled\n", "Upstream commit");
    s.git(other, {"push", "-q", "origin", "HEAD:refs/heads/" + branch});
    const std::string upstreamHead = s.head(other);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().forgetRefs({"refs/remotes/origin/f1", "refs/remotes/origin/f2"});
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/f1") && !refExists(s, repo, "refs/remotes/origin/f2"); }));
    s.settle();
    s.session()->actions().pull(ggui::PullMode::Merge);
    GG_REQUIRE(s.settle());
    GG_CHECK(s.gitMayFail(repo, {"merge-base", "--is-ancestor", upstreamHead, "HEAD"}).ok());
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/f1"));
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/f2"));
}

GG_TEST("refs", "a remote-tracking branch with a slash in its name can be forgotten and deleted on the remote")
{
    const fs::path repo = repoWithRemoteBranches(s, {"feature/x", "feature/y"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.itemExists(remoteRow("origin/feature/x").c_str()));
    s.contextMenu(remoteRow("origin/feature/x").c_str(), "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/remotes/origin/feature/x"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/remotes/origin/feature/y"));
    s.contextMenu(remoteRow("origin/feature/y").c_str(), "Delete on remote...");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "feature/y"}).empty(); }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/feature/y"));
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "feature/x"}).find("feature/x") != std::string::npos);
}

GG_TEST("refs", "delete tags dialog deletes the checked local tags; one undo restores them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* name : {"t1", "t2", "t3"})
        s.git(repo, {"tag", name});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteTagsDialog({"t1", "t2", "t3"}, "");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags?");
    ctx->ItemUncheck("//Delete tags/**/t3##tags_t3");
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/tags/t1") && !refExists(s, repo, "refs/tags/t2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/tags/t3"));
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/tags/t1") && refExists(s, repo, "refs/tags/t2"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/tags/t3"));
}

GG_TEST("refs", "deleting many tags with long names needs no long command line; one undo restores them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string head = s.head(repo);
    const std::string pad(60, 'x');
    std::vector<std::string> names;
    std::string create;
    for (int i = 0; i < 3000; ++i) {
        names.push_back("many-" + pad + "-" + std::to_string(i));
        create += "create refs/tags/" + names.back() + " " + head + "\n";
    }
    s.git(repo, {"update-ref", "--stdin"}, create);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().deleteTags(names); // over 180 KB of names
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"tag", "--list", "many-*"}).empty(); }, 30.0f));
    s.settle();
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] { return refExists(s, repo, "refs/tags/" + names.front()) && refExists(s, repo, "refs/tags/" + names.back()); }, 30.0f));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"tag", "--list", "many-*"}).size() > 3000 * 60);
}

GG_TEST("refs", "delete tags dialog with a remote deletes the checked tags on that remote")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"v1", "v2"}) {
        s.git(repo, {"tag", name});
        s.git(repo, {"push", "-q", "origin", name});
    }
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showDeleteTagsDialog({"v1", "v2"}, "origin");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags on 'origin'?");
    ctx->ItemUncheck("//Delete tags/**/v2##tags_v2");
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"ls-remote", "--tags", "."}).find("refs/tags/v1") == std::string::npos; }));
    s.settle();
    GG_CHECK(s.gitOut(origin(s, repo), {"ls-remote", "--tags", "."}).find("refs/tags/v2") != std::string::npos);
    GG_CHECK(refExists(s, repo, "refs/tags/v1")); // the local tag stays
}

GG_TEST("refs", "Delete branch: a branch merged into its local upstream has no warning and is deleted")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "--track", "tracked", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("tracked").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    GG_CHECK(s.waitUntil([&] { return mergedRowShown(s); }, 5.0f));
    GG_CHECK(deleteWarning(s).empty());
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/tracked"); }));
    s.settle();
}

GG_TEST("refs", "Delete branch: Local and all remotes checks an unmerged branch against HEAD, not its remote branch")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"switch", "-q", "-c", "ahead", "HEAD~1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "only on ahead"});
    s.git(repo, {"push", "-q", "-u", "origin", "ahead"});
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("ahead").c_str(), "Delete/Local and all remotes");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    GG_CHECK(s.waitUntil([&] { return !deleteWarning(s).empty(); }, 5.0f));
    GG_CHECK(deleteWarning(s).find("is not merged into HEAD") != std::string::npos);
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/ahead"); }));
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "ahead"}).empty(); }));
    s.settle();
}

GG_TEST("refs", "delete a branch on its remote, and everywhere")
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
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/both"); }));
    s.settle();
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "both"}).empty());
}

GG_TEST("refs", "deleting a remote branch unsets the upstream of the local branches that track it")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "remote-only", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "remote-only"});
    s.git(repo, {"branch", "both", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "both"});
    s.git(repo, {"branch", "other-name", "HEAD~1"});
    s.git(repo, {"branch", "--set-upstream-to=origin/both", "other-name"}); // a second branch tracks origin/both
    s.git(repo, {"branch", "keeper", "HEAD~1"});
    s.git(repo, {"branch", "--set-upstream-to=origin/main", "keeper"}); // tracks another remote branch
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const auto upstream = [&](const char* b) { return gg::trim(s.gitMayFail(repo, {"rev-parse", "--abbrev-ref", std::string(b) + "@{upstream}"}).out); };
    GG_CHECK_STR_EQ(upstream("remote-only"), "origin/remote-only");
    // On its remote: the local branch stays, without an upstream.
    s.contextMenu(branchRow("remote-only").c_str(), "Delete/On its remote");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "remote-only"}).empty(); }));
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "--get", "branch.remote-only.merge"}).out.empty(); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/remote-only"));
    GG_CHECK(s.gitMayFail(repo, {"config", "--get", "branch.remote-only.remote"}).out.empty());
    // Local and all remotes: another local branch that tracks the remote branch loses its upstream too.
    s.contextMenu(branchRow("both").c_str(), "Delete/Local and all remotes");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/heads/both"); }));
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "--get", "branch.other-name.merge"}).out.empty(); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/other-name"));
    // A branch that tracks something else is untouched.
    GG_CHECK_STR_EQ(upstream("keeper"), "origin/main");
}

GG_TEST("refs", "a failed delete of a remote branch keeps the upstream of the local branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "topic", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "topic"});
    // The remote refuses the delete.
    s.git(origin(s, repo), {"config", "receive.denyDeletes", "true"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    s.contextMenu(branchRow("topic").c_str(), "Delete/On its remote");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    s.settle();
    GG_CHECK(!s.gitOut(origin(s, repo), {"branch", "--list", "topic"}).empty());
    GG_CHECK_STR_EQ(gg::trim(s.gitMayFail(repo, {"config", "--get", "branch.topic.merge"}).out), "refs/heads/topic");
}

GG_TEST("refs", "branches: remote node, local branch and remote-tracking branch have their own menus")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "topic", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "topic"});
    s.git(repo, {"branch", "-D", "topic"}); // the branch is now only on the remote
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    const std::string node = "//Branches/remote_group_origin/origin";
    const std::string rrow = "//Branches/remote_group_origin/origin/rbranch_origin:topic/###rbranch_origin:topic";
    const std::string lrow = branchRow("main");
    GG_REQUIRE(s.itemExists(rrow.c_str()));

    // The remote: remote items, no branch items.
    ctx->ItemClick(node.c_str(), ImGuiMouseButton_Right);
    for (const char* item : {"Copy name", "Copy URL", "Fetch", "Fetch and prune", "Fetch more...", "Forget...", "Pull", "Prune on fetch", "Edit URL...", "Delete"})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    for (const char* item : {"Check out", "Merge into HEAD...", "Rename...", "Push", "Delete on remote...", "Set upstream..."})
        GG_CHECK(!ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    ctx->PopupCloseAll();
    ctx->Yield(2);

    // A local branch: no remote-level items.
    ctx->ItemClick(lrow.c_str(), ImGuiMouseButton_Right);
    for (const char* item : {"Check out", "Rename...", "Push", "Set upstream...", "Delete"})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    for (const char* item : {"Fetch", "Fetch and prune", "Fetch more...", "Forget...", "Copy URL", "Edit URL...", "Prune on fetch", "Delete on remote..."})
        GG_CHECK(!ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    GG_CHECK(ctx->ItemExists("//$FOCUSED/Remote origin")); // main has origin/main
    ctx->PopupCloseAll();
    ctx->Yield(2);

    // A remote-tracking branch: branch items, no remote-level items, no local-branch-only ones.
    ctx->ItemClick(rrow.c_str(), ImGuiMouseButton_Right);
    // No local branch "topic": Check out opens a dialog, so it ends with "...".
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###check_out"), "Check out...###check_out");
    for (const char* item : {"Create local branch...", "Merge into HEAD...", "Rebase HEAD onto branch", "Copy name", "Delete on remote..."})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    for (const char* item : {"Fetch", "Copy URL", "Edit URL...", "Remote origin", "Rename...", "Set upstream...", "Set as upstream", "Push"})
        GG_CHECK(!ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    ctx->PopupCloseAll();
    ctx->Yield(2);

    // Delete it on the remote although no local branch has that name.
    s.contextMenu(rrow.c_str(), "Delete on remote...");
    GG_REQUIRE(s.dialogOpen("Delete branch"));
    s.dialogButton("Delete branch", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "topic"}).empty(); }));
    GG_CHECK(s.waitUntil([&] { return !s.itemExists(rrow.c_str()); }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/remotes/origin/topic"));
}

// Whether the "Remote <name>" submenu is in the open context menu of a local branch row.
static bool hasRemoteSubmenu(Scenario& s, const std::string& row, const char* name)
{
    auto* ctx = s.ctx;
    ctx->ItemClick(row.c_str(), ImGuiMouseButton_Right);
    const bool found = ctx->ItemExists((std::string("//$FOCUSED/") + name).c_str());
    ctx->PopupCloseAll();
    ctx->Yield(2);
    return found;
}

GG_TEST("refs", "branches: a local branch with one remote ref has a Remote submenu; Set as upstream sets the upstream")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "side", "HEAD~1"});
    s.git(repo, {"push", "-q", "origin", "side"}); // origin/side exists; side has no upstream
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("side").c_str()); }));
    GG_REQUIRE(s.session()->snapshot()->findBranch("side"));
    GG_CHECK(s.session()->snapshot()->findBranch("side")->upstream.empty());
    // The submenu has the remote-tracking branch items, but not Check out or Create local branch.
    ctx->ItemClick(branchRow("side").c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Open, "//$FOCUSED/Remote origin");
    ctx->Yield(2);
    for (const char* item : {"Reveal", "Copy name", "Create branch...", "Merge into HEAD...", "Rebase HEAD onto branch", "Set as upstream", "Delete on remote..."})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    for (const char* item : {"Create local branch...", "###check_out"})
        GG_CHECK(!ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(branchRow("side").c_str(), "Remote origin/Set as upstream");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "--get", "branch.side.merge"}).out.find("refs/heads/side") != std::string::npos; }));
    GG_CHECK_STR_EQ(gg::trim(s.gitMayFail(repo, {"config", "--get", "branch.side.remote"}).out), "origin");
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->findBranch("side")->upstream, "origin/side");
    // It is the upstream now: the item is disabled.
    ctx->ItemClick(branchRow("side").c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Open, "//$FOCUSED/Remote origin");
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->PopupCloseAll();
    ctx->Yield(2);
}

GG_TEST("refs", "branches: no Remote submenu without a remote ref, with two remote refs or with a local upstream")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "lonely", "HEAD~1"});
    s.git(repo, {"branch", "--track", "local-up", "main"}); // branch.local-up.remote is "."
    GG_REQUIRE(s.gitOut(repo, {"config", "--get", "branch.local-up.remote"}) == ".");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("local-up").c_str()); }));
    GG_CHECK(hasRemoteSubmenu(s, branchRow("main"), "Remote origin")); // one remote ref
    GG_CHECK(!hasRemoteSubmenu(s, branchRow("lonely"), "Remote origin"));
    GG_CHECK(!hasRemoteSubmenu(s, branchRow("local-up"), "Remote origin"));
    // A second remote with a ref of the same name: two refs, so no submenu.
    s.git(repo, {"remote", "add", "second", origin(s, repo).string()});
    s.git(repo, {"fetch", "-q", "second"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("main").c_str()); }));
    GG_CHECK(!hasRemoteSubmenu(s, branchRow("main"), "Remote origin"));
    GG_CHECK(!hasRemoteSubmenu(s, branchRow("main"), "Remote second"));
}

GG_TEST("refs", "branches: Set as upstream in the menu of a child row of a branch with two remote refs")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "second", origin(s, repo).string()});
    s.git(repo, {"fetch", "-q", "second"});
    s.forgetPanelState("Branches"); // the nodes start closed
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_main/###expand"); }));
    GG_CHECK_STR_EQ(s.session()->snapshot()->findBranch("main")->upstream, "origin/main");
    ctx->ItemClick("//Branches/branch_main/###expand");
    ctx->Yield(2);
    const std::string origin = "//Branches/branch_main/###remote_origin", second = "//Branches/branch_main/###remote_second";
    GG_REQUIRE(s.itemExists(second.c_str()));
    // The menu of a child row: the items of a remote-tracking branch, with Set as upstream; no Check out.
    ctx->ItemClick(second.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    for (const char* item : {"Reveal", "Copy name", "Create branch...", "Merge into HEAD...", "Set as upstream", "Delete on remote..."})
        GG_CHECK(ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    for (const char* item : {"Create local branch...", "###check_out"})
        GG_CHECK(!ctx->ItemExists((std::string("//$FOCUSED/") + item).c_str()));
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    // The upstream child has it disabled.
    ctx->ItemClick(origin.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(second.c_str(), "Set as upstream");
    GG_CHECK(s.waitUntil([&] { return gg::trim(s.gitMayFail(repo, {"config", "--get", "branch.main.remote"}).out) == "second"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->findBranch("main")->upstream, "second/main");
    // The node stays open and the upstream mark moves.
    GG_CHECK(s.itemText(second.c_str()).find("(upstream)") != std::string::npos);
    GG_CHECK(s.itemText(origin.c_str()).find("(upstream)") == std::string::npos);
    ctx->ItemClick("//Branches/branch_main/###expand"); // closed again for the next test
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(second.c_str()));
}

GG_TEST("refs", "branches: an upstream with another name is a second child row with its own ID, label and menu")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "other", "HEAD~1"});
    s.git(repo, {"push", "-q", "origin", "other"});
    s.git(repo, {"branch", "--set-upstream-to=origin/other", "main"}); // origin/main (the name) and origin/other
    s.forgetPanelState("Branches");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_main/###expand"); }));
    GG_CHECK_STR_EQ(s.session()->snapshot()->findBranch("main")->upstream, "origin/other");
    ctx->ItemClick("//Branches/branch_main/###expand");
    ctx->Yield(2);
    const std::string usual = "//Branches/branch_main/###remote_origin", renamed = "//Branches/branch_main/###remote_origin:other";
    GG_REQUIRE(s.itemExists(usual.c_str()));
    GG_REQUIRE(s.itemExists(renamed.c_str()));
    GG_CHECK_STR_EQ(s.itemText(usual.c_str()), "origin  " + s.revParse(repo, "origin/main").substr(0, 7));
    // The test engine keeps 31 characters of a label: the end "(upstream)" is cut.
    GG_CHECK(s.itemText(renamed.c_str()).rfind("origin/other  " + s.revParse(repo, "origin/other").substr(0, 7) + "  (upstr", 0) == 0);
    // Each menu works: the upstream has Set as upstream disabled, the other ref has it enabled.
    ctx->ItemClick(renamed.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(ctx->ItemExists("//$FOCUSED/Copy name"));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    ctx->ItemClick(usual.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(usual.c_str(), "Set as upstream");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"config", "--get", "branch.main.merge"}).out.find("refs/heads/main") != std::string::npos; }));
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->findBranch("main")->upstream, "origin/main");
    // origin/main is the upstream and the name: one ref, so the node is gone. origin/other is not a ref of main now.
    GG_CHECK(!s.itemExists("//Branches/branch_main/###expand"));
}

GG_TEST("refs", "branches: <remote>/HEAD is not a remote ref of a branch; an upstream with another name is")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "set-head", "origin", "-a"});
    s.git(repo, {"branch", "--track", "renamed", "origin/main"}); // the upstream has another short name
    GG_REQUIRE(refExists(s, repo, "refs/remotes/origin/HEAD"));
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("renamed").c_str()); }));
    // origin/HEAD and origin/main give main one ref only; "renamed" counts its upstream origin/main.
    GG_CHECK(hasRemoteSubmenu(s, branchRow("main"), "Remote origin"));
    GG_CHECK(hasRemoteSubmenu(s, branchRow("renamed"), "Remote origin"));
    ctx->ItemClick(branchRow("renamed").c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Open, "//$FOCUSED/Remote origin");
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Set as upstream").ItemFlags & ImGuiItemFlags_Disabled); // already its upstream
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.git(repo, {"remote", "set-head", "origin", "-d"});
}

GG_TEST("refs", "a remote name with a slash gives the right remote for its branches and upstreams")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "team/fork", s.gitOut(repo, {"remote", "get-url", "origin"})});
    s.git(repo, {"fetch", "-q", "team/fork"});
    s.git(repo, {"branch", "--track", "forked", "team/fork/main"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        for (const auto& r : snap->remoteBranches)
            if (r.name == "team/fork/main")
                return r.remote == "team/fork";
        return false;
    }));
    const auto snap = s.session()->snapshot();
    const auto* b = snap->findBranch("forked");
    GG_REQUIRE(b != nullptr);
    GG_CHECK_STR_EQ(b->upstreamRemote, "team/fork");
}

GG_TEST("refs", "branches: Check out on a remote-tracking branch opens Create branch when no local branch has its name")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "topic", "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "topic"});
    s.git(repo, {"branch", "-D", "topic"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    const std::string rrow = "//Branches/remote_group_origin/origin/rbranch_origin:topic/###rbranch_origin:topic";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rrow.c_str()); }));
    const std::string before = symbolicHead(s, repo);
    // Cancel creates nothing.
    s.contextMenu(rrow.c_str(), "###check_out");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    GG_CHECK(s.session()->snapshot()->findBranch("topic") == nullptr);
    GG_CHECK(!refExists(s, repo, "refs/heads/topic"));
    s.dialogButton("Create branch", "Cancel");
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/heads/topic"));
    GG_CHECK_STR_EQ(symbolicHead(s, repo), before);
    // Create makes the branch with the upstream and checks it out.
    s.contextMenu(rrow.c_str(), "###check_out");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->checked("checkout"));
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "topic"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "--abbrev-ref", "topic@{upstream}"}), "origin/topic");
    // With the local branch, the item is "Check out" and needs no dialog.
    s.git(repo, {"switch", "-q", before});
    s.settle();
    ctx->ItemClick(rrow.c_str(), ImGuiMouseButton_Right);
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###check_out"), "Check out###check_out");
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(rrow.c_str(), "###check_out");
    GG_CHECK(!s.dialogOpen("Create branch", 1.0f));
    GG_CHECK(s.waitUntil([&] { return symbolicHead(s, repo) == "topic"; }));
}

GG_TEST("refs", "branches: <remote>/HEAD is not listed; the snapshot keeps it apart from the branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "set-head", "origin", "-a"});
    GG_REQUIRE(refExists(s, repo, "refs/remotes/origin/HEAD"));
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_CHECK(s.itemExists("//Branches/remote_group_origin/origin/rbranch_origin:main/###rbranch_origin:main"));
    GG_CHECK(!s.itemExists("//Branches/remote_group_origin/origin/rbranch_origin:HEAD/###rbranch_origin:HEAD"));
    GG_CHECK(!s.itemExists("//Branches/remote_group_origin/origin/rhead_origin:HEAD/###rhead_origin:HEAD"));
    // The snapshot keeps it apart from the branches and knows its target.
    const auto& snap = *s.session()->snapshot();
    GG_CHECK(std::none_of(snap.remoteBranches.begin(), snap.remoteBranches.end(),
        [](const auto& r) { return r.name == "origin/HEAD"; }));
    GG_REQUIRE(snap.remoteHeads.size() == 1);
    GG_CHECK_STR_EQ(snap.remoteHeads[0].symref, "origin/main");
    GG_CHECK(!snap.remoteHeads[0].dangling);
    // History: only origin/main has a badge.
    const auto* tip = s.session()->history().row(ggui::core::Oid::fromHex(s.revParse(repo, "origin/main")));
    GG_REQUIRE(tip);
    GG_CHECK(std::none_of(tip->refs.begin(), tip->refs.end(), [](const auto& b) { return b.name == "origin/HEAD"; }));
    GG_CHECK(std::any_of(tip->refs.begin(), tip->refs.end(), [](const auto& b) { return b.name == "origin/main"; }));
    // Dangling: the target branch is gone; still not listed.
    s.git(repo, {"symbolic-ref", "refs/remotes/origin/HEAD", "refs/remotes/origin/gone"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.session()->snapshot()->remoteHeads.size() == 1);
    GG_CHECK(s.session()->snapshot()->remoteHeads[0].dangling);
    GG_CHECK(!s.itemExists("//Branches/remote_group_origin/origin/rbranch_origin:HEAD/###rbranch_origin:HEAD"));
    s.git(repo, {"remote", "set-head", "origin", "-d"}); // the after-test fsck rejects a dangling symref
}

GG_TEST("refs", "tags: a long annotated tag message is wrapped and cut after 10 lines in the tooltip; a short one is shown whole")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    std::string longMessage, lastWord;
    for (int i = 0; longMessage.size() < 3000; ++i) {
        lastWord = "word" + std::to_string(i);
        longMessage += lastWord + " ";
    }
    s.git(repo, {"tag", "-a", "long-tag", "-m", longMessage});
    s.git(repo, {"tag", "-a", "short-tag", "-m", "A short message"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("long-tag").c_str()) && s.itemExists(tagRow("short-tag").c_str()); }));
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fontSize = ImGui::GetFontSize();

    ctx->MouseMove(tagRow("long-tag").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_REQUIRE(tip != nullptr && tip->Active);
    // At most the wrap width (40 font sizes) plus padding; at most 10 lines plus padding, and all 10 are used.
    GG_CHECK(tip->Size.x <= fontSize * 40.0f + style.WindowPadding.x * 2.0f + 1.0f);
    GG_CHECK(tip->Size.y <= fontSize * 10.0f + style.WindowPadding.y * 2.0f + 1.0f);
    GG_CHECK(tip->Size.y >= fontSize * 9.0f);
    GG_CHECK(s.textShown("//##Tooltip_00", "word0 word1 "));
    GG_CHECK(s.textShown("//##Tooltip_00", "\xE2\x80\xA6"));
    GG_CHECK(!s.textShown("//##Tooltip_00", lastWord));

    ctx->MouseMove(tagRow("short-tag").c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    tip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_REQUIRE(tip != nullptr && tip->Active);
    GG_CHECK(s.textShown("//##Tooltip_00", "A short message"));
    GG_CHECK(!s.textShown("//##Tooltip_00", "\xE2\x80\xA6"));
    GG_CHECK(tip->Size.y <= fontSize * 2.0f + style.WindowPadding.y * 2.0f + 1.0f);
}

GG_TEST("refs", "tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote")
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

namespace {

// Selects the tags `names` in the Tags panel: a click on the first, Ctrl+click on the others.
void selectTags(Scenario& s, const std::vector<std::string>& names)
{
    ImGuiTestContext* ctx = s.ctx;
    ctx->ItemClick(tagRow(names.front()).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    for (size_t i = 1; i < names.size(); ++i)
        ctx->ItemClick(tagRow(names[i]).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
}

// A repository with the tags t1, t2 and t3 (t1 and t3 also on origin).
fs::path repoWithTags(Scenario& s)
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"t1", "t2", "t3"})
        s.git(repo, {"tag", name});
    s.git(repo, {"push", "-q", "origin", "t1", "t3"});
    return repo;
}

} // namespace

GG_TEST("refs", "the multi menu of tags deletes the selected tags locally")
{
    const fs::path repo = repoWithTags(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("t3").c_str()); }));
    selectTags(s, {"t1", "t3"});
    GG_REQUIRE(s.session()->tags().selection().names.size() == 2);
    s.contextMenu(tagRow("t3").c_str(), "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags?");
    GG_CHECK(s.app.dialogs().current()->checkedOptions("tags") == (std::vector<std::string>{"t1", "t3"}));
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/tags/t1") && !refExists(s, repo, "refs/tags/t3"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/tags/t2"));
    // The remote keeps its tags.
    GG_CHECK(refExists(s, origin(s, repo), "refs/tags/t1"));
}

GG_TEST("refs", "the multi menu of tags deletes on a remote only the selected tags that it has")
{
    const fs::path repo = repoWithTags(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("t3").c_str()); }));
    GG_REQUIRE(s.waitUntil([&] {
        const auto& tags = s.session()->remoteTags();
        auto it = tags.find("origin");
        return it != tags.end() && it->second.ok;
    }));
    selectTags(s, {"t1", "t2", "t3"});
    GG_REQUIRE(s.session()->tags().selection().names.size() == 3);
    s.contextMenu(tagRow("t1").c_str(), "Delete/On origin");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags on 'origin'?");
    GG_CHECK(s.app.dialogs().current()->checkedOptions("tags") == (std::vector<std::string>{"t1", "t3"}));
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, origin(s, repo), "refs/tags/t1") && !refExists(s, origin(s, repo), "refs/tags/t3"); }));
    s.settle();
    // The local tags stay.
    GG_CHECK(refExists(s, repo, "refs/tags/t1") && refExists(s, repo, "refs/tags/t2") && refExists(s, repo, "refs/tags/t3"));
}

GG_TEST("refs", "the multi menu of tags shows or hides all selected tags in History")
{
    const fs::path repo = repoWithTags(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("t3").c_str()); }));
    const auto& history = s.session()->history();
    const auto visible = [&](const char* name) { return history.refVisible(std::string("refs/tags/") + name); };
    GG_CHECK(visible("t1") && visible("t2") && visible("t3"));
    selectTags(s, {"t1", "t2"});
    GG_REQUIRE(s.session()->tags().selection().names.size() == 2);
    s.toggleVisibility(tagRow("t2").c_str());
    GG_CHECK(s.waitUntil([&] { return !visible("t1") && !visible("t2"); }));
    GG_CHECK(visible("t3"));
    s.toggleVisibility(tagRow("t2").c_str());
    GG_CHECK(s.waitUntil([&] { return visible("t1") && visible("t2"); }));
}

GG_TEST("refs", "the menu of a tag outside the selection, or of one selected tag, is the single menu")
{
    const fs::path repo = repoWithTags(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("t3").c_str()); }));
    const auto hasItem = [&](const char* item) {
        return ctx->ItemInfo((std::string("//$FOCUSED/") + item).c_str(), ImGuiTestOpFlags_NoError).ID != 0;
    };
    // One selected tag: Reveal and Copy name are there.
    selectTags(s, {"t1"});
    ctx->ItemClick(tagRow("t1").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(hasItem("Reveal") && hasItem("Copy name"));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    // A tag outside a selection of two: the single menu too.
    selectTags(s, {"t1", "t2"});
    ctx->ItemClick(tagRow("t3").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(hasItem("Reveal") && hasItem("Copy name"));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    // A tag in the selection: the multi menu has no Reveal.
    ctx->ItemClick(tagRow("t2").c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(!hasItem("Reveal") && !hasItem("Copy name"));
    ctx->PopupCloseAll();
    ctx->Yield(2);
}

GG_TEST("refs", "remotes: add, edit URL, prune on fetch, delete")
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

GG_TEST("refs", "create a branch from a reflog entry")
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

GG_TEST("refs", "tooltips wait until scrolling stops")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 60; ++i)
        s.git(repo, {"branch", "topic/b" + std::to_string(100 + i)});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = branchRow("topic/b110");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    auto tipShown = [&] {
        ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
        return tip != nullptr && tip->Active;
    };
    // A branch in a group has its full name in the tooltip.
    ctx->MouseMove(row.c_str());
    GG_CHECK(s.waitUntil([&] { return tipShown(); }, 5.0f));
    // While the wheel scrolls the list, no tooltip (whatever row is under the mouse). The wheel turns in every
    // frame, so the scroll position changes in every frame: a slow frame cannot let the quiet time pass.
    for (int i = 0; i < 6; ++i) {
        ctx->MouseWheelY(i % 2 == 0 ? -1.0f : 1.0f);
        ctx->Yield(1);
        GG_CHECK(!tipShown());
    }
    // Once it stops, tooltips come back.
    GG_CHECK(s.waitUntil([&] { return tipShown(); }, 5.0f));
}

GG_TEST("refs", "tooltips wait until scrolling stops also when the hover delay has passed on another row")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 60; ++i)
        s.git(repo, {"branch", "topic/b" + std::to_string(100 + i)});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string plain = branchRow("main");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(plain.c_str()); }));
    auto tipShown = [&] {
        ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
        return tip != nullptr && tip->Active;
    };
    // The hover delay is shared by all rows, and `main` (no tooltip) does not restart it. The mouse rests where
    // `main` is; the list is scrolled down one notch, so a row with a tooltip is under the mouse.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ScrollToTop(s.child("//Branches", "##list").c_str()); // the window keeps its scroll from an earlier test
    ctx->Yield(2);
    const ImVec2 mainPos = ctx->ItemInfo(plain.c_str()).RectFull.GetCenter();
    ctx->MouseMoveToPos(mainPos);
    ctx->MouseWheelY(-1.0f);
    GG_REQUIRE(s.waitUntil([&] { return tipShown(); }, 5.0f));
    // One notch back up: `main` is under the mouse, and one down again: a row with a tooltip, at once. Not while it
    // moves. The wheel turns in every frame, so the scroll position changes in every frame: a slow frame cannot let
    // the quiet time pass. The last notch is down.
    for (int i = 0; i < 6; ++i) {
        ctx->MouseWheelY(i % 2 == 0 ? 1.0f : -1.0f);
        ctx->Yield(1);
        GG_CHECK(!tipShown());
    }
    GG_CHECK(s.waitUntil([&] { return tipShown(); }, 5.0f));
}

GG_TEST("refs", "the group menu of local branches deletes the group branches, subgroups included")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* b : {"feature/one", "feature/two", "feature/sub/x", "feature/sub/y", "other/a", "other/b", "solo"})
        s.git(repo, {"branch", b, "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("feature/one").c_str()); }));
    GG_REQUIRE(s.itemExists("//Branches/###group_local:feature:"));
    s.contextMenu("//Branches/###group_local:feature:", "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    // Exactly the branches of the group, in the order of the rows.
    const std::vector<std::string> expected{"feature/one", "feature/sub/x", "feature/sub/y", "feature/two"};
    GG_CHECK(form->checkedOptions("branches") == expected);
    GG_CHECK(s.waitUntil([&] {
        const ggui::Form* f = s.app.dialogs().current();
        return f && f->buttons[0].enabled(*f);
    }, 5.0f));
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] {
        for (const auto& b : expected)
            if (refExists(s, repo, "refs/heads/" + b))
                return false;
        return true;
    }));
    s.settle();
    for (const char* b : {"other/a", "other/b", "solo", "main"})
        GG_CHECK(refExists(s, repo, std::string("refs/heads/") + b));
}

GG_TEST("refs", "the group menu of a nested subgroup deletes only the subgroup branches")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* b : {"feature/one", "feature/two", "feature/sub/x", "feature/sub/y"})
        s.git(repo, {"branch", b, "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("feature/sub/x").c_str()); }));
    s.contextMenu("//Branches/###group_local:feature:/###group_local:feature:sub:", "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    const std::vector<std::string> expected{"feature/sub/x", "feature/sub/y"};
    GG_CHECK(form->checkedOptions("branches") == expected);
    s.dialogButton("Delete branches", "Cancel");
}

GG_TEST("refs", "the group menu item On its remote is enabled only when a group branch has an upstream")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* b : {"up/a", "up/b", "none/a", "none/b"})
        s.git(repo, {"branch", b, "HEAD~1"});
    s.git(repo, {"push", "-q", "-u", "origin", "up/a"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(branchRow("none/b").c_str()); }));
    const auto disabled = [&](const char* group) -> bool {
        ctx->ItemClick(group, ImGuiMouseButton_Right);
        ctx->MenuAction(ImGuiTestAction_Open, "//$FOCUSED/Delete");
        ctx->Yield(2);
        const ImGuiTestItemInfo info = ctx->ItemInfo("//$FOCUSED/On its remote", ImGuiTestOpFlags_NoError);
        GG_CHECK(info.ID != 0);
        ctx->PopupCloseAll();
        ctx->Yield(2);
        return (info.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    };
    GG_CHECK(disabled("//Branches/###group_local:none:"));
    GG_CHECK(!disabled("//Branches/###group_local:up:"));
    // The item sends only the branches that have an upstream.
    s.contextMenu("//Branches/###group_local:up:", "Delete/On its remote");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    const auto checked = form->checkedOptions("branches");
    GG_CHECK(checked.size() == 1 && checked.front() == "up/a");
    s.dialogButton("Delete branches", "Cancel");
}

GG_TEST("refs", "the group menu of remote branches forgets the group refs; one undo restores them")
{
    const fs::path repo = repoWithRemoteBranches(s, {"feature/one", "feature/two", "feature/sub/x", "other/a", "solo"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(remoteRow("origin/feature/one").c_str()); }));
    const char* group = "//Branches/remote_group_origin/origin/###group_remote:origin:feature:";
    GG_REQUIRE(s.itemExists(group));
    ctx->ItemClick(group, ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemExists("//$FOCUSED/###history_visibility"));
    GG_CHECK(ctx->ItemExists("//$FOCUSED/Delete on remote..."));
    GG_CHECK(ctx->ItemExists("//$FOCUSED/Forget..."));
    GG_CHECK(!ctx->ItemExists("//$FOCUSED/Delete"));
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu(group, "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    // Exactly the branches of the group, in the order of the rows.
    const std::vector<std::string> expected{"origin/feature/one", "origin/feature/sub/x", "origin/feature/two"};
    GG_CHECK(form->checkedOptions("branches") == expected);
    s.dialogButton("Forget", "Forget");
    GG_CHECK(s.waitUntil([&] {
        for (const auto& b : expected)
            if (refExists(s, repo, "refs/remotes/" + b))
                return false;
        return true;
    }));
    s.settle();
    for (const char* b : {"other/a", "solo"})
        GG_CHECK(refExists(s, repo, std::string("refs/remotes/origin/") + b));
    // The server keeps the branches.
    GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", "feature/*"}).find("feature/one") != std::string::npos);
    s.session()->actions().undo(false);
    GG_CHECK(s.waitUntil([&] {
        for (const auto& b : expected)
            if (!refExists(s, repo, "refs/remotes/" + b))
                return false;
        return true;
    }));
}

GG_TEST("refs", "the group menu of remote branches deletes the group branches on the remote, subgroups included")
{
    const fs::path repo = repoWithRemoteBranches(s, {"feature/one", "feature/two", "feature/sub/x", "other/a", "solo"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(remoteRow("origin/feature/one").c_str()); }));
    s.contextMenu("//Branches/remote_group_origin/origin/###group_remote:origin:feature:", "Delete on remote...");
    GG_REQUIRE(s.dialogOpen("Delete branches"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    const std::vector<std::string> expected{"origin/feature/one", "origin/feature/sub/x", "origin/feature/two"};
    GG_CHECK(form->checkedOptions("branches") == expected);
    s.dialogButton("Delete branches", "Delete");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(origin(s, repo), {"branch", "--list", "feature/*"}).empty(); }));
    s.settle();
    for (const char* b : {"other/a", "solo"})
        GG_CHECK(s.gitOut(origin(s, repo), {"branch", "--list", b}).find(b) != std::string::npos);
}

GG_TEST("refs", "the group menu of a nested remote subgroup acts only on the subgroup branches")
{
    const fs::path repo = repoWithRemoteBranches(s, {"feature/one", "feature/sub/x", "feature/sub/y"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.openRemote());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(remoteRow("origin/feature/sub/x").c_str()); }));
    s.contextMenu("//Branches/remote_group_origin/origin/###group_remote:origin:feature:/###group_remote:origin:feature:sub:",
        "Forget...");
    GG_REQUIRE(s.dialogOpen("Forget"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_REQUIRE(form != nullptr);
    GG_CHECK(form->checkedOptions("branches") == (std::vector<std::string>{"origin/feature/sub/x", "origin/feature/sub/y"}));
    s.dialogButton("Forget", "Cancel");
}

// A repository with the tags release/1.0, release/2.0 and plain (no slash), the first and the third on origin.
static fs::path repoWithTagFolder(Scenario& s)
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"release/1.0", "release/2.0", "plain"})
        s.git(repo, {"tag", name});
    s.git(repo, {"push", "-q", "origin", "release/1.0", "plain"});
    return repo;
}

GG_TEST("refs", "Tags: tags with a slash group in folders; the filter opens a closed folder")
{
    const fs::path repo = repoWithTagFolder(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("plain").c_str()); }));
    GG_CHECK(s.itemExists("//Tags/###group_tag:release:"));
    GG_CHECK_STR_EQ(s.itemText(tagRow("release/1.0").c_str()), "1.0");
    GG_CHECK_STR_EQ(s.itemText(tagRow("release/2.0").c_str()), "2.0");
    GG_CHECK_STR_EQ(s.itemText(tagRow("plain").c_str()), "plain");
    // A closed folder hides its rows; filtering opens it again.
    ctx->ItemClick("//Tags/###group_tag:release:");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(tagRow("release/1.0").c_str()));
    GG_CHECK(s.itemExists(tagRow("plain").c_str()));
    ctx->ItemInputValue("//Tags/##tag_filter", "2.0");
    ctx->Yield(2);
    GG_CHECK(s.itemExists(tagRow("release/2.0").c_str()));
    GG_CHECK(!s.itemExists(tagRow("release/1.0").c_str()));
    GG_CHECK(!s.itemExists(tagRow("plain").c_str()));
    // Clear the filter and open the folder again, so that the rows of the folder are drawn.
    ctx->ItemInputValue("//Tags/##tag_filter", "");
    ctx->Yield(2);
    if (!s.itemExists(tagRow("release/1.0").c_str()))
        ctx->ItemClick("//Tags/###group_tag:release:");
    ctx->Yield(2);
    GG_CHECK(s.itemExists(tagRow("release/1.0").c_str()));
}

GG_TEST("refs", "the folder menu of tags shows or hides the tags below it; Ctrl shows only these")
{
    const fs::path repo = repoWithTagFolder(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/###group_tag:release:"); }));
    const auto& history = s.session()->history();
    const auto visible = [&](const char* name) { return history.refVisible(std::string("refs/tags/") + name); };
    GG_CHECK(visible("release/1.0") && visible("release/2.0") && visible("plain"));
    s.contextMenu("//Tags/###group_tag:release:", "###history_visibility");
    GG_CHECK(s.waitUntil([&] { return !visible("release/1.0") && !visible("release/2.0"); }));
    GG_CHECK(visible("plain"));
    s.contextMenu("//Tags/###group_tag:release:", "###history_visibility");
    GG_CHECK(s.waitUntil([&] { return visible("release/1.0") && visible("release/2.0"); }));
    // Ctrl: only the tags of the folder show.
    ctx->ItemClick("//Tags/###group_tag:release:", ImGuiMouseButton_Right);
    ctx->Yield(2);
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick("//$FOCUSED/###history_visibility");
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(s.waitUntil([&] { return !visible("plain"); }));
    GG_CHECK(visible("release/1.0") && visible("release/2.0"));
}

GG_TEST("refs", "the folder menu of tags deletes the tags below it locally and on a remote")
{
    const fs::path repo = repoWithTagFolder(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/###group_tag:release:"); }));
    GG_REQUIRE(s.waitUntil([&] {
        const auto& tags = s.session()->remoteTags();
        auto it = tags.find("origin");
        return it != tags.end() && it->second.ok;
    }));
    const std::vector<std::string> both{"release/1.0", "release/2.0"};
    s.contextMenu("//Tags/###group_tag:release:", "Delete/On origin");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    // Only the tag of the folder that origin has.
    GG_CHECK(s.app.dialogs().current()->checkedOptions("tags") == (std::vector<std::string>{"release/1.0"}));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags on 'origin'?");
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, origin(s, repo), "refs/tags/release/1.0"); }));
    s.settle();
    // The remote keeps the tag that was not in the list, and the local tags stay.
    GG_CHECK(refExists(s, origin(s, repo), "refs/tags/plain"));
    GG_CHECK(refExists(s, repo, "refs/tags/release/1.0") && refExists(s, repo, "refs/tags/release/2.0"));
    s.contextMenu("//Tags/###group_tag:release:", "Delete/Local");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->message, "Delete these tags?");
    GG_CHECK(s.app.dialogs().current()->checkedOptions("tags") == both);
    s.dialogButton("Delete tags", "Delete");
    GG_CHECK(s.waitUntil([&] { return !refExists(s, repo, "refs/tags/release/1.0") && !refExists(s, repo, "refs/tags/release/2.0"); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/tags/plain"));
}

GG_TEST("refs", "a Shift click on a tag selects the range of the drawn rows, folders included")
{
    const fs::path repo = repoWithTagFolder(s);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/###group_tag:release:"); }));
    // Open the folder if an earlier state closed it.
    if (!s.itemExists(tagRow("release/2.0").c_str()))
        ctx->ItemOpen("//Tags/###group_tag:release:");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(tagRow("release/2.0").c_str()); }));
    // The rows in the order drawn: plain, the folder (not selectable), release/1.0, release/2.0.
    ctx->ItemClick(tagRow("plain").c_str());
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(tagRow("release/2.0").c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    const auto& names = s.session()->tags().selection().names;
    GG_CHECK(names.size() == 3 && names.count("release/1.0") && names.count("release/2.0") && names.count("plain"));
    GG_CHECK(!names.count("release"));
}

GG_TEST("refs", "tags only on a remote group in folders, and the folder menu has Delete only")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    for (const char* name : {"far/a", "far/b"})
        s.git(origin(s, repo), {"tag", name, "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Tags/###group_rtag:far:"); }));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//Tags/rtag_far:a/###rtag_far:a")); }));
    GG_CHECK(s.itemText("//Tags/rtag_far:a/###rtag_far:a").rfind("a  (origin)", 0) == 0);
    ctx->ItemClick("//Tags/###group_rtag:far:", ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/###history_visibility", ImGuiTestOpFlags_NoError).ID == 0);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Delete", ImGuiTestOpFlags_NoError).ID != 0);
    ctx->PopupCloseAll();
    ctx->Yield(2);
    s.contextMenu("//Tags/###group_rtag:far:", "Delete/On origin");
    GG_REQUIRE(s.dialogOpen("Delete tags"));
    GG_CHECK(s.app.dialogs().current()->checkedOptions("tags") == (std::vector<std::string>{"far/a", "far/b"}));
    s.dialogButton("Delete tags", "Cancel");
}

} // namespace ggtest
