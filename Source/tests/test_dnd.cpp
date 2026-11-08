// Drag and drop in History (§4.2).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

// c1 … c4 on main (each adds a file f<i>.txt), "side" at c2.
struct Chain {
    fs::path path;
    std::vector<std::string> c; // c[0] = c1
};

Chain makeChain(Scenario& s, const std::string& name = {})
{
    Chain r;
    r.path = s.fixture(Recipe::Empty, name);
    for (int i = 1; i <= 4; ++i) {
        s.commitFile(r.path, "f" + std::to_string(i) + ".txt", std::to_string(i) + "\n", "c" + std::to_string(i));
        r.c.push_back(s.head(r.path));
        if (i == 2)
            s.git(r.path, {"branch", "side"});
    }
    return r;
}

// A branch "dest" with one own commit on c1: a base that main's commits do not contain.
std::string addDest(Scenario& s, const Chain& r)
{
    s.git(r.path, {"switch", "-q", "-c", "dest", r.c[0]});
    s.commitFile(r.path, "d.txt", "d\n", "dest commit");
    const std::string id = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    return id;
}

std::vector<std::string> subjects(Scenario& s, const fs::path& repo)
{
    std::vector<std::string> out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"log", "--format=%s", "main"})))
        if (!l.empty())
            out.push_back(l);
    return out;
}

bool changedFrom(Scenario& s, const fs::path& repo, const std::string& before)
{
    const bool ok = s.waitUntil([&] { return s.revParse(repo, "main") != before; });
    s.settle();
    return ok;
}

void drag(Scenario& s, const std::string& from, const std::string& to, ImGuiKeyChord mods = 0)
{
    s.waitUntil([&] { return s.itemExists(from.c_str()) && s.itemExists(to.c_str()); });
    if (mods)
        s.ctx->KeyDown(mods);
    s.ctx->ItemDragAndDrop(from.c_str(), to.c_str());
    if (mods)
        s.ctx->KeyUp(mods);
    s.ctx->Yield(2);
}

} // namespace

GG_TEST("dnd", "commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    GG_REQUIRE(s.openRepository(r.path));
    // Shift: c4 after c1.
    std::string tip = s.revParse(r.path, "main");
    drag(s, rowRef(r.c[3]), rowRef(r.c[0]), ImGuiMod_Shift);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c4", "c1"}));
    // Ctrl+Shift: the new tip (c3) before c2 → c1, c4, c3, c2.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(s.revParse(r.path, "main~1")), ImGuiMod_Ctrl | ImGuiMod_Shift);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c2", "c3", "c4", "c1"}));
    // Ctrl: the tip (c2) squashed into c4.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(s.revParse(r.path, "main~2")), ImGuiMod_Ctrl);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path).size() == 3);
    GG_CHECK(s.gitOut(r.path, {"show", "--name-only", "--format=", "main~1"}).find("f2.txt") != std::string::npos);
    // Alt: the tip (c3) is the tip to rebase onto dest; its 2 own commits since c1 both move: main = c1, dest commit, squashed, c3.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(dest), ImGuiMod_Alt);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~2"), dest);
    GG_CHECK(subjects(s, r.path).size() == 4);
    GG_CHECK(subjects(s, r.path).front() == "c3" && subjects(s, r.path)[2] == "dest commit");
    // No modifier: the chooser; Copy after.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(r.c[0]));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Copy after"); }));
    ctx->ItemClick("//$FOCUSED/Copy after");
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path).size() == 5);
    GG_CHECK(subjects(s, r.path).front() == "c3" && subjects(s, r.path)[3] == "c3");
}

