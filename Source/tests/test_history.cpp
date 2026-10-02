// History panel (§4.2).
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <set>

namespace ggtest {

namespace {

using ggui::core::Oid;

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

const ggui::core::HistoryRow* findRow(Scenario& s, const std::string& hex)
{
    return s.session()->history().row(Oid::fromHex(hex));
}

bool hasBadge(const ggui::core::HistoryRow* row, ggui::core::RefKind kind, const std::string& name)
{
    if (!row)
        return false;
    return std::any_of(row->refs.begin(), row->refs.end(),
        [&](const ggui::core::RefBadge& b) { return b.kind == kind && b.name == name; });
}

} // namespace

GG_TEST("history", "graph, rows, badges and short IDs")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.expandMerge(s.revParse(repo, "main")));
    const auto& rows = s.session()->history().rows();
    const auto all = gg::splitLines(s.gitOut(repo, {"rev-list", "--all", "--topo-order"}));
    GG_CHECK_EQ(rows.size(), all.size());
    std::set<std::string> seen;
    for (const auto& row : rows) {
        // Topological: every parent comes after its child.
        for (const auto& p : row.parents)
            GG_CHECK(!seen.count(p.hex()));
        seen.insert(row.id.hex());
        GG_CHECK_STR_EQ(row.shortId, row.id.hex().substr(0, 7));
        GG_CHECK_STR_EQ(row.subject, s.gitOut(repo, {"log", "-1", "--format=%s", row.id.hex()}));
        GG_CHECK_STR_EQ(row.author, "Test User");
        GG_CHECK_EQ(row.time, std::stoll(s.gitOut(repo, {"log", "-1", "--format=%at", row.id.hex()})));
    }
    // The merge commit has two parents and draws an edge into a second lane.
    const std::string mergeHex = s.revParse(repo, "main");
    const auto* merge = findRow(s, mergeHex);
    GG_REQUIRE(merge != nullptr);
    GG_CHECK_EQ(merge->parents.size(), static_cast<size_t>(2));
    const bool curve = std::any_of(merge->lines.begin(), merge->lines.end(),
        [](const ggui::core::GraphLine& l) { return l.fromPos == 1 && l.toPos == 2 && l.fromLane != l.toLane; });
    GG_CHECK(curve);
    const auto* feature = findRow(s, s.revParse(repo, "feature"));
    const auto* firstParent = findRow(s, s.revParse(repo, "main^1"));
    GG_REQUIRE(feature != nullptr && firstParent != nullptr);
    GG_CHECK(feature->lane != firstParent->lane);
    // Badges: current branch (outlined), other branches.
    GG_CHECK(hasBadge(merge, ggui::core::RefKind::LocalBranch, "main"));
    GG_CHECK(merge->refs.front().current);
    GG_CHECK(hasBadge(feature, ggui::core::RefKind::LocalBranch, "feature"));
    GG_CHECK(hasBadge(findRow(s, s.revParse(repo, "topic")), ggui::core::RefKind::LocalBranch, "topic"));
    // Rows and badges are real UI items.
    GG_CHECK(s.itemExists(rowRef(mergeHex).c_str()));
    GG_CHECK(s.itemExists(("//History/**/" + mergeHex + "/###badge_main").c_str()));
    GG_CHECK(s.itemText(rowRef(mergeHex).c_str()).rfind(merge->shortId + " Merge feature", 0) == 0);
}

GG_TEST("history", "published vs unpublished commits")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    const auto* local = findRow(s, s.revParse(repo, "main"));
    const auto* remote = findRow(s, s.revParse(repo, "origin/main"));
    const auto* shared = findRow(s, s.revParse(repo, "main~1"));
    GG_REQUIRE(local && remote && shared);
    GG_CHECK(!local->published);
    GG_CHECK(remote->published);
    GG_CHECK(shared->published);
    GG_CHECK(hasBadge(remote, ggui::core::RefKind::RemoteBranch, "origin/main"));
    // Change information agrees.
    ctx->ItemClick(rowRef(local->id.hex()).c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->info().details() && s.session()->info().details()->id == local->id; }));
    GG_CHECK(!s.session()->info().details()->published);
    ctx->ItemClick(rowRef(shared->id.hex()).c_str());
    GG_CHECK(s.waitUntil([&] { return s.session()->info().details() && s.session()->info().details()->id == shared->id; }));
    GG_CHECK(s.session()->info().details()->published);
}

