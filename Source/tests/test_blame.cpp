// Blame panel (§4.6).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "tests/Harness.hpp"
#include "util/SyntaxHighlight.hpp"

#include <libgg/GitRunner.hpp>

#include <cmath>

namespace ggtest {

namespace {

struct BlameRepo {
    fs::path path;
    std::string c1, c2, c3;
};

BlameRepo makeRepo(Scenario& s)
{
    BlameRepo r;
    r.path = s.fixture(Recipe::Empty, "blame");
    s.commitFile(r.path, "story.txt", "L1\nL2\nL3\nL4\nL5\n", "Write the story");
    r.c1 = s.head(r.path);
    gg::RunRequest req;
    s.write(r.path, "story.txt", "L1\nL2\nL3 edited\nL4\nL5\n");
    s.git(r.path, {"add", "story.txt"});
    s.git(r.path, {"commit", "-q", "--author=Other Author <other@example.com>", "-m", "Edit line 3"});
    r.c2 = s.head(r.path);
    s.git(r.path, {"mv", "story.txt", "tale.txt"});
    s.write(r.path, "tale.txt", "L1\nL2\nL3 edited\nL4\nL5 renamed\n");
    s.git(r.path, {"add", "tale.txt"});
    s.git(r.path, {"commit", "-q", "-m", "Rename and edit line 5"});
    r.c3 = s.head(r.path);
    s.write(r.path, "tale.txt", "L1 uncommitted\nL2\nL3 edited\nL4\nL5 renamed\n");
    return r;
}

// The gutter item of a line: it is in the editor's window. A line scrolled out of view has none (the
// fixtures are short).
std::string lineRef(Scenario& s, int n) { return s.child("//Blame", "##blame_editor") + "/###blame_line_" + std::to_string(n); }

const ggui::core::BlameLine* line(Scenario& s, int n)
{
    const auto& b = s.session()->blame().blame();
    if (!b || n < 1 || static_cast<size_t>(n) > b->lines.size())
        return nullptr;
    return &b->lines[static_cast<size_t>(n - 1)];
}

bool blameShows(Scenario& s, const std::string& path, const std::string& commit)
{
    const auto& b = s.session()->blame().blame();
    return b && b->query.path == path && b->query.commit.hex() == commit;
}

void blameFromChanges(Scenario& s, const char* group, const std::string& path)
{
    std::string ref = s.child("//Changes", "##files");
    if (group)
        ref += std::string("/") + group;
    ref += "/" + path + "/###file_" + path;
    s.contextMenu(ref.c_str(), "Blame file");
    s.showPanel("Blame");
}

} // namespace

GG_TEST("blame", "blame at a commit and on the working tree")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    // Working tree: the edited first line is not committed.
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    GG_CHECK(line(s, 1)->commit.isNull());
    GG_CHECK_STR_EQ(line(s, 1)->text, "L1 uncommitted");
    GG_CHECK_STR_EQ(line(s, 3)->commit.hex(), r.c2);
    GG_CHECK_STR_EQ(line(s, 5)->commit.hex(), r.c3);
    GG_CHECK_STR_EQ(line(s, 2)->commit.hex(), r.c1);
    GG_CHECK(s.itemText(lineRef(s, 1).c_str()).rfind("Not committed", 0) == 0);
    GG_CHECK(s.textShown("//Blame", "Not committed"));
    // The code is the whole file in the text editor, with its line numbers; a .txt file has no language.
    GG_CHECK(s.textShown("//Blame", "L5 renamed"));
    GG_CHECK_STR_EQ(s.session()->blame().languageName(), "None");
    // The cursor the editor starts with is not a selection.
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), -1);
    GG_CHECK(!s.session()->blame().hasSelection());
    // The header names the file and where it is blamed in words, never "@" (UI wording rule).
    GG_CHECK(s.textShown("//Blame", "tale.txt at working tree"));
    GG_CHECK(!s.textShown("//Blame", "@"));
    // Same as git blame --porcelain for the committed version.
    const std::string porcelain = s.git(r.path, {"blame", "--porcelain", "HEAD", "--", "tale.txt"}).out;
    GG_CHECK(porcelain.find(r.c2 + " 3 3") != std::string::npos);
    // At a commit (from History → Changes).
    s.ctx->ItemClick(("//History/**/###row_" + r.c2).c_str());
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, nullptr, "story.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "story.txt", r.c2); }));
    GG_CHECK_STR_EQ(line(s, 3)->commit.hex(), r.c2);
    GG_CHECK_STR_EQ(line(s, 3)->author, "Other Author");
    GG_CHECK_STR_EQ(line(s, 3)->summary, "Edit line 3");
    GG_CHECK_STR_EQ(line(s, 1)->commit.hex(), r.c1);
    GG_CHECK(s.textShown("//Blame", "story.txt at " + r.c2.substr(0, 7)));
}

