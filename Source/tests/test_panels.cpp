// Side panels: Branches, Tags, Worktrees, Remotes, Reflog (§4.7).
#include "panels/HistoryPanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Widgets.hpp"
#include "tests/Harness.hpp"

#include "util/Ui.hpp"

#include <libgg/Worktrees.hpp>

#include <IconsMaterialSymbols.h>

#include <algorithm>
#include <cstdint>

namespace ggtest {

GG_TEST("panels", "branches: filter, current, upstream, reveal, copy")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "other"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const auto* main = s.session()->snapshot()->findBranch("main");
    GG_REQUIRE(main != nullptr);
    GG_CHECK(main->isHead);
    GG_CHECK_STR_EQ(main->upstream, "origin/main");
    GG_CHECK_EQ(main->ahead, 1);
    GG_CHECK_EQ(main->behind, 1);
    GG_CHECK(s.itemText("//Branches/branch_main/###branch_main").find("origin/main") != std::string::npos);
    // Nested names are addressable; filter narrows the list.
    GG_CHECK(s.itemExists("//Branches/branch_feature:one/###branch_feature:one"));
    ctx->ItemInputValue("//Branches/##branch_filter", "feat");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists("//Branches/branch_main/###branch_main"));
    GG_CHECK(s.itemExists("//Branches/branch_feature:one/###branch_feature:one"));
    s.contextMenu("//Branches/branch_feature:one/###branch_feature:one", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "feature/one");
    s.contextMenu("//Branches/branch_feature:one/###branch_feature:one", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "feature/one"); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "");
    // Remote-tracking branches under their remote.
    ctx->Yield(2);
    const std::string remoteGroup = "//Branches/remote_group_origin";
    GG_CHECK(s.itemExists((remoteGroup + "/origin").c_str()));
}

GG_TEST("panels", "tags: filter, visibility, reveal, copy")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"tag", "v1.0", "HEAD~3"});
    s.git(repo, {"tag", "-a", "-m", "Release two", "v2.0", "HEAD~1"});
    s.git(repo, {"branch", "-f", "side", "HEAD~3"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    const auto snapshot = s.session()->snapshot(); // keeps tags alive while the UI refreshes
    const auto& tags = snapshot->tags;
    GG_REQUIRE(tags.size() == 2);
    GG_CHECK(!tags[0].annotated && tags[1].annotated);
    GG_CHECK_STR_EQ(tags[1].message, "Release two\n");
    // Labels are the plain names, annotated or not.
    GG_CHECK_STR_EQ(s.itemText("//Tags/tag_v2.0/###tag_v2.0"), "v2.0");
    GG_CHECK_STR_EQ(s.itemText("//Tags/tag_v1.0/###tag_v1.0"), "v1.0");
    ctx->ItemInputValue("//Tags/##tag_filter", "v2");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists("//Tags/tag_v1.0/###tag_v1.0"));
    s.contextMenu("//Tags/tag_v2.0/###tag_v2.0", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "v2.0");
    s.contextMenu("//Tags/tag_v2.0/###tag_v2.0", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD~1"); }));
    // Visibility: Ctrl-click shows only this tag's history.
    ctx->ItemInputValue("//Tags/##tag_filter", "");
    ctx->Yield(2);
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick("//Tags/tag_v1.0/###eye");
    ctx->KeyUp(ImGuiMod_Ctrl);
    auto& history = s.session()->history();
    GG_CHECK(s.waitUntil([&] { return !history.loading() && history.rows().size() == 2; }));
    GG_CHECK(!history.refVisible("refs/tags/v2.0"));
    ctx->ItemClick("//Tags/tag_v2.0/###eye");
    GG_CHECK(s.waitUntil([&] { return history.refVisible("refs/tags/v2.0") && history.rows().size() == 4; }));
}

GG_TEST("panels", "worktrees: main, locked, stale; copy, reveal, open")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path opened = s.fakeTool(kFileManager);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    const auto snapshot = s.session()->snapshot(); // keeps wts alive while the UI refreshes
    const auto& wts = snapshot->worktrees;
    GG_REQUIRE(wts.size() == 4);
    GG_CHECK(wts[0].isMain && wts[0].isCurrent);
    const std::string wt1 = repo.filename().string() + "-wt1";
    const std::string wt2 = repo.filename().string() + "-wt2";
    const std::string wt3 = repo.filename().string() + "-wt3";
    auto find = [&](const std::string& name) {
        return std::find_if(wts.begin(), wts.end(), [&](const auto& w) { return w.name == name; });
    };
    GG_REQUIRE(find(wt1) != wts.end() && find(wt2) != wts.end() && find(wt3) != wts.end());
    GG_CHECK_STR_EQ(find(wt1)->branch, "wt1");
    GG_CHECK(find(wt2)->locked);
    GG_CHECK_STR_EQ(find(wt2)->lockReason, "test lock");
    GG_CHECK(find(wt3)->prunable);
    // Same list as git worktree list --porcelain.
    const std::string porcelain = s.gitOut(repo, {"worktree", "list", "--porcelain"});
    GG_CHECK(porcelain.find("locked test lock") != std::string::npos);
    GG_CHECK(porcelain.find("prunable") != std::string::npos);
    const std::string row = "//Worktrees/worktree_" + wt1 + "/###row";
    GG_CHECK(s.itemExists(row.c_str()));
    s.contextMenu(row.c_str(), "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), wt1);
    s.contextMenu(row.c_str(), "Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), (s.root() / wt1).string());
    s.contextMenu(row.c_str(), "Reveal HEAD");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "wt1"); }));
    s.showPanel("Worktrees");
    s.contextMenu(row.c_str(), "Open directory");
    GG_CHECK(s.waitUntil([&] {
        return s.read(opened.parent_path(), opened.filename().string()).find((s.root() / wt1).string()) != std::string::npos;
    }));
}

// The label of the repository leaf in the tree of the Repositories panel ("" when there is none).
static std::string repoLeafLabel(const std::vector<ggui::RepoNode>& nodes, const fs::path& path)
{
    for (const auto& n : nodes) {
        if (n.isGroup()) {
            const std::string inner = repoLeafLabel(n.children, path);
            if (!inner.empty())
                return inner;
        } else if (fs::equivalent(n.path, path)) {
            return n.label;
        }
    }
    return std::string();
}

// The ID path below the panel of the repository leaf: the group nodes, then the row ("" when there is none).
static std::string repoLeafPath(const std::vector<ggui::RepoNode>& nodes, const fs::path& path)
{
    auto colon = [](std::string text) { // the panel writes ':' for '/' in an ID
        std::replace(text.begin(), text.end(), '/', ':');
        return text;
    };
    for (const auto& n : nodes) {
        if (n.isGroup()) {
            const std::string inner = repoLeafPath(n.children, path);
            if (!inner.empty())
                return "###group_" + colon(n.group) + "/" + inner;
        } else if (fs::equivalent(n.path, path)) {
            return "repo_" + colon(n.label) + "/###row";
        }
    }
    return std::string();
}

static std::string repoRow(Scenario& s, const fs::path& path)
{
    return "//Repositories/" + repoLeafPath(ggui::buildRepoTree(s.app.settings().data().repositories), path);
}

GG_TEST("panels", "repositories: the open repository has the mark, an alias makes a group")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::Linear, "first");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const auto& panel = s.session()->repositories();
    GG_REQUIRE(s.waitUntil([&] { return !panel.openPath().empty(); }));
    GG_CHECK_STR_EQ(panel.openPath(), ggui::normalizeRepoPath(repo.string()));
    const std::string row = repoRow(s, repo);
    GG_CHECK(s.itemExists(row.c_str()));
    // With only its main worktree it is a plain row: no expand arrow.
    GG_CHECK(!(ctx->ItemInfo(row.c_str()).StatusFlags & ImGuiItemStatusFlags_Openable));
    GG_CHECK(panel.selectedPath().empty());
    // The row of the open repository is the one compared for the mark; a double-click on it keeps the session.
    GG_REQUIRE(s.app.settings().data().repositories.size() == 1);
    GG_CHECK_STR_EQ(s.app.settings().data().repositories.front().path, panel.openPath());
    const auto* session = s.session();
    ctx->ItemDoubleClick(row.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    // An alias "grp/name" moves the repository into the group "grp" under the label "name".
    s.app.settings().setAlias(ggui::normalizeRepoPath(repo.string()), "grp/name");
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//Repositories/###group_grp"));
    GG_CHECK(s.itemExists("//Repositories/###group_grp/repo_name/###row"));
    GG_CHECK(!s.itemExists(row.c_str()));
    // A nested group has its own node.
    s.app.settings().setAlias(ggui::normalizeRepoPath(repo.string()), "grp/sub/name");
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//Repositories/###group_grp/###group_grp:sub/repo_name/###row"));
}

