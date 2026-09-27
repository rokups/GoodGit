// Diff panel (§4.5; P1-08, P1-18).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string numbered(int n, int changedA = -1, int changedB = -1)
{
    std::string s;
    for (int i = 1; i <= n; ++i)
        s += (i == changedA || i == changedB) ? "int line" + std::to_string(i) + " = 1; // changed\n"
                                              : "int line" + std::to_string(i) + " = 0;\n";
    return s;
}

std::string pngHeader(int w, int h)
{
    std::string p("\x89PNG\r\n\x1a\n", 8);
    p += std::string("\0\0\0\x0dIHDR", 8);
    auto be32 = [&](int v) {
        p.push_back(static_cast<char>((v >> 24) & 0xff));
        p.push_back(static_cast<char>((v >> 16) & 0xff));
        p.push_back(static_cast<char>((v >> 8) & 0xff));
        p.push_back(static_cast<char>(v & 0xff));
    };
    be32(w);
    be32(h);
    p += std::string("\x08\x06\0\0\0", 5);
    p += std::string(16, '\0');
    return p;
}

struct DiffRepo {
    fs::path path;
    std::string base, change;
};

// Base commit and a commit that changes several kinds of files.
DiffRepo makeRepo(Scenario& s)
{
    DiffRepo r;
    r.path = s.fixture(Recipe::Empty, "diffs");
    s.write(r.path, "code.cpp", numbered(40));
    s.write(r.path, "ws.txt", "a b\n");
    s.write(r.path, "blob.bin", std::string("\0\1\2\3", 4));
    s.write(r.path, "pic.png", pngHeader(1, 1));
    s.write(r.path, "script.sh", "#!/bin/sh\necho hi\n");
    s.write(r.path, "old.txt", "rename me\nline two\nline three\nline four\n");
    std::string big;
    for (int i = 0; i < 25000; ++i)
        big += "big " + std::to_string(i) + "\n";
    s.write(r.path, "big.txt", big);
    s.git(r.path, {"add", "."});
    s.git(r.path, {"commit", "-q", "-m", "Base"});
    r.base = s.head(r.path);
    s.write(r.path, "code.cpp", numbered(40, 5, 35));
    s.write(r.path, "ws.txt", "a  b\n");
    s.write(r.path, "blob.bin", std::string("\0\1\2\3\4", 5));
    s.write(r.path, "pic.png", pngHeader(2, 3));
    fs::permissions(r.path / "script.sh", fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);
    s.git(r.path, {"mv", "old.txt", "new.txt"});
    s.write(r.path, "new.txt", "rename me\nline two\nline three\nline four changed\n");
    std::string big2;
    for (int i = 0; i < 25000; ++i)
        big2 += "BIG " + std::to_string(i) + "\n";
    s.write(r.path, "big.txt", big2);
    s.git(r.path, {"add", "-A"});
    s.git(r.path, {"commit", "-q", "-m", "Change everything"});
    r.change = s.head(r.path);
    return r;
}

std::string body(Scenario& s) { return s.child("//Diff", "##diff_body"); }

void showFile(Scenario& s, const std::string& commit, const std::string& path)
{
    s.ctx->ItemClick(("//History/**/###row_" + commit).c_str());
    s.waitUntil([&] { return !s.session()->changes().rows().empty(); });
    const std::string ref = s.child("//Changes", "##files") + "/" + Scenario::escapeRef(path) + "/###file_"
        + Scenario::escapeRef(path);
    s.ctx->ItemClick(ref.c_str());
    s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == path;
    });
    s.showPanel("Diff");
}

const ggui::core::DiffFile* file(Scenario& s)
{
    const auto& d = s.session()->diff().diff();
    return d && !d->files.empty() ? &d->files[0] : nullptr;
}

} // namespace

