// History panel (§4.2).
#include "panels/CommitMenu.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Widgets.hpp"
#include "tests/Harness.hpp"
#include "util/Ui.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

GG_TEST("history", "the copy-ID menu item by part clicked and Shift: all five rows")
{
    const std::string hex = "0123456789abcdef0123456789abcdef01234567";
    const auto check = [&](bool longId, bool onPrefix, bool shift, const char* label, const std::string& text) {
        const ggui::IdCopyChoice choice = ggui::idCopyChoice(hex, longId, onPrefix, shift);
        GG_CHECK_STR_EQ(choice.label, label);
        GG_CHECK_STR_EQ(choice.text, text);
    };
    check(true, true, false, "Copy 0123456", hex.substr(0, 7));
    check(true, false, false, "Copy full ID", hex);
    check(false, false, true, "Copy full ID", hex);
    check(false, true, false, "Copy 012", hex.substr(0, 3));
    check(false, false, false, "Copy 0123456", hex.substr(0, 7));
    // A long ID: Shift changes nothing.
    check(true, true, true, "Copy 0123456", hex.substr(0, 7));
    check(true, false, true, "Copy full ID", hex);
    // A short ID with Shift: the full ID, on the prefix too.
    check(false, true, true, "Copy full ID", hex);
    // A menu opened from the keyboard: the 7 characters, with Shift the full ID.
    GG_CHECK_STR_EQ(ggui::idCopyChoiceKeyboard(hex, false).label, "Copy 0123456");
    GG_CHECK_STR_EQ(ggui::idCopyChoiceKeyboard(hex, false).text, hex.substr(0, 7));
    GG_CHECK_STR_EQ(ggui::idCopyChoiceKeyboard(hex, true).label, "Copy full ID");
    GG_CHECK_STR_EQ(ggui::idCopyChoiceKeyboard(hex, true).text, hex);
}

GG_TEST("history", "copy ID and full description; tooltip ID")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string id = s.revParse(repo, "HEAD~2");
    // The row shows the short ID: 3 characters in the text colour, the other 4 dimmed.
    GG_CHECK(s.idShownDimmed("//History", id.substr(0, 7), 3));
    // The Copy submenu has one item that states the ID it copies: a click on the row outside the ID is the tail
    // (7 characters), Shift held at the click gives the full ID.
    ctx->ItemClick(rowRef(id).c_str(), ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Copy");
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy " + id.substr(0, 7) + "###copy_id");
    GG_CHECK(s.itemLabel("//$FOCUSED/###ID3").empty() && s.itemLabel("//$FOCUSED/###IDfull").empty());
    ctx->ItemClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), id.substr(0, 7));
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->Yield(2);
    ctx->ItemClick(rowRef(id).c_str(), ImGuiMouseButton_Right);
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Copy");
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy full ID###copy_id");
    ctx->ItemClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), id);
    // A right click on the ID's highlighted prefix: the 3 characters.
    const ImGuiTestItemInfo row = ctx->ItemInfo(rowRef(id).c_str());
    const ImVec2 range = s.session()->history().idPrefixRange();
    GG_REQUIRE(range.y > range.x);
    // The range spans the first 3 characters of the ID as drawn.
    GG_CHECK(std::abs((range.y - range.x) - ImGui::CalcTextSize(id.c_str(), id.c_str() + 3).x) < 0.5f);
    ctx->MouseMoveToPos(ImVec2((range.x + range.y) * 0.5f, row.RectFull.GetCenter().y));
    ctx->MouseClick(ImGuiMouseButton_Right);
    ctx->MenuAction(ImGuiTestAction_Hover, "//$FOCUSED/Copy");
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy " + id.substr(0, 3) + "###copy_id");
    ctx->ItemClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), id.substr(0, 3));
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