GG_TEST("history", "Working tree and Index rows")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.itemExists("//History/**/###row_wt"));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->staged.empty(); }));
    ctx->ItemClick("//History/**/###row_index");
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::Index);
    // Unstaging everything removes the Index row (the watcher notices the plain git step).
    s.git(repo, {"reset", "-q"});
    GG_CHECK(s.waitUntil([&] { return !s.itemExists("//History/**/###row_index"); }));
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
}

GG_TEST("history", "stash badges on base commits")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    const std::string base = s.revParse(repo, "stash@{0}^1");
    GG_CHECK(hasBadge(findRow(s, base), ggui::core::RefKind::Stash, "stash@{0}"));
    GG_CHECK(s.itemExists(("//History/**/" + base + "/###badge_stash@{0}").c_str()));
    ctx->ItemClick("//History/Stashes##hist_stashes");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(("//History/**/" + base + "/###badge_stash@{0}").c_str()));
    ctx->ItemClick("//History/Stashes##hist_stashes");
    ctx->Yield(2);
    GG_CHECK(s.itemExists(("//History/**/" + base + "/###badge_stash@{0}").c_str()));
}

GG_TEST("history", "scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    s.git(repo, {"branch", "alias", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const std::string topic = s.revParse(repo, "topic");
    const std::string mainTip = s.revParse(repo, "main");
    GG_CHECK(findRow(s, topic) != nullptr);
    // A click on the row itself changes nothing.
    ctx->ItemClick("//Branches/branch_topic/###branch_topic");
    ctx->Yield(3);
    GG_CHECK(s.session()->history().refVisible("refs/heads/topic"));
    // The eye icon hides the branch.
    ctx->ItemClick("//Branches/branch_topic/###eye");
    GG_CHECK(s.waitUntil([&] { return findRow(s, topic) == nullptr && !s.session()->history().loading(); }));
    GG_CHECK(findRow(s, mainTip) != nullptr);
    // Again shows it.
    ctx->ItemClick("//Branches/branch_topic/###eye");
    GG_CHECK(s.waitUntil([&] { return findRow(s, topic) != nullptr; }));
    // A hidden branch whose commit other refs keep in view loses its badge there.
    GG_CHECK(s.itemExists(("//History/**/" + mainTip + "/###badge_alias").c_str()));
    ctx->ItemClick("//Branches/branch_alias/###eye");
    GG_CHECK(s.waitUntil([&] { return !s.session()->history().loading() && findRow(s, mainTip) != nullptr; }));
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(("//History/**/" + mainTip + "/###badge_alias").c_str()));
    GG_CHECK(s.itemExists(("//History/**/" + mainTip + "/###badge_main").c_str()));
    ctx->ItemClick("//Branches/branch_alias/###eye");
    // Ctrl-click: only this branch.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick("//Branches/branch_feature/###eye");
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(s.waitUntil([&] { return findRow(s, mainTip) == nullptr && findRow(s, topic) == nullptr; }));
    GG_CHECK(findRow(s, s.revParse(repo, "feature")) != nullptr);
    ctx->ItemClick("//History/Show all refs##hist_all");
    GG_CHECK(s.waitUntil([&] { return findRow(s, mainTip) != nullptr && findRow(s, topic) != nullptr; }));
    // Hide all, then Show all, from the Branches panel.
    ctx->ItemClick("//Branches/###hide_all_branches");
    GG_CHECK(s.waitUntil([&] { return !s.session()->history().loading() && findRow(s, mainTip) == nullptr && findRow(s, topic) == nullptr; }));
    for (const char* b : {"main", "topic", "feature", "alias"})
        GG_CHECK(!s.session()->history().refVisible(std::string("refs/heads/") + b));
    ctx->ItemClick("//Branches/###show_all_branches");
    GG_CHECK(s.waitUntil([&] { return findRow(s, mainTip) != nullptr && findRow(s, topic) != nullptr; }));
    for (const char* b : {"main", "topic", "feature", "alias"})
        GG_CHECK(s.session()->history().refVisible(std::string("refs/heads/") + b));
}