namespace {

// A drag onto the top (or bottom) edge of the target row: the gap above (or below) it.
void dragToGap(Scenario& s, const std::string& from, const std::string& to, bool top)
{
    s.waitUntil([&] { return s.itemExists(from.c_str()) && s.itemExists(to.c_str()); });
    const ImRect info = s.ctx->ItemInfo(to.c_str()).RectFull;
    const ImVec2 target(info.GetCenter().x, top ? info.Min.y + 2 : info.Max.y - 2);
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(from.c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    s.ctx->MouseMoveToPos(target);
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
}

} // namespace

GG_TEST("dnd", "commit dropped between rows reorders with no modifier")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    const auto restore = [&] {
        s.git(r.path, {"reset", "-q", "--hard", r.c[3]});
        s.settle();
        s.waitUntil([&] { return s.itemExists(rowRef(r.c[3]).c_str()); });
    };
    // c4 below c2.
    std::string tip = s.revParse(r.path, "main");
    dragToGap(s, rowRef(r.c[3]), rowRef(r.c[1]), false);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c4", "c1"}));
    // c1 above c4 (the new tip).
    restore();
    tip = s.revParse(r.path, "main");
    dragToGap(s, rowRef(r.c[0]), rowRef(r.c[3]), true);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c1", "c4", "c3", "c2"}));
    // c4 below c1 (the last row: the oldest position).
    restore();
    tip = s.revParse(r.path, "main");
    dragToGap(s, rowRef(r.c[3]), rowRef(r.c[0]), false);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c1", "c4"}));
    // c3 below c4: the gap is next to c3, so nothing happens and no chooser opens.
    restore();
    tip = s.revParse(r.path, "main");
    dragToGap(s, rowRef(r.c[2]), rowRef(r.c[3]), false);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    GG_CHECK(!s.itemExists("//$FOCUSED/Copy after"));
    // c3 above c2: the same gap seen from the other row.
    dragToGap(s, rowRef(r.c[2]), rowRef(r.c[1]), true);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    GG_CHECK(!s.itemExists("//$FOCUSED/Copy after"));
    // The border between two rows: the padding between the Selectables is not dead. c4 is moved to the border
    // below c2, between the bottom of the c2 row and the top of the c1 row.
    restore();
    tip = s.revParse(r.path, "main");
    {
        const ImRect upper = s.ctx->ItemInfo(rowRef(r.c[1]).c_str()).RectFull;
        const ImRect lower = s.ctx->ItemInfo(rowRef(r.c[0]).c_str()).RectFull;
        GG_CHECK(lower.Min.y >= upper.Max.y - 0.5f);
        const float y = 0.5f * (upper.Max.y + lower.Min.y);
        const ImVec2 target(upper.GetCenter().x, y);
        s.ctx->MouseMoveToPos(s.ctx->ItemInfo(rowRef(r.c[3]).c_str()).RectFull.GetCenter());
        s.ctx->MouseDown(ImGuiMouseButton_Left);
        s.ctx->Yield(2);
        s.ctx->MouseMoveToPos(target);
        s.ctx->Yield(2);
        s.ctx->MouseUp(ImGuiMouseButton_Left);
        s.ctx->Yield(2);
    }
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c4", "c1"}));
    // Shift does not change a gap drop: c4 below c2 gives the same result.
    restore();
    tip = s.revParse(r.path, "main");
    s.ctx->KeyDown(ImGuiMod_Shift);
    dragToGap(s, rowRef(r.c[3]), rowRef(r.c[1]), false);
    s.ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c4", "c1"}));
}