namespace {

// Clicks a row with a modifier held (0: a plain click).
void clickRow(Scenario& s, const std::string& hex, ImGuiKeyChord mod = 0)
{
    ImGuiTestContext* ctx = s.ctx;
    if (mod)
        ctx->KeyDown(mod);
    ctx->ItemClick(rowRef(hex).c_str());
    if (mod)
        ctx->KeyUp(mod);
    ctx->Yield(2);
}

// The selected commits (the primary one and the extra ones) as a set of hex IDs.
std::set<std::string> selectedCommits(Scenario& s)
{
    std::set<std::string> out;
    if (s.session()->selection().kind == ggui::SelKind::Commit)
        out.insert(s.session()->selection().id.hex());
    for (const auto& id : s.session()->history().extraSelection())
        out.insert(id.hex());
    return out;
}

} // namespace

GG_TEST("history", "shift-click selects the range from the anchor, downward and upward, the clicked row is primary")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    std::string c[5];
    for (int i = 0; i < 5; ++i)
        c[i] = s.revParse(repo, "HEAD~" + std::to_string(i));
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c[4]).c_str()); }));
    auto& history = s.session()->history();
    // Downward: c1 (plain) to c3 (Shift).
    clickRow(s, c[1]);
    clickRow(s, c[3], ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[3]);
    GG_CHECK_EQ(history.extraSelection().size(), static_cast<size_t>(2));
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[1], c[2], c[3]}));
    // The anchor stays: Shift-click upward replaces the range with c1 to c0.
    clickRow(s, c[0], ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[0]);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[0], c[1]}));
    GG_CHECK_EQ(history.extraSelection().size(), static_cast<size_t>(1));
    // Upward from a new anchor.
    clickRow(s, c[3]);
    clickRow(s, c[1], ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[1]);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[1], c[2], c[3]}));
    // Adjacent commits of a linear history are a range (the rebase item needs that).
    GG_CHECK(ggui::selectionShape(*s.session()).range());
    GG_CHECK_EQ(ggui::selectionShape(*s.session()).count(), static_cast<size_t>(3));
    // A plain click after a range clears it.
    clickRow(s, c[4]);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[4]);
    GG_CHECK(history.extraSelection().empty());
    // Shift-click on the anchor row selects just that row.
    clickRow(s, c[4], ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[4]}));
}

GG_TEST("history", "shift-click after a ctrl-click takes the ctrl-clicked row as the anchor")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    std::string c[5];
    for (int i = 0; i < 5; ++i)
        c[i] = s.revParse(repo, "HEAD~" + std::to_string(i));
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c[4]).c_str()); }));
    clickRow(s, c[0]);
    clickRow(s, c[2], ImGuiMod_Ctrl);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[0], c[2]}));
    // The range runs from c2, not from c0; it replaces the Ctrl-clicked set.
    clickRow(s, c[4], ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[2], c[3], c[4]}));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[4]);
}

GG_TEST("history", "ctrl-click selects the commit when nothing or the Working tree row is selected")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string c0 = s.head(repo);
    const std::string c1 = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c1).c_str()); }));
    // Nothing selected: the Ctrl-clicked commit becomes the selection.
    s.session()->select(ggui::Selection{});
    ctx->Yield(2);
    GG_REQUIRE(s.session()->selection().kind == ggui::SelKind::None);
    clickRow(s, c0, ImGuiMod_Ctrl);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::Commit);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c0}));
    ctx->ItemClick("//History/**/###row_wt");
    ctx->Yield(2);
    GG_REQUIRE(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    clickRow(s, c1, ImGuiMod_Ctrl);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::Commit);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c1);
    GG_CHECK(s.session()->history().extraSelection().empty());
    // With a primary commit, Ctrl-click adds as before.
    clickRow(s, c0, ImGuiMod_Ctrl);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c0, c1}));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c1);
    // Shift-click with the Working tree row selected acts as a plain click.
    ctx->ItemClick("//History/**/###row_wt");
    ctx->Yield(2);
    clickRow(s, c1, ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c1}));
}