GG_TEST("panels", "repositories: a row shows the detail of the recent menu, also for a repository that is not recent")
{
    s.app.settings().data().repositories.clear();
    const std::string longBranch = "feature/a-very-long-branch-name-for-the-repositories";
    const fs::path a = s.fixture(Recipe::Linear, "detail-a");
    const fs::path b = s.fixture(Recipe::WithRemote, "detail-b");
    const fs::path c = s.fixture(Recipe::Linear, "detail-c");
    // The detail of a row: the text after the label and two spaces.
    const auto detailOf = [](const std::string& text) {
        const size_t at = text.find("  ");
        return at == std::string::npos ? std::string() : text.substr(at + 2);
    };
    const auto recentDetailOf = [&](const std::string& key) {
        const auto& list = s.app.settings().data().recent;
        const auto it = std::find(list.begin(), list.end(), key);
        return it == list.end() ? std::string("<not recent>") : detailOf(s.app.recentMenuText(static_cast<size_t>(it - list.begin())));
    };
    s.git(a, {"branch", "-m", "main", longBranch});
    GG_REQUIRE(s.openRepository(a));
    GG_REQUIRE(s.openRepository(b));
    const std::string keyA = ggui::normalizeRepoPath(a.string());
    const std::string keyB = ggui::normalizeRepoPath(b.string());
    const std::string keyC = ggui::normalizeRepoPath(c.string());
    // A is listed but no longer recent; the summary requests that opening C starts cover it.
    s.app.settings().forgetRecent(keyA);
    const auto& recent = s.app.settings().data().recent;
    GG_REQUIRE(std::find(recent.begin(), recent.end(), keyA) == recent.end());
    s.app.settings().setAlias(keyB, "grp/bee");
    GG_REQUIRE(s.openRepository(c));
    s.showPanel("Repositories");
    const auto& panel = s.session()->repositories();
    const auto& settings = s.app.settings().data();
    const std::string shortName = ggui::elideMiddle(longBranch, settings.historyBadgePrefix, settings.historyBadgeSuffix);
    GG_REQUIRE(shortName != longBranch);
    GG_REQUIRE(s.waitUntil([&] { return panel.rowText(keyA).find(shortName) != std::string::npos; }));
    GG_CHECK_STR_EQ(panel.rowText(keyA), "detail-a  " + shortName);
    GG_CHECK(panel.rowText(keyA).find(longBranch) == std::string::npos);
    // B has an upstream; it is in a group, whose row has no detail.
    GG_REQUIRE(s.waitUntil([&] { return panel.rowText(keyB).find("origin/main") != std::string::npos; }));
    GG_CHECK(panel.rowText(keyB).starts_with("bee  main " ICON_MS_ARROW_RIGHT_ALT " origin/main"));
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//Repositories/###group_grp/repo_bee/###row"));
    // A recent repository shows the detail of its row in the Recent menu.
    GG_CHECK(!detailOf(panel.rowText(keyB)).empty());
    GG_CHECK_STR_EQ(detailOf(panel.rowText(keyB)), recentDetailOf(keyB));
    // The open repository: the state of its session, so a checkout shows at once, in the panel and in the menu.
    GG_CHECK_STR_EQ(panel.rowText(keyC), "detail-c  main");
    GG_CHECK_STR_EQ(detailOf(panel.rowText(keyC)), recentDetailOf(keyC));
    s.git(c, {"checkout", "-b", "topic"});
    s.session()->refresh();
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot() && s.session()->snapshot()->headBranch == "topic"; }));
    ctx->Yield(2);
    GG_CHECK_STR_EQ(panel.rowText(keyC), "detail-c  topic");
    GG_CHECK_STR_EQ(recentDetailOf(keyC), "topic");
}

GG_TEST("panels", "repositories: the tree node row of the open repository with a linked worktree shows the detail")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string row = repoRow(s, repo);
    GG_REQUIRE(s.itemExists(row.c_str()));
    GG_CHECK(ctx->ItemInfo(row.c_str()).StatusFlags & ImGuiItemStatusFlags_Openable);
    const std::string key = ggui::normalizeRepoPath(repo.string());
    const auto& panel = s.session()->repositories();
    ctx->Yield(2);
    const std::string text = panel.rowText(key);
    GG_CHECK(text.starts_with(repo.filename().string() + "  "));
    GG_CHECK(text.size() > repo.filename().string().size() + 2);
    GG_CHECK(text.find(s.session()->snapshot()->headBranch) != std::string::npos);
}

GG_TEST("panels", "repositories: a click selects, a double-click opens")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    const std::string row = repoRow(s, second);
    GG_REQUIRE(s.itemExists(row.c_str()));
    ctx->ItemClick(row.c_str());
    ctx->Yield(3);
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), ggui::normalizeRepoPath(second.string()));
    GG_CHECK(fs::equivalent(s.session()->path(), first));
    ctx->ItemDoubleClick(row.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), second); }));
}

GG_TEST("panels", "repositories: Enter opens the selected repository")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    const std::string row = repoRow(s, second);
    GG_REQUIRE(s.itemExists(row.c_str()));
    ctx->KeyPress(ImGuiKey_Enter); // nothing is selected yet
    ctx->Yield(3);
    s.settle();
    GG_CHECK(fs::equivalent(s.session()->path(), first));
    ctx->ItemClick(row.c_str());
    ctx->Yield(3);
    s.settle();
    GG_CHECK(fs::equivalent(s.session()->path(), first));
    // Enter is the panel's only while it has the focus.
    ctx->WindowFocus("//History");
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    s.settle();
    GG_CHECK(fs::equivalent(s.session()->path(), first));
    ctx->WindowFocus("//Repositories");
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), second); }));
}

// The reference of the row of a worktree below the row of the open repository.
static std::string worktreeRow(const std::string& repoRowRef, const std::string& name)
{
    return repoRowRef + "/worktree_" + name + "/###row";
}

// Opens the node of the open repository with a click on its arrow (ItemOpen would click the label).
static void openRepoNode(ImGuiTestContext* ctx, const std::string& row)
{
    const ImGuiTestItemInfo info = ctx->ItemInfo(row.c_str());
    if (info.StatusFlags & ImGuiItemStatusFlags_Opened)
        return;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float x = info.RectFull.Min.x + style.FramePadding.x + ImGui::GetFontSize() * 0.5f;
    ctx->MouseMoveToPos(ImVec2(x, (info.RectFull.Min.y + info.RectFull.Max.y) * 0.5f));
    ctx->MouseClick(0);
    ctx->Yield(2);
}

GG_TEST("panels", "repositories: the open repository expands to its worktrees")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path plain = s.fixture(Recipe::Linear, "plain");
    GG_REQUIRE(s.openRepository(repo));
    s.app.settings().addRepository(plain.string());
    s.showPanel("Repositories");
    const auto snapshot = s.session()->snapshot();
    GG_REQUIRE(snapshot->worktrees.size() == 4);
    const std::string row = repoRow(s, repo);
    const std::string plainRow = repoRow(s, plain);
    GG_REQUIRE(s.itemExists(row.c_str()) && s.itemExists(plainRow.c_str()));
    // Only the open repository has an expand arrow, and its node is closed by default.
    GG_CHECK(ctx->ItemInfo(row.c_str()).StatusFlags & ImGuiItemStatusFlags_Openable);
    GG_CHECK(!(ctx->ItemInfo(plainRow.c_str()).StatusFlags & ImGuiItemStatusFlags_Openable));
    for (const auto& w : snapshot->worktrees)
        GG_CHECK(!s.itemExists(worktreeRow(row, w.name).c_str()));
    // A click on the label selects the row and does not open the node; so does a double-click.
    ctx->ItemClick(row.c_str());
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), ggui::normalizeRepoPath(repo.string()));
    GG_CHECK(!s.itemExists(worktreeRow(row, snapshot->worktrees.front().name).c_str()));
    const auto* session = s.session();
    ctx->ItemDoubleClick(row.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    GG_CHECK(!s.itemExists(worktreeRow(row, snapshot->worktrees.front().name).c_str()));
    // The node has the context menu of a repository row, closed and open.
    s.contextMenu(row.c_str(), "Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), ggui::normalizeRepoPath(repo.string()));
    // The arrow opens it: a row for each worktree.
    openRepoNode(ctx, row);
    for (const auto& w : snapshot->worktrees)
        GG_CHECK(s.itemExists(worktreeRow(row, w.name).c_str()));
    ImGui::SetClipboardText("");
    s.contextMenu(row.c_str(), "Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), ggui::normalizeRepoPath(repo.string()));
    // Remove from list on the open repository drops a selected worktree row with it.
    ctx->ItemClick(worktreeRow(row, snapshot->worktrees.front().name).c_str());
    ctx->Yield(2);
    GG_CHECK(!s.session()->repositories().selectedPath().empty());
    s.contextMenu(row.c_str(), "Remove from list");
    ctx->Yield(2);
    GG_CHECK(s.session()->repositories().selectedPath().empty());
    GG_CHECK(!s.itemExists(row.c_str()));
}

GG_TEST("panels", "repositories: a worktree row selects on a click and opens on a double-click")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string row = repoRow(s, repo);
    GG_REQUIRE(s.itemExists(row.c_str()));
    openRepoNode(ctx, row);
    const std::string wt1 = repo.filename().string() + "-wt1";
    const std::string wtRow = worktreeRow(row, wt1);
    GG_REQUIRE(s.itemExists(wtRow.c_str()));
    ctx->ItemClick(wtRow.c_str());
    ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), (s.root() / wt1).string());
    GG_CHECK(fs::equivalent(s.session()->path(), repo));
    ctx->ItemDoubleClick(wtRow.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), s.root() / wt1); }));
}