GG_TEST("dnd", "the drag tooltip states the action of a drop")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    auto& history = s.session()->history();
    const auto id = [&](int i) { return r.c[static_cast<size_t>(i)].substr(0, 7); };
    const std::string tip = s.revParse(r.path, "main");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(r.c[3]).c_str()) && s.itemExists(rowRef(r.c[0]).c_str()); }));
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(rowRef(r.c[3]).c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    // The gap between c2 and c1.
    const ImRect c2 = s.ctx->ItemInfo(rowRef(r.c[1]).c_str()).RectFull;
    s.ctx->MouseMoveToPos(ImVec2(c2.GetCenter().x, c2.Max.y - 2));
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move between " + id(1) + " and " + id(0));
    // The middle of c2: no modifier lists the choices, Ctrl squashes.
    s.ctx->MouseMoveToPos(c2.GetCenter());
    s.ctx->Yield(3);
    GG_CHECK(history.dropHint().rfind("Choose an action for " + id(1), 0) == 0);
    GG_CHECK(history.dropHint().find("Ctrl: squash") != std::string::npos);
    s.ctx->KeyDown(ImGuiMod_Ctrl);
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Squash into " + id(1));
    s.ctx->KeyUp(ImGuiMod_Ctrl);
    // Shift on the middle: Move after.
    s.ctx->KeyDown(ImGuiMod_Shift);
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move after " + id(1));
    s.ctx->KeyDown(ImGuiMod_Ctrl);
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move before " + id(1));
    s.ctx->KeyUp(ImGuiMod_Ctrl);
    s.ctx->KeyUp(ImGuiMod_Shift);
    // The bottom of the last row: the oldest position.
    const ImRect c1 = s.ctx->ItemInfo(rowRef(r.c[0]).c_str()).RectFull;
    s.ctx->MouseMoveToPos(ImVec2(c1.GetCenter().x, c1.Max.y - 2));
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move below " + id(0));
    // The gap next to the dragged row (the top of c3) does nothing: no hint.
    const ImRect c3 = s.ctx->ItemInfo(rowRef(r.c[2]).c_str()).RectFull;
    s.ctx->MouseMoveToPos(ImVec2(c3.GetCenter().x, c3.Min.y + 2));
    s.ctx->Yield(3);
    GG_CHECK(history.dropHint().empty());
    // Escape cancels the drag: nothing changes and the hint clears.
    s.ctx->MouseMoveToPos(c2.GetCenter());
    s.ctx->Yield(3);
    GG_CHECK(!history.dropHint().empty());
    s.ctx->KeyPress(ImGuiKey_Escape);
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    GG_CHECK(history.dropHint().empty());
    GG_CHECK(!s.itemExists("//$FOCUSED/Copy after"));
    // The top of the first row is the new tip (c3 is dragged: c4 is not next to it).
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(rowRef(r.c[2]).c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    const ImRect c4 = s.ctx->ItemInfo(rowRef(r.c[3]).c_str()).RectFull;
    s.ctx->MouseMoveToPos(ImVec2(c4.GetCenter().x, c4.Min.y + 2));
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move above " + id(3) + " (new tip)");
    s.ctx->KeyPress(ImGuiKey_Escape);
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    GG_CHECK(history.dropHint().empty());
}

namespace {

std::string badgeRef(const std::string& commit, const std::string& branch) { return "//History/**/" + commit + "/###badge_" + branch; }

// A drag of a badge onto an exact position (ItemDragAndDrop drops at the centre of the target item).
void dragToPos(Scenario& s, const std::string& from, ImVec2 pos)
{
    s.waitUntil([&] { return s.itemExists(from.c_str()); });
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(from.c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    s.ctx->MouseMoveToPos(pos);
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
}

bool menuOpen(Scenario& s) { return s.itemExists("//$FOCUSED/###move"); }

bool itemDisabled(Scenario& s, const char* item)
{
    return (s.ctx->ItemInfo((std::string("//$FOCUSED/") + item).c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0;
}

} // namespace

GG_TEST("dnd", "a branch badge dropped on the edge of a row opens the branch menu")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    const std::string tip = s.revParse(r.path, "main");
    s.waitUntil([&] { return s.itemExists(rowRef(tip).c_str()); });
    const ImRect rowRect = s.ctx->ItemInfo(rowRef(tip).c_str()).RectFull;
    // 2 px below the top edge of the tip row, right of the badges: the zone of a gap for a commit drop.
    s.waitUntil([&] { return s.itemExists(badgeRef(r.c[1], "side").c_str()); });
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(badgeRef(r.c[1], "side").c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    s.ctx->MouseMoveToPos(ImVec2(rowRect.Max.x - 10.0f, rowRect.Min.y + 2.0f));
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    s.ctx->KeyPress(ImGuiKey_Escape);
    s.ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
}

GG_TEST("dnd", "a branch badge onto a commit opens a menu; Escape changes nothing; Move moves the branch")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    // Onto the row of main's tip: the target is main (the only branch there), the current one.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[3]));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(s.itemExists("//$FOCUSED/###merge"));
    GG_CHECK(s.itemExists("//$FOCUSED/###rebase"));
    GG_CHECK(!itemDisabled(s, "###merge"));
    GG_CHECK(!itemDisabled(s, "###rebase")); // side is not the current branch, and still rebases (it is behind main)
    GG_CHECK(!itemDisabled(s, "###move"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[3]);
    // Onto a commit without a branch: the target is the commit, so Merge is off. Move moves the branch.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[2]));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(itemDisabled(s, "###merge"));
    GG_CHECK(!itemDisabled(s, "###move"));
    ctx->ItemClick("//$FOCUSED/###move");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c[2]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[3]);
}

GG_TEST("dnd", "a branch badge onto a commit with Shift moves the branch; onto its own commit nothing happens")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    // Its own row and its own badge: no menu, no move.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[1]));
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    drag(s, badgeRef(r.c[1], "side"), badgeRef(r.c[1], "side"));
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
    // Shift: the move at once.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[3]), ImGuiMod_Shift);
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c[3]; }));
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
}