GG_TEST("history", "shift+arrow extends the range from the anchor, a plain arrow collapses it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    std::string c[5];
    for (int i = 0; i < 5; ++i)
        c[i] = s.revParse(repo, "HEAD~" + std::to_string(i));
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c[4]).c_str()); }));
    clickRow(s, c[1]);
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_DownArrow);
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[3]);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[1], c[2], c[3]}));
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[1], c[2]}));
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[3]}));
    // A plain arrow sets the anchor; Shift+End extends the range to the last row.
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_End);
    ctx->Yield(3);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[3], c[4]}));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[4]);
}

GG_TEST("history", "shift+arrow onto the Working tree row keeps the range; a shift-click on it selects it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string c0 = s.head(repo);
    const std::string c1 = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c1).c_str()); }));
    clickRow(s, c1);
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c0, c1}));
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::Commit);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c0, c1}));
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    GG_CHECK(s.session()->history().extraSelection().empty());
}

GG_TEST("history", "shift-click without a usable anchor or primary commit")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    std::string c[5];
    for (int i = 0; i < 5; ++i)
        c[i] = s.revParse(repo, "HEAD~" + std::to_string(i));
    // Two branches: a filter on their name leaves c1 and c4 in the list.
    s.git(repo, {"branch", "pick-a", c[1]});
    s.git(repo, {"branch", "pick-b", c[4]});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(c[4]).c_str()); }));
    auto& history = s.session()->history();
    // The anchor is selected but a filter hides it: the primary commit is the anchor.
    clickRow(s, c[1]);
    clickRow(s, c[3], ImGuiMod_Ctrl);
    ctx->ItemInputValue("//History/##hist_filter", "pick-");
    GG_REQUIRE(s.waitUntil([&] { return history.visibleIds().size() == 2; }));
    ctx->Yield(2);
    clickRow(s, c[4], ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[1], c[4]}));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), c[4]);
    ctx->ItemInputValue("//History/##hist_filter", "");
    GG_REQUIRE(s.waitUntil([&] { return history.visibleIds().size() == history.rows().size(); }));
    ctx->Yield(2);
    // A Ctrl-click that removes the anchor row: the anchor is the primary commit.
    clickRow(s, c[0]);
    clickRow(s, c[2], ImGuiMod_Ctrl);
    clickRow(s, c[2], ImGuiMod_Ctrl);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[0]}));
    clickRow(s, c[3], ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[0], c[1], c[2], c[3]}));
    // The selection is not a commit any more (a stash from a side panel): Shift-click is a plain click.
    clickRow(s, c[0]);
    clickRow(s, c[2], ImGuiMod_Ctrl);
    s.session()->select(ggui::Selection{});
    ctx->Yield(2);
    clickRow(s, c[4], ImGuiMod_Shift);
    GG_CHECK((selectedCommits(s) == std::set<std::string>{c[4]}));
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