// The paths in the permanent repository list, as they are stored.
static std::vector<std::string> listedRepositories(Scenario& s)
{
    std::vector<std::string> out;
    for (const auto& entry : s.app.settings().data().repositories)
        out.push_back(entry.path);
    return out;
}

GG_TEST("panels", "repositories: a linked worktree that is opened stays under its main repository")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const std::string mainPath = ggui::normalizeRepoPath(repo.string());
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string row = repoRow(s, repo);
    GG_REQUIRE(s.itemExists(row.c_str()));
    openRepoNode(ctx, row);
    const std::string wt1 = repo.filename().string() + "-wt1";
    const std::string wtRow = worktreeRow(row, wt1);
    GG_REQUIRE(s.itemExists(wtRow.c_str()));
    ctx->ItemDoubleClick(wtRow.c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), s.root() / wt1); }));
    s.showPanel("Repositories");
    ctx->Yield(3);
    // The list has no entry for the worktree folder; the main repository is the open row.
    const auto listed = listedRepositories(s);
    GG_CHECK(listed.size() == 1);
    GG_CHECK(!listed.empty() && listed.front() == mainPath);
    GG_REQUIRE(s.itemExists(row.c_str()));
    GG_CHECK_STR_EQ(s.session()->repositories().openPath(), mainPath);
    openRepoNode(ctx, row);
    for (const auto& w : s.session()->snapshot()->worktrees)
        GG_CHECK(s.itemExists(worktreeRow(row, w.name).c_str()));
    // A double-click on the row of the main repository opens nothing.
    const auto* session = s.session();
    ctx->ItemDoubleClick(row.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    // Enter on the selected main repository row opens nothing either.
    ctx->ItemClick(row.c_str());
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), mainPath);
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    // A double-click on the "main" worktree row opens the main worktree again.
    const std::string mainWorktree = worktreeRow(row, "main");
    GG_REQUIRE(s.itemExists(mainWorktree.c_str()));
    ctx->ItemDoubleClick(mainWorktree.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), repo); }));
    GG_CHECK(listedRepositories(s).size() == 1);
}

GG_TEST("panels", "repositories: opening a linked worktree directly lists its main repository")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(wt1));
    GG_CHECK(s.waitUntil([&] { return !s.app.settings().data().repositories.empty(); }));
    ctx->Yield(3);
    const auto listed = listedRepositories(s);
    GG_CHECK(listed.size() == 1);
    GG_CHECK(!listed.empty() && listed.front() == ggui::normalizeRepoPath(repo.string()));
    // The recent list keeps the path of the worktree folder.
    GG_CHECK(!s.app.settings().data().recent.empty());
    GG_CHECK(fs::equivalent(s.app.settings().data().recent.front(), wt1));
}

GG_TEST("panels", "repositories: a worktree entry in the list stays, opens on a double-click and is a plain row")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    s.app.settings().addRepository(wt1.string());
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string row = repoRow(s, repo);
    const std::string wtEntry = repoRow(s, wt1);
    GG_REQUIRE(s.itemExists(row.c_str()) && s.itemExists(wtEntry.c_str()));
    GG_CHECK(!(ctx->ItemInfo(wtEntry.c_str()).StatusFlags & ImGuiItemStatusFlags_Openable));
    ctx->ItemDoubleClick(wtEntry.c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), wt1); }));
    s.showPanel("Repositories");
    ctx->Yield(3);
    // A double-click on the plain entry of the worktree that the window shows does not replace the session.
    const auto* shown = s.session();
    ctx->ItemDoubleClick(wtEntry.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == shown);
    // Both entries stay; the row of the main repository is the open one.
    GG_CHECK(listedRepositories(s).size() == 2);
    GG_CHECK(s.itemExists(wtEntry.c_str()));
    GG_CHECK_STR_EQ(s.session()->repositories().openPath(), ggui::normalizeRepoPath(repo.string()));
}

GG_TEST("panels", "repositories: the current and a missing worktree do not open, Enter opens the selected one")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string row = repoRow(s, repo);
    GG_REQUIRE(s.itemExists(row.c_str()));
    openRepoNode(ctx, row);
    const std::string current = worktreeRow(row, "main");
    GG_REQUIRE(s.itemExists(current.c_str()));
    const auto* session = s.session();
    ctx->ItemDoubleClick(current.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    ctx->KeyPress(ImGuiKey_Enter); // the double-click selected it
    ctx->Yield(3);
    s.settle();
    GG_CHECK(s.session() == session);
    // A missing worktree: neither a double-click nor Enter opens it.
    const auto snapshot = s.session()->snapshot();
    const auto missing = std::find_if(snapshot->worktrees.begin(), snapshot->worktrees.end(),
        [](const auto& w) { return w.missing; });
    GG_REQUIRE(missing != snapshot->worktrees.end());
    const std::string missingRow = worktreeRow(row, missing->name);
    ctx->ItemDoubleClick(missingRow.c_str());
    ctx->Yield(5);
    s.settle();
    GG_CHECK(s.session() == session);
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), missing->path.string());
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    s.settle();
    GG_CHECK(s.session() == session);
    // Enter opens another worktree.
    const std::string wt1 = repo.filename().string() + "-wt1";
    ctx->ItemClick(worktreeRow(row, wt1).c_str());
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), s.root() / wt1); }));
}

GG_TEST("panels", "repositories: the context menu sets and clears an alias")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::Linear, "first");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    const std::string openPath = ggui::normalizeRepoPath(repo.string());
    const std::string base = repo.filename().string();
    const std::string plainRow = repoRow(s, repo);
    GG_REQUIRE(s.itemExists(plainRow.c_str()));
    s.contextMenu(plainRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("group").empty());
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("alias") == base);
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("alias_path") == openPath);
    // "Set" with the defaults stores nothing.
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_REQUIRE(s.app.settings().data().repositories.size() == 1);
    GG_CHECK(s.app.settings().data().repositories.front().alias.empty());
    s.contextMenu(plainRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    s.dialogText("Set alias", "group", "grp");
    s.dialogText("Set alias", "alias", "name");
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.app.settings().data().repositories.front().alias, "grp/name");
    const std::string groupRow = "//Repositories/###group_grp/repo_name/###row";
    GG_CHECK(s.itemExists(groupRow.c_str()));
    // The dialog starts with the parts of the current alias; empty texts remove it.
    s.contextMenu(groupRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("group") == "grp");
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("alias") == "name");
    s.dialogText("Set alias", "group", "");
    s.dialogText("Set alias", "alias", "");
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_CHECK(s.app.settings().data().repositories.front().alias.empty());
    GG_CHECK(s.itemExists(plainRow.c_str()));
    GG_CHECK(!s.itemExists(groupRow.c_str()));
    // The Alias field has the focus: typed text and Enter set the alias.
    s.contextMenu(plainRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    ctx->KeyCharsReplace("typed");
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.app.settings().data().repositories.front().alias, "typed");
    GG_CHECK(!s.dialogOpen("Set alias"));
    // A group with an empty alias takes the base name.
    s.contextMenu(repoRow(s, repo).c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    s.dialogText("Set alias", "group", "grp");
    s.dialogText("Set alias", "alias", "");
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.app.settings().data().repositories.front().alias, "grp/" + base);
    // Copy path puts the path of the entry on the clipboard.
    s.contextMenu(repoRow(s, repo).c_str(), "Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), openPath);
}

// The alias the settings hold for a repository ("" when it has none).
static std::string repoAlias(Scenario& s, const fs::path& path)
{
    const std::string key = ggui::normalizeRepoPath(path.string());
    for (const auto& e : s.app.settings().data().repositories)
        if (e.path == key)
            return e.alias;
    return std::string();
}

