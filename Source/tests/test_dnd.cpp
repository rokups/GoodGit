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
    // Alt: the tip (c3) rebased onto c1 (with descendants): main = c1, c3.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(r.c[0]), ImGuiMod_Alt);
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c1"}));
    // No modifier: the chooser; Copy after.
    tip = s.revParse(r.path, "main");
    drag(s, rowRef(tip), rowRef(r.c[0]));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//$FOCUSED/Copy after"); }));
    ctx->ItemClick("//$FOCUSED/Copy after");
    GG_CHECK(changedFrom(s, r.path, tip));
    GG_CHECK(subjects(s, r.path) == (std::vector<std::string>{"c3", "c3", "c1"}));
}

GG_TEST("dnd", "a branch badge onto a commit moves the branch")
{
    const Chain r = makeChain(s);
    GG_REQUIRE(s.openRepository(r.path));
    drag(s, "//History/**/" + r.c[1] + "/###badge_side", rowRef(r.c[3]));
    GG_CHECK(s.waitUntil([&] { return s.revParse(r.path, "side") == r.c[3]; }));
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
    choose(s.revParse(r.path, "main"), s.revParse(r.path, "main~2"), "Rebase onto");
    GG_CHECK(subjects(s, r.path).size() == 2u);

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