GG_TEST("dnd", "the drag tooltip of a branch badge states the action of a drop")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    auto& history = s.session()->history();
    const std::string id = r.c[3].substr(0, 7);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(r.c[1], "side").c_str()) && s.itemExists(rowRef(r.c[3]).c_str()); }));
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(badgeRef(r.c[1], "side").c_str()).RectFull.GetCenter());
    s.ctx->MouseDown(ImGuiMouseButton_Left);
    s.ctx->Yield(2);
    // A row without the branch, right of the badges: no modifier names the menu, Shift names the move.
    const ImRect row = s.ctx->ItemInfo(rowRef(r.c[3]).c_str()).RectFull;
    s.ctx->MouseMoveToPos(row.GetCenter());
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Choose an action for side on " + id + "\nShift: move the branch");
    // The tooltip is the one of a commit drag (the ID of the branch commit) with the pill of the branch.
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", r.c[1].substr(0, 7)));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "side"));
    s.ctx->KeyDown(ImGuiMod_Shift);
    s.ctx->Yield(3);
    GG_CHECK_STR_EQ(history.dropHint(), "Move side to " + id);
    s.ctx->KeyUp(ImGuiMod_Shift);
    // The row of the branch itself: nothing to do, no hint.
    s.ctx->MouseMoveToPos(s.ctx->ItemInfo(rowRef(r.c[1]).c_str()).RectFull.GetCenter());
    s.ctx->Yield(3);
    GG_CHECK(history.dropHint().empty());
    // Escape cancels the drag: no menu, the branch stays and the hint clears.
    s.ctx->MouseMoveToPos(row.GetCenter());
    s.ctx->Yield(3);
    GG_CHECK(!history.dropHint().empty());
    s.ctx->KeyPress(ImGuiKey_Escape);
    s.ctx->Yield(2);
    s.ctx->MouseUp(ImGuiMouseButton_Left);
    s.ctx->Yield(3);
    s.settle();
    GG_CHECK(!menuOpen(s));
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
    GG_CHECK(history.dropHint().empty());
}