GG_TEST("diff", "unified view, context lines, expandable context", "DIFF-UNIFIED", "DIFF-CONTEXT", "DIFF-EXPAND",
    "DIFF-EXPAND-SHIFT")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "code.cpp");
    const auto* f = file(s);
    GG_REQUIRE(f != nullptr);
    GG_CHECK_EQ(f->hunks.size(), static_cast<size_t>(2));
    GG_CHECK(f->hunks[0].header.rfind("@@ -2,7 +2,7 @@", 0) == 0);
    GG_CHECK_EQ(f->additions, 2);
    GG_CHECK_EQ(f->deletions, 2);
    GG_CHECK(s.itemExists((body(s) + "/###hunk_0").c_str()));
    GG_CHECK(s.itemExists((body(s) + "/###expand_0").c_str()));
    // Context 1: shorter hunks.
    ctx->ItemInputValue("//Diff/Context##diff_context", 1);
    GG_CHECK(s.waitUntil([&] { return file(s) && file(s)->hunks.size() == 2 && file(s)->hunks[0].lines.size() == 4; }));
    ctx->ItemInputValue("//Diff/Context##diff_context", 3);
    GG_REQUIRE(s.waitUntil([&] { return file(s) && file(s)->hunks[0].lines.size() == 8; }));
    // Gap 1 lies between the two hunks: click reveals 10 lines, Shift+click the rest.
    ctx->ItemClick((body(s) + "/###expand_1").c_str());
    GG_CHECK_EQ(s.session()->diff().gapShown(1), 10);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick((body(s) + "/###expand_1").c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_EQ(s.session()->diff().gapShown(1), -1);
    GG_CHECK(!s.itemExists((body(s) + "/###expand_1").c_str()));
}

GG_TEST("diff", "side-by-side view with syntax highlighting", "DIFF-SIDE-BY-SIDE", "DIFF-SYNTAX")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "code.cpp");
    s.comboSelect("//Diff/##diff_view", "Side by side");
    ctx->Yield(3);
    GG_CHECK(s.app.settings().data().diffSideBySide);
    GG_CHECK_STR_EQ(s.session()->diff().languageName(), "C++");
    // Two editors, removed lines left and added lines right, aligned line for line.
    GG_CHECK(s.itemExists(s.child(body(s).c_str(), "##sbs_left").c_str()));
    GG_CHECK(s.itemExists(s.child(body(s).c_str(), "##sbs_right").c_str()));
    GG_CHECK(s.itemExists((s.child(body(s).c_str(), "##sbs_left") + "/###hunk_0").c_str()));
    s.comboSelect("//Diff/##diff_view", "Unified");
    ctx->Yield(2);
    GG_CHECK(!s.app.settings().data().diffSideBySide);
}

GG_TEST("diff", "text is selectable with the mouse in both views", "DIFF-SELECT-TEXT", "DIFF-EDITOR-VIEWS")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "code.cpp");
    auto drag = [&](const std::string& windowRef) -> std::string {
        ImGuiWindow* w = ctx->GetWindowByRef(windowRef.c_str());
        if (!w) {
            ctx->LogError("no editor window %s", windowRef.c_str());
            return {};
        }
        const float line = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
        // From the middle of the text on the 3rd line to the middle of the 5th line.
        const ImVec2 from(w->InnerRect.Min.x + w->InnerRect.GetWidth() * 0.45f, w->InnerRect.Min.y + line * 2.5f);
        const ImVec2 to(w->InnerRect.Min.x + w->InnerRect.GetWidth() * 0.35f, w->InnerRect.Min.y + line * 4.5f);
        ctx->MouseMoveToPos(from);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->MouseMoveToPos(to);
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(2);
        return s.session()->diff().selectedText();
    };
    // Unified: a free selection across lines (not whole rows) is copied as is.
    const std::string unified = drag(body(s));
    GG_CHECK(unified.find('\n') != std::string::npos);
    GG_CHECK(!unified.empty() && unified.back() != '\n');
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
    GG_CHECK_STR_EQ(s.clipboard(), unified);
    // Side by side: both sides are editors too.
    s.comboSelect("//Diff/##diff_view", "Side by side");
    ctx->Yield(3);
    const std::string left = drag(s.child(body(s).c_str(), "##sbs_left"));
    GG_CHECK(left.find('\n') != std::string::npos);
    const std::string right = drag(s.child(body(s).c_str(), "##sbs_right"));
    GG_CHECK(right.find('\n') != std::string::npos);
    s.screenshot("diff-side-by-side");
    s.comboSelect("//Diff/##diff_view", "Unified");
    ctx->Yield(2);
    s.screenshot("diff-unified");
}

GG_TEST("diff", "whitespace modes", "DIFF-WS-MODES")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "ws.txt");
    GG_REQUIRE(file(s) != nullptr);
    GG_CHECK_EQ(file(s)->hunks.size(), static_cast<size_t>(1));
    s.comboSelect("//Diff/##diff_ws", "Ignore changes");
    GG_CHECK(s.waitUntil([&] { return !file(s) || file(s)->hunks.empty(); }));
    s.comboSelect("//Diff/##diff_ws", "Ignore all");
    GG_CHECK(s.waitUntil([&] { return !file(s) || file(s)->hunks.empty(); }));
    s.comboSelect("//Diff/##diff_ws", "Whitespace: normal");
    GG_CHECK(s.waitUntil([&] { return file(s) && file(s)->hunks.size() == 1; }));
}