GG_TEST("history", "row highlights span the whole row pitch: adjacent rows leave no gap")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.openRepository(repo));
    const std::string a = s.revParse(repo, "HEAD~1");
    const std::string b = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(a).c_str()) && s.itemExists(rowRef(b).c_str()); }));
    // Select both rows, so the selection highlight covers them.
    ctx->ItemClick(rowRef(a).c_str());
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(rowRef(b).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    ctx->Yield(3);
    const ImGuiTestItemInfo ia = ctx->ItemInfo(rowRef(a).c_str());
    const ImGuiTestItemInfo ib = ctx->ItemInfo(rowRef(b).c_str());
    GG_REQUIRE(ia.ID != 0 && ib.ID != 0);
    const float pitch = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    // The Selectable covers a full pitch (the row's cell padding included) and the next row starts
    // where this one ends.
    GG_CHECK(std::abs(ia.RectFull.GetHeight() - pitch) < 0.51f);
    GG_CHECK(std::abs(ib.RectFull.GetHeight() - pitch) < 0.51f);
    GG_CHECK(std::abs(ia.RectFull.Max.y - ib.RectFull.Min.y) < 0.51f);
}

GG_TEST("history", "search by message, ID, branch and tag; no graph while filtering")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    s.git(repo, {"tag", "v-special", "feature~1"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.expandMerge(s.revParse(repo, "main")));
    auto& history = s.session()->history();
    auto expectOnly = [&](const char* text, const std::vector<std::string>& ids) {
        ctx->ItemInputValue("//History/##hist_filter", text);
        const bool ok = s.waitUntil([&] {
            const auto v = history.visibleIds();
            if (v.size() != ids.size())
                return false;
            for (size_t i = 0; i < ids.size(); ++i)
                if (v[i].hex() != ids[i])
                    return false;
            return true;
        });
        if (!ok)
            ctx->LogError("filter '%s': %zu visible rows", text, history.visibleIds().size());
        GG_CHECK(ok);
    };
    expectOnly("Feature 2", {s.revParse(repo, "feature")});
    expectOnly(s.revParse(repo, "topic").substr(0, 10).c_str(), {s.revParse(repo, "topic")});
    expectOnly("v-special", {s.revParse(repo, "feature~1")});
    expectOnly("topic", {s.revParse(repo, "topic")});
    // While filtering, the graph column is hidden; rows still read and select as usual.
    ctx->Yield(2);
    GG_CHECK(!history.graphShown());
    ImGuiTable* filtered = ImGui::TableFindByID(ctx->GetID("//History/##hist_table_filtered"));
    GG_CHECK(filtered != nullptr && filtered->ColumnsCount == 3);
    GG_CHECK(s.itemText(rowRef(s.revParse(repo, "topic")).c_str()).rfind(findRow(s, s.revParse(repo, "topic"))->shortId + " Topic", 0) == 0);
    ctx->ItemClick(rowRef(s.revParse(repo, "topic")).c_str());
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.revParse(repo, "topic"));
    ctx->ItemInputValue("//History/##hist_filter", "");
    GG_CHECK(s.waitUntil([&] { return history.visibleIds().size() == history.rows().size(); }));
    ctx->Yield(2);
    GG_CHECK(history.graphShown());
    // "Conflicted only" is a filter too.
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
    ctx->Yield(2);
    GG_CHECK(!history.graphShown());
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
    ctx->Yield(2);
    GG_CHECK(history.graphShown());
}

GG_TEST("history", "merges start collapsed; expand and collapse merged history")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string f1 = s.revParse(repo, "feature~1");
    // Merged and deleted: its commits are only reachable through the merge. (A branch that still
    // points into the merged side keeps its commits shown.)
    s.git(repo, {"branch", "-D", "feature"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string merge = s.revParse(repo, "main");
    // Collapsed by default: the merged side (feature~1, feature) is hidden. The merge icon
    // toggles it.
    GG_REQUIRE(s.waitUntil([&] { return findRow(s, merge) != nullptr && !s.session()->history().loading(); }));
    GG_CHECK(findRow(s, merge)->collapsed);
    GG_CHECK(findRow(s, f1) == nullptr);
    GG_CHECK(s.waitUntil([&] { return findRow(s, merge)->collapsedCount == 2; }));
    ctx->ItemClick(("//History/**/" + merge + "/##merge_icon").c_str());
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) != nullptr && !findRow(s, merge)->collapsed; }));
    ctx->ItemClick(("//History/**/" + merge + "/##merge_icon").c_str());
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) == nullptr && findRow(s, merge) && findRow(s, merge)->collapsed; }));
    // Same through the context menu.
    s.contextMenu(rowRef(merge).c_str(), "Expand merged history");
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) != nullptr; }));
    s.contextMenu(rowRef(merge).c_str(), "Collapse merged history");
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) == nullptr; }));
}