GG_TEST("dnd", "a branch badge onto a branch badge: Merge into the current branch, Rebase of the current branch")
{
    const Chain r = makeChain(s);
    // feat: one commit on side's commit (c2); main stays the current branch.
    s.git(r.path, {"checkout", "-q", "-b", "feat", "side"});
    s.commitFile(r.path, "feat.txt", "x\n", "feat1");
    const std::string feat = s.head(r.path);
    s.git(r.path, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(feat, "feat").c_str()); }));
    // Onto side's badge: side is not the current branch, so Merge is off. Rebase is on for any local branch.
    drag(s, badgeRef(feat, "feat"), badgeRef(r.c[1], "side"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(itemDisabled(s, "###merge"));
    GG_CHECK(!itemDisabled(s, "###rebase"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    // Onto main's badge: Merge feat into main.
    const std::string tip = s.revParse(r.path, "main");
    drag(s, badgeRef(feat, "feat"), badgeRef(tip, "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(!itemDisabled(s, "###merge"));
    ctx->ItemClick("//$FOCUSED/###merge");
    GG_REQUIRE(s.waitUntil([&] { return s.dialogOpen("Merge into HEAD"); }));
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main^2"), feat);
}

GG_TEST("dnd", "the target of a branch drop: the badge under the mouse, else the only branch, else the commit")
{
    const Chain r = makeChain(s);
    s.git(r.path, {"branch", "other", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    const std::string tip = r.c[3];
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(tip, "other").c_str()) && s.itemExists(badgeRef(tip, "main").c_str()); }));
    const auto* row = s.session()->history().row(ggui::core::Oid::fromHex(tip));
    GG_REQUIRE(row != nullptr);
    const std::string shortId = row->shortId;
    auto label = [&] { return std::string(ctx->ItemInfo("//$FOCUSED/###merge").DebugLabel); };
    // On the badge of main (the current branch): Merge is on.
    dragToPos(s, badgeRef(r.c[1], "side"), ctx->ItemInfo(badgeRef(tip, "main").c_str()).RectFull.GetCenter());
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(label().find("Merge side into main") == 0);
    GG_CHECK(!itemDisabled(s, "###merge"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    // On the badge of other: Merge is off.
    dragToPos(s, badgeRef(r.c[1], "side"), ctx->ItemInfo(badgeRef(tip, "other").c_str()).RectFull.GetCenter());
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(label().find("Merge side into other") == 0);
    GG_CHECK(itemDisabled(s, "###merge"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    // On the row outside the badges, with two branches there: the target is the commit.
    const ImRect rowRect = ctx->ItemInfo(rowRef(tip).c_str()).RectFull;
    dragToPos(s, badgeRef(r.c[1], "side"), ImVec2(rowRect.Max.x - 10.0f, rowRect.GetCenter().y));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(label().find("Merge side into " + shortId) == 0);
    GG_CHECK(itemDisabled(s, "###merge"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
}

GG_TEST("dnd", "a branch of another worktree moves through the Move branch dialog")
{
    const Chain r = makeChain(s);
    s.git(r.path, {"worktree", "add", "-q", (r.path.parent_path() / "wt_side").string(), "side"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(r.c[1], "side").c_str()); }));
    // The menu: Rebase is off for a branch that another worktree has checked out.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[3]));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(itemDisabled(s, "###rebase"));
    GG_CHECK(!itemDisabled(s, "###move"));
    ctx->MouseMove("//$FOCUSED/###rebase");
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "side is checked out in the worktree"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(!menuOpen(s));
    // Shift: the dialog with the warning, not the move.
    drag(s, badgeRef(r.c[1], "side"), rowRef(r.c[3]), ImGuiMod_Shift);
    GG_REQUIRE(s.waitUntil([&] { return s.dialogOpen("Move branch"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "side"), r.c[1]);
    s.dialogButton("Move branch", "Move");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c[3]; }));
}

GG_TEST("dnd", "a branch badge onto a branch badge: Rebase of the current branch onto the target")
{
    const Chain r = makeChain(s);
    s.git(r.path, {"checkout", "-q", "-b", "feat", "side"});
    s.commitFile(r.path, "feat.txt", "x\n", "feat1");
    const std::string feat = s.head(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(feat, "feat").c_str()); }));
    // feat is the current branch: Rebase is on, Merge is off (main is not the current branch).
    drag(s, badgeRef(feat, "feat"), badgeRef(r.c[3], "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(!itemDisabled(s, "###rebase"));
    GG_CHECK(itemDisabled(s, "###merge"));
    ctx->ItemClick("//$FOCUSED/###rebase");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "feat~1") == r.c[3]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[3]);
}

GG_TEST("dnd", "a branch badge onto a branch badge: Rebase of a branch that is not checked out")
{
    const Chain r = makeChain(s);
    // topic: two own commits on c2, an older commit of main. main stays the current branch.
    s.git(r.path, {"checkout", "-q", "-b", "topic", r.c[1]});
    s.commitFile(r.path, "t1.txt", "1\n", "topic1");
    s.commitFile(r.path, "t2.txt", "2\n", "topic2");
    const std::string topic = s.head(r.path);
    s.git(r.path, {"checkout", "-q", "main"});
    std::ofstream(r.path / "f1.txt") << "changed\n"; // an unstaged change in a tracked file
    const std::string status = s.statusPorcelain(r.path);
    GG_CHECK(!status.empty());
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(topic, "topic").c_str()); }));
    drag(s, badgeRef(topic, "topic"), badgeRef(r.c[3], "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(!itemDisabled(s, "###rebase"));
    ctx->ItemClick("//$FOCUSED/###rebase");
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "topic~2") == r.c[3]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[3]);
    GG_CHECK_STR_EQ(s.head(r.path), r.c[3]);
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(r.path, {"symbolic-ref", "HEAD"})), "refs/heads/main");
    GG_CHECK(s.statusPorcelain(r.path) == status);
}

GG_TEST("dnd", "a branch badge onto a branch badge: Rebase uses the branch when a tag has the same name")
{
    const Chain r = makeChain(s);
    // The tag topic is on c1; the branch topic has two own commits on c2.
    s.git(r.path, {"tag", "topic", r.c[0]});
    s.git(r.path, {"checkout", "-q", "-b", "topic", r.c[1]});
    s.commitFile(r.path, "t1.txt", "1\n", "topic1");
    s.commitFile(r.path, "t2.txt", "2\n", "topic2");
    const std::string topic = s.head(r.path);
    s.git(r.path, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(topic, "topic").c_str()); }));
    drag(s, badgeRef(topic, "topic"), badgeRef(r.c[3], "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(!itemDisabled(s, "###rebase"));
    ctx->ItemClick("//$FOCUSED/###rebase");
    // The short name is ambiguous: full ref names.
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "refs/heads/topic~2") == r.c[3]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "refs/tags/topic"), r.c[0]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "refs/heads/main"), r.c[3]);
}

