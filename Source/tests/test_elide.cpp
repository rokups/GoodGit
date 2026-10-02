// One-line texts (GG-2): a commit subject, stash message or tag message that does not fit its place is
// cut with an ellipsis; only the first line of a message is ever shown.
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Widgets.hpp"
#include "tests/Harness.hpp"

#include <imgui_internal.h>

#include <algorithm>

namespace ggtest {

namespace {

// ~300 characters, then a body: only the first line is a subject.
std::string longSubject()
{
    std::string s = "Rework the way the long-running synchronisation of repositories is scheduled";
    while (s.size() < 300)
        s += " and then make sure every single one of its callers still behaves";
    return s;
}

const std::string kEllipsis = "\xE2\x80\xA6";

struct Repo {
    fs::path path;
    std::string subject = longSubject();
    std::string stash = "wip: " + longSubject();
    std::string commit;
};

// The linear fixture plus a commit with a long subject and a body, and a stash with a long message.
Repo makeRepo(Scenario& s)
{
    Repo r;
    r.path = s.fixture(Recipe::Linear);
    s.commitFile(r.path, "long.txt", "one\n", r.subject + "\n\nA body line.\nAnother body line.\n");
    r.commit = s.head(r.path);
    s.write(r.path, "long.txt", "two\n");
    s.git(r.path, {"stash", "push", "-q", "-m", r.stash});
    return r;
}

// The table column of History that holds x (an item's left edge), nullptr when there is none.
const ImGuiTableColumn* columnAt(ImGuiWindow* window, float x)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Tables.GetMapSize(); ++i) {
        ImGuiTable* t = g.Tables.TryGetMapData(i);
        if (!t || t->InnerWindow != window)
            continue;
        for (int c = 0; c < t->ColumnsCount; ++c)
            if (x >= t->Columns[c].MinX && x < t->Columns[c].MaxX)
                return &t->Columns[c];
    }
    return nullptr;
}

std::string descRef(const std::string& hex) { return "//History/**/###desc_" + hex; }

} // namespace

GG_TEST("elide", "firstLine and fitText")
{
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("one\ntwo")), "one");
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("one\r\ntwo")), "one");
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("one  \t\ntwo")), "one");
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("one")), "one");
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("\nbody")), "");
    GG_CHECK_STR_EQ(std::string(ggui::firstLine("")), "");

    const std::string text = longSubject();
    GG_CHECK(ggui::fitText(text, 1e6f) == text);
    for (const float width : {40.0f, 120.0f, 333.0f}) {
        const std::string fit = ggui::fitText(text, width);
        GG_CHECK(fit.size() > kEllipsis.size() && fit.compare(fit.size() - kEllipsis.size(), kEllipsis.size(), kEllipsis) == 0);
        GG_CHECK(ImGui::CalcTextSize(fit.c_str()).x <= width);
    }
}

GG_TEST("elide", "Changes cuts the subject of a commit only where the line ends")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(r.commit)) != nullptr; }));
    ctx->ItemClick(("//History/**/###row_" + r.commit).c_str());
    // Neither scanning nor loading: the "scanning..." note is not drawn, so the subject has its room.
    GG_REQUIRE(s.waitUntil([&] {
        const auto& changes = s.session()->changes();
        return !changes.scanning() && !changes.rows().empty() && changes.rows()[0].group == ggui::FileGroup::Commit;
    }));
    ctx->Yield(3);
    const ImGuiTestItemInfo title = ctx->ItemInfo("//Changes/###changes_title", ImGuiTestOpFlags_NoError);
    GG_REQUIRE(title.ID != 0);
    ImGuiWindow* window = ctx->GetWindowByRef("//Changes");
    GG_REQUIRE(window != nullptr);
    // The title is cut...
    GG_CHECK(title.RectFull.GetWidth() < ImGui::CalcTextSize(r.subject.c_str()).x);
    bool ellipsis = false;
    for (const auto& line : s.drawnText("//Changes"))
        ellipsis = ellipsis || line.find(kEllipsis) != std::string::npos;
    GG_CHECK(ellipsis);
    // ...but reaches the window's edge to within the ellipsis and a glyph or two (it was a whole
    // " scanning... " short of it).
    const float gap = window->WorkRect.Max.x - title.RectFull.Max.x;
    GG_CHECK(gap >= 0.0f);
    GG_CHECK(gap < ImGui::CalcTextSize("\xE2\x80\xA6MM").x);
}

