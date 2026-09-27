// Visual layout checks: graph continuity, row geometry; saves screenshots for review (§4.2, §6).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"

#include <map>
#include <set>

namespace ggtest {

namespace {

// A history with long-lived branches, several merges and a branch off a merged side.
fs::path graphRepo(Scenario& s)
{
    const fs::path repo = s.fixture(Recipe::Merges);
    s.git(repo, {"switch", "-q", "-c", "long", "HEAD~2"});
    for (int i = 0; i < 3; ++i)
        s.commitFile(repo, "long" + std::to_string(i) + ".txt", "l\n", "Long " + std::to_string(i));
    s.git(repo, {"switch", "-q", "main"});
    s.commitFile(repo, "main3.txt", "m3\n", "Main 3");
    s.git(repo, {"merge", "-q", "--no-ff", "-m", "Merge long", "long"});
    s.git(repo, {"switch", "-q", "-c", "late", "feature~1"});
    s.commitFile(repo, "late.txt", "late\n", "Late off feature");
    s.git(repo, {"switch", "-q", "main"});
    s.commitFile(repo, "main4.txt", "m4\n", "Main 4");
    return repo;
}


// Text that shares a line must share a baseline. Glyph quads in the last frame's draw lists are
// mapped back to their glyphs (by texture coordinates) to recover each glyph's baseline; two glyphs
// of one window whose lines overlap vertically but sit on different baselines are misaligned.
std::vector<std::string> baselineMismatches(size_t* glyphCount)
{
    const float lineHeight = ImGui::GetTextLineHeight();
    using Glyph = Scenario::DrawnGlyph;
    std::vector<std::string> out;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        auto found = Scenario::drawnGlyphs(w);
        *glyphCount += found.size();
        std::sort(found.begin(), found.end(), [](const Glyph& a, const Glyph& b) { return a.baseline < b.baseline; });
        for (size_t k = 1; k < found.size(); ++k) {
            const float d = found[k].baseline - found[k - 1].baseline;
            if (d > 0.5f && d < lineHeight * 0.75f) {
                auto text = [&](float top) {
                    std::vector<const Glyph*> line;
                    for (const auto& g : found)
                        if (std::abs(g.baseline - top) < 0.25f)
                            line.push_back(&g);
                    std::sort(line.begin(), line.end(), [](const Glyph* a, const Glyph* b) { return a->x < b->x; });
                    std::string t;
                    for (const Glyph* g : line) {
                        char buf[5] = {};
                        ImTextCharToUtf8(buf, g->codepoint);
                        t += g->codepoint < 0xE000 ? buf : "<icon>";
                        if (t.size() > 60)
                            break;
                    }
                    return t;
                };
                out.push_back(std::string(w->Name) + ": '" + text(found[k - 1].baseline) + "' vs '" + text(found[k].baseline)
                    + "' (" + std::to_string(d) + " px)");
            }
        }
    }
    return out;
}

void expectBaselines(ImGuiTestContext* ctx, const char* scene, bool& ok)
{
    ctx->Yield(3);
    size_t count = 0;
    const auto mismatches = baselineMismatches(&count);
    ctx->LogInfo("baseline [%s]: %zu glyphs checked", scene, count);
    if (count < 20) {
        ctx->LogError("baseline [%s]: glyph detection found too little text", scene);
        ok = false;
    }
    for (const auto& m : mismatches) {
        ctx->LogError("baseline [%s] %s", scene, m.c_str());
        ok = false;
    }
}

} // namespace

GG_TEST("visual", "graph lines are continuous from row to row", "HIST-GRAPH", "HIST-GRAPH-CONTINUOUS")
{
    const fs::path repo = graphRepo(s);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && !history.rows().empty(); }));
    s.screenshot("history-graph");
    // Every lane leaving a row at its bottom edge enters the next visible row at its top edge.
    const auto visible = history.visibleIndexes();
    const auto& rows = history.rows();
    for (size_t i = 0; i + 1 < visible.size(); ++i) {
        std::set<int> out, in;
        for (const auto& l : rows[static_cast<size_t>(visible[i])].lines)
            if (l.toPos == 2)
                out.insert(l.toLane);
        for (const auto& l : rows[static_cast<size_t>(visible[i + 1])].lines)
            if (l.fromPos == 0)
                in.insert(l.fromLane);
        GG_CHECK(out == in);
    }
    // Rows are drawn at a constant pitch equal to the graph's row height.
    GG_CHECK(history.rowPitchConsistent());
}