GG_TEST("history", "a merge offers collapse only when collapsing hides commits")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string merge = s.revParse(repo, "main");
    const std::string f2 = s.revParse(repo, "feature");
    const std::string f1 = s.revParse(repo, "feature~1");
    // The merged branch still exists: its commits stay in view, so the merge has nothing to hide.
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return findRow(s, merge) != nullptr && !s.session()->history().loading(); }));
    GG_CHECK(!findRow(s, merge)->collapsed);
    GG_CHECK(!findRow(s, merge)->collapsible);
    GG_CHECK(findRow(s, f1) != nullptr && findRow(s, f2) != nullptr);
    // The icon still marks the merge, but toggles nothing; ordinary commits have none.
    GG_CHECK(s.itemExists(("//History/**/" + merge + "/##merge_icon").c_str()));
    GG_CHECK(!s.itemExists(("//History/**/" + f2 + "/##merge_icon").c_str()));
    ctx->ItemClick(("//History/**/" + merge + "/##merge_icon").c_str());
    ctx->Yield(2);
    GG_CHECK(!findRow(s, merge)->collapsed && findRow(s, f1) != nullptr);
    ctx->ItemClick(rowRef(merge).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(!s.itemExists("//$FOCUSED/Collapse merged history"));
    GG_CHECK(!s.itemExists("//$FOCUSED/Expand merged history"));
    ctx->KeyPress(ImGuiKey_Escape);
    s.app.closeRepository();

    // Deleted, but an older commit on top of its tip keeps it in view; the walk only finds that
    // after the merge. Once History is complete the merge (0 hidden) offers no toggle either.
    s.git(repo, {"branch", "-D", "feature"});
    const long long f2Time = std::stoll(s.gitOut(repo, {"log", "-1", "--format=%ct", f2}));
    const std::string when = std::to_string(f2Time + 1) + " +0000";
    const std::string object = "tree " + s.revParse(repo, f2 + "^{tree}") + "\nparent " + f2 + "\nauthor Test User <test@example.com> "
        + when + "\ncommitter Test User <test@example.com> " + when + "\n\nKeep\n";
    const std::string keep = gg::trim(s.git(repo, {"hash-object", "-w", "-t", "commit", "--stdin"}, object).out);
    s.git(repo, {"update-ref", "refs/heads/keep", keep});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return findRow(s, merge) != nullptr && !s.session()->history().loading(); }));
    GG_CHECK(findRow(s, f1) != nullptr && findRow(s, f2) != nullptr);
    GG_CHECK(findRow(s, merge)->collapsedCount == 0);
    GG_CHECK(s.itemExists(("//History/**/" + merge + "/##merge_icon").c_str()));
    ctx->ItemClick(rowRef(merge).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(!s.itemExists("//$FOCUSED/Expand merged history"));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("history", "expanding or collapsing a merge keeps the whole list in view while History reloads")
{
    const fs::path repo = s.largeFixture();
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 120.0f));
    std::string merge;
    for (const auto& row : history.rows())
        if (history.mergeToggle(row) && row.collapsed) {
            merge = row.id.hex();
            break;
        }
    GG_REQUIRE(!merge.empty());
    // Bring the merge into view (setup; the toggle below is clicked like a user).
    s.session()->revealCommit(ggui::core::Oid::fromHex(merge));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/" + merge + "/##merge_icon").c_str()); }));
    for (const char* step : {"expand", "collapse"}) {
        const size_t before = history.rows().size();
        GG_REQUIRE(before > 200); // more than the walk's first batch
        ctx->ItemClick(("//History/**/" + merge + "/##merge_icon").c_str());
        // Every frame until the reload ends shows at least as many rows as before (or the new list).
        size_t fewest = before;
        for (int frame = 0; frame < 2000 && (frame < 3 || history.loading()); ++frame) {
            fewest = std::min(fewest, history.rows().size());
            ctx->Yield();
        }
        const size_t after = history.rows().size();
        ctx->LogInfo("%s: %zu rows before, %zu after, fewest %zu", step, before, after, fewest);
        GG_CHECK(!history.loading());
        GG_CHECK(fewest >= std::min(before, after));
    }
}