GG_TEST("blame", "gutter: blocks and width")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    auto& blame = s.session()->blame();
    GG_REQUIRE(s.waitUntil([&] { return blame.gutterWidth() > 0; }));
    // The blocks (uncommitted line 1, c1, c2, c1 again, c3) alternate; lines of one block share the parity.
    for (int i = 0; i < 5; ++i)
        GG_CHECK_EQ(blame.blockParity(i), i % 2);
    GG_CHECK_EQ(blame.blockParity(5), -1);
    // The file ends in a newline: the editor has no empty line after the last one.
    GG_CHECK_EQ(blame.editorLines(), 5);
    const float shortGutter = blame.gutterWidth();
    // Authors longer than the cap make the gutter wider, up to the cap: 40 and 80 characters are cut alike.
    const std::string longName = std::string(40, 'x'), longerName = std::string(80, 'x');
    auto blameWide = [&](const std::string& file, const std::string& author, float& gutter) {
        s.write(r.path, file, "A1\nA2\nA3\n");
        s.git(r.path, {"add", file});
        s.git(r.path, {"commit", "-q", "--author=" + author + " <x@example.com>", "-m", "Add " + file});
        const std::string id = s.head(r.path);
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + id).c_str()); }));
        s.ctx->ItemClick(("//History/**/###row_" + id).c_str());
        GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 1; }));
        blameFromChanges(s, nullptr, file);
        GG_REQUIRE(s.waitUntil([&] { return blameShows(s, file, id); }));
        GG_REQUIRE(s.waitUntil([&] { return blame.blockParity(2) == 0; }));
        s.ctx->Yield(3);
        GG_CHECK_EQ(blame.blockParity(1), 0); // one block
        GG_CHECK_EQ(blame.editorLines(), 3);
        gutter = blame.gutterWidth();
    };
    float longGutter = 0, longerGutter = 0;
    blameWide("wide40.txt", longName, longGutter);
    blameWide("wide80.txt", longerName, longerGutter);
    GG_CHECK(longGutter > shortGutter);
    GG_CHECK(std::abs(longerGutter - longGutter) < 1.0f);
}

GG_TEST("blame", "selection: a selection ending at column 0, a drag over the gutter")
{
    // The last line of gaps.txt is empty, and so is line 2.
    const std::string repo = s.fixture(Recipe::Empty, "blame-gaps");
    s.commitFile(repo, "gaps.txt", "G1\n\nG3\n\n", "Write the gaps");
    const std::string id = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + id).c_str()); }));
    ctx->ItemClick(("//History/**/###row_" + id).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 1; }));
    blameFromChanges(s, nullptr, "gaps.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "gaps.txt", id); }));
    auto& blame = s.session()->blame();
    GG_CHECK_EQ(blame.editorLines(), 4);
    auto press = [&](ImGuiKeyChord chord) {
        ctx->KeyPress(chord);
        ctx->Yield(2);
    };
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Blame");
    for (int presses = 0; presses < 40 && blame.selectionFirst() != 0; ++presses)
        press(ImGuiKey_DownArrow);
    GG_REQUIRE(blame.selectionFirst() == 0);
    // Ctrl+A after Shift+Down presses selects the whole text: the empty last line is in.
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    press(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    GG_CHECK_EQ(blame.cursorLine(), 3);
    // From the top, Shift+Down onto the empty line 2 ends at its start: the line is not included, nor is
    // line 3 at the start of the next step. The step after that is the whole text: the empty last line is.
    press(ImGuiMod_Ctrl | ImGuiKey_Home);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 0);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    // From "G3", Shift+Down ends at the start of the empty last line: not the whole text, so it is left out.
    press(ImGuiMod_Ctrl | ImGuiKey_Home);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    // Ctrl+A selects the whole text: the empty last line is in.
    press(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 3);
}

GG_TEST("blame", "dragging over the gutter selects whole rows")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    auto& session = *s.session();
    session.blameFile("tale.txt", ggui::core::Oid::fromHex(r.c3));
    ctx->Yield(2);
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    auto& blame = session.blame();
    auto infoShows = [&](const std::string& hex) { return session.infoOverride() && session.infoOverride()->id.hex() == hex; };
    // The test engine moves to an item that is not hovered (another is held) only by position.
    auto rowCenter = [&](int n) { return ctx->ItemInfo(lineRef(s, n).c_str()).RectFull.GetCenter(); };
    // Down: from line 2 to line 4, live while the button is held.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->MouseMove(lineRef(s, 2).c_str());
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(rowCenter(4));
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    GG_CHECK(blame.hasSelection());
    ctx->MouseMoveToPos(rowCenter(3));
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); })); // the line under the mouse (3) is c2's
    ctx->MouseMoveToPos(rowCenter(4));
    ctx->MouseUp(0);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    // Up: from line 5 to line 3.
    ctx->MouseMoveToPos(rowCenter(5));
    ctx->MouseDown(0);
    ctx->MouseMoveToPos(rowCenter(3));
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK_EQ(blame.selectionLast(), 4);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    ctx->MouseUp(0);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK_EQ(blame.selectionLast(), 4);
    // Shift+press still extends from the anchor.
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(lineRef(s, 1).c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 4);
}

GG_TEST("blame", "filter, history, tooltips")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    // The filter marks the matching lines (text, author, ID, summary); no line is hidden.
    ctx->ItemInputValue("//Blame/##blame_filter", "edited");
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().matchCount(), 1);
    GG_CHECK(s.itemExists(lineRef(s, 3).c_str()));
    GG_CHECK(s.itemExists(lineRef(s, 2).c_str()));
    ctx->ItemInputValue("//Blame/##blame_filter", "other author");
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().matchCount(), 1);
    ctx->ItemInputValue("//Blame/##blame_filter", "L");
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().matchCount(), 5);
    // Typing selects nothing; the Enter that ends ItemInputValue() steps to the first match.
    GG_CHECK_EQ(s.session()->blame().matchPos(), 0);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 0);
    ctx->ItemInputValue("//Blame/##blame_filter", "");
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().matchCount(), 0);
    // Tooltip with the commit summary.
    ctx->MouseMove(lineRef(s, 3).c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_CHECK(tip != nullptr && tip->Active);
    GG_CHECK(s.idShownDimmed("//##Tooltip_00", r.c2, 7));
    // Blame before this change → a second entry; back/forward buttons and mouse buttons.
    // Line 3 was last changed in c2 (as story.txt): before that is c1's story.txt.
    s.contextMenu(lineRef(s, 3).c_str(), "Blame before this change");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "story.txt", r.c1); }));
    ctx->ItemClick("//Blame/###blame_back");
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    ctx->ItemClick("//Blame/###blame_fwd");
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "story.txt", r.c1); }));
    ctx->MouseMove(lineRef(s, 1).c_str());
    ctx->MouseClick(ImGuiMouseButton(3));
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    ctx->MouseMove(lineRef(s, 1).c_str());
    ctx->MouseClick(ImGuiMouseButton(4));
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "story.txt", r.c1); }));
}