GG_TEST("visual", "screenshots of the main views", "HIST-GRAPH")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    ctx->KeyPress(ImGuiKey_F6);
    s.settle();
    s.screenshot("changes-diff-unified");
}

GG_TEST("visual", "text shares a baseline across widgets on one line", "UI-TEXT-BASELINE")
{
    bool ok = true;
    expectBaselines(ctx, "welcome", ok);
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    for (int scale : {100, 125, 150}) {
        const std::string at = "@" + std::to_string(scale) + " ";
        s.app.openSettings();
        ctx->Yield(2);
        ctx->ItemClick("//Settings/##settings_tabs/General");
        ctx->ItemInputValue("//Settings/##settings_tabs/General/UI scale##scale", scale);
        for (const char* tab : {"General", "Git", "Hooks"}) {
            ctx->Yield(2);
            ctx->ItemClick((std::string("//Settings/##settings_tabs/") + tab).c_str());
            expectBaselines(ctx, (at + tab).c_str(), ok);
        }
        ctx->WindowClose("//Settings");
        s.showPanel("Diff");
        ctx->ItemClick(("//Changes/**/###file_" + s.session()->changes().rows()[0].path).c_str());
        s.settle();
        expectBaselines(ctx, (at + "repository").c_str(), ok);
        for (const char* panel : {"Tags", "Remotes", "Stashes", "Reflog", "Operations", "Blame"}) {
            s.showPanel(panel);
            expectBaselines(ctx, (at + panel).c_str(), ok);
        }
        ctx->ItemClick("//##Toolbar/###tb_commit");
        GG_REQUIRE(s.dialogOpen("Commit"));
        expectBaselines(ctx, (at + "commit dialog").c_str(), ok);
        s.dialogButton("Commit", "Cancel");
        s.screenshot("baseline-" + std::to_string(scale));
    }
    // Mid-merge: the state badge and its buttons share the toolbar's baseline.
    const fs::path merge = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(merge));
    GG_REQUIRE(s.itemExists("//##Toolbar/###tb_state"));
    expectBaselines(ctx, "mid-merge", ok);
    GG_CHECK(ok);
}

GG_TEST("visual", "icon glyphs are vertically centred on the text", "UI-ICON-ALIGN")
{
    // Compare glyph boxes as baked for the UI and mono fonts at a few sizes: an icon's centre sits
    // on the centre of a capital letter ("H") within a pixel.
    const char* icons[] = {ICON_MS_DOWNLOAD, ICON_MS_UNDO, ICON_MS_CALL_SPLIT, ICON_MS_SEARCH, ICON_MS_ADD};
    for (ImFont* font : {ggui::theme().uiFont(), ggui::theme().monoFont()}) {
        for (float size : {13.0f, 15.0f, 20.0f, 30.0f}) {
            ImFontBaked* baked = font->GetFontBaked(size);
            const ImFontGlyph* h = baked->FindGlyph('H');
            GG_REQUIRE(h != nullptr);
            const float textCentre = (h->Y0 + h->Y1) * 0.5f;
            for (const char* icon : icons) {
                unsigned int c = 0;
                ImTextCharFromUtf8(&c, icon, nullptr);
                const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(c));
                GG_REQUIRE(g != nullptr);
                const float iconCentre = (g->Y0 + g->Y1) * 0.5f;
                ctx->LogInfo("size %.0f icon U+%04X centre %.2f vs text %.2f (glyph %.1f..%.1f, H %.1f..%.1f)", size, c,
                    iconCentre, textCentre, g->Y0, g->Y1, h->Y0, h->Y1);
                GG_CHECK(std::abs(iconCentre - textCentre) <= std::max(1.0f, size * 0.05f));
            }
        }
    }
}

} // namespace ggtest