GG_TEST("history", "keyboard navigation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto navOn = [&](const std::string& hex) { return g.NavId == ctx->ItemInfo(rowRef(hex).c_str()).ID; };
    ctx->ItemClick("//History/**/###row_wt");
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2); // SelectOnNav presses the row the frame after the cursor lands on it
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.head(repo));
    // The nav cursor and the selection are one thing.
    GG_CHECK(navOn(s.head(repo)));
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.revParse(repo, "HEAD~1"));
    GG_CHECK(navOn(s.revParse(repo, "HEAD~1")));
    ctx->KeyPress(ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.head(repo));
    GG_CHECK(navOn(s.head(repo)));
    ctx->KeyPress(ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    GG_CHECK(g.NavId == ctx->ItemInfo("//History/**/###row_wt").ID);
    ctx->KeyPress(ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
}

GG_TEST("history", "copy ID and full description; tooltip ID")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string id = s.revParse(repo, "HEAD~2");
    // The row shows the short ID: 3 characters in the text colour, the other 4 dimmed.
    GG_CHECK(s.idShownDimmed("//History", id.substr(0, 7), 3));
    // The menu has three items that state the ID itself; none depends on Shift.
    ctx->ItemClick(rowRef(id).c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Copy");
    GG_CHECK(s.itemLabel("//$FOCUSED/###ID3").find(id.substr(0, 3) + "###") != std::string::npos);
    GG_CHECK(s.itemLabel("//$FOCUSED/###ID7").find(id.substr(0, 7) + "###") != std::string::npos);
    GG_CHECK(s.itemLabel("//$FOCUSED/###IDfull").find("Full ID###") != std::string::npos);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    GG_CHECK(s.itemLabel("//$FOCUSED/###ID7").find(id.substr(0, 7) + "###") != std::string::npos);
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->ItemClick("//$FOCUSED/###ID3");
    GG_CHECK_STR_EQ(s.clipboard(), id.substr(0, 3));
    s.contextMenu(rowRef(id).c_str(), "Copy/###ID7");
    GG_CHECK_STR_EQ(s.clipboard(), id.substr(0, 7));
    s.contextMenu(rowRef(id).c_str(), "Copy/###IDfull");
    GG_CHECK_STR_EQ(s.clipboard(), id);
    // The row tooltip starts with the full ID: 7 characters in the text colour, the rest dimmed.
    ctx->MouseMove(rowRef(id).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.idShownDimmed("//##Tooltip_00", id, 7));
    s.contextMenu(rowRef(id).c_str(), "Copy/Full description");
    const std::string desc = s.clipboard();
    GG_CHECK(desc.rfind(id + " Add f3", 0) == 0);
    GG_CHECK(desc.find("Author: Test User <test@example.com>") != std::string::npos);
    // Right-click selected the row.
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), id);
}

namespace {

// A history taller than the panel: 150 commits on main after the Linear fixture.
fs::path tallRepo(Scenario& s)
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 150; ++i)
        s.git(repo, {"commit", "-q", "--allow-empty", "-m", "Main " + std::to_string(i)});
    return repo;
}

float rowTop(Scenario& s, const std::string& hex)
{
    const ImGuiTestItemInfo info = s.ctx->ItemInfo(rowRef(hex).c_str(), ImGuiTestOpFlags_NoError);
    return info.ID ? info.RectFull.Min.y : -1.0f;
}

} // namespace

