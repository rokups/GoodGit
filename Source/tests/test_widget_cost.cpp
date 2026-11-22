// Per-frame cost of widgets and dialogs (GG-133): work is bounded by what is visible, not by the data.
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Widgets.hpp"
#include "tests/Harness.hpp"

#include <imgui_internal.h>

namespace ggtest {

GG_TEST("widget_cost", "a check list of 10000 options filters once for each filter change and draws a bounded number of rows")
{
    ggui::Form f;
    f.title = "Many tags";
    ggui::Field& list = f.add(ggui::Field{ggui::Field::CheckList, "tags", "Tags"});
    std::vector<std::string> names;
    for (int i = 0; i < 10000; ++i)
        names.push_back("tag-" + std::to_string(i));
    list.setOptions(names);
    f.buttons.push_back({"Cancel", {}});
    s.app.dialogs().open(std::move(f));
    GG_REQUIRE(s.dialogOpen("Many tags"));
    ctx->Yield(2);

    const int runs = ggui::g_fieldFilterRuns;
    const int rows = ggui::g_fieldRowsDrawn;
    ctx->Yield(5);
    GG_CHECK(ggui::g_fieldFilterRuns == runs); // no change: no filter pass
    GG_CHECK((ggui::g_fieldRowsDrawn - rows) / 5 < 100); // the clipper bounds the rows of a frame
    GG_CHECK(ggui::g_fieldRowsDrawn - rows > 0);

    ctx->ItemInputValue("//Many tags/##tags_filter", "tag-77");
    ctx->Yield(5);
    GG_CHECK(ggui::g_fieldFilterRuns == runs + 1); // one pass for the one change
}

GG_TEST("widget_cost", "row buttons are not built while a filter is active and no row is hovered")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (const char* b : {"feature/one", "feature/two", "solo/x"})
        s.git(repo, {"branch", b});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    ctx->ItemClick("//Branches/##branch_filter"); // the filter is the active item
    ctx->MouseMoveToPos(ImVec2(2.0f, 2.0f));
    ctx->Yield(3);
    GG_REQUIRE(ImGui::GetCurrentContext()->ActiveId != 0);
    const int builds = ggui::g_rowActionBuilds;
    ctx->Yield(5);
    GG_CHECK(ggui::g_rowActionBuilds == builds);

    // With the mouse on a row, its buttons are built (the counter works).
    ctx->MouseMoveToPos(ctx->ItemInfo("//Branches/branch_solo:x/###branch_solo:x").RectFull.GetCenter());
    ctx->Yield(3);
    GG_CHECK(ggui::g_rowActionBuilds > builds);
}

} // namespace ggtest