GG_TEST("dnd", "a branch badge onto a branch badge: Rebase lands on the destination branch when a tag has its name")
{
    const Chain r = makeChain(s);
    // The tag main is on c1; the branch topic has two own commits on c2; the branch main is on c4.
    s.git(r.path, {"tag", "main", r.c[0]});
    s.git(r.path, {"switch", "-q", "-c", "topic", r.c[1]});
    s.commitFile(r.path, "t1.txt", "1\n", "topic1");
    s.commitFile(r.path, "t2.txt", "2\n", "topic2");
    const std::string topic = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(topic, "topic").c_str()); }));
    drag(s, badgeRef(topic, "topic"), badgeRef(r.c[3], "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(!itemDisabled(s, "###rebase"));
    ctx->ItemClick("//$FOCUSED/###rebase");
    // The short name is ambiguous: full ref names.
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "refs/heads/topic~2") == s.revParse(r.path, "refs/heads/main"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "refs/heads/main"), r.c[3]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "refs/tags/main"), r.c[0]);
}

GG_TEST("dnd", "a branch badge onto a branch badge: Merge uses the branch when a tag has the same name")
{
    const Chain r = makeChain(s);
    // The tag feat is on c1; the branch feat has one own commit on c2.
    s.git(r.path, {"tag", "feat", r.c[0]});
    s.git(r.path, {"switch", "-q", "-c", "feat", "side"});
    s.commitFile(r.path, "feat.txt", "x\n", "feat1");
    const std::string feat = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(badgeRef(feat, "feat").c_str()); }));
    drag(s, badgeRef(feat, "feat"), badgeRef(r.c[3], "main"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    ctx->ItemClick("//$FOCUSED/###merge");
    GG_REQUIRE(s.waitUntil([&] { return s.dialogOpen("Merge into HEAD"); }));
    // The native merge hands the operand to git, which prefers a tag to a branch of the same name.
    s.dialogCheck("Merge into HEAD", "native", "Use native git merge (stops with index conflicts)");
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(r.path, {"rev-parse", "-q", "--verify", "HEAD^2"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^1"), r.c[3]);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^2"), feat);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%s"}), "Merge branch 'feat'");
}

GG_TEST("dnd", "files onto a commit: a commit's files into its parent; working tree files into any commit")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    // c4's file onto c3: c3 now adds f3 and f4, c4 is empty.
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(r.c[3])) != nullptr; });
    ctx->ItemClick(rowRef(r.c[3]).c_str());
    s.waitUntil([&] { return s.session()->changes().rows().size() == 1; });
    std::string tip = s.revParse(r.path, "main");
    drag(s, s.child("//Changes", "##files") + "/f4.txt/###file_f4.txt", rowRef(r.c[2]));
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "--name-only", "--format=", "main~1"}), "f3.txt\nf4.txt");
    GG_CHECK(s.gitOut(r.path, {"show", "--name-only", "--format=", "main"}).empty());
    // A working tree change onto c1: folded into it, the working tree stays as it is, clean.
    s.write(r.path, "f1.txt", "1 changed\n");
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((s.child("//Changes", "##files") + "/Unstaged/f1.txt/###file_f1.txt").c_str()); }));
    tip = s.revParse(r.path, "main");
    drag(s, s.child("//Changes", "##files") + "/Unstaged/f1.txt/###file_f1.txt", rowRef(r.c[0]));
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"show", "main~3:f1.txt"}), "1 changed");
    GG_CHECK_STR_EQ(s.read(r.path, "f1.txt"), "1 changed\n");
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("dnd", "the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    GG_REQUIRE(s.openRepository(r.path));
    auto choose = [&](const std::string& from, const std::string& to, const char* item) {
        const std::string tip = s.revParse(r.path, "main");
        drag(s, rowRef(from), rowRef(to));
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists((std::string("//$FOCUSED/") + item).c_str()); }));
        ctx->ItemClick((std::string("//$FOCUSED/") + item).c_str());
        GG_CHECK(changedFrom(s, r.path, tip));
    };
    // Escape closes the chooser: nothing happens. Dropping a commit on itself does nothing either.
    std::string tip = s.revParse(r.path, "main");
    drag(s, rowRef(r.c[3]), rowRef(r.c[0]));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Move before"); }));
    // A click elsewhere (the Branches filter, away from the popup) closes it.
    ctx->MouseMoveToPos(ctx->ItemInfo("//Branches/##branch_filter").RectFull.GetCenter());
    ctx->MouseClick(ImGuiMouseButton_Left);
    ctx->Yield(3);
    drag(s, rowRef(r.c[3]), rowRef(r.c[3]));
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//$FOCUSED/Move before"));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    choose(r.c[3], r.c[1], "Move before");
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c2", "c4", "c1"}));
    choose(s.revParse(r.path, "main"), s.revParse(r.path, "main~3"), "Move after");
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c2", "c4", "c3", "c1"}));
    choose(s.revParse(r.path, "main"), s.revParse(r.path, "main~1"), "Squash into");
    GG_CHECK(subjects(s, r.path).size() == 3u);
    // The tip and its parent, the 2 commits since c1, move onto dest.
    choose(s.revParse(r.path, "main"), dest, "Rebase onto");
    GG_CHECK(subjects(s, r.path).size() == 4u);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~2"), dest);

    // A commit's files: onto its child, onto HEAD further up, and anywhere else (refused).
    const Chain q = makeChain(s, "files");
    GG_REQUIRE(s.openRepository(q.path));
    auto dragFile = [&](const std::string& commit, const std::string& file, const std::string& onto) {
        GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(commit)) != nullptr; }));
        ctx->ItemClick(rowRef(commit).c_str());
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists((s.child("//Changes", "##files") + "/" + file + "/###file_" + file).c_str()); }));
        drag(s, s.child("//Changes", "##files") + "/" + file + "/###file_" + file, rowRef(onto));
    };
    tip = s.revParse(q.path, "main");
    dragFile(q.c[1], "f2.txt", tip); // to HEAD, further up
    GG_CHECK(changedFrom(s, q.path, tip));
    GG_CHECK_STR_EQ(s.gitOut(q.path, {"show", "--name-only", "--format=", "main"}), "f2.txt\nf4.txt");
    tip = s.revParse(q.path, "main");
    dragFile(s.revParse(q.path, "main~1"), "f3.txt", tip); // to its child
    GG_CHECK(changedFrom(s, q.path, tip));
    GG_CHECK_STR_EQ(s.gitOut(q.path, {"show", "--name-only", "--format=", "main"}), "f2.txt\nf3.txt\nf4.txt");
    tip = s.revParse(q.path, "main");
    dragFile(tip, "f4.txt", s.revParse(q.path, "main~3")); // neither parent, child nor HEAD
    GG_CHECK(s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.title == "Move changes")
                return true;
        return false;
    }));
    GG_CHECK_STR_EQ(s.revParse(q.path, "main"), tip);
}

} // namespace ggtest

