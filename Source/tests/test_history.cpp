// History panel (§4.2; P1-07, P1-15).
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

GG_TEST("history", "graph, rows, badges and short IDs", "HIST-GRAPH", "HIST-ROW-FIELDS", "HIST-BADGES",
    "HIST-SHORT-ID")
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
        GG_CHECK_STR_EQ(row.shortId, s.gitOut(repo, {"rev-parse", "--short", row.id.hex()}));
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

GG_TEST("history", "published vs unpublished commits", "HIST-PUBLISHED-COLOUR", "REMOTE-PUBLISHED", "INFO-PUBLISHED")
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

GG_TEST("history", "Working tree and Index rows", "HIST-WT-ROW", "HIST-INDEX-ROW")
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

GG_TEST("history", "stash badges on base commits", "HIST-STASH-BADGES")
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

GG_TEST("history", "scope follows the side panels", "HIST-SCOPE", "BR-TOGGLE", "BR-CTRL-ONLY")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    GG_REQUIRE(s.openRepository(repo));
    const std::string topic = s.revParse(repo, "topic");
    const std::string mainTip = s.revParse(repo, "main");
    GG_CHECK(findRow(s, topic) != nullptr);
    // Click hides the branch.
    ctx->ItemClick("//Branches/branch_topic/###branch_topic");
    GG_CHECK(s.waitUntil([&] { return findRow(s, topic) == nullptr && !s.session()->history().loading(); }));
    GG_CHECK(findRow(s, mainTip) != nullptr);
    // Click again shows it.
    ctx->ItemClick("//Branches/branch_topic/###branch_topic");
    GG_CHECK(s.waitUntil([&] { return findRow(s, topic) != nullptr; }));
    // Ctrl-click: only this branch.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick("//Branches/branch_feature/###branch_feature");
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK(s.waitUntil([&] { return findRow(s, mainTip) == nullptr && findRow(s, topic) == nullptr; }));
    GG_CHECK(findRow(s, s.revParse(repo, "feature")) != nullptr);
    ctx->ItemClick("//History/Show all refs##hist_all");
    GG_CHECK(s.waitUntil([&] { return findRow(s, mainTip) != nullptr && findRow(s, topic) != nullptr; }));
}

GG_TEST("history", "search by message, ID, branch and tag; no graph while filtering", "HIST-SEARCH",
    "HIST-FILTER-NO-GRAPH")
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

GG_TEST("history", "merges start collapsed; expand and collapse merged history", "HIST-MERGE-EXPAND",
    "HIST-MERGE-COLLAPSED-DEFAULT")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string f1 = s.revParse(repo, "feature~1");
    // Merged and deleted: its commits are only reachable through the merge. (A branch that still
    // points into the merged side keeps its commits shown.)
    s.git(repo, {"branch", "-D", "feature"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string merge = s.revParse(repo, "main");
    // Collapsed by default: the merged side (feature~1, feature) is hidden. The merge bubble
    // toggles it.
    GG_REQUIRE(s.waitUntil([&] { return findRow(s, merge) != nullptr && !s.session()->history().loading(); }));
    GG_CHECK(findRow(s, merge)->collapsed);
    GG_CHECK(findRow(s, f1) == nullptr);
    GG_CHECK(s.waitUntil([&] { return findRow(s, merge)->collapsedCount == 2; }));
    ctx->ItemClick(("//History/**/" + merge + "/###merge_toggle").c_str());
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) != nullptr && !findRow(s, merge)->collapsed; }));
    ctx->ItemClick(("//History/**/" + merge + "/###merge_toggle").c_str());
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) == nullptr && findRow(s, merge) && findRow(s, merge)->collapsed; }));
    // Same through the context menu.
    s.contextMenu(rowRef(merge).c_str(), "Expand merged history");
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) != nullptr; }));
    s.contextMenu(rowRef(merge).c_str(), "Collapse merged history");
    GG_CHECK(s.waitUntil([&] { return findRow(s, f1) == nullptr; }));
}

GG_TEST("history", "keyboard navigation", "HIST-KEY-UPDOWN")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//History/**/###row_wt");
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    ctx->KeyPress(ImGuiKey_DownArrow);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.head(repo));
    ctx->KeyPress(ImGuiKey_DownArrow);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.revParse(repo, "HEAD~1"));
    ctx->KeyPress(ImGuiKey_UpArrow);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), s.head(repo));
    ctx->KeyPress(ImGuiKey_UpArrow);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
    ctx->KeyPress(ImGuiKey_UpArrow);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::WorkingTree);
}

GG_TEST("history", "copy ID and full description; tooltip ID", "HIST-CTX-COPY-ID", "HIST-CTX-COPY-DESC",
    "APP-COPY-ID-SHIFT", "APP-ID-DIMMED")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    const std::string id = s.revParse(repo, "HEAD~2");
    const std::string shortId = s.gitOut(repo, {"rev-parse", "--short", id});
    s.contextMenu(rowRef(id).c_str(), "Copy/ID");
    GG_CHECK_STR_EQ(s.clipboard(), shortId);
    ctx->ItemClick(rowRef(id).c_str(), ImGuiMouseButton_Right);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->MenuClick("//$FOCUSED/Copy/ID");
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.clipboard(), id);
    // The row tooltip starts with the full ID, its short prefix undimmed.
    ctx->MouseMove(rowRef(id).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.idShownDimmed("//##Tooltip_00", id, shortId.size()));
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

GG_TEST("history", "scroll position stays anchored on the rows in view", "HIST-SCROLL-ANCHOR")
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

GG_TEST("history", "tooltips wait until scrolling stops", "HIST-TOOLTIP-SCROLL")
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

GG_TEST("history", "large history: first page, Show more, reveal, cancel", "HIST-SHOW-MORE", "HIST-REVEAL",
    "HIST-REVEAL-CANCEL", "APP-CANCEL-LONG-OPS")
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

GG_TEST("history", "cancel a long history load and a reveal", "HIST-REVEAL-CANCEL", "APP-CANCEL-LONG-OPS")
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
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//##Toolbar/Cancel##tb_cancel"); }, 10.0f));
    ctx->ItemClick("//##Toolbar/Cancel##tb_cancel");
    GG_CHECK(s.waitUntil([&] { return s.session()->activities().empty(); }, 30.0f));
    GG_CHECK(s.session()->selection().id.hex() != root);
    GG_CHECK(history.rows().size() < 100000);
}

GG_TEST("history", "first rows of a large history appear quickly", "HIST-LOAD-FAST")
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
        GG_CHECK(ms(first) < 700);
        ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
        ctx->Yield(3);
    }
}

} // namespace ggtest