GG_TEST("history", "tooltip text is built once while the tooltip stays shown")
{
    const fs::path repo = tallRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && history.rows().size() > 20; }));
    // Two adjacent rows: the mouse does not pass over another row on its way from one to the other.
    const std::string first = s.revParse(repo, "HEAD~3");
    const std::string second = s.revParse(repo, "HEAD~4");
    auto tipShown = [&] {
        ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
        return tip != nullptr && tip->Active;
    };
    const int start = ggui::tooltipTextBuilds();
    ctx->MouseMove(rowRef(first).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(tipShown());
    GG_CHECK(ggui::tooltipTextBuilds() > start);
    // Shown for many more frames: its text was built when it appeared, and not again.
    const int built = ggui::tooltipTextBuilds();
    ctx->Yield(10);
    GG_CHECK(tipShown());
    GG_CHECK_EQ(ggui::tooltipTextBuilds(), built);
    // The next row's tooltip is another text: built once more, then kept.
    ctx->MouseMove(rowRef(second).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(tipShown());
    GG_CHECK_EQ(ggui::tooltipTextBuilds(), built + 1);
    ctx->Yield(10);
    GG_CHECK(tipShown());
    GG_CHECK_EQ(ggui::tooltipTextBuilds(), built + 1);
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

GG_TEST("history", "elideMiddle cuts on codepoint boundaries and leaves short names alone")
{
    using ggui::elideMiddle;
    const std::string dots = "\xE2\x80\xA6";
    // Exactly prefix + suffix + 1 characters stay; one more is elided.
    GG_CHECK_STR_EQ(elideMiddle("abcdefgh", 4, 3), "abcdefgh");
    GG_CHECK_STR_EQ(elideMiddle("abcdefghi", 4, 3), "abcd" + dots + "ghi");
    // Multi-byte names count and cut in codepoints.
    const std::string lt = "\xC4\x85\xC4\x8D\xC4\x99\xC4\x97\xC4\xAF\xC5\xA1\xC5\xB3\xC5\xAB\xC5\xBE"; // ąčęėįšųūž
    const std::string name = lt + "-" + lt + "-" + lt;
    const std::string got = elideMiddle(name, 3, 2);
    GG_CHECK_STR_EQ(got, "\xC4\x85\xC4\x8D\xC4\x99" + dots + "\xC5\xAB\xC5\xBE");
    GG_CHECK_STR_EQ(elideMiddle(lt + "-", 8, 1), lt + "-"); // 10 characters, limit 10
    GG_CHECK_STR_EQ(elideMiddle(lt + "-", 7, 1), lt.substr(0, 14) + dots + "-");
    // Lengths below 1 act as 1.
    GG_CHECK_STR_EQ(elideMiddle("abcd", 0, 0), "a" + dots + "d");
    GG_CHECK_STR_EQ(elideMiddle("abc", -5, 0), "abc");
    GG_CHECK_STR_EQ(elideMiddle("", 1, 1), "");
}

GG_TEST("history", "a long branch or tag name is elided in its badge, the ID keeps the full name, and the settings set the lengths")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string longName = "a-very-long-branch-name-for-elision";
    const std::string longTag = "a-very-long-tag-name-for-the-elision";
    s.git(repo, {"branch", longName, "main"});
    s.git(repo, {"tag", longTag, "main"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "main");
    const std::string dots = "\xE2\x80\xA6";
    GG_REQUIRE(s.waitUntil([&] { return findRow(s, tip) != nullptr && !s.session()->history().loading(); }));
    ctx->Yield(2);
    const std::string longRef = "//History/**/" + tip + "/###badge_" + longName;
    const std::string shortRef = "//History/**/" + tip + "/###badge_main";
    GG_REQUIRE(s.itemExists(longRef.c_str()));
    GG_REQUIRE(s.itemExists(shortRef.c_str()));
    // The label is the icon, a space, the visible text and the ID part (the test engine keeps its first 31 bytes).
    auto visible = [&](const std::string& ref) {
        const std::string label = s.itemLabel(ref.c_str());
        return label.substr(0, label.find("###"));
    };
    auto endsWith = [](const std::string& a, const std::string& b) { return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0; };
    GG_CHECK(endsWith(visible(longRef), " " + longName.substr(0, 12) + dots + longName.substr(longName.size() - 12)));
    GG_CHECK(endsWith(visible(shortRef), " main"));
    // A tag's badge follows the same rule.
    const std::string tagRef = "//History/**/" + tip + "/###badge_" + longTag;
    GG_REQUIRE(s.itemExists(tagRef.c_str()));
    GG_CHECK(endsWith(visible(tagRef), " " + longTag.substr(0, 12) + dots + longTag.substr(longTag.size() - 12)));
    // The Settings fields change the lengths.
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge prefix##badge_prefix", 4);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge suffix##badge_suffix", 3);
    ctx->Yield(3);
    GG_CHECK_EQ(s.app.settings().data().historyBadgePrefix, 4);
    GG_CHECK_EQ(s.app.settings().data().historyBadgeSuffix, 3);
    GG_CHECK(endsWith(visible(longRef), " " + longName.substr(0, 4) + dots + longName.substr(longName.size() - 3)));
    GG_CHECK(endsWith(visible(tagRef), " " + longTag.substr(0, 4) + dots + longTag.substr(longTag.size() - 3)));
    GG_CHECK(endsWith(visible(shortRef), " main")); // 4 characters: 4 + 3 + 1 would still hold it
    // Out-of-range values are clamped.
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge prefix##badge_prefix", 0);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge suffix##badge_suffix", 500);
    ctx->Yield(3);
    GG_CHECK_EQ(s.app.settings().data().historyBadgePrefix, 1);
    GG_CHECK_EQ(s.app.settings().data().historyBadgeSuffix, 100);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge prefix##badge_prefix", 4);
    ctx->ItemInputValue("//Settings/##settings_tabs/General/History badge suffix##badge_suffix", 3);
    ctx->Yield(3);
    // They are read back by a restart (settings.json).
    GG_REQUIRE(s.waitIdle());
    s.app.resetForTest();
    ctx->Yield(2);
    GG_CHECK_EQ(s.app.settings().data().historyBadgePrefix, 4);
    GG_CHECK_EQ(s.app.settings().data().historyBadgeSuffix, 3);
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

namespace {

// A badge by its row and name. The ID is computed (a '/' in a name does not go through a wildcard reference).
ImGuiTestItemInfo badgeItem(Scenario& s, const std::string& hex, const std::string& name)
{
    const ImGuiTestItemInfo row = s.ctx->ItemInfo(rowRef(hex).c_str(), ImGuiTestOpFlags_NoError);
    if (row.ID == 0)
        return {};
    return s.ctx->ItemInfo(ImGuiTestRef(ImHashStr(("###badge_" + name).c_str(), 0, row.ParentID)), ImGuiTestOpFlags_NoError);
}

bool badgeShown(Scenario& s, const std::string& hex, const std::string& name) { return badgeItem(s, hex, name).ID != 0; }

// A badge takes no hover: the mouse goes to its centre and the row below it gets the click.
void clickBadge(Scenario& s, const std::string& hex, const std::string& name, ImGuiMouseButton button)
{
    s.ctx->MouseMoveToPos(badgeItem(s, hex, name).RectFull.GetCenter());
    s.ctx->MouseClick(button);
}

// A double click on a badge (the double click on a row is ctx->ItemDoubleClick).
void doubleClickBadge(Scenario& s, const std::string& hex, const std::string& name)
{
    s.ctx->MouseMoveToPos(badgeItem(s, hex, name).RectFull.GetCenter());
    s.ctx->MouseDoubleClick(ImGuiMouseButton_Left);
}

// Lets the double click time pass, so that the next click is not a part of the last one.
void waitOutDoubleClick(Scenario& s) { s.ctx->SleepNoSkip(2.0f * ImGui::GetIO().MouseDoubleClickTime, 0.1f); }

bool onBranch(Scenario& s, const std::string& name)
{
    const auto snap = s.session()->snapshot();
    return !snap->headDetached && snap->headBranch == name;
}

bool detachedAt(Scenario& s, const std::string& hex)
{
    const auto snap = s.session()->snapshot();
    return snap->headDetached && snap->head.hex() == hex;
}

} // namespace

GG_TEST("history", "a right click on a local branch badge shows the branch menu and selects the row")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "side");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "side"); }));
    GG_CHECK(s.session()->selection().id.hex() != tip);
    clickBadge(s, tip, "side", ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Rename..."); }));
    // The items of the branch menu, not the commit menu.
    GG_CHECK(s.itemExists("//$FOCUSED/Merge into HEAD..."));
    GG_CHECK(!s.itemExists("//$FOCUSED/Create branch..."));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), tip);
    ctx->KeyPress(ImGuiKey_Escape);
    // A left click on the badge selects its row.
    ctx->ItemClick(rowRef(s.revParse(repo, "HEAD")).c_str());
    GG_CHECK(s.session()->selection().id.hex() != tip);
    clickBadge(s, tip, "side", ImGuiMouseButton_Left);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), tip);
}