GG_TEST("panels", "repositories: a drag onto a group sets the group part of the alias")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.app.settings().setAlias(ggui::normalizeRepoPath(second.string()), "grp/other");
    s.showPanel("Repositories");
    ctx->Yield(2);
    // A repository without an alias gets "group/<label>"; the label is the base name.
    const std::string row = repoRow(s, first);
    const std::string label = repoLeafLabel(ggui::buildRepoTree(s.app.settings().data().repositories), first);
    GG_REQUIRE(s.itemExists(row.c_str()));
    GG_REQUIRE(s.itemExists("//Repositories/###group_grp"));
    ctx->ItemDragAndDrop(row.c_str(), "//Repositories/###group_grp");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "grp/" + label);
    GG_CHECK(s.itemExists(("//Repositories/###group_grp/repo_" + label + "/###row").c_str()));
}

GG_TEST("panels", "repositories: a drag keeps the last segment of the alias, a drop on the own group changes nothing")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.app.settings().setAlias(ggui::normalizeRepoPath(first.string()), "a/x");
    s.app.settings().setAlias(ggui::normalizeRepoPath(second.string()), "b/y");
    s.showPanel("Repositories");
    ctx->Yield(2);
    const std::string own = "//Repositories/###group_a/repo_x/###row";
    GG_REQUIRE(s.itemExists(own.c_str()));
    // Onto the own group: the alias stays.
    ctx->ItemDragAndDrop(own.c_str(), "//Repositories/###group_a");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "a/x");
    // Onto the group "b": "a/x" becomes "b/x" and the group "a" goes away.
    ctx->ItemDragAndDrop(own.c_str(), "//Repositories/###group_b");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "b/x");
    GG_CHECK(s.itemExists("//Repositories/###group_b/repo_x/###row"));
    GG_CHECK(!s.itemExists("//Repositories/###group_a"));
}

GG_TEST("panels", "repositories: a deduplicated name shows in a group")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "alpha/dupe");
    const fs::path second = s.fixture(Recipe::Merges, "beta/dupe");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    ctx->Yield(2);
    const std::string firstRow = "//Repositories/###group_alpha/repo_dupe/###row";
    const std::string secondRow = "//Repositories/###group_beta/repo_dupe/###row";
    GG_CHECK(s.itemExists(firstRow.c_str()));
    GG_CHECK(s.itemExists(secondRow.c_str()));
    GG_CHECK(!s.itemExists("//Repositories/repo_dupe/###row"));
    GG_CHECK_STR_EQ(repoRow(s, first), firstRow);
    // A drop on the own group (from the folder prefix) changes nothing: no alias is stored.
    ctx->ItemDragAndDrop(firstRow.c_str(), "//Repositories/###group_alpha");
    ctx->Yield(2);
    GG_CHECK(repoAlias(s, first).empty());
    GG_CHECK(s.itemExists(firstRow.c_str()));
    // A drop on the top level stores the base name as the alias: the row leaves the group.
    GG_REQUIRE(s.itemExists("//Repositories/###top_level"));
    ctx->ItemDragAndDrop(firstRow.c_str(), "//Repositories/###top_level");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "dupe");
    GG_CHECK(s.itemExists("//Repositories/repo_dupe/###row"));
    GG_CHECK(!s.itemExists(firstRow.c_str()));
}

GG_TEST("panels", "repositories: Set alias on a repository in a prefix group starts with the prefix group")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "alpha/dupe");
    const fs::path second = s.fixture(Recipe::Merges, "beta/dupe");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    ctx->Yield(2);
    const std::string firstRow = "//Repositories/###group_alpha/repo_dupe/###row";
    GG_REQUIRE(s.itemExists(firstRow.c_str()));
    s.contextMenu(firstRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("group") == "alpha");
    GG_CHECK(s.app.dialogs().current() && s.app.dialogs().current()->text("alias") == "dupe");
    // "Set" with no change stores nothing: the row stays in the group.
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_CHECK(repoAlias(s, first).empty());
    GG_CHECK(s.itemExists(firstRow.c_str()));
    // An empty group with the base name stores the base name: the row goes to the top level.
    s.contextMenu(firstRow.c_str(), "Set alias...");
    GG_REQUIRE(s.dialogOpen("Set alias"));
    s.dialogText("Set alias", "group", "");
    s.dialogButton("Set alias", "Set");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "dupe");
    GG_CHECK(s.itemExists("//Repositories/repo_dupe/###row"));
    GG_CHECK(!s.itemExists(firstRow.c_str()));
}

GG_TEST("panels", "repositories: a drop on a group with a space at an end of its name stores nothing")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, " sp/dupe");
    const fs::path second = s.fixture(Recipe::Merges, "beta/dupe");
    const fs::path third = s.fixture(Recipe::Linear, "other");
    s.track(second);
    s.track(third);
    GG_REQUIRE(s.openRepository(third));
    s.app.settings().addRepository(first.string());
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    ctx->Yield(2);
    const std::string row = "//Repositories/repo_other/###row";
    GG_REQUIRE(s.itemExists(row.c_str()));
    GG_REQUIRE(s.itemExists("//Repositories/###group_ sp"));
    ctx->ItemDragAndDrop(row.c_str(), "//Repositories/###group_ sp");
    ctx->Yield(2);
    GG_CHECK(repoAlias(s, third).empty());
    GG_CHECK(s.itemExists(row.c_str()));
    GG_CHECK(!s.itemExists("//Repositories/###group_sp"));
}

GG_TEST("panels", "repositories: a drag onto the empty area leaves the group")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.app.settings().setAlias(ggui::normalizeRepoPath(first.string()), "a/x");
    s.showPanel("Repositories");
    ctx->Yield(2);
    GG_REQUIRE(s.itemExists("//Repositories/###top_level"));
    ctx->ItemDragAndDrop("//Repositories/###group_a/repo_x/###row", "//Repositories/###top_level");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(repoAlias(s, first), "x");
    GG_CHECK(s.itemExists("//Repositories/repo_x/###row"));
    GG_CHECK(!s.itemExists("//Repositories/###group_a"));
    // Without an alias a repository stays without one.
    const std::string plain = repoRow(s, second);
    ctx->ItemDragAndDrop(plain.c_str(), "//Repositories/###top_level");
    ctx->Yield(2);
    GG_CHECK(repoAlias(s, second).empty());
    GG_CHECK(s.itemExists(plain.c_str()));
}

GG_TEST("panels", "repositories: the alias does not show in the toolbar switcher")
{
    s.app.settings().data().repositories.clear();
    const fs::path repo = s.fixture(Recipe::Linear, "first");
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Repositories");
    s.app.settings().setAlias(ggui::normalizeRepoPath(repo.string()), "grp/aliased");
    ctx->Yield(2);
    ctx->ItemClick("//###Toolbar/##tb_repo");
    ctx->Yield(2);
    const std::string label = s.itemLabel("//##Combo_00/###switch_0");
    GG_CHECK(label.find(repo.filename().string()) != std::string::npos);
    GG_CHECK(label.find("aliased") == std::string::npos);
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("panels", "repositories: Remove from list, and Open in the context menu")
{
    s.app.settings().data().repositories.clear();
    const fs::path first = s.fixture(Recipe::Linear, "first");
    const fs::path second = s.fixture(Recipe::Merges, "second");
    s.track(second);
    GG_REQUIRE(s.openRepository(first));
    s.app.settings().addRepository(second.string());
    s.showPanel("Repositories");
    const std::string firstRow = repoRow(s, first);
    const std::string row = repoRow(s, second);
    GG_REQUIRE(s.itemExists(row.c_str()));
    const auto* session = s.session();
    // Open is disabled on the open repository.
    ctx->ItemClick(firstRow.c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/Open").ItemFlags & ImGuiItemFlags_Disabled);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    // Remove from list: the entry and its row go, the selection goes with them.
    ctx->ItemClick(row.c_str());
    ctx->Yield(3);
    GG_CHECK_STR_EQ(s.session()->repositories().selectedPath(), ggui::normalizeRepoPath(second.string()));
    s.contextMenu(row.c_str(), "Remove from list");
    ctx->Yield(3);
    GG_CHECK_EQ(s.app.settings().data().repositories.size(), size_t(1));
    GG_CHECK(!s.itemExists(row.c_str()));
    GG_CHECK(s.session()->repositories().selectedPath().empty());
    // A removed repository does not open on Enter.
    ctx->WindowFocus("//Repositories");
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(3);
    s.settle();
    GG_CHECK(s.session() == session);
    // Open in the context menu opens the repository.
    s.app.settings().addRepository(second.string());
    ctx->Yield(2);
    GG_REQUIRE(s.itemExists(row.c_str()));
    s.contextMenu(row.c_str(), "Open");
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened() && fs::equivalent(s.session()->path(), second); }));
}