GG_TEST("blame", "line menu: before, originating source, reveal, copy, blocks")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    s.ctx->ItemClick(("//History/**/###row_" + r.c3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, nullptr, "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    // Line 2 came from story.txt in the first commit.
    GG_CHECK_STR_EQ(line(s, 2)->origPath, "story.txt");
    s.contextMenu(lineRef(s, 2).c_str(), "Show originating source");
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "story.txt", r.c1); }));
    GG_CHECK_EQ(s.session()->blame().query()->scrollToLine, 2);
    // The cursor is on that line; it is not a selection.
    GG_CHECK_EQ(s.session()->blame().cursorLine(), 1);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), -1);
    ctx->ItemClick("//Blame/###blame_back");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    // Before line 5's change (in c3) is c2's version of story.txt.
    s.contextMenu(lineRef(s, 5).c_str(), "Blame before this change");
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "story.txt", r.c2); }));
    GG_CHECK_STR_EQ(line(s, 5)->text, "L5");
    GG_CHECK_EQ(s.session()->blame().cursorLine(), 4);
    // Down from the panel's buttons, with nothing selected, selects the line the blame opened on.
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Blame");
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 4);
    GG_CHECK_EQ(s.session()->blame().cursorLine(), 4);
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    // Reveal and copy the commit of line 3.
    s.contextMenu(lineRef(s, 3).c_str(), "###Copy commit ID3");
    GG_CHECK_STR_EQ(s.clipboard(), r.c2.substr(0, 3));
    s.contextMenu(lineRef(s, 3).c_str(), "###Copy commit ID7");
    GG_CHECK_STR_EQ(s.clipboard(), r.c2.substr(0, 7));
    s.contextMenu(lineRef(s, 3).c_str(), "###Copy commit IDfull");
    GG_CHECK_STR_EQ(s.clipboard(), r.c2);
    // The commit column shows the 7-character ID, 3 highlighted and 4 dimmed (c1 is not the header's commit).
    GG_CHECK(s.idShownDimmed("//Blame", r.c1.substr(0, 7), 3));
    s.contextMenu(lineRef(s, 3).c_str(), "Reveal commit");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == r.c2; }));
    // Blocks: lines 1–2 come from c1.
    s.showPanel("Blame");
    s.contextMenu(lineRef(s, 1).c_str(), "Select change block");
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 0);
    GG_CHECK_EQ(s.session()->blame().selectionLast(), 1);
    GG_CHECK_STR_EQ(s.session()->blame().selectedText(), "L1\nL2");
    s.contextMenu(lineRef(s, 2).c_str(), "Copy change block");
    GG_CHECK_STR_EQ(s.clipboard(), "L1\nL2\n");
    // Nothing comes before the first commit, nor before the commit that added a file.
    s.contextMenu(lineRef(s, 1).c_str(), "Blame before this change");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("The commit has no parent") != std::string::npos);
    s.commitFile(r.path, "fresh.txt", "new\n", "Add a fresh file");
    const std::string c4 = s.head(r.path);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + c4).c_str()); }));
    ctx->ItemClick(("//History/**/###row_" + c4).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 1; }));
    blameFromChanges(s, nullptr, "fresh.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "fresh.txt", c4); }));
    s.contextMenu(lineRef(s, 1).c_str(), "Blame before this change");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("The file did not exist before this change") != std::string::npos);
}