GG_TEST("history", "an item of the branch menu works from the badge")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "side");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "side"); }));
    clickBadge(s, tip, "side", ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/Rename...");
    GG_REQUIRE(s.dialogOpen("Rename branch"));
    s.dialogText("Rename branch", "name", "renamed");
    s.dialogButton("Rename branch", "Rename");
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->findBranch("renamed") != nullptr; }));
    GG_CHECK(s.session()->snapshot()->findBranch("side") == nullptr);
    GG_CHECK_STR_EQ(s.revParse(repo, "renamed"), tip);
}

GG_TEST("history", "a right click on a remote branch badge and on a tag badge shows their menus")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"tag", "v1.0", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string remoteTip = s.revParse(repo, "origin/main");
    const std::string tagTip = s.revParse(repo, "v1.0");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tagTip, "v1.0"); }));
    GG_CHECK(hasBadge(findRow(s, remoteTip), ggui::core::RefKind::RemoteBranch, "origin/main"));
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, remoteTip, "origin/main"); }));
    clickBadge(s, remoteTip, "origin/main", ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Delete on remote..."); }));
    GG_CHECK(s.itemExists("//$FOCUSED/Create local branch..."));
    GG_CHECK(!s.itemExists("//$FOCUSED/Create branch..."));
    ctx->KeyPress(ImGuiKey_Escape);
    clickBadge(s, tagTip, "v1.0", ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Push tag"); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Create branch..."));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), tagTip);
    ctx->KeyPress(ImGuiKey_Escape);
    // The Copy name item of the tag menu.
    clickBadge(s, tagTip, "v1.0", ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "v1.0");
}