GG_TEST("panels", "remotes: list and copy")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "backup", "https://example.invalid/backup.git"});
    s.git(repo, {"config", "remote.backup.prune", "true"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    const auto snapshot = s.session()->snapshot(); // keeps remotes alive while the UI refreshes
    const auto& remotes = snapshot->remotes;
    GG_REQUIRE(remotes.size() == 2);
    GG_CHECK_STR_EQ(remotes[0].name, "backup");
    GG_CHECK(remotes[0].pruneOnFetch);
    GG_CHECK(remotes[1].url.rfind("file://", 0) == 0);
    s.contextMenu("//Remotes/remote_origin/###row", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "origin");
}

GG_TEST("panels", "remotes: name@host label with the host dimmed, tooltip shows the URL once")
{
    using ggui::remoteHost;
    GG_CHECK_STR_EQ(remoteHost("https://github.com/owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("https://user:secret@git.example.org:8443/owner/repo.git"), "git.example.org");
    GG_CHECK_STR_EQ(remoteHost("ssh://git@github.com:22/owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("ssh://host.lan"), "host.lan");
    GG_CHECK_STR_EQ(remoteHost("ssh://git@[::1]:2222/repo"), "[::1]");
    GG_CHECK_STR_EQ(remoteHost("git@github.com:owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("github.com:owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("file:///srv/git/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("/srv/git/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("../repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("C:/repos/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost(""), "");

    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "backup", "https://user@example.invalid:8443/backup.git"});
    s.git(repo, {"remote", "add", "split", "https://example.invalid/fetch.git"});
    s.git(repo, {"remote", "set-url", "--push", "split", "ssh://git@example.invalid/push.git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    GG_CHECK(s.idShownDimmed("//Remotes", "backup@example.invalid", 6));
    // A local remote has no host: just the name.
    GG_CHECK(s.textShown("//Remotes", "origin"));
    GG_CHECK(!s.textShown("//Remotes", "origin@"));
    ctx->MouseMove("//Remotes/remote_backup/###row");
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "https://user@example.invalid:8443/backup.git"));
    GG_CHECK(s.drawnText("//##Tooltip_00").size() == 1);
    ctx->MouseMove("//Remotes/remote_split/###row");
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "Fetch: https://example.invalid/fetch.git"));
    GG_CHECK(s.textShown("//##Tooltip_00", "Push: ssh://git@example.invalid/push.git"));
}

GG_TEST("panels", "reflog: HEAD, branch, stash; filter; copy; reveal")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    s.git(repo, {"switch", "-q", "-c", "temp", "HEAD~1"});
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Reflog");
    auto& reflog = s.session()->reflog();
    const auto headLines = gg::splitLines(s.gitOut(repo, {"reflog", "--format=%H"}));
    GG_REQUIRE(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->entries.size() == headLines.size(); }));
    GG_CHECK_STR_EQ(reflog.reflog()->entries[0].newId.hex(), headLines[0]);
    GG_CHECK(reflog.reflog()->entries[0].message.find("checkout: moving from temp to main") != std::string::npos);
    // Column headers use Git words (no "Change" column: a row is a ref moving between commits).
    GG_CHECK(s.textShown("//Reflog", "Commits"));
    const std::string table = "//Reflog/##reflog_table";
    const std::string oldId = s.revParse(repo, "HEAD@{1}");
    // One copy item: for the ID the right click was on (its prefix: 3 characters, the rest: 7), the new ID anywhere
    // else on the row; Shift makes it the full ID.
    const std::string copyItem = "//$FOCUSED/###copy_id";
    const std::string newHead = headLines[0].substr(0, 7), oldHead = oldId.substr(0, 7);
    auto copied = [&](const std::string& label, const std::string& text) {
        GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + label + "###copy_id");
        ctx->MenuClick(copyItem.c_str());
        GG_CHECK_STR_EQ(s.clipboard(), text);
    };
    GG_REQUIRE(s.rightClickIdChars("//Reflog", newHead, 0, 3));
    copied(newHead.substr(0, 3), newHead.substr(0, 3));
    GG_REQUIRE(s.rightClickIdChars("//Reflog", newHead, 3, 7));
    copied(newHead, newHead);
    GG_REQUIRE(s.rightClickIdChars("//Reflog", oldHead, 0, 3));
    copied(oldHead.substr(0, 3), oldHead.substr(0, 3));
    GG_REQUIRE(s.rightClickIdChars("//Reflog", oldHead, 3, 7));
    copied(oldHead, oldHead);
    GG_REQUIRE(s.rightClickIdChars("//Reflog", oldHead, 3, 7, true));
    copied("full ID", oldId);
    GG_REQUIRE(s.rightClickIdChars("//Reflog", newHead, 0, 3, true));
    copied("full ID", headLines[0]);
    // Off the IDs (the message), the new ID, and the old ID's item after it.
    const std::string oldItem = "//$FOCUSED/old/###copy_id";
    const std::string row0 = table + "/r0/###reflog_0";
    ctx->ItemClick(row0.c_str(), ImGuiMouseButton_Right);
    GG_CHECK_STR_EQ(s.itemLabel(oldItem.c_str()), "Copy old " + oldHead + "###copy_id");
    copied(newHead, newHead);
    ctx->ItemClick(row0.c_str(), ImGuiMouseButton_Right);
    ctx->ItemClick(oldItem.c_str()); // MenuClick would read "old" as a menu
    GG_CHECK_STR_EQ(s.clipboard(), oldHead);
    // With Shift held at the click both items give the full ID; the old one says which.
    ctx->MouseMove(row0.c_str());
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy full ID###copy_id");
    GG_CHECK_STR_EQ(s.itemLabel(oldItem.c_str()), "Copy old full ID###copy_id");
    ctx->ItemClick(oldItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), oldId);
    // On an ID, only that ID's item.
    GG_REQUIRE(s.rightClickIdChars("//Reflog", newHead, 3, 7));
    GG_CHECK(s.itemExists(copyItem.c_str()) && !s.itemExists(oldItem.c_str()));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    // Opened with Alt+Space (no click): the new ID's item, then the old ID's.
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->NavMoveTo(row0.c_str());
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + newHead + "###copy_id");
    GG_CHECK_STR_EQ(s.itemLabel(oldItem.c_str()), "Copy old " + oldHead + "###copy_id");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    // Both IDs of a row are 7 characters: 3 highlighted, 4 dimmed.
    GG_CHECK(s.idShownDimmed("//Reflog", headLines[0].substr(0, 7), 3));
    GG_CHECK(s.idShownDimmed("//Reflog", oldId.substr(0, 7), 3));
    s.contextMenu((table + "/r0/###reflog_0").c_str(), "Reveal old commit");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD@{1}"); }));
    s.showPanel("Reflog");
    s.contextMenu((std::string("//Reflog/##reflog_table") + "/r0/###reflog_0").c_str(), "Reveal new commit");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == headLines[0]; }));
    s.showPanel("Reflog");
    // Filter.
    ctx->ItemInputValue("//Reflog/##reflog_filter", "moving from temp");
    ctx->Yield(2);
    GG_CHECK(s.itemExists((std::string("//Reflog/##reflog_table") + "/r0/###reflog_0").c_str()));
    GG_CHECK(!s.itemExists((std::string("//Reflog/##reflog_table") + "/r1/###reflog_1").c_str()));
    ctx->ItemInputValue("//Reflog/##reflog_filter", "");
    // Other reflogs: a branch and the stash.
    s.comboSelect("//Reflog/##reflog_ref", "###ref_refs:stash");
    const auto stashes = gg::splitLines(s.gitOut(repo, {"reflog", "show", "--format=%H", "refs/stash"}));
    GG_CHECK(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->ref == "refs/stash" && reflog.reflog()->entries.size() == stashes.size(); }));
    s.comboSelect("//Reflog/##reflog_ref", "###ref_refs:heads:temp");
    GG_CHECK(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->ref == "refs/heads/temp" && reflog.reflog()->entries.size() == 1; }));
}