namespace ggtest {

namespace {

bool isAncestor(Scenario& s, const fs::path& repo, const std::string& ancestor, const std::string& rev)
{
    return s.gitMayFail(repo, {"merge-base", "--is-ancestor", ancestor, rev}).ok();
}

} // namespace

GG_TEST("dnd", "Alt on a commit rebases the tip of its rule: the checked-out branch moves, HEAD stays attached")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    GG_REQUIRE(s.openRepository(r.path));
    const std::string tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(dest), ImGuiMod_Alt);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(!menuOpen(s));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~3"), dest);
    GG_CHECK(subjects(s, r.path).front() == "c4");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"symbolic-ref", "HEAD"}).substr(0, 15), "refs/heads/main");
}

GG_TEST("dnd", "Alt on a tag-only tip commit rebases nothing: the chooser opens with Rebase onto disabled")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    // A commit with only a tag: no branch on it, no child.
    s.git(r.path, {"switch", "-q", "--detach", r.c[3]});
    s.commitFile(r.path, "t.txt", "t\n", "tagged");
    const std::string tagged = s.head(r.path);
    s.git(r.path, {"tag", "t1"});
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tagged).c_str()); }));
    const std::string before = s.gitOut(r.path, {"for-each-ref", "--format=%(refname) %(objectname)"});
    drag(s, rowRef(tagged), rowRef(dest), ImGuiMod_Alt);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Rebase onto"); }));
    // The item is enabled until the engine answers that the tip has no descendant.
    GG_CHECK(s.waitUntil([&] { return itemDisabled(s, "Rebase onto"); }));
    GG_CHECK(!itemDisabled(s, "Copy after"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref", "--format=%(refname) %(objectname)"}), before);
}

GG_TEST("dnd", "the chooser's Rebase onto moves the branch that holds a middle commit")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    GG_REQUIRE(s.openRepository(r.path));
    const std::string tip = s.revParse(r.path, "main");
    drag(s, rowRef(r.c[2]), rowRef(dest)); // c3: not a tip, c4 is its child
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Rebase onto"); }));
    GG_CHECK(!itemDisabled(s, "Rebase onto"));
    ctx->ItemClick("//$FOCUSED/Rebase onto");
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(isAncestor(s, r.path, dest, "main"));
    GG_CHECK(subjects(s, r.path).front() == "c4");
}