GG_TEST("history", "a right click on the row outside its badges shows the commit menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "side");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "side"); }));
    const ImGuiTestItemInfo row = ctx->ItemInfo(rowRef(tip).c_str());
    ctx->MouseMoveToPos(ImVec2(row.RectFull.Max.x - 4.0f, row.RectFull.GetCenter().y));
    ctx->MouseClick(ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Create branch..."); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Rename..."));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("history", "a right click on the Author cell of a row with more badges than fit shows the commit menu")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // Twelve badges of 25 characters each are wider than the Description column: the ones that do not fit are
    // clipped and lie under the Author and Date cells.
    for (int i = 0; i < 12; ++i)
        s.git(repo, {"branch", "a-very-long-branch-name-for-clipping-" + std::to_string(i), "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "a-very-long-branch-name-for-clipping-11"); }));
    const ImGuiTestItemInfo row = ctx->ItemInfo(rowRef(tip).c_str());
    // Where the Author cell is: the Date column is 8 and the Author column 9 font sizes wide.
    const float fontSize = ImGui::GetFontSize();
    const float x = row.RectFull.Max.x - fontSize * 8 - fontSize * 4.5f;
    // The clicked place is under a badge that is not visible.
    bool covered = false;
    for (int i = 0; i < 12; ++i) {
        const ImRect r = badgeItem(s, tip, "a-very-long-branch-name-for-clipping-" + std::to_string(i)).RectFull;
        covered = covered || (r.Min.x <= x && x <= r.Max.x);
    }
    GG_CHECK(covered);
    ctx->MouseMoveToPos(ImVec2(x, row.RectFull.GetCenter().y));
    ctx->MouseClick(ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Create branch..."); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Rename..."));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("history", "the badge menu closes when its ref goes away")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "HEAD");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "side"); }));
    clickBadge(s, tip, "side", ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Rename..."); }));
    // The branch is renamed outside ggui: its badge and the menu go.
    s.git(repo, {"branch", "-m", "side", "gone"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->findBranch("gone") != nullptr; }));
    GG_CHECK(s.waitUntil([&] { return !s.itemExists("//$FOCUSED/Rename..."); }));
    GG_CHECK(s.itemExists(rowRef(tip).c_str()));
}

GG_TEST("history", "a right click on a worktree badge shows the worktree menu")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.openRepository(repo));
    const auto snap = s.session()->snapshot();
    const ggui::core::WorktreeInfo* linked = nullptr;
    for (const auto& w : snap->worktrees)
        if (!w.isCurrent && !w.bare && !w.head.isNull() && !linked)
            linked = &w;
    GG_REQUIRE(linked != nullptr);
    const std::string head = linked->head.hex();
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, head, linked->name); }));
    clickBadge(s, head, linked->name, ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Open in new window"); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Create branch..."));
    ctx->MenuClick("//$FOCUSED/Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), linked->name);
}