GG_TEST("panels", "stashes: copy the stash's ID and its base's ID")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    const std::string row = "//Stashes/stash_0/###row";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    const std::string stashId = s.revParse(repo, "stash@{0}"), baseId = s.revParse(repo, "stash@{0}^1");
    const std::string stash7 = stashId.substr(0, 7), base7 = baseId.substr(0, 7);
    const std::string copyItem = "//$FOCUSED/###copy_id", baseItem = "//$FOCUSED/base/###copy_id";
    auto copied = [&](const std::string& label, const std::string& text) {
        GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + label + "###copy_id");
        ctx->MenuClick(copyItem.c_str());
        GG_CHECK_STR_EQ(s.clipboard(), text);
    };
    // A right click on the base ID: only the base's item (the highlighted 3 characters, the rest, Shift: full).
    GG_REQUIRE(s.rightClickIdChars("//Stashes", base7, 0, 3));
    GG_CHECK(!s.itemExists(baseItem.c_str()));
    copied(base7.substr(0, 3), base7.substr(0, 3));
    GG_REQUIRE(s.rightClickIdChars("//Stashes", base7, 3, 7));
    GG_CHECK(!s.itemExists(baseItem.c_str()));
    copied(base7, base7);
    GG_REQUIRE(s.rightClickIdChars("//Stashes", base7, 3, 7, true));
    GG_CHECK(!s.itemExists(baseItem.c_str()));
    copied("full ID", baseId);
    GG_REQUIRE(s.rightClickIdChars("//Stashes", base7, 0, 3, true));
    GG_CHECK(!s.itemExists(baseItem.c_str()));
    copied("full ID", baseId);
    // Elsewhere on the row (its start, over the "stash@{0}" text): the stash's commit, and the base's after it.
    const ImRect rect = ctx->ItemInfo(row.c_str()).RectFull;
    const ImVec2 off(rect.Min.x + 8.0f, rect.GetCenter().y);
    ctx->MouseMoveToPos(off);
    ctx->MouseClick(ImGuiMouseButton_Right);
    GG_CHECK_STR_EQ(s.itemLabel(baseItem.c_str()), "Copy base " + base7 + "###copy_id");
    copied(stash7, stash7);
    ctx->MouseMoveToPos(off);
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->ItemClick(baseItem.c_str()); // MenuClick would read "base" as a menu
    GG_CHECK_STR_EQ(s.clipboard(), base7);
    // With Shift held at the click both items give the full ID; the base one says which.
    ctx->MouseMoveToPos(off);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.itemLabel(baseItem.c_str()), "Copy base full ID###copy_id");
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy full ID###copy_id");
    ctx->ItemClick(baseItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), baseId);
    ctx->MouseMoveToPos(off);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->MenuClick(copyItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), stashId);
    // Opened with Alt+Space (no click): the stash's item, then the base's.
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->NavMoveTo(row.c_str());
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + stash7 + "###copy_id");
    GG_CHECK_STR_EQ(s.itemLabel(baseItem.c_str()), "Copy base " + base7 + "###copy_id");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    ctx->SetInputMode(ImGuiInputSource_Mouse);
}

GG_TEST("panels", "details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"tag", "-a", "-m", "An annotated tag", "ann"});
    const fs::path locked = s.root() / "locked-wt";
    s.git(repo, {"worktree", "add", "-q", "--detach", locked.string()});
    s.git(repo, {"worktree", "lock", "--reason", "on a USB stick", locked.string()});
    GG_REQUIRE(s.openRepository(repo));
    auto hover = [&](const std::string& ref) {
        ctx->MouseMove(ref.c_str());
        ctx->SleepNoSkip(1.0f, 0.1f);
    };

    // A remote-tracking branch: its menu, and its visibility in History.
    s.showPanel("Branches");
    const std::string rbranch = "//Branches/remote_group_origin/origin/rbranch_origin:main/###rbranch_origin:main";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rbranch.c_str()); }));
    s.contextMenu(rbranch.c_str(), "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "origin/main");
    s.contextMenu(rbranch.c_str(), "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "origin/main"); }));
    auto& history = s.session()->history();
    const std::string rbranchEye = "//Branches/remote_group_origin/origin/rbranch_origin:main/###eye";
    ctx->ItemClick(rbranchEye.c_str());
    GG_CHECK(!history.refVisible("refs/remotes/origin/main"));
    ctx->ItemClick(rbranchEye.c_str());
    GG_CHECK(history.refVisible("refs/remotes/origin/main"));
    // Tooltips: an annotated tag's message, a remote's URLs, a locked worktree's reason.
    s.showPanel("Tags");
    hover("//Tags/tag_ann/###tag_ann");
    s.showPanel("Remotes");
    hover("//Remotes/remote_origin/###row");
    s.showPanel("Worktrees");
    const std::string wtRow = "//Worktrees/worktree_" + locked.filename().string() + "/###row";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtRow.c_str()); }));
    hover(wtRow);
    ctx->ItemClick(wtRow.c_str(), ImGuiMouseButton_Right);
    GG_CHECK(s.itemExists("//$FOCUSED/Unlock"));
    ctx->KeyPress(ImGuiKey_Escape);
    // The reflog filtered by a commit ID.
    s.showPanel("Reflog");
    const std::string head = s.head(repo);
    ctx->ItemInputValue("//Reflog/##reflog_filter", head.substr(0, 12).c_str());
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//Reflog/##reflog_table/r0/###reflog_0"));
    ctx->ItemInputValue("//Reflog/##reflog_filter", "");
    // Stash changes from the Stashes panel.
    s.write(repo, "dirty.txt", "dirty\n");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->untracked.empty(); }));
    s.showPanel("Stashes");
    ctx->ItemClick("//Stashes/Push##stash_push");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogText("Stash changes", "message", "from the Stashes panel");
    s.dialogCheck("Stash changes", "untracked", "Include untracked files");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"stash", "list"}).find("from the Stashes panel") != std::string::npos; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / "dirty.txt"));
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    s.write(repo, "dirty.txt", "dirty\n");

    // Operations: a commit its pre-commit hook refuses is listed as failed; the buttons undo and redo.
    s.write(repo / ".git" / "hooks", "pre-commit", "#!/bin/sh\necho no >&2\nexit 1\n");
    fs::permissions(repo / ".git" / "hooks" / "pre-commit", fs::perms::owner_all);
    s.git(repo, {"add", "dirty.txt"});
    ctx->ItemClick("//###Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Refused");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.dismissError());
    fs::remove(repo / ".git" / "hooks" / "pre-commit");
    s.showPanel("Operations");
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Operations", "(failed)"); }));
    // Ctrl+N needs a commit a branch can advance from: HEAD's (origin/main, selected above, has no local branch).
    s.session()->selectCommit(ggui::core::Oid::fromHex(s.head(repo)));
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->operations().empty() && s.session()->operations().back().label == "new commit"; }));
    s.settle();
    const std::string made = s.head(repo);
    // A copy: the operations list is reloaded while the UI runs.
    const std::string opId = s.session()->operations().back().id;
    const std::string row = s.child("//Operations", "##ops_table") + "/**/op_" + opId + "/###row";
    hover(row);
    s.contextMenu(row.c_str(), "Copy operation ID");
    GG_CHECK_STR_EQ(s.clipboard(), opId);
    ctx->ItemClick("//Operations/###ops_undo");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != made; }));
    s.settle();
    ctx->ItemClick("//Operations/###ops_redo");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == made; }));
    s.settle();
    s.git(repo, {"worktree", "unlock", locked.string()});
    s.git(repo, {"worktree", "remove", "--force", locked.string()});
}

// Row hover actions (GG-50): icon buttons at a row's right edge while it is hovered.
GG_TEST("panels", "branches: hover actions appear on the hovered row only and check out or push")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "other"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string other = "//Branches/branch_other/###branch_other";
    const std::string mainRow = "//Branches/branch_main/###branch_main";
    const std::string otherCheckout = "//Branches/branch_other/###act_checkout";
    const std::string otherPush = "//Branches/branch_other/###act_push";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(other.c_str()); }));
    // Not hovered: no buttons.
    ctx->MouseMove("//Branches/##branch_filter");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(otherCheckout.c_str()));
    GG_CHECK(!s.itemExists(otherPush.c_str()));
    // Hovered: both; the mouse leaving removes them.
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists(otherCheckout.c_str()));
    GG_CHECK(s.itemExists(otherPush.c_str()));
    // ... and only that row's: main's are absent while other is hovered.
    GG_CHECK(!s.itemExists("//Branches/branch_main/###act_push"));
    GG_CHECK(!s.itemExists("//Branches/branch_main/###act_checkout"));
    ctx->MouseMove("//Branches/##branch_filter");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(otherCheckout.c_str()));
    GG_CHECK(!s.itemExists(otherPush.c_str()));
    // The HEAD branch has no Check out; Push stays.
    ctx->MouseMove(mainRow.c_str());
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//Branches/branch_main/###act_checkout"));
    GG_CHECK(s.itemExists("//Branches/branch_main/###act_push"));
    // Check out does what the menu item does.
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    ctx->ItemClick(otherCheckout.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "other"; }));
    s.settle();
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(otherCheckout.c_str()));
}

GG_TEST("panels", "branches: Push on a branch without an upstream opens Push to from the hover button")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "other"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string other = "//Branches/branch_other/###branch_other";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(other.c_str()); }));
    auto& history = s.session()->history();
    GG_CHECK(history.refVisible("refs/heads/other"));
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    const ImGuiID pushId = ctx->ItemInfo("//Branches/branch_other/###act_push").ID;
    ctx->ItemClick("//Branches/branch_other/###act_push");
    GG_REQUIRE(s.dialogOpen("Push to"));
    ctx->Yield(3);
    GG_CHECK_STR_EQ(s.session()->snapshot()->headBranch, "main");
    GG_CHECK(history.refVisible("refs/heads/other"));
    s.dialogButton("Push to", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->headBranch, "main");
    // The button is not a keyboard stop: the click did not move the navigation focus onto it.
    GG_CHECK(ImGui::GetCurrentContext()->NavId != pushId);
}