GG_TEST("elide", "History shows the first line cut at the Description column; the stash row stays in its list")
{
    const Repo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(r.commit)) != nullptr; }));
    // The row's subject is the first line only.
    const auto* row = s.session()->history().row(ggui::core::Oid::fromHex(r.commit));
    GG_CHECK_STR_EQ(row->subject, r.subject);

    ctx->ItemClick(("//History/**/###row_" + r.commit).c_str()); // (scrolls the row into view)
    ctx->Yield(2);
    const ImGuiTestItemInfo desc = ctx->ItemInfo(descRef(r.commit).c_str(), ImGuiTestOpFlags_NoError);
    GG_REQUIRE(desc.ID != 0);
    const ImGuiTableColumn* column = columnAt(desc.Window, desc.RectFull.Min.x);
    GG_REQUIRE(column != nullptr);
    // The text is cut: its item is as wide as the room left in the column, not as wide as the subject.
    GG_CHECK(desc.RectFull.GetWidth() < ImGui::CalcTextSize(r.subject.c_str()).x);
    GG_CHECK(desc.RectFull.Max.x <= column->MaxX + 0.5f);
    // The ellipsis itself is drawn (the History's glyphs include U+2026).
    bool ellipsis = false;
    for (const auto& line : s.drawnText("//History"))
        ellipsis = ellipsis || line.find(kEllipsis) != std::string::npos;
    GG_CHECK(ellipsis);

    // The stash row: a label cut to the list's width, the item inside the Stashes window.
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    const ImGuiTestItemInfo list = ctx->ItemInfo("//Stashes");
    const ImGuiTestItemInfo stashRow = ctx->ItemInfo("//Stashes/stash_0/###row");
    GG_CHECK(stashRow.RectFull.Max.x <= list.RectFull.Max.x + 0.5f);
    // (drawnText leaves the spaces out.)
    const auto bare = [](std::string text) {
        text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
        return text;
    };
    bool cut = false, whole = false;
    // At the default (narrow) width the message keeps its minimum and the base and date are clipped.
    for (const auto& line : s.drawnText("//Stashes")) {
        cut = cut || (line.find("stash@{0}") != std::string::npos && line.find(kEllipsis) != std::string::npos);
        whole = whole || line.find(bare(r.stash)) != std::string::npos;
    }
    GG_CHECK(cut);
    GG_CHECK(!whole);
    const std::string base = s.gitOut(r.path, {"rev-parse", "--short=7", "stash@{0}^1"});
    const std::string date = bare(s.gitOut(r.path, {"log", "-1", "--format=%cd", "--date=format-local:%Y-%m-%d %H:%M", "stash@{0}"}));
    const auto trailingShown = [&] {
        for (const auto& line : s.drawnText("//Stashes")) {
            const size_t dots = line.find(kEllipsis);
            if (dots != std::string::npos && line.find(base, dots) != std::string::npos && line.find(date, dots) != std::string::npos)
                return true;
        }
        return false;
    };
    GG_CHECK(!trailingShown());
    // Wide enough for both: the message is cut so that the base and the date follow it, drawn whole.
    // (The sash is the dock splitter just past the panel's right edge.)
    ImGuiWindow* stashes = ctx->GetWindowByRef("//Stashes");
    GG_REQUIRE(stashes != nullptr);
    const float widthBefore = stashes->Size.x;
    const ImVec2 grip(stashes->Pos.x + stashes->Size.x + 2.0f, stashes->Pos.y + stashes->Size.y * 0.5f);
    ctx->MouseMoveToPos(grip);
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(ImVec2(grip.x + ImGui::GetFontSize() * 30.0f, grip.y));
    ctx->MouseUp(0);
    ctx->Yield(5);
    GG_REQUIRE(stashes->Size.x > widthBefore);
    GG_CHECK(trailingShown());
}

} // namespace ggtest