GG_TEST("diff", "binary, image, submodule and mode-change placeholders", "DIFF-BINARY", "DIFF-IMAGE",
    "DIFF-SUBMODULE", "DIFF-MODE-CHANGE")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "blob.bin");
    GG_CHECK(s.itemText("//Diff/###diff_binary").find("Binary file") != std::string::npos);
    showFile(s, r.change, "pic.png");
    GG_REQUIRE(file(s) != nullptr);
    GG_CHECK_STR_EQ(file(s)->oldImage, "1x1");
    GG_CHECK_STR_EQ(file(s)->newImage, "2x3");
    GG_CHECK(s.itemExists("//Diff/###diff_image"));
    showFile(s, r.change, "script.sh");
    GG_CHECK(s.itemText("//Diff/###diff_mode").find("Mode changed 100644") != std::string::npos);

    const fs::path super = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(super));
    showFile(s, s.head(super), "sub");
    GG_CHECK(s.itemText("//Diff/###diff_submodule").find("Submodule sub") != std::string::npos);
    GG_CHECK(file(s) && file(s)->submodule);
    GG_CHECK_STR_EQ(file(s)->newId.hex(), s.revParse(super, "HEAD:sub"));
}

GG_TEST("diff", "renames, compare this file with HEAD, large diffs", "DIFF-RENAME", "DIFF-VS-HEAD", "DIFF-LOAD-FULL")
{
    const DiffRepo r = makeRepo(s);
    s.commitFile(r.path, "code.cpp", numbered(40, 20), "Later change");
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "new.txt");
    GG_REQUIRE(file(s) != nullptr);
    GG_CHECK(file(s)->kind == ggui::core::ChangeKind::Renamed);
    GG_CHECK_STR_EQ(file(s)->oldPath, "old.txt");
    // Compare only code.cpp of the "change" commit with HEAD.
    showFile(s, r.change, "code.cpp");
    ctx->ItemClick("//Diff/Compare only this file with HEAD##diff_vs_head");
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && d->query.kind == ggui::core::DiffKind::Commits && d->query.a.hex() == s.head(r.path);
    }));
    GG_REQUIRE(file(s) != nullptr);
    GG_CHECK_EQ(file(s)->hunks.size(), static_cast<size_t>(3));
    ctx->ItemClick("//Diff/Compare only this file with HEAD##diff_vs_head");
    // Large diff: capped, then loaded in full.
    showFile(s, r.change, "big.txt");
    GG_REQUIRE(file(s) != nullptr);
    GG_CHECK(file(s)->truncated);
    ctx->ItemClick("//Diff/Load full diff##load_full");
    GG_CHECK(s.waitUntil([&] { return file(s) && !file(s)->truncated && file(s)->additions == 25000; }, 60.0f));
}

GG_TEST("diff", "select lines, Ctrl+C and the context menu", "DIFF-COPY-KEY", "DIFF-CTX-COPY", "DIFF-CTX-BLAME")
{
    const DiffRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    showFile(s, r.change, "code.cpp");
    // Rows: 0 = gap, 1 = hunk header, 2.. = lines of the first hunk.
    ctx->ItemClick((body(s) + "/###line_4").c_str());
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick((body(s) + "/###line_6").c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    const std::string expected = "int line4 = 0;\nint line5 = 0;\nint line5 = 1; // changed\n";
    GG_CHECK_STR_EQ(s.session()->diff().selectedText(), expected);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
    GG_CHECK_STR_EQ(s.clipboard(), expected);
    ImGui::SetClipboardText("");
    s.contextMenu((body(s) + "/###line_4").c_str(), "Copy");
    GG_CHECK_STR_EQ(s.clipboard(), expected);
    // Right-click outside the selection selects that line first.
    s.contextMenu((body(s) + "/###line_2").c_str(), "Copy");
    GG_CHECK_STR_EQ(s.clipboard(), "int line2 = 0;\n");
    s.contextMenu((body(s) + "/###line_4").c_str(), "Blame file");
    GG_CHECK(s.waitUntil([&] {
        const auto& b = s.session()->blame().blame();
        return b && b->query.path == "code.cpp" && b->query.commit.hex() == r.change;
    }));
}

} // namespace ggtest