GG_TEST("blame", "keyboard: the arrows move the selection from a pressed line, Shift range, Esc of the line menu")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    auto& blame = s.session()->blame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto press = [&](ImGuiKeyChord chord) {
        ctx->KeyPress(chord);
        ctx->Yield(2);
    };
    // A press on a line's gutter selects it and gives the editor the keyboard.
    ctx->ItemClick(lineRef(s, 1).c_str());
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 0);
    GG_CHECK_EQ(blame.cursorLine(), 0);
    GG_CHECK_STR_EQ(blame.selectedText(), "L1 uncommitted");
    // The code cannot be changed: Ctrl+X and Shift+Delete copy the selection and delete nothing.
    for (ImGuiKeyChord cut : {ImGuiMod_Ctrl | ImGuiKey_X, ImGuiMod_Shift | ImGuiKey_Delete}) {
        ImGui::SetClipboardText("");
        press(cut);
        GG_CHECK_STR_EQ(s.clipboard(), "L1 uncommitted");
        GG_CHECK_STR_EQ(blame.selectedText(), "L1 uncommitted");
        GG_CHECK(s.textShown("//Blame", "L1 uncommitted"));
        GG_CHECK_EQ(blame.selectionFirst(), 0);
    }
    // The selection follows the cursor.
    press(ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.cursorLine(), 1);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    // Shift+Down extends the range, Shift+Up shrinks it.
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    press(ImGuiMod_Shift | ImGuiKey_UpArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    // A line's menu opened by the mouse does not move the cursor ...
    ctx->ItemClick(lineRef(s, 5).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    GG_CHECK_EQ(blame.cursorLine(), 2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    // ... and the Esc that closed the menu did not clear the selection.
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    // Esc clears the selection; the cursor and the keyboard stay in the editor.
    press(ImGuiKey_Escape);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_CHECK(!blame.hasSelection());
    GG_CHECK_EQ(blame.cursorLine(), 2);
    press(ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.cursorLine(), 3);
    GG_CHECK_EQ(blame.selectionFirst(), 3);
    // Shift with a press on another line's gutter selects the lines between.
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(lineRef(s, 2).c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    GG_CHECK_EQ(blame.cursorLine(), 1);
}

GG_TEST("blame", "keyboard only: Down into the code, select by arrows, Shift range, Alt+Space menu")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    auto& blame = s.session()->blame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto press = [&](ImGuiKeyChord chord) {
        ctx->KeyPress(chord);
        ctx->Yield(2);
    };
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Blame");
    // The cursor starts on the first line without selecting it; Down hands the keyboard to the code and
    // selects that line.
    int presses = 0;
    for (; presses < 40 && blame.selectionFirst() != 0; ++presses)
        press(ImGuiKey_DownArrow);
    GG_REQUIRE(blame.selectionFirst() == 0);
    GG_CHECK(presses <= 3);
    GG_CHECK_EQ(blame.cursorLine(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 0);
    // From there the arrows are the editor's: the selection follows the cursor.
    press(ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.cursorLine(), 1);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    // Shift+Down extends the range, Shift+Up shrinks it.
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    press(ImGuiMod_Shift | ImGuiKey_DownArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    press(ImGuiMod_Shift | ImGuiKey_UpArrow);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    // Alt+Space opens the cursor line's menu.
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_CHECK(g.OpenPopupStack[0].PopupId == ImHashStr("##blame_menu"));
    GG_CHECK_EQ(blame.cursorLine(), 2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    // The Esc that closed the menu did not clear the selection.
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
    // After Ctrl+A the cursor is on the last line: the menu is its.
    press(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(blame.cursorLine(), 4);
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
}

GG_TEST("blame", "Ctrl+F focuses the filter, not the editor's find window")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick(lineRef(s, 2).c_str());
    ctx->Yield(2);
    GG_REQUIRE(s.session()->blame().selectionFirst() == 1);
    const ImGuiID filter = ctx->ItemInfo("//Blame/##blame_filter").ID;
    GG_REQUIRE(filter != 0);
    GG_CHECK(g.ActiveId != filter);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
    ctx->Yield(3);
    GG_CHECK(g.ActiveId == filter);
    // The editor's find UI is a child window "find-replace" (ImGuiColorTextEdit, TextEditor.cpp).
    bool find = false;
    for (ImGuiWindow* w : g.Windows)
        find = find || std::string(w->Name).find("find-replace") != std::string::npos;
    GG_CHECK(!find);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 1);
    // The field takes the typing, with the selection kept.
    ctx->KeyChars("edited");
    ctx->Yield(2);
    GG_CHECK_EQ(s.session()->blame().matchCount(), 1);
    GG_CHECK_EQ(s.session()->blame().selectionFirst(), 1);
}

GG_TEST("blame", "Enter in the filter, F3 and Shift+F3 step through the matches")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    auto& session = *s.session();
    auto& blame = session.blame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto press = [&](ImGuiKeyChord chord) {
        ctx->KeyPress(chord);
        ctx->Yield(2);
    };
    // Nothing matches: "No matches", no stepping.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick("//Blame/##blame_filter");
    ctx->KeyChars("zzz");
    ctx->Yield(2);
    GG_CHECK_EQ(blame.matchCount(), 0);
    GG_CHECK_EQ(blame.matchPos(), -1);
    GG_CHECK(s.textShown("//Blame", "No matches"));
    press(ImGuiKey_Enter);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    // Five lines match "L" (the text of every line); the position is the first one, not visited yet.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyChars("L");
    ctx->Yield(2);
    GG_REQUIRE(blame.matchCount() == 5);
    GG_CHECK_EQ(blame.matchPos(), 0);
    GG_CHECK(s.textShown("//Blame", "1 of 5"));
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    // Enter goes to the first match and stays in the filter, so it can be pressed again.
    const ImGuiID filter = ctx->ItemInfo("//Blame/##blame_filter").ID;
    press(ImGuiKey_Enter);
    GG_CHECK_EQ(blame.matchPos(), 0);
    GG_CHECK_EQ(blame.cursorLine(), 0);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK(session.infoOverride() && session.infoOverride()->kind == ggui::SelKind::WorkingTree);
    GG_CHECK(g.ActiveId == filter);
    press(ImGuiKey_Enter);
    GG_CHECK_EQ(blame.matchPos(), 1);
    GG_CHECK_EQ(blame.cursorLine(), 1);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    GG_CHECK(g.ActiveId == filter);
    GG_CHECK(s.textShown("//Blame", "2 of 5"));
    // F3 and Shift+F3 step forward and back, and give the code the keyboard.
    press(ImGuiKey_F3);
    GG_CHECK_EQ(blame.matchPos(), 2);
    GG_CHECK_EQ(blame.cursorLine(), 2);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK(session.infoOverride() && session.infoOverride()->id.hex() == r.c2);
    GG_CHECK(g.ActiveId != filter);
    press(ImGuiMod_Shift | ImGuiKey_F3);
    GG_CHECK_EQ(blame.matchPos(), 1);
    GG_CHECK_EQ(blame.cursorLine(), 1);
    press(ImGuiMod_Shift | ImGuiKey_F3);
    press(ImGuiMod_Shift | ImGuiKey_F3);
    GG_CHECK_EQ(blame.matchPos(), 4); // wrapped
    GG_CHECK_EQ(blame.cursorLine(), 4);
    press(ImGuiKey_F3);
    GG_CHECK_EQ(blame.matchPos(), 0); // wrapped
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    // A match is the selection: Esc clears it, F3 selects the next one again.
    press(ImGuiKey_Escape);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    press(ImGuiKey_F3);
    GG_CHECK_EQ(blame.matchPos(), 1);
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    ctx->Yield(2);
    GG_CHECK(s.textShown("//Blame", "2 of 5"));
}

GG_TEST("blame", "the selected line's change is shown in Change information; Esc and closing give it back")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + r.c1).c_str()); }));
    auto& session = *s.session();
    auto& info = session.info();
    auto histRow = [&](const std::string& hex) { return "//History/**/###row_" + hex; };
    auto infoShows = [&](const std::string& hex) { return info.details() && info.details()->id.hex() == hex; };
    ctx->ItemClick(histRow(r.c1).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c1); }));
    const ggui::Selection history = session.selection();
    session.blameFile("tale.txt", ggui::core::Oid::fromHex(r.c3));
    ctx->Yield(2); // the Blame window exists from the next frame
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    auto& blame = session.blame();
    GG_CHECK(!session.infoOverride());
    // A click on a line of another commit: its change is shown, the history selection is untouched.
    ctx->ItemClick(lineRef(s, 3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    GG_CHECK(session.infoOverride() && session.infoOverride()->id.hex() == r.c2);
    GG_CHECK(session.selection() == history);
    // The arrows move to a line of the next commit: it follows.
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiKey_DownArrow);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c3); }));
    GG_CHECK_EQ(blame.selectionFirst(), 4);
    GG_CHECK(session.selection() == history);
    // A history selection made meanwhile does not change what is shown ...
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick(histRow(r.c2).c_str());
    GG_REQUIRE(s.waitUntil([&] { return session.selection().id.hex() == r.c2; }));
    ctx->Yield(5);
    GG_CHECK(infoShows(r.c3));
    // ... and Esc in the Blame window clears the selection: Change information shows it.
    ctx->WindowFocus("//Blame");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_CHECK(!session.infoOverride());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    // Selecting again, then closing the panel drops the override.
    ctx->ItemClick(lineRef(s, 1).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c1); }));
    ctx->ItemClick(lineRef(s, 5).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c3); }));
    s.app.settings().data().panels["Blame"] = false;
    ctx->Yield(3);
    GG_CHECK(!session.infoOverride());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    // The working tree blame: an uncommitted line shows the working tree form.
    session.blameFile("tale.txt", ggui::core::Oid());
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    ctx->ItemClick(lineRef(s, 1).c_str());
    GG_REQUIRE(s.waitUntil([&] { return session.infoOverride().has_value(); }));
    GG_CHECK(info.selection().kind == ggui::SelKind::WorkingTree);
    ctx->ItemClick(lineRef(s, 3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    GG_CHECK(info.selection().kind == ggui::SelKind::Commit);
}

GG_TEST("blame", "Change information and the blame selection: Esc cases, Shift range, parent click, reload, filter")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + r.c1).c_str()); }));
    auto& session = *s.session();
    auto& info = session.info();
    auto& blame = session.blame();
    auto infoShows = [&](const std::string& hex) { return info.details() && info.details()->id.hex() == hex; };
    ctx->ItemClick(("//History/**/###row_" + r.c1).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c1); }));
    // The blame at c3: lines 1, 2, 4 are c1's, line 3 is c2's, line 5 is c3's.
    session.blameFile("tale.txt", ggui::core::Oid::fromHex(r.c3));
    ctx->Yield(2); // the Blame window exists from the next frame
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    // Esc without a selection: nothing changes.
    ctx->WindowFocus("//Blame");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!session.infoOverride());
    GG_CHECK(infoShows(r.c1));
    // Esc right after moving by the arrows (the keyboard is in the editor, not in the Blame window itself).
    ctx->ItemClick(lineRef(s, 3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiKey_DownArrow);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c3); }));
    GG_CHECK_EQ(blame.selectionFirst(), 4);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_CHECK(!session.infoOverride());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c1); }));
    // A Shift range shows the line pressed last, not the anchor's.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick(lineRef(s, 3).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_DownArrow);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK_EQ(blame.selectionLast(), 4);
    GG_CHECK(session.infoOverride() && session.infoOverride()->id.hex() == r.c3);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c3); }));
    // Esc in the filter field does not clear the selection.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick("//Blame/##blame_filter");
    ctx->KeyChars("L");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK(session.infoOverride().has_value());
    ctx->ItemInputValue("//Blame/##blame_filter", "");
    ctx->Yield(2);
    // A parent clicked in Change information ends the blame selection and shows the parent.
    ctx->ItemClick("//Change information/**/###parent_0");
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_CHECK(!session.infoOverride());
    GG_CHECK_STR_EQ(session.selection().id.hex(), r.c2);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
    // Another blame loaded with a line selected: the selection and the override are gone.
    ctx->ItemClick(lineRef(s, 5).c_str());
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c3); }));
    session.blameFile("tale.txt", ggui::core::Oid::fromHex(r.c3));
    ctx->Yield(3);
    GG_CHECK(!session.infoOverride());
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_REQUIRE(s.waitUntil([&] { return infoShows(r.c2); }));
}

