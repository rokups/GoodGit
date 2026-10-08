// Drag and drop in History (§4.2).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

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
    GG_CHECK(itemDisabled(s, "###rebase")); // side is not the current branch
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
    // Onto side's badge: side is not the current branch, so Merge is off.
    drag(s, badgeRef(feat, "feat"), badgeRef(r.c[1], "side"));
    GG_REQUIRE(s.waitUntil([&] { return menuOpen(s); }));
    GG_CHECK(itemDisabled(s, "###merge"));
    GG_CHECK(itemDisabled(s, "###rebase"));
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