GG_TEST("history", "a right click on a stash badge shows the stash menu")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    const std::string base = s.revParse(repo, "stash@{0}^1");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, base, "stash@{0}"); }));
    clickBadge(s, base, "stash@{0}", ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Branch from stash..."); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Create branch..."));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("history", "a double click on a row with one branch checks it out")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "side");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); }));
    ctx->ItemDoubleClick(rowRef(tip).c_str());
    GG_CHECK(s.waitUntil([&] { return onBranch(s, "side"); }));
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), tip);
}

GG_TEST("history", "a double click on a row with two branches asks which")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "one", "HEAD~2"});
    s.git(repo, {"branch", "two", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "one");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); }));
    ctx->ItemDoubleClick(rowRef(tip).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Detached"); }));
    GG_CHECK(onBranch(s, "main"));
    ctx->MenuClick("//$FOCUSED/two");
    GG_CHECK(s.waitUntil([&] { return onBranch(s, "two"); }));
}

GG_TEST("history", "the Detached item of the branch choice asks, then detaches")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "one", "HEAD~2"});
    s.git(repo, {"branch", "two", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "one");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); }));
    ctx->ItemDoubleClick(rowRef(tip).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Detached"); }));
    ctx->MenuClick("//$FOCUSED/Detached");
    GG_REQUIRE(s.dialogOpen("Checkout detached"));
    GG_CHECK(onBranch(s, "main"));
    s.dialogButton("Checkout detached", "Checkout");
    GG_CHECK(s.waitUntil([&] { return detachedAt(s, tip); }));
}

GG_TEST("history", "a double click on a row without a branch asks, then detaches")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string commit = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(commit).c_str()); }));
    ctx->ItemDoubleClick(rowRef(commit).c_str());
    GG_REQUIRE(s.dialogOpen("Checkout detached"));
    s.dialogButton("Checkout detached", "Cancel");
    GG_CHECK(s.waitIdle());
    GG_CHECK(onBranch(s, "main"));
    waitOutDoubleClick(s);
    ctx->ItemDoubleClick(rowRef(commit).c_str());
    GG_REQUIRE(s.dialogOpen("Checkout detached"));
    s.dialogButton("Checkout detached", "Checkout");
    GG_CHECK(s.waitUntil([&] { return detachedAt(s, commit); }));
}

GG_TEST("history", "a double click on the row of the current branch does nothing")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // A second branch on the commit: without the guard the chooser would open.
    s.git(repo, {"branch", "other", "HEAD"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "HEAD");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); }));
    ctx->ItemDoubleClick(rowRef(tip).c_str());
    GG_CHECK(s.waitIdle());
    ctx->Yield(3);
    GG_CHECK(!s.dialogOpen("Checkout detached", 1.0f));
    GG_CHECK(!s.itemExists("//$FOCUSED/Detached"));
    GG_CHECK(onBranch(s, "main"));
}

GG_TEST("history", "a double click with Ctrl down checks nothing out")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "side");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); }));
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemDoubleClick(rowRef(tip).c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(s.waitIdle());
    ctx->Yield(3);
    GG_CHECK(onBranch(s, "main"));
}

GG_TEST("history", "a double click that starts on a menu does not check out")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "HEAD");
    const std::string below = s.revParse(repo, "HEAD~3");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(below).c_str()); }));
    // The first click closes the row menu (the menu is right of the pointer, the row is not under it); the
    // second one is on a row that got no first click.
    ctx->ItemClick(rowRef(tip).c_str(), ImGuiMouseButton_Right);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Copy"); }));
    const ImRect rect = ctx->ItemInfo(rowRef(below).c_str()).RectFull;
    ctx->MouseMoveToPos(ImVec2(rect.Min.x + 30.0f, rect.GetCenter().y));
    ctx->MouseDoubleClick(ImGuiMouseButton_Left);
    GG_CHECK(s.waitIdle());
    ctx->Yield(3);
    GG_CHECK(!s.dialogOpen("Checkout detached", 1.0f));
    GG_CHECK(onBranch(s, "main"));
}