namespace {

// code.cpp: c1 wrote its lines 1-2, c2 lines 3-4 (two blocks of two lines).
struct CodeRepo {
    fs::path path;
    std::string c1, c2;
};

CodeRepo makeCodeRepo(Scenario& s)
{
    CodeRepo r;
    r.path = s.fixture(Recipe::Empty, "blame-code");
    s.commitFile(r.path, "code.cpp", "int first = 1;\nint second = 2;\n", "Write two lines");
    r.c1 = s.head(r.path);
    s.commitFile(r.path, "code.cpp", "int first = 1;\nint second = 2;\nint third = 3;\nint fourth = 4;\n", "Write two more");
    r.c2 = s.head(r.path);
    return r;
}

// Opens the blame of `path` at `commit` in the Blame window and waits until it is drawn.
bool openBlame(Scenario& s, const std::string& path, const std::string& commit)
{
    s.session()->blameFile(path, ggui::core::Oid::fromHex(commit));
    s.ctx->Yield(2);
    s.showPanel("Blame");
    if (!s.waitUntil([&] { return blameShows(s, path, commit); }))
        return false;
    s.ctx->Yield(3);
    return true;
}

bool historyShows(Scenario& s, const std::string& commit)
{
    return s.waitUntil([&] { return s.itemExists(("//History/**/###row_" + commit).c_str()); });
}

// The mouse position over a gutter row's code text, `dx` pixels right of the gutter, and over its line number.
ImVec2 textPos(Scenario& s, int n, float dx)
{
    const ImRect r = s.ctx->ItemInfo(lineRef(s, n).c_str()).RectFull;
    return ImVec2(r.Max.x + dx, r.GetCenter().y);
}
// The width of a glyph of the editor's monospace font.
float glyphWidth()
{
    ImFontBaked* baked = ggui::theme().monoFont()->GetFontBaked(ImGui::GetStyle().FontSizeBase * ImGui::GetStyle().FontScaleMain);
    return baked->GetCharAdvance('0');
}
ImVec2 numberPos(Scenario& s, int n)
{
    const ImRect r = s.ctx->ItemInfo(lineRef(s, n).c_str()).RectFull;
    return ImVec2(r.Min.x - 1.5f * glyphWidth(), r.GetCenter().y);
}

// The text editor has its own click timer: a left press within the double click time of the previous one is a
// double or triple click there. Every left press in these tests waits this out first.
void waitOutDoubleClick(Scenario& s) { s.ctx->SleepNoSkip(2.0f * ImGui::GetIO().MouseDoubleClickTime, 0.1f); }

// Drags with the left button from one position to another.
void drag(Scenario& s, ImVec2 from, ImVec2 to)
{
    waitOutDoubleClick(s);
    s.ctx->MouseMoveToPos(from);
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    s.ctx->MouseMoveToPos(to);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
}

void rightClickAt(Scenario& s, ImVec2 pos)
{
    s.ctx->MouseMoveToPos(pos);
    s.ctx->MouseClick(ImGuiMouseButton_Right);
    s.ctx->Yield(3);
}

// Puts the theme back when a test ends, however it ends.
struct ThemeGuard {
    ggui::Theme theme = ggui::theme().theme();
    float scale = ggui::theme().scale();
    ~ThemeGuard() { ggui::theme().apply(theme, scale); }
};

} // namespace