GG_TEST("history", "keyboard only: nav into the list, select by arrows, Alt+Space menu, Ctrl+Space, Page/End")
{
    const fs::path repo = tallRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && history.rows().size() > 150; }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//History");
    // Down until the cursor is on the Working tree row (it starts on a header / widget above the list).
    const ImGuiID wt = ctx->ItemInfo("//History/**/###row_wt").ID;
    for (int i = 0; i < 40 && g.NavId != wt; ++i) {
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(2);
    }
    GG_REQUIRE(g.NavId == wt);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    // Two more rows down: HEAD, then HEAD~1; the selection follows the cursor.
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    const std::string second = s.revParse(repo, "HEAD~1");
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), second);
    GG_CHECK(g.NavId == ctx->ItemInfo(rowRef(second).c_str()).ID);
    // Alt+Space opens the cursor row's context menu.
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), second);
    // Ctrl+Down moves the cursor without selecting; Ctrl+Space then adds that row.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), second);
    GG_CHECK(history.extraSelection().empty());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Space);
    ctx->Yield(2);
    GG_REQUIRE(history.extraSelection().size() == 1);
    GG_CHECK_STR_EQ(history.extraSelection()[0].hex(), s.revParse(repo, "HEAD~2"));
    // Plain Space on the cursor row keeps the multi-selection.
    ctx->KeyPress(ImGuiKey_Space);
    ctx->Yield(2);
    GG_CHECK_EQ(history.extraSelection().size(), static_cast<size_t>(1));
    // A plain move collapses it to the cursor row.
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK(history.extraSelection().empty());
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.revParse(repo, "HEAD~3"));
    // End jumps the cursor (and the selection) to the last loaded row, past the clipper's range.
    ctx->KeyPress(ImGuiKey_End);
    ctx->Yield(5);
    GG_REQUIRE(s.session()->selection().kind == ggui::SelKind::Commit);
    const ggui::core::HistoryRow* last = nullptr;
    for (const auto& r : history.rows())
        last = &r;
    GG_REQUIRE(last != nullptr);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), last->id.hex());
    GG_CHECK(g.NavId == ctx->ItemInfo(rowRef(last->id.hex()).c_str()).ID);
}

GG_TEST("history", "keyboard: a programmatic selection moves the nav cursor, so the next arrow is relative to it")
{
    const fs::path repo = tallRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && history.rows().size() > 150; }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//History");
    const ImGuiID wt = ctx->ItemInfo("//History/**/###row_wt").ID;
    for (int i = 0; i < 40 && g.NavId != wt; ++i) {
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(2);
    }
    GG_REQUIRE(g.NavId == wt);
    // Reveal a commit far below (what Branches / a pending reveal / F7 do): selection and cursor go there.
    const std::string far = s.revParse(repo, "HEAD~100");
    s.session()->revealCommit(ggui::core::Oid::fromHex(far));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->selection().id.hex() == far && s.itemExists(rowRef(far).c_str()); }));
    ctx->Yield(3);
    GG_CHECK(g.NavId == ctx->ItemInfo(rowRef(far).c_str()).ID);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(3);
    const std::string next = s.revParse(repo, "HEAD~101");
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), next);
    GG_CHECK(g.NavId == ctx->ItemInfo(rowRef(next).c_str()).ID);
}

GG_TEST("history", "scroll position stays anchored on the rows in view")
{
    const fs::path repo = tallRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && history.rows().size() > 150; }));
    // Walk down to a row in the middle with the keyboard (the list scrolls along).
    const std::string viewed = s.revParse(repo, "HEAD~75");
    ctx->ItemClick(rowRef(s.head(repo)).c_str());
    for (int i = 0; i < 75; ++i)
        ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(3);
    GG_REQUIRE(s.session()->selection().id.hex() == viewed);
    const float before = rowTop(s, viewed);
    GG_REQUIRE(before > 0.0f);
    // New commits made with plain git arrive at the top (a refresh reloads History).
    for (int i = 0; i < 5; ++i)
        s.git(repo, {"commit", "-q", "--allow-empty", "-m", "Newer " + std::to_string(i)});
    GG_REQUIRE(s.waitUntil([&] { return history.row(ggui::core::Oid::fromHex(s.head(repo))) != nullptr && !history.loading(); }));
    ctx->Yield(5);
    GG_CHECK(std::abs(rowTop(s, viewed) - before) < 1.0f);
    // The Index row appears above the commits.
    s.write(repo, "staged.txt", "x\n");
    s.git(repo, {"add", "staged.txt"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->staged.empty(); }));
    ctx->Yield(5);
    GG_CHECK(std::abs(rowTop(s, viewed) - before) < 1.0f);
    // Scrolling to the selection still wins (keyboard navigation).
    for (int i = 0; i < 40; ++i)
        ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(3);
    const std::string below = s.revParse(repo, "HEAD~" + std::to_string(75 + 5 + 40));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), below);
    GG_CHECK(rowTop(s, below) > 0.0f);
}

