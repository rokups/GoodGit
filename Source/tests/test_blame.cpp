// Blame panel (§4.6).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/GitRunner.hpp>

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

// Items inside a table are hashed under the table's ID (not its scrolling child window).
std::string table(Scenario&) { return "//Blame/##blame_table"; }
std::string lineRef(Scenario& s, int n) { return table(s) + "/l" + std::to_string(n) + "/###blame_line_" + std::to_string(n); }

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

GG_TEST("blame", "filter, history, tooltips")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    ctx->ItemInputValue("//Blame/##blame_filter", "edited");
    ctx->Yield(2);
    GG_CHECK(s.itemExists(lineRef(s, 3).c_str()));
    GG_CHECK(!s.itemExists(lineRef(s, 2).c_str()));
    ctx->ItemInputValue("//Blame/##blame_filter", "");
    ctx->Yield(2);
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
    ctx->ItemClick("//Blame/###blame_back");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", r.c3); }));
    // Before line 5's change (in c3) is c2's version of story.txt.
    s.contextMenu(lineRef(s, 5).c_str(), "Blame before this change");
    GG_CHECK(s.waitUntil([&] { return blameShows(s, "story.txt", r.c2); }));
    GG_CHECK_STR_EQ(line(s, 5)->text, "L5");
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

GG_TEST("blame", "keyboard only: nav into the lines, select by arrows, Shift range, Alt+Space menu")
{
    const BlameRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    blameFromChanges(s, "Unstaged", "tale.txt");
    GG_REQUIRE(s.waitUntil([&] { return blameShows(s, "tale.txt", ""); }));
    auto& blame = s.session()->blame();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto idOf = [&](int n) { return ctx->ItemInfo(lineRef(s, n).c_str()).ID; };
    auto press = [&](ImGuiKeyChord chord) {
        ctx->KeyPress(chord);
        ctx->Yield(2);
    };
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Blame");
    // Down until the cursor is on the first line (it starts on a button above the table).
    const ImGuiID first = idOf(1);
    GG_REQUIRE(first != 0);
    for (int i = 0; i < 40 && g.NavId != first; ++i)
        press(ImGuiKey_DownArrow);
    GG_REQUIRE(g.NavId == first);
    GG_CHECK_EQ(blame.selectionFirst(), 0);
    GG_CHECK_EQ(blame.selectionLast(), 0);
    // The selection follows the cursor.
    press(ImGuiKey_DownArrow);
    GG_CHECK(g.NavId == idOf(2));
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
    // Alt+Space opens the cursor line's context menu.
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    // The Esc that closed the menu did not clear the selection.
    GG_CHECK_EQ(blame.selectionFirst(), 1);
    GG_CHECK_EQ(blame.selectionLast(), 2);
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
    // Esc right after moving by the arrows (the nav window is the table, not the Blame window itself).
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

} // namespace ggtest
