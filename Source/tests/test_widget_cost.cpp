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

namespace {

float oldWidth(const char* b, const char* e) { return ImGui::CalcTextSize(b, e).x; }

// The linear algorithms that fitText, elideStart and elideMiddleToWidth had before GG-133.
std::string oldFit(const std::string& text, float width)
{
    if (oldWidth(text.c_str(), nullptr) <= width)
        return text;
    const float room = width - oldWidth("\xE2\x80\xA6", nullptr);
    size_t cut = 0;
    while (cut < text.size()) {
        size_t next = cut + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
            ++next;
        if (oldWidth(text.c_str(), text.c_str() + next) > room)
            break;
        cut = next;
    }
    return text.substr(0, cut) + "\xE2\x80\xA6";
}

std::string oldStart(const std::string& text, float width)
{
    if (oldWidth(text.c_str(), nullptr) <= width)
        return text;
    const float room = width - oldWidth("\xE2\x80\xA6", nullptr);
    size_t cut = text.size();
    while (cut > 0) {
        size_t prev = cut - 1;
        while (prev > 0 && (static_cast<unsigned char>(text[prev]) & 0xC0) == 0x80)
            --prev;
        if (oldWidth(text.c_str() + prev, text.c_str() + text.size()) > room)
            break;
        cut = prev;
    }
    return "\xE2\x80\xA6" + text.substr(cut);
}

std::string oldMiddle(const std::string& text, float width)
{
    if (oldWidth(text.c_str(), nullptr) <= width)
        return text;
    size_t count = 0;
    for (const char c : text)
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    for (size_t keep = count; keep > 2; --keep) {
        const std::string cut = ggui::elideMiddle(text, static_cast<int>((keep + 1) / 2), static_cast<int>(keep / 2));
        if (oldWidth(cut.c_str(), nullptr) <= width)
            return cut;
    }
    return ggui::elideMiddle(text, 1, 1);
}

std::string longPath(size_t n)
{
    std::string p;
    for (int i = 0; p.size() < n; ++i)
        p += "dir" + std::to_string(i) + "/";
    return p.substr(0, n);
}

} // namespace

GG_TEST("widget_cost", "the elide functions give the same text as the linear search, with a logarithmic number of measures")
{
    const std::vector<std::string> texts = {"", "a", "ab", "short text", "src/app/shell/Widgets.cpp",
        "\xC3\xA4\xC3\xB6\xC3\xBC-\xE2\x82\xAC-\xF0\x9F\x98\x80 mixed UTF-8 text \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E and more", longPath(300),
        longPath(4000)};
    for (const std::string& t : texts)
        for (float w : {0.0f, 5.0f, 20.0f, 60.0f, 150.0f, 400.0f, 1e6f}) {
            GG_CHECK(ggui::fitText(t, w) == oldFit(t, w));
            GG_CHECK(ggui::elideStart(t, w) == oldStart(t, w));
            GG_CHECK(ggui::elideMiddleToWidth(t, w) == oldMiddle(t, w));
        }

    const std::string path = longPath(4000);
    for (const auto& fn : {&ggui::fitText, &ggui::elideStart, &ggui::elideMiddleToWidth}) {
        ggui::g_textMeasures = 0;
        fn(path, 300.0f);
        GG_CHECK(ggui::g_textMeasures > 0 && ggui::g_textMeasures <= 40);
    }
}

} // namespace ggtest