GG_TEST("blame", "text: a drag selects, Ctrl+C and Copy put it on the clipboard, Select all, the text cannot be changed")
{
    const CodeRepo r = makeCodeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(historyShows(s, r.c2));
    GG_REQUIRE(openBlame(s, "code.cpp", r.c2));
    auto& blame = s.session()->blame();
    const std::string whole = "int first = 1;\nint second = 2;\nint third = 3;\nint fourth = 4;";
    GG_CHECK_STR_EQ(blame.text(), whole);
    GG_CHECK_STR_EQ(blame.languageName(), "C++");
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    // A drag over the code of one line, from its start past its end.
    drag(s, textPos(s, 1, 10.0f), textPos(s, 1, 20.0f * glyphWidth()));
    GG_CHECK(blame.hasSelection());
    GG_CHECK_STR_EQ(blame.selectedText(), "int first = 1;");
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 0);
    // A drag that ends inside a line selects the start of it only.
    drag(s, textPos(s, 1, 10.0f), textPos(s, 1, 10.0f + 4.0f * glyphWidth()));
    const std::string part = blame.selectedText();
    GG_CHECK(!part.empty());
    GG_CHECK(part.size() < std::string("int first = 1;").size());
    GG_CHECK(std::string("int first = 1;").rfind(part, 0) == 0);
    // Across two lines.
    drag(s, textPos(s, 1, 10.0f), textPos(s, 2, 20.0f * glyphWidth()));
    GG_CHECK_STR_EQ(blame.selectedText(), "int first = 1;\nint second = 2;");
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 1);
    // Ctrl+C.
    ImGui::SetClipboardText("x");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.clipboard(), "int first = 1;\nint second = 2;");
    // The text menu's Copy does the same, and Select all selects the whole text.
    ImGui::SetClipboardText("x");
    rightClickAt(s, textPos(s, 2, 20.0f));
    ctx->MenuClick("//$FOCUSED/Copy");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.clipboard(), "int first = 1;\nint second = 2;");
    rightClickAt(s, textPos(s, 2, 20.0f));
    ctx->MenuClick("//$FOCUSED/Select all");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(blame.selectedText(), whole);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    // The code cannot be edited: typing, paste, Delete, Backspace, Enter and Tab change nothing.
    waitOutDoubleClick(s);
    ctx->MouseMoveToPos(textPos(s, 3, 20.0f));
    ctx->MouseClick(ImGuiMouseButton_Left);
    ctx->Yield(2);
    GG_REQUIRE(blame.cursorLine() == 2);
    ImGui::SetClipboardText("pasted");
    ctx->KeyChars("x");
    for (ImGuiKeyChord key : {ImGuiKeyChord(ImGuiMod_Ctrl | ImGuiKey_V), ImGuiKeyChord(ImGuiKey_Delete), ImGuiKeyChord(ImGuiKey_Backspace),
             ImGuiKeyChord(ImGuiKey_Enter), ImGuiKeyChord(ImGuiKey_Tab)}) {
        ctx->KeyPress(key);
        ctx->Yield(2);
    }
    ctx->KeyChars("y");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(blame.text(), whole);
    GG_CHECK_EQ(blame.editorLines(), 4);
    GG_CHECK_EQ(blame.cursorLine(), 2);
}

