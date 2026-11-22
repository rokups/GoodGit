// Per-frame cost of widgets and dialogs (GG-133): work is bounded by what is visible, not by the data.
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "tests/Harness.hpp"

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

} // namespace ggtest