GG_TEST("history", "a double click on a local branch badge checks it out")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "one", "HEAD~2"});
    s.git(repo, {"branch", "two", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "one");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "one") && badgeShown(s, tip, "two"); }));
    doubleClickBadge(s, tip, "two");
    GG_CHECK(s.waitUntil([&] { return onBranch(s, "two"); }));
    GG_CHECK(!s.itemExists("//$FOCUSED/Detached"));
}

GG_TEST("history", "a double click on a remote branch badge makes or checks out the local branch")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const std::string first = s.revParse(repo, "origin/main~1");
    const std::string second = s.revParse(repo, "origin/main~2");
    s.git(repo, {"update-ref", "refs/remotes/origin/topic", first});
    s.git(repo, {"update-ref", "refs/remotes/origin/other", second});
    s.git(repo, {"branch", "other", second});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, first, "origin/topic") && badgeShown(s, second, "origin/other"); }));
    // With a local branch of the short name: it is checked out.
    doubleClickBadge(s, second, "origin/other");
    GG_CHECK(s.waitUntil([&] { return onBranch(s, "other"); }));
    waitOutDoubleClick(s);
    // Without: the Create branch dialog opens with the short name as the name.
    doubleClickBadge(s, first, "origin/topic");
    GG_REQUIRE(s.dialogOpen("Create branch"));
    s.dialogButton("Create branch", "Create");
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->findBranch("topic") != nullptr; }));
    GG_CHECK_STR_EQ(s.revParse(repo, "topic"), first);
}

GG_TEST("history", "a double click on a tag badge asks, then detaches")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"tag", "v1", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string tip = s.revParse(repo, "v1");
    GG_REQUIRE(s.waitUntil([&] { return badgeShown(s, tip, "v1"); }));
    doubleClickBadge(s, tip, "v1");
    GG_REQUIRE(s.dialogOpen("Checkout detached"));
    GG_CHECK(onBranch(s, "main"));
    s.dialogButton("Checkout detached", "Checkout");
    GG_CHECK(s.waitUntil([&] { return detachedAt(s, tip); }));
}

GG_TEST("history", "the Check out menu has Detached and detaches without a dialog")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string commit = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(commit).c_str()); }));
    s.contextMenu(rowRef(commit).c_str(), "Check out/Detached");
    GG_CHECK(s.waitUntil([&] { return detachedAt(s, commit); }));
    GG_CHECK(!s.dialogOpen("Checkout detached", 0.5f));
}

GG_TEST("history", "the commit menu uses git words")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string commit = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(commit).c_str()); }));
    ctx->ItemClick(rowRef(commit).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//$FOCUSED/Drop commit..."));
    GG_CHECK(s.itemExists("//$FOCUSED/Edit commit (checkout detached)"));
    GG_CHECK(!s.itemExists("//$FOCUSED/Abandon..."));
    // Every item exists and each one is below the one before.
    const char* labels[] = {"New detached", "Check out", "Create branch...", "Create tag...", "Move branch",
        "Merge into HEAD...", "Rebase onto...", "Interactive rebase...", "Reset main to here...###reset_here",
        "Interactive rebase selection...", "Cherry-pick", "Revert", "Edit commit (checkout detached)", "Duplicate",
        "Squash...", "Split...", "Simplify parents", "Drop commit...", "Copy"};
    float last = -1.0f;
    for (const char* label : labels) {
        const ImGuiTestItemInfo info = ctx->ItemInfo((std::string("//$FOCUSED/") + label).c_str(), ImGuiTestOpFlags_NoError);
        GG_CHECK(info.ID != 0);
        GG_CHECK(std::string(info.DebugLabel).find(std::string(label).substr(0, std::string(label).find("###"))) == 0);
        GG_CHECK(info.RectFull.Min.y > last);
        last = info.RectFull.Min.y;
    }
    ctx->KeyPress(ImGuiKey_Escape);
}

} // namespace ggtest