GG_TEST("blame", "text menu and line number menu: the line items act on the line hit, the cursor stays")
{
    const CodeRepo r = makeCodeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(historyShows(s, r.c2));
    GG_REQUIRE(openBlame(s, "code.cpp", r.c2));
    auto& blame = s.session()->blame();
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->MouseMoveToPos(textPos(s, 1, 20.0f));
    ctx->MouseClick(ImGuiMouseButton_Left);
    ctx->Yield(2);
    GG_REQUIRE(blame.cursorLine() == 0);
    // The text menu on line 4 (the second line of c2's block): Copy and Select all, then the line items.
    rightClickAt(s, textPos(s, 4, 20.0f));
    GG_CHECK_EQ(blame.cursorLine(), 0);
    for (const char* item : {"Copy", "Select all", "Blame before this change", "Show originating source", "Reveal commit",
             "###Copy commit ID7", "Select change block", "Copy change block"})
        GG_CHECK(s.itemExists((std::string("//$FOCUSED/") + item).c_str()));
    ctx->MenuClick("//$FOCUSED/Select change block");
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    GG_CHECK_EQ(blame.selectionLast(), 3);
    GG_CHECK_STR_EQ(blame.selectedText(), "int third = 3;\nint fourth = 4;");
    // Copy change block on line 1 of c1's block, from the text menu; the selection and the cursor are not touched.
    const int cursor = blame.cursorLine();
    rightClickAt(s, textPos(s, 2, 20.0f));
    GG_CHECK_EQ(blame.cursorLine(), cursor);
    ctx->MenuClick("//$FOCUSED/Copy change block");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.clipboard(), "int first = 1;\nint second = 2;\n");
    GG_CHECK_EQ(blame.cursorLine(), cursor);
    GG_CHECK_EQ(blame.selectionFirst(), 2);
    // The line number menu has the line items (no Copy / Select all): the commit of the line hit.
    rightClickAt(s, numberPos(s, 1));
    GG_REQUIRE(s.itemExists("//$FOCUSED/Copy change block")); // the menu is open
    GG_CHECK(!s.itemExists("//$FOCUSED/Select all"));
    GG_CHECK_EQ(blame.cursorLine(), cursor);
    ctx->MenuClick("//$FOCUSED/###Copy commit ID7");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.clipboard(), r.c1.substr(0, 7));
    rightClickAt(s, numberPos(s, 3));
    ctx->MenuClick("//$FOCUSED/###Copy commit ID7");
    ctx->Yield(2);
    GG_CHECK_STR_EQ(s.clipboard(), r.c2.substr(0, 7));
    GG_CHECK_EQ(blame.cursorLine(), cursor);
    rightClickAt(s, numberPos(s, 2));
    ctx->MenuClick("//$FOCUSED/Select change block");
    ctx->Yield(2);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 1);
}

GG_TEST("blame", "the language follows the file name")
{
    const std::string repo = s.fixture(Recipe::Empty, "blame-lang");
    s.commitFile(repo, "code.cpp", "int main() { return 0; }\n", "Add C++");
    const std::string cpp = s.head(repo);
    s.commitFile(repo, "tool.py", "def run():\n    return 1\n", "Add Python");
    const std::string py = s.head(repo);
    s.commitFile(repo, "notes.txt", "just words\n", "Add notes");
    const std::string txt = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(historyShows(s, txt));
    auto& blame = s.session()->blame();
    GG_REQUIRE(openBlame(s, "code.cpp", cpp));
    GG_CHECK_STR_EQ(blame.languageName(), "C++");
    GG_REQUIRE(openBlame(s, "tool.py", py));
    GG_CHECK_STR_EQ(blame.languageName(), "Python");
    GG_REQUIRE(openBlame(s, "notes.txt", txt));
    GG_CHECK_STR_EQ(blame.languageName(), "None");
    GG_REQUIRE(openBlame(s, "code.cpp", txt));
    GG_CHECK_STR_EQ(blame.languageName(), "C++");
}

GG_TEST("blame", "theme switch: the editor's palette, the filter marks and the gutter follow")
{
    const CodeRepo r = makeCodeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(historyShows(s, r.c2));
    GG_REQUIRE(openBlame(s, "code.cpp", r.c2));
    auto& blame = s.session()->blame();
    ThemeGuard guard;
    ggui::theme().apply(ggui::Theme::Dark, guard.scale);
    ctx->Yield(3);
    const auto dark = ggui::editorPalette();
    GG_CHECK(blame.usesThemePalette());
    // "int" is in all four lines; two steps put the position on the second match.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick("//Blame/##blame_filter");
    ctx->KeyChars("int");
    ctx->Yield(3);
    GG_REQUIRE(blame.matchCount() == 4);
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    for (int i = 0; i < 2; ++i) {
        ctx->KeyPress(ImGuiKey_F3);
        ctx->Yield(2);
    }
    GG_CHECK_EQ(blame.matchPos(), 1);
    GG_CHECK(s.textShown("//Blame", "2 of 4"));
    ggui::theme().apply(ggui::Theme::Light, guard.scale);
    ctx->Yield(4);
    GG_CHECK(ggui::editorPalette() != dark);
    GG_CHECK(blame.usesThemePalette());
    GG_CHECK_EQ(blame.matchCount(), 4);
    GG_CHECK_EQ(blame.matchPos(), 1);
    GG_CHECK(s.textShown("//Blame", "2 of 4"));
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    for (int n = 1; n <= 4; ++n)
        GG_CHECK(s.itemExists(lineRef(s, n).c_str()));
    ggui::theme().apply(ggui::Theme::Dark, guard.scale);
    ctx->Yield(4);
    GG_CHECK(blame.usesThemePalette());
    GG_CHECK(ggui::editorPalette() == dark);
    GG_CHECK_EQ(blame.matchPos(), 1);
}