// The modal of the test above would hide a click that also reached the row; a button that opens nothing shows it.
GG_TEST("panels", "branches: a click on a hover button is not a click on the row")
{
    const fs::path repo = s.fixture(Recipe::Linear); // no remote: Push is disabled
    s.git(repo, {"branch", "other"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string other = "//Branches/branch_other/###branch_other";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(other.c_str()); }));
    const std::string head = s.session()->snapshot()->headBranch;
    GG_REQUIRE(head != "other");
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists("//Branches/branch_other/###act_push"));
    ctx->ItemDoubleClick("//Branches/branch_other/###act_push");
    ctx->Yield(5);
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->headBranch, head);
    // The row's own double click still checks the branch out.
    ctx->MouseMove(other.c_str());
    ctx->Yield(3);
    ctx->ItemDoubleClick(other.c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "other"; }));
}

// The strip of buttons belongs to the hovered row alone: one pixel inside either edge of a row, a neighbour
// shows nothing (a selectable's rect already includes half the item spacing on each side).
GG_TEST("panels", "branches: hover buttons stay on their own row at its edges")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "alpha"});
    s.git(repo, {"branch", "other"});
    s.git(repo, {"branch", "zeta"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string other = "//Branches/branch_other/###branch_other";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(other.c_str()); }));
    const auto absent = [&](const char* name) {
        return !s.itemExists(("//Branches/branch_" + std::string(name) + "/###act_push").c_str());
    };
    const ImGuiTestItemInfo row = ctx->ItemInfo(other.c_str());
    const float x = row.RectFull.Min.x + 10.0f;
    for (const float y : {row.RectFull.Max.y - 1.0f, row.RectFull.Min.y + 1.0f}) {
        ctx->MouseMoveToPos(ImVec2(x, y));
        ctx->Yield(3);
        GG_CHECK(s.itemExists("//Branches/branch_other/###act_push"));
        GG_CHECK(absent("alpha"));
        GG_CHECK(absent("zeta"));
        GG_CHECK(absent("main"));
    }
}

GG_TEST("panels", "branches: a remote-tracking row's hover Check out opens Create branch and creates the tracking branch")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "-f", "side", "HEAD~1"});
    s.git(repo, {"push", "-q", "origin", "side"});
    s.git(repo, {"branch", "-D", "side"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = "//Branches/remote_group_origin/origin/rbranch_origin:side/###rbranch_origin:side";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    const std::string checkout = "//Branches/remote_group_origin/origin/rbranch_origin:side/###act_checkout";
    ctx->MouseMove("//Branches/##branch_filter");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(checkout.c_str()));
    ctx->MouseMove(row.c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists(checkout.c_str()));
    ctx->ItemClick(checkout.c_str());
    GG_REQUIRE(s.dialogOpen("Create branch"));
    GG_CHECK(s.session()->snapshot()->findBranch("side") == nullptr);
    GG_CHECK(s.gitOut(repo, {"branch", "--list", "side"}).empty());
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headBranch == "side"; }));
    const auto snapshot = s.session()->snapshot();
    const auto* side = snapshot->findBranch("side");
    GG_REQUIRE(side != nullptr);
    GG_CHECK_STR_EQ(side->upstream, "origin/side");
}

GG_TEST("panels", "branches: a folder group's eye hides and shows all its branches; Ctrl-click shows only them")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "feature/two", "HEAD~1"});
    s.git(repo, {"branch", "feature/three", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto& history = s.session()->history();
    const std::string groupEye = "//Branches/group_local:feature:/###eye";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(groupEye.c_str()); }));
    const std::string one = "refs/heads/feature/one", two = "refs/heads/feature/two", three = "refs/heads/feature/three";
    ctx->ItemClick(groupEye.c_str());
    GG_CHECK(!history.refVisible(one) && !history.refVisible(two) && !history.refVisible(three));
    GG_CHECK(history.refVisible("refs/heads/main"));
    ctx->ItemClick(groupEye.c_str());
    GG_CHECK(history.refVisible(one) && history.refVisible(two) && history.refVisible(three));
    // Mixed (one hidden by its own eye): a click shows all.
    ctx->ItemClick("//Branches/branch_feature:one/###eye");
    GG_CHECK(!history.refVisible(one) && history.refVisible(two));
    ctx->ItemClick(groupEye.c_str());
    GG_CHECK(history.refVisible(one) && history.refVisible(two) && history.refVisible(three));
    // The click did not close the group.
    GG_CHECK(s.itemExists("//Branches/branch_feature:one/###branch_feature:one"));
    // Ctrl-click: only these, also with a member hidden.
    ctx->ItemClick("//Branches/branch_feature:one/###eye");
    GG_CHECK(!history.refVisible(one));
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(groupEye.c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(history.refVisible(one) && history.refVisible(two) && history.refVisible(three));
    GG_CHECK(!history.refVisible("refs/heads/main") && !history.refVisible("refs/remotes/origin/main"));
}

GG_TEST("panels", "branches: a remote node's eye hides and shows all its remote-tracking branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "feature/two", "HEAD~1"});
    s.git(repo, {"push", "-q", "origin", "feature/one", "feature/two"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto& history = s.session()->history();
    const std::string nodeEye = "//Branches/remote_group_origin/###eye";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(nodeEye.c_str()); }));
    const std::string main = "refs/remotes/origin/main", one = "refs/remotes/origin/feature/one",
                      two = "refs/remotes/origin/feature/two";
    ctx->ItemClick(nodeEye.c_str());
    GG_CHECK(!history.refVisible(main) && !history.refVisible(one) && !history.refVisible(two));
    GG_CHECK(history.refVisible("refs/heads/main") && history.refVisible("refs/heads/feature/one"));
    ctx->ItemClick(nodeEye.c_str());
    GG_CHECK(history.refVisible(main) && history.refVisible(one) && history.refVisible(two));
    // A folder group under the remote has its own eye.
    const std::string folderEye = "//Branches/remote_group_origin/origin/group_remote:origin:feature:/###eye";
    GG_REQUIRE(s.itemExists(folderEye.c_str()));
    ctx->ItemClick(folderEye.c_str());
    GG_CHECK(history.refVisible(main) && !history.refVisible(one) && !history.refVisible(two));
    GG_CHECK(history.refVisible("refs/heads/feature/one"));
    // Mixed: a click on the remote node shows all.
    ctx->ItemClick(nodeEye.c_str());
    GG_CHECK(history.refVisible(main) && history.refVisible(one) && history.refVisible(two));
    // Ctrl-click: only the remote's branches.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(nodeEye.c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(history.refVisible(main) && history.refVisible(one) && history.refVisible(two));
    GG_CHECK(!history.refVisible("refs/heads/main") && !history.refVisible("refs/heads/feature/one"));
}

GG_TEST("panels", "branches: with a filter typed a group's eye changes only the listed branches")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "feature/other", "HEAD~1"});
    s.git(repo, {"branch", "feature/two", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_feature:one/###eye"); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "feature/o");
    const std::string groupEye = "//Branches/group_local:feature:/###eye";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(groupEye.c_str()) && !s.itemExists("//Branches/branch_feature:two/###eye"); }));
    ctx->ItemClick(groupEye.c_str());
    GG_CHECK(!history.refVisible("refs/heads/feature/one") && !history.refVisible("refs/heads/feature/other"));
    GG_CHECK(history.refVisible("refs/heads/feature/two"));
    ctx->ItemClick(groupEye.c_str());
    GG_CHECK(history.refVisible("refs/heads/feature/one") && history.refVisible("refs/heads/feature/other"));
    ctx->ItemInputValue("//Branches/##branch_filter", "");
}

GG_TEST("panels", "branches: a filter opens a closed group")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "feature/other", "HEAD~1"});
    s.git(repo, {"branch", "feature/two", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string row = "//Branches/branch_feature:one/###eye";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->ItemClose("//Branches/###group_local:feature:");
    GG_REQUIRE(s.waitUntil([&] { return !s.itemExists(row.c_str()); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "feature/o");
    GG_CHECK(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "");
}

GG_TEST("panels", "tags: the hover action reveals a tag; there is no Delete button")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"tag", "v1.0", "HEAD~3"});
    s.git(repo, {"tag", "v2.0", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    const std::string v1 = "//Tags/tag_v1.0/###tag_v1.0";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(v1.c_str()); }));
    ctx->MouseMove("//Tags/##tag_filter");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//Tags/tag_v1.0/###act_reveal"));
    ctx->MouseMove(v1.c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists("//Tags/tag_v1.0/###act_reveal"));
    GG_CHECK(!s.itemExists("//Tags/tag_v1.0/###act_delete"));
    GG_CHECK(!s.itemExists("//Tags/tag_v2.0/###act_reveal"));
    ctx->ItemClick("//Tags/tag_v1.0/###act_reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD~3"); }));
}