GG_TEST("dnd", "Alt on the detached commit with a branch opens the chooser with one Rebase item for each choice")
{
    const Chain r = makeChain(s);
    const std::string dest = addDest(s, r);
    s.git(r.path, {"switch", "-q", "--detach", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    const std::string tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(dest), ImGuiMod_Alt);
    // No immediate rebase: the chooser asks which tip moves.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Rebase onto: main"); }));
    GG_CHECK(s.itemExists("//$FOCUSED/Rebase onto: The detached commit and its descendants"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), tip);
    ctx->ItemClick("//$FOCUSED/Rebase onto: main");
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK_STR_EQ(s.revParse(r.path, "main~3"), dest);
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK_STR_EQ(s.head(r.path), s.revParse(r.path, "main")); // the detached HEAD was on the moved commit: it follows
}

GG_TEST("dnd", "Alt on the tip of a branch that is not checked out moves that branch (fast-forward when behind)")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    // side is at c2, behind main (c4); main stays checked out.
    drag(s, rowRef(r.c[1]), rowRef(r.c[3]), ImGuiMod_Alt);
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c[3]; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c[3]);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"symbolic-ref", "HEAD"}).substr(0, 15), "refs/heads/main");
}

GG_TEST("dnd", "the drag tooltip of a commit shows the pill of the branch an Alt drop moves; a branch drag shows its pill")
{
    const Chain r = makeChain(s);
    s.git(r.path, {"switch", "-q", "--detach", r.c[3]});
    s.commitFile(r.path, "t.txt", "t\n", "tagged");
    const std::string tagged = s.head(r.path);
    s.git(r.path, {"tag", "t1"});
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rowRef(tagged).c_str()); }));
    auto dragging = [&](const std::string& from, ImVec2 off) {
        s.waitUntil([&] { return s.itemExists(from.c_str()); });
        const ImVec2 start = ctx->ItemInfo(from.c_str()).RectFull.GetCenter();
        ctx->MouseMoveToPos(start);
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield(2);
        ctx->MouseMoveToPos(ImVec2(start.x + off.x, start.y + off.y));
        ctx->Yield(3);
    };
    auto release = [&] { // the drop opens a chooser: Escape closes it
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(2);
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(3);
    };
    // The tip of main (HEAD): the pill of main, then the text.
    dragging(rowRef(r.c[3]), ImVec2(0, 60));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "main"));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "c4"));
    release();
    // A commit without a rule branch: the text only.
    dragging(rowRef(r.c[2]), ImVec2(0, 60));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "c3"));
    GG_CHECK(!s.textShown("//##Tooltip_DragDrop_00", "main"));
    release();
    // The branch badge: the pill of the branch, then the text of its commit.
    dragging(badgeRef(r.c[1], "side"), ImVec2(0, 60));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "side"));
    GG_CHECK(s.textShown("//##Tooltip_DragDrop_00", "c2"));
    release();
}

GG_TEST("dnd", "a dragged commit fills the History filter and the Changes \"Compare with\" field")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    auto& history = s.session()->history();
    // History filter: the ID applies as if typed.
    drag(s, rowRef(r.c[1]), "//History/##hist_filter");
    GG_CHECK(s.waitUntil([&] {
        const auto v = history.visibleIds();
        return history.searchActive() && v.size() == 1 && v[0].hex() == r.c[1];
    }));
    ctx->ItemInputValue("//History/##hist_filter", "");
    GG_REQUIRE(s.waitUntil([&] { return history.visibleIds().size() == history.rows().size(); }));
    // Changes "Compare with": applies right away.
    ctx->ItemClick(rowRef(r.c[3]).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Changes/##compare_with"); }));
    drag(s, rowRef(r.c[0]), "//Changes/##compare_with");
    GG_CHECK(s.waitUntil([&] {
        const auto t = s.session()->changes().compareTarget();
        return t.kind == ggui::CompareTarget::Rev && t.rev == r.c[0];
    }));
}

} // namespace ggtest