GG_TEST("history", "tooltips wait until scrolling stops")
{
    const fs::path repo = tallRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && history.rows().size() > 150; }));
    const std::string row = s.revParse(repo, "HEAD~10");
    auto tipShown = [&] {
        ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
        return tip != nullptr && tip->Active;
    };
    ctx->MouseMove(rowRef(row).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(tipShown());
    // While the wheel scrolls the list, no tooltip (whatever row is under the mouse).
    ctx->MouseWheelY(-1.0f);
    ctx->Yield(2);
    GG_CHECK(history.scrolling());
    GG_CHECK(!tipShown());
    ctx->MouseWheelY(-1.0f);
    ctx->SleepNoSkip(0.15f, 0.05f);
    GG_CHECK(!tipShown());
    // Once it stops, tooltips come back.
    ctx->SleepNoSkip(1.2f, 0.1f);
    GG_CHECK(!history.scrolling());
    GG_CHECK(tipShown());
}

GG_TEST("history", "large history: first page, Show more, reveal, cancel")
{
    const fs::path repo = s.largeFixture();
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 60.0f));
    GG_CHECK(history.truncated());
    GG_CHECK_EQ(history.rows().size(), static_cast<size_t>(ggui::HistoryPanel::kPageSize));
    // The walk stopped after a page; the last row loads the next one.
    ctx->ScrollToBottom(s.child("//History", "##hist_table").c_str());
    ctx->ItemClick("//History/**/###hist_load_more");
    GG_CHECK(s.waitUntil([&] { return history.rows().size() >= 2u * ggui::HistoryPanel::kPageSize; }, 60.0f));

    // Reveal a commit far beyond what is loaded: tag t/0 points near the root.
    const std::string deep = s.revParse(repo, "t/0^{commit}");
    GG_REQUIRE(history.row(Oid::fromHex(deep)) == nullptr);
    s.showPanel("Tags");
    ctx->ItemInputValue("//Tags/##tag_filter", "t/0");
    ctx->Yield(2);
    s.contextMenu("//Tags/tag_t:0/###tag_t:0", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == deep; }, 120.0f));
    GG_CHECK(history.row(Oid::fromHex(deep)) != nullptr);
}

GG_TEST("history", "cancel a long history load and a reveal")
{
    const fs::path repo = s.largeFixture();
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 60.0f));
    // Reveal a deep commit and cancel from the toolbar right away.
    const std::string root = s.revParse(repo, "t/0^{commit}");
    s.showPanel("Tags");
    ctx->ItemInputValue("//Tags/##tag_filter", "t/0");
    ctx->Yield(2);
    s.contextMenu("//Tags/tag_t:0/###tag_t:0", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//###Toolbar/Cancel##tb_cancel"); }, 10.0f));
    ctx->ItemClick("//###Toolbar/Cancel##tb_cancel");
    // Other reads (the working tree status of the large fixture) finish on their own; on a slow
    // runner that alone can take 30 s.
    GG_CHECK(s.waitUntil([&] { return s.session()->activities().empty(); }, static_cast<float>(timeBudgetMs(60000) / 1000)));
    GG_CHECK(s.session()->selection().id.hex() != root);
    GG_CHECK(history.rows().size() < 100000);
}

GG_TEST("history", "first rows of a large history appear quickly")
{
    // GGUI_PERF_REPO measures another repository as well (a real one, opened read-only).
    std::vector<fs::path> repos{s.largeFixture()};
    if (const char* extra = std::getenv("GGUI_PERF_REPO"))
        repos.emplace_back(extra);
    for (const fs::path& repo : repos) {
        // Timed from the Open click on Welcome, not from when the harness sees an idle session.
        ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
        const auto start = std::chrono::steady_clock::now();
        ctx->ItemClick("//Welcome/Open");
        GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->history().rows().size() >= 50; }, 60.0f));
        auto& history = s.session()->history();
        const auto first = std::chrono::steady_clock::now();
        GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 120.0f));
        const auto done = std::chrono::steady_clock::now();
        const auto ms = [&](auto t) { return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t - start).count()); };
        ctx->LogInfo("%s: first rows %lld ms, page complete %lld ms, %zu rows", repo.string().c_str(), ms(first), ms(done),
            history.rows().size());
        spdlog::info("history timing {}: first rows {} ms, page {} ms, {} rows", repo.string(), ms(first), ms(done),
            history.rows().size());
        GG_CHECK(ms(first) < timeBudgetMs(700));
        ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
        ctx->Yield(3);
    }
}

} // namespace ggtest