GG_TEST("panels", "remotes: hover actions Fetch and Pull on the hovered row only")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "backup", "https://example.invalid/backup.git"});
    // On origin/main, so that the pull below is a fast-forward.
    s.git(repo, {"reset", "-q", "--hard", "origin/main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    const std::string origin = "//Remotes/remote_origin/###row";
    const std::string backup = "//Remotes/remote_backup/###row";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(origin.c_str()) && s.itemExists(backup.c_str()); }));
    auto disabled = [&](const char* ref) { return (ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) != 0; };
    // Not hovered: no buttons.
    ctx->MouseMove("//Remotes/Fetch all##fetch_all");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//Remotes/remote_origin/###act_fetch"));
    GG_CHECK(!s.itemExists("//Remotes/remote_origin/###act_pull"));
    // Hovered: both, on that row only. main tracks origin/main, so Pull is there; backup has no tracking branch.
    ctx->MouseMove(origin.c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists("//Remotes/remote_origin/###act_fetch"));
    GG_CHECK(s.itemExists("//Remotes/remote_origin/###act_pull"));
    GG_CHECK(!s.itemExists("//Remotes/remote_backup/###act_fetch"));
    GG_CHECK(!disabled("//Remotes/remote_origin/###act_fetch"));
    GG_CHECK(!disabled("//Remotes/remote_origin/###act_pull"));
    ctx->MouseMove(backup.c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists("//Remotes/remote_backup/###act_pull"));
    GG_CHECK(!disabled("//Remotes/remote_backup/###act_fetch"));
    GG_CHECK(disabled("//Remotes/remote_backup/###act_pull"));
    // Fetch does what the menu item does: a commit pushed to the remote since arrives as origin/main.
    const fs::path other = s.root() / (repo.filename().string() + "-other");
    s.commitFile(other, "later.txt", "later\n", "Later on the remote");
    s.git(other, {"push", "-q", "origin", "main"});
    const std::string pushed = s.head(other);
    GG_REQUIRE(s.revParse(repo, "origin/main") != pushed);
    const std::string before = s.head(repo);
    ctx->MouseMove(origin.c_str());
    ctx->Yield(3);
    ctx->ItemClick("//Remotes/remote_origin/###act_fetch");
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "origin/main") == pushed; }));
    // A fetch only: the branch stays where it was until Pull.
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), before);
    ctx->MouseMove(origin.c_str());
    ctx->Yield(3);
    ctx->ItemClick("//Remotes/remote_origin/###act_pull");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == pushed; }));
}

GG_TEST("panels", "stashes: hover actions Apply and Pop leave the selection alone")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    auto stashRowRef = [](int index) { return "//Stashes/stash_" + std::to_string(index) + "/###row"; };
    auto stashes = [&] { // SIZE_MAX while refs/stash is being rewritten (git briefly fails then)
        const auto r = s.gitMayFail(repo, {"stash", "list"});
        return r.ok() ? gg::splitLines(r.out).size() : SIZE_MAX;
    };
    auto stashButton = [](int index, const char* name) { return "//Stashes/stash_" + std::to_string(index) + "/###" + name; };
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(stashRowRef(1).c_str()) && s.itemExists(stashRowRef(2).c_str()); }));
    // Not hovered: no buttons; hovered: both, on that row only.
    ctx->MouseMove("//Stashes/Push##stash_push");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(stashButton(2, "act_apply").c_str()));
    ctx->MouseMove(stashRowRef(2).c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists(stashButton(2, "act_apply").c_str()));
    GG_CHECK(s.itemExists(stashButton(2, "act_pop").c_str()));
    GG_CHECK(!s.itemExists(stashButton(1, "act_apply").c_str()));
    GG_CHECK(!s.itemExists(stashButton(0, "act_pop").c_str()));
    // Select stash 1; a click on a button of another row does not move the selection.
    ctx->ItemClick(stashRowRef(1).c_str());
    const auto selected = s.session()->selection();
    GG_REQUIRE(selected.kind == ggui::SelKind::Stash);
    // Apply (stash 2: "worktree change") applies and keeps the stash.
    ctx->MouseMove(stashRowRef(2).c_str());
    ctx->Yield(3);
    ctx->ItemClick(stashButton(2, "act_apply").c_str());
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "a.txt") == "a changed\n"; }));
    s.settle();
    GG_CHECK_EQ(stashes(), 3u);
    GG_CHECK(s.session()->selection().kind == selected.kind && s.session()->selection().id == selected.id);
    s.git(repo, {"checkout", "--", "a.txt"});
    // Pop (stash 0: "with untracked") applies and drops it.
    ctx->MouseMove(stashRowRef(0).c_str());
    ctx->Yield(3);
    ctx->ItemClick(stashButton(0, "act_pop").c_str());
    GG_CHECK(s.waitUntil([&] { return stashes() == 2; }));
    s.settle();
    GG_CHECK(fs::exists(repo / "new.txt"));
    GG_CHECK(s.session()->selection().kind == selected.kind && s.session()->selection().id == selected.id);
}

GG_TEST("panels", "worktrees: hover actions Open here (not on the current one) and Open directory")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    const std::string wt1 = repo.filename().string() + "-wt1";
    const std::string wt3 = repo.filename().string() + "-wt3"; // its directory is gone
    auto row = [](const std::string& name) { return "//Worktrees/worktree_" + name + "/###row"; };
    auto button = [](const std::string& name, const char* act) { return "//Worktrees/worktree_" + name + "/###" + act; };
    auto disabled = [&](const std::string& ref) { return (ctx->ItemInfo(ref.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0; };
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row(wt1).c_str()) && s.itemExists(row(wt3).c_str()); }));
    // Not hovered: no buttons.
    ctx->MouseMove("//Worktrees/###add_worktree");
    ctx->Yield(3);
    GG_CHECK(!s.itemExists(button(wt1, "act_open_dir").c_str()));
    // The current (main) worktree: Open directory only.
    ctx->MouseMove(row("main").c_str());
    ctx->Yield(3);
    GG_CHECK(s.itemExists(button("main", "act_open_dir").c_str()));
    GG_CHECK(!s.itemExists(button("main", "act_open_here").c_str()));
    GG_CHECK(!s.itemExists(button(wt1, "act_open_dir").c_str()));
    // A missing one: both buttons, disabled.
    ctx->MouseMove(row(wt3).c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists(button(wt3, "act_open_here").c_str()));
    GG_CHECK(disabled(button(wt3, "act_open_here")));
    GG_CHECK(disabled(button(wt3, "act_open_dir")));
    // Another worktree: both, enabled; Open here switches this window to it.
    ctx->MouseMove(row(wt1).c_str());
    ctx->Yield(3);
    GG_REQUIRE(s.itemExists(button(wt1, "act_open_here").c_str()));
    GG_CHECK(!disabled(button(wt1, "act_open_here")));
    GG_CHECK(s.itemExists(button(wt1, "act_open_dir").c_str()));
    GG_CHECK(!disabled(button(wt1, "act_open_dir")));
    ctx->ItemClick(button(wt1, "act_open_here").c_str());
    GG_CHECK(s.waitUntil([&] {
        auto* session = s.session();
        return session && session->opened() && gg::worktrees::samePath(session->path(), s.root() / wt1);
    }));
}

GG_TEST("panels", "stashes: a message with ## is shown whole")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    s.write(repo, "a.txt", "a for the ## stash\n");
    s.gitOut(repo, {"stash", "push", "-q", "-m", "## hd"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    // The message is short enough for fitText() and for the test engine's 31-character item label.
    GG_CHECK_STR_EQ(s.itemText("//Stashes/stash_0/###row"), "stash@{0} On main: ## hd");
    // ImGui cuts a label at "##": the row must still draw the message past it. The panel is narrow here, so the
    // row's trailing ID and date overlap the text and textShown() cannot match it whole; no other text has a '#'.
    GG_CHECK(s.waitUntil([&] {
        size_t hashes = 0;
        for (const std::string& line : s.drawnText("//Stashes"))
            hashes += static_cast<size_t>(std::count(line.begin(), line.end(), '#'));
        return hashes == 2;
    }));
}

GG_TEST("panels", "branches: a name with ## is shown whole")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.gitOut(repo, {"branch", "topic##tail"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    // A branch row is drawn by visibilityRow(), like the kept commit, remote branch and tag rows.
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Branches", "topic##tail"); }));
    GG_CHECK(s.itemText("//Branches/branch_topic##tail/###branch_topic##tail") == "topic##tail");
}

} // namespace ggtest