GG_TEST("blame", "a long file: open at a line, Down, Alt+Space and F3 bring lines into view, a drag past the edges")
{
    // 200 lines of long.txt in four commits: c1 wrote 1-100, c2 101-150, c3 151-200, c4 changed line 50.
    const std::string repo = s.fixture(Recipe::Empty, "blame-long");
    auto content = [](int count, bool edited) {
        std::string text;
        for (int i = 1; i <= count; ++i)
            text += i == 50 && edited ? "row 50 edited\n" : i == 190 ? "row 190 marker\n" : "row " + std::to_string(i) + "\n";
        return text;
    };
    s.commitFile(repo, "long.txt", content(100, false), "Write 100 rows");
    s.commitFile(repo, "long.txt", content(150, false), "Write 50 more rows");
    s.commitFile(repo, "long.txt", content(200, false), "Write the last 50 rows");
    s.commitFile(repo, "long.txt", content(200, true), "Edit row 50");
    const std::string head = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(historyShows(s, head));
    auto& session = *s.session();
    auto& blame = session.blame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // Opened at line 150 (as "Show originating source" does): the cursor is there, in view, nothing selected.
    blame.open("long.txt", ggui::core::Oid::fromHex(head), false, 150);
    ctx->Yield(2);
    s.showPanel("Blame");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "long.txt", head); }));
    ctx->Yield(4);
    GG_CHECK_EQ(blame.editorLines(), 200);
    GG_CHECK_EQ(blame.cursorLine(), 149);
    GG_CHECK_EQ(blame.selectionFirst(), -1);
    GG_CHECK(blame.firstVisibleLine() > 0);
    GG_CHECK(blame.firstVisibleLine() <= 149 && blame.lastVisibleLine() >= 149);
    GG_CHECK(s.itemExists(lineRef(s, 150).c_str()));
    GG_CHECK(!s.itemExists(lineRef(s, 1).c_str())); // out of view: not drawn
    // Down from the toolbar selects that line and does not scroll.
    const int first = blame.firstVisibleLine();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Blame");
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 149);
    GG_CHECK_EQ(blame.cursorLine(), 149);
    GG_CHECK_EQ(blame.firstVisibleLine(), first);
    // Scroll the cursor line out of view with the wheel (positive: up, negative: down).
    auto scrollTo = [&](bool top) {
        ctx->SetInputMode(ImGuiInputSource_Mouse);
        ctx->MouseMoveToPos(textPos(s, blame.firstVisibleLine() + 3, 40.0f));
        for (int i = 0; i < 30 && (top ? blame.firstVisibleLine() > 0 : blame.lastVisibleLine() < 199); ++i) {
            ctx->MouseWheelY(top ? 20.0f : -20.0f);
            ctx->Yield(2);
        }
        ctx->SetInputMode(ImGuiInputSource_Keyboard);
    };
    // Down with the cursor line out of view scrolls it into view: the Blame window has the keyboard (not the
    // editor), nothing is selected after Esc, then the cursor line is scrolled out.
    ctx->WindowFocus("//Blame");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_REQUIRE(blame.selectionFirst() == -1);
    scrollTo(true);
    GG_REQUIRE(blame.firstVisibleLine() == 0);
    GG_REQUIRE(blame.lastVisibleLine() < 149);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(5);
    GG_CHECK_EQ(blame.selectionFirst(), 149);
    GG_CHECK(blame.firstVisibleLine() <= 149 && blame.lastVisibleLine() >= 149);
    // Scrolled out again, Alt+Space brings the line back too.
    scrollTo(true);
    GG_REQUIRE(blame.firstVisibleLine() == 0);
    GG_CHECK(blame.lastVisibleLine() < 149);
    GG_CHECK(!s.itemExists(lineRef(s, 150).c_str()));
    GG_CHECK_EQ(blame.cursorLine(), 149);
    // Alt+Space brings the cursor line back into view and opens its menu.
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(5);
    GG_CHECK(blame.firstVisibleLine() <= 149 && blame.lastVisibleLine() >= 149);
    GG_CHECK(s.itemExists(lineRef(s, 150).c_str()));
    GG_REQUIRE(g.OpenPopupStack.Size == 1);
    GG_CHECK(g.OpenPopupStack[0].PopupId == ImHashStr("##blame_menu"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    GG_CHECK_EQ(blame.selectionFirst(), 149);
    // The filter brings the match (line 190) into view; scrolled away again, F3 brings it back.
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    ctx->ItemClick("//Blame/##blame_filter");
    ctx->KeyChars("marker");
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->Yield(4);
    GG_REQUIRE(blame.matchCount() == 1);
    GG_CHECK(blame.lastVisibleLine() >= 189);
    GG_CHECK_EQ(blame.selectionFirst(), 149); // the filter does not select
    scrollTo(true);
    GG_REQUIRE(blame.firstVisibleLine() == 0);
    GG_CHECK(!s.itemExists(lineRef(s, 190).c_str()));
    ctx->KeyPress(ImGuiKey_F3);
    ctx->Yield(5);
    GG_CHECK_EQ(blame.matchPos(), 0);
    GG_CHECK_EQ(blame.cursorLine(), 189);
    GG_CHECK_EQ(blame.selectionFirst(), 189);
    GG_CHECK(blame.firstVisibleLine() <= 189 && blame.lastVisibleLine() >= 189);
    GG_CHECK(s.itemExists(lineRef(s, 190).c_str()));
    // A drag from a visible row with the mouse held below / above the editor stops at the last / first row in view.
    scrollTo(true);
    GG_REQUIRE(blame.firstVisibleLine() == 0);
    const int last = blame.lastVisibleLine();
    GG_REQUIRE(last < 199);
    const ImGuiWindow* editor = ctx->WindowInfo(s.child("//Blame", "##blame_editor").c_str()).Window;
    GG_REQUIRE(editor != nullptr);
    ctx->SetInputMode(ImGuiInputSource_Mouse);
    const ImVec2 press = ctx->ItemInfo(lineRef(s, 6).c_str()).RectFull.GetCenter();
    ctx->MouseMoveToPos(press);
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->MouseMoveToPos(ImVec2(press.x, editor->Rect().Max.y + 60.0f));
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), 5);
    GG_CHECK_EQ(blame.selectionLast(), blame.lastVisibleLine());
    GG_CHECK(blame.selectionLast() < 199);
    ctx->MouseMoveToPos(ImVec2(press.x, editor->Rect().Min.y - 40.0f));
    ctx->Yield(3);
    GG_CHECK_EQ(blame.selectionFirst(), blame.firstVisibleLine());
    GG_CHECK_EQ(blame.selectionLast(), 5);
    ctx->MouseUp(ImGuiMouseButton_Left);
    ctx->Yield(3);
}

} // namespace ggtest
