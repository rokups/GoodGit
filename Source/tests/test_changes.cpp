// Changes panel, Change information panel and patches (§4.4, §4.11).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <imgui_internal.h>

#include <algorithm>

namespace ggtest {

namespace {

using ggui::FileGroup;

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    std::string ref = s.child("//Changes", "##files");
    if (group)
        ref += std::string("/") + group;
    return ref + "/" + Scenario::escapeRef(path) + "/###file_" + Scenario::escapeRef(path);
}

std::vector<std::string> paths(Scenario& s, FileGroup g)
{
    std::vector<std::string> out;
    for (const auto& r : s.session()->changes().rows())
        if (r.group == g)
            out.push_back(r.path);
    return out;
}

const ggui::FileRow* rowFor(Scenario& s, const std::string& path)
{
    for (const auto& r : s.session()->changes().rows())
        if (r.path == path)
            return &r;
    return nullptr;
}

// The name of the window that has the keyboard focus (its root window; empty when none).
std::string focusedWindow()
{
    const ImGuiWindow* w = ImGui::GetCurrentContext()->NavWindow;
    return w ? w->RootWindow->Name : std::string();
}

void selectCommit(Scenario& s, const std::string& hex)
{
    s.ctx->ItemClick(("//History/**/###row_" + hex).c_str());
    s.waitUntil([&] { return !s.session()->changes().rows().empty(); });
}

} // namespace

GG_TEST("changes", "working tree groups: staged, unstaged, untracked, conflicted")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::Staged) == (V{"a.txt", "c.txt", "e.txt"}));
    GG_CHECK(paths(s, FileGroup::Unstaged) == (V{"b.txt", "c.txt", "n.txt"}));
    GG_CHECK(paths(s, FileGroup::Untracked) == (V{"u.txt"}));
    // Same grouping as git status --porcelain=v2.
    const std::string porcelain = s.statusPorcelain(repo);
    GG_CHECK(porcelain.find("1 M. ") != std::string::npos);
    GG_CHECK(porcelain.find("? u.txt") != std::string::npos);
    const auto* renamed = rowFor(s, "e.txt");
    GG_REQUIRE(renamed != nullptr);
    GG_CHECK(renamed->kind == ggui::core::ChangeKind::Renamed);
    GG_CHECK_STR_EQ(renamed->oldPath, "d.txt");
    const auto* ita = rowFor(s, "n.txt");
    GG_REQUIRE(ita != nullptr);
    GG_CHECK(ita->intentToAdd);
    GG_CHECK(s.itemText(fileRef(s, "Staged", "e.txt").c_str()).rfind("R  d.txt", 0) == 0);
    GG_CHECK(s.itemText(fileRef(s, "Untracked", "u.txt").c_str()).rfind("?  u.txt", 0) == 0);
    GG_CHECK(s.itemExists((s.child("//Changes", "##files") + "/Staged/###group").c_str()));

    const fs::path merge = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(merge));
    GG_REQUIRE(s.waitUntil([&] { return !paths(s, FileGroup::Conflicted).empty(); }));
    const auto* conflict = rowFor(s, "f.txt");
    GG_REQUIRE(conflict != nullptr);
    GG_CHECK_STR_EQ(conflict->conflict, "both modified");
    GG_CHECK(s.session()->status()->conflicted[0].stage1 && s.session()->status()->conflicted[0].stage2
        && s.session()->status()->conflicted[0].stage3);
    GG_CHECK(s.itemText(fileRef(s, "Conflicted", "f.txt").c_str()).rfind("U  f.txt", 0) == 0);
}

GG_TEST("changes", "commit files, filter, compare with HEAD, header")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "changed\n");
    s.write(repo, "sub/x.txt", "x\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Two files"});
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::Commit) == (V{"f1.txt", "sub/x.txt"}));
    GG_CHECK(s.itemExists(fileRef(s, nullptr, "sub/x.txt").c_str()));
    ctx->ItemInputValue("//Changes/##changes_filter", "sub");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(fileRef(s, nullptr, "f1.txt").c_str()));
    GG_CHECK(s.itemExists(fileRef(s, nullptr, "sub/x.txt").c_str()));
    ctx->ItemInputValue("//Changes/##changes_filter", "");
    // Compare an older commit with HEAD: everything that differs between them.
    selectCommit(s, s.revParse(repo, "HEAD~3"));
    GG_CHECK(paths(s, FileGroup::Commit) == (V{"f3.txt"}));
    ctx->ItemInputValue("//Changes/##compare_with", "HEAD");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f1.txt", "f4.txt", "f5.txt", "sub/x.txt"}); }));
    // A file of that comparison: its diff is HEAD against the commit, and plain text has no highlighting.
    ctx->ItemClick(fileRef(s, nullptr, "f4.txt").c_str());
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && d->query.kind == ggui::core::DiffKind::Commits && d->query.b.hex() == s.revParse(repo, "HEAD~3");
    }));
    GG_CHECK_STR_EQ(s.session()->diff().languageName(), "None");
    // The working tree ("Work Tree" in any case, spaces around): a local edit to f2.txt joins in.
    s.write(repo, "f2.txt", "edited\n");
    ctx->ItemInputValue("//Changes/##compare_with", "  work TREE ");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f1.txt", "f2.txt", "f4.txt", "f5.txt", "sub/x.txt"}); }));
    s.git(repo, {"checkout", "--", "f2.txt"});
    // The field's menu fills in HEAD.
    s.contextMenu("//Changes/##compare_with", "HEAD");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f1.txt", "f4.txt", "f5.txt", "sub/x.txt"}); }));
    // An unknown revision: said so, no files.
    ctx->ItemInputValue("//Changes/##compare_with", "no-such-rev");
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Changes/###compare_error"); }));
    GG_CHECK_STR_EQ(s.itemText("//Changes/###compare_error"), "Unknown revision: no-such-rev");
    GG_CHECK(paths(s, FileGroup::Commit).empty());
    // Empty: the commit's own changes again.
    ctx->ItemInputValue("//Changes/##compare_with", "");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f3.txt"}); }));
    GG_CHECK(!s.itemExists("//Changes/###compare_error"));
    // The menu's Work Tree, then Clear.
    s.write(repo, "f2.txt", "edited again\n");
    s.contextMenu("//Changes/##compare_with", "Work Tree");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f1.txt", "f2.txt", "f4.txt", "f5.txt", "sub/x.txt"}); }));
    s.contextMenu("//Changes/##compare_with", "Clear");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f3.txt"}); }));
    s.git(repo, {"checkout", "--", "f2.txt"});
    const std::string shown = s.revParse(repo, "HEAD~3");
    GG_CHECK_STR_EQ(s.itemText("//Changes/###changes_title_id"), shown.substr(0, 7));
    // The title's ID copies like every standalone ID: a click on the prefix or the rest, and the copy item.
    s.clickIdText("//Changes/###changes_title_id", 3, true);
    GG_CHECK_STR_EQ(s.clipboard(), shown.substr(0, 3));
    s.clickIdText("//Changes/###changes_title_id", 3, false);
    GG_CHECK_STR_EQ(s.clipboard(), shown);
    s.rightClickIdText("//Changes/###changes_title_id", 3, false);
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy " + shown.substr(0, 7) + "###copy_id");
    ctx->MenuClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), shown.substr(0, 7));
    s.rightClickIdText("//Changes/###changes_title_id", 3, true, true);
    GG_CHECK_STR_EQ(s.itemLabel("//$FOCUSED/###copy_id"), "Copy full ID###copy_id");
    ctx->MenuClick("//$FOCUSED/###copy_id");
    GG_CHECK_STR_EQ(s.clipboard(), shown);
    GG_CHECK(s.session()->selection().kind == ggui::SelKind::Commit);
    // The working tree: the zero ID before "Working tree"; Compare with HEAD disabled, in both panels.
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->selection().kind == ggui::SelKind::WorkingTree; }));
    const std::string zeros(7, '0');
    GG_CHECK_STR_EQ(s.itemText("//Changes/###changes_title_id"), zeros);
    // The zeros are not a commit's ID: a click copies nothing.
    ImGui::SetClipboardText("unchanged");
    ctx->ItemClick("//Changes/###changes_title_id");
    GG_CHECK_STR_EQ(s.clipboard(), "unchanged");
    GG_CHECK_STR_EQ(s.itemText("//Changes/###changes_title"), "Working tree");
    GG_CHECK(ctx->ItemInfo("//Changes/##compare_with").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(ctx->ItemInfo("//Diff/##diff_compare_with").ItemFlags & ImGuiItemFlags_Disabled);
}

GG_TEST("changes", "selecting a commit selects its first file and shows its diff")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "changed\n");
    s.write(repo, "sub/x.txt", "x\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Two files"});
    GG_REQUIRE(s.openRepository(repo));
    auto& changes = s.session()->changes();
    auto shows = [&](const std::string& path) {
        const auto& d = s.session()->diff().diff();
        return d && d->query.path == path;
    };
    selectCommit(s, s.head(repo));
    GG_REQUIRE(s.waitUntil([&] { return shows("f1.txt"); }));
    GG_CHECK(changes.selectedKeys() == (std::set<std::string>{"Files:f1.txt"}));
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "f1.txt");
    // The keyboard focus stays in History: the arrows go on through the commits.
    GG_CHECK_STR_EQ(focusedWindow(), "History");
    // Another commit: its first file.
    selectCommit(s, s.revParse(repo, "HEAD~3"));
    GG_REQUIRE(s.waitUntil([&] { return shows("f3.txt"); }));
    GG_CHECK(changes.selectedKeys() == (std::set<std::string>{"Files:f3.txt"}));
    GG_CHECK_STR_EQ(focusedWindow(), "History");
    // A new compare target keeps the current file when the comparison still has it.
    selectCommit(s, s.head(repo));
    GG_REQUIRE(s.waitUntil([&] { return shows("f1.txt"); }));
    ctx->ItemClick(fileRef(s, nullptr, "sub/x.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] { return shows("sub/x.txt"); }));
    ctx->ItemInputValue("//Changes/##compare_with", "HEAD~3");
    GG_REQUIRE(s.waitUntil([&] { return changes.rows().size() == 4; }));
    GG_REQUIRE(s.waitUntil([&] { return shows("sub/x.txt"); }));
    GG_CHECK(changes.selectedKeys() == (std::set<std::string>{"Files:sub/x.txt"}));
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "sub/x.txt");
    ctx->ItemInputValue("//Changes/##compare_with", "");
    // A filter that hides every file: no file becomes current.
    ctx->ItemInputValue("//Changes/##changes_filter", "no-such-file");
    selectCommit(s, s.revParse(repo, "HEAD~3"));
    GG_REQUIRE(s.waitUntil([&] { return changes.rows().size() == 1 && changes.rows()[0].path == "f3.txt"; }));
    ctx->Yield(2);
    GG_CHECK(changes.current() == nullptr);
    GG_CHECK(changes.selectedKeys().empty());
    ctx->ItemInputValue("//Changes/##changes_filter", "");
}

GG_TEST("changes", "the title shows the ID by the ID rule: 3 characters normal, the rest dimmed")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    GG_CHECK(s.idShownDimmed("//Changes", s.head(repo).substr(0, 7), 3));
    // The working tree's zero ID is drawn the same way.
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->selection().kind == ggui::SelKind::WorkingTree; }));
    ctx->Yield(2);
    GG_CHECK(s.idShownDimmed("//Changes", std::string(7, '0'), 3));
}

GG_TEST("changes", "multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    auto& changes = s.session()->changes();
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(1));
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(2));
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(fileRef(s, "Untracked", "u.txt").c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    // b.txt (Unstaged) … u.txt: b, c, n (unstaged), u
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(4));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(7));
    // Ctrl-click toggles off.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(6));
    // Arrow keys move the current file and the Diff panel follows.
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    ImGuiContext& g = *ImGui::GetCurrentContext();
    GG_CHECK(g.NavId != 0);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2); // SelectOnNav presses the row the frame after the cursor lands on it
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "c.txt");
    // Nav and selection are one cursor: the nav cursor is on the same row.
    GG_CHECK(g.NavId == ctx->ItemInfo(fileRef(s, "Staged", "c.txt").c_str()).ID);
    GG_CHECK(s.waitUntil([&] { return s.session()->diff().file() && s.session()->diff().file()->path == "c.txt"; }));
    ctx->KeyPress(ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "a.txt");
    GG_CHECK(g.NavId == ctx->ItemInfo(fileRef(s, "Staged", "a.txt").c_str()).ID && g.NavLayer == ImGuiNavLayer_Main);
}

GG_TEST("changes", "keyboard only: nav into the list, select by arrows, Shift range, Ctrl+Space, Alt+Space menu")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    auto& changes = s.session()->changes();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto staged = [&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}); };
    auto idOf = [&](const char* group, const char* path) { return ctx->ItemInfo(fileRef(s, group, path).c_str()).ID; };
    const std::string before = staged();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Changes");
    // Down until the cursor is on the first file row (it starts on a header / button above the list).
    const ImGuiID first = idOf("Staged", "a.txt");
    for (int i = 0; i < 40 && g.NavId != first; ++i) {
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(2);
    }
    GG_REQUIRE(g.NavId == first);
    GG_REQUIRE(changes.selectedKeys().size() == 1);
    // Two more rows down: the selection follows the cursor.
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(1));
    GG_REQUIRE(changes.current() != nullptr);
    const ggui::FileRow* third = changes.current();
    const ImGuiID thirdId = idOf(third->group == FileGroup::Staged ? "Staged" : "Unstaged", third->path.c_str());
    GG_CHECK(g.NavId == thirdId);
    GG_CHECK(changes.selectedKeys().count(third->key()) == 1);
    // Alt+Space opens the cursor row's context menu.
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 1);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    GG_CHECK(g.OpenPopupStack.Size == 0);
    // Shift+Up extends the selection from the anchor (third row) over the second.
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(2));
    // Ctrl+Up moves the cursor (to the first row) without selecting; Ctrl+Space then adds that row.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_UpArrow);
    ctx->Yield(2);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(2));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Space);
    ctx->Yield(2);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(3));
    s.settle();
    GG_CHECK_STR_EQ(staged(), before); // Ctrl+Space selects, it does not stage
    // Plain Space on the 3-row selection acts on all of it (it does not collapse to the cursor row first).
    ctx->KeyPress(ImGuiKey_Space);
    GG_CHECK(s.waitUntil([&] { return staged().find("a.txt") == std::string::npos && staged().find("e.txt") == std::string::npos; }));
}

GG_TEST("changes", "Space toggles staging once; Alt+Space only opens the menu")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    auto& changes = s.session()->changes();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto staged = [&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}); };
    GG_CHECK(staged().find("b.txt") == std::string::npos);
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str());
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(1));
    // Alt+Space is the context-menu chord: it stages nothing and activates nothing (no refocusing).
    const ImGuiID nav = g.NavId;
    ctx->KeyPress(ImGuiMod_Alt | ImGuiKey_Space);
    ctx->Yield(3);
    GG_CHECK(staged().find("b.txt") == std::string::npos);
    GG_CHECK(g.NavLayer == ImGuiNavLayer_Main);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(1));
    ctx->KeyPress(ImGuiKey_Escape); // closes the row menu it opened
    ctx->Yield(2);
    GG_CHECK(g.NavId == nav);
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str());
    ctx->KeyPress(ImGuiKey_Space);
    GG_CHECK(s.waitUntil([&] { return staged().find("b.txt") != std::string::npos; }));
    s.settle();
    // Once: still staged (a second toggle would have unstaged it), nothing else was selected or staged.
    GG_CHECK(staged().find("b.txt") != std::string::npos);
    GG_CHECK(g.NavLayer == ImGuiNavLayer_Main);
    // Space on the staged file: unstaged, once.
    ctx->ItemClick(fileRef(s, "Staged", "b.txt").c_str());
    ctx->KeyPress(ImGuiKey_Space);
    GG_CHECK(s.waitUntil([&] { return staged().find("b.txt") == std::string::npos; }));
    s.settle();
    GG_CHECK(staged().find("b.txt") == std::string::npos);
    GG_CHECK(g.NavLayer == ImGuiNavLayer_Main);
}

GG_TEST("changes", "keys act on the nav cursor: Ctrl+Up then Space toggles only the cursor row; headers stage and discard nothing")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    auto& changes = s.session()->changes();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    auto staged = [&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}); };
    auto idOf = [&](const char* group, const char* path) { return ctx->ItemInfo(fileRef(s, group, path).c_str()).ID; };
    const std::string before = staged();
    ctx->SetInputMode(ImGuiInputSource_Keyboard);
    ctx->WindowFocus("//Changes");
    const ImGuiID first = idOf("Staged", "a.txt");
    for (int i = 0; i < 40 && g.NavId != first; ++i) {
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->Yield(2);
    }
    GG_REQUIRE(g.NavId == first);
    // Select down two rows, then Ctrl+Up: the cursor moves to the second row, the selection stays the third.
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->KeyPress(ImGuiKey_DownArrow);
    ctx->Yield(2);
    const std::string thirdKey = changes.current()->key();
    GG_REQUIRE(changes.selectedKeys().size() == 1);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_UpArrow);
    ctx->Yield(2);
    const ggui::FileRow* cursor = changes.current();
    GG_REQUIRE(cursor != nullptr);
    GG_CHECK(g.NavId == idOf(cursor->group == FileGroup::Staged ? "Staged" : cursor->group == FileGroup::Unstaged ? "Unstaged" : "Untracked",
                             cursor->path.c_str()));
    const std::string cursorPath = cursor->path;
    const bool cursorStaged = cursor->group == FileGroup::Staged;
    GG_CHECK(cursor->key() != thirdKey);
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK(changes.current()->key() == cursor->key()); // the cursor row is current
    GG_CHECK(changes.selectedKeys().count(thirdKey) == 1);
    ctx->KeyPress(ImGuiKey_Space);
    GG_CHECK(s.waitUntil([&] { return staged() != before; }));
    s.settle();
    // Only the cursor row toggled: the old selection (the third row) was not touched.
    const std::string after = staged();
    GG_CHECK_EQ(after.find(cursorPath) != std::string::npos, !cursorStaged);
    auto lines = [](const std::string& t) { return std::count(t.begin(), t.end(), '\n'); };
    GG_CHECK_EQ(lines(after), lines(before) + (cursorStaged ? -1 : 1));
    // The cursor on the Staged header: Space and D change no file.
    const std::string headerRef = s.child("//Changes", "##files") + "/Staged/###group";
    const ImGuiID header = ctx->ItemInfo(headerRef.c_str()).ID;
    for (int i = 0; i < 6 && g.NavId != header; ++i) {
        ctx->KeyPress(ImGuiKey_UpArrow);
        ctx->Yield(2);
    }
    GG_REQUIRE(g.NavId == header);
    const std::string stagedNow = staged();
    const size_t rowsNow = changes.rows().size();
    ctx->KeyPress(ImGuiKey_Space);
    ctx->Yield(3);
    ctx->KeyPress(ImGuiKey_D);
    ctx->Yield(3);
    s.settle();
    GG_CHECK_STR_EQ(staged(), stagedNow);
    GG_CHECK_EQ(changes.rows().size(), rowsNow); // nothing was discarded
    ctx->KeyPress(ImGuiKey_Space); // Space toggled the header (collapsed); reopen it, the window state outlives the test
    ctx->Yield(3);
    GG_CHECK(s.itemExists(fileRef(s, "Staged", "b.txt").c_str()) || s.itemExists(fileRef(s, "Staged", "a.txt").c_str()));
}

GG_TEST("changes", "file context menu: copy, patch, save patch, blame")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "dir/file.txt", "one\n");
    s.write(repo, "f1.txt", "line 1 changed\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Two"});
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    const std::string ref = fileRef(s, nullptr, "dir/file.txt");
    s.contextMenu(ref.c_str(), "Copy/Name");
    GG_CHECK_STR_EQ(s.clipboard(), "file.txt");
    s.contextMenu(ref.c_str(), "Copy/Relative path");
    GG_CHECK_STR_EQ(s.clipboard(), "dir/file.txt");
    s.contextMenu(ref.c_str(), "Copy/Absolute path");
    GG_CHECK_STR_EQ(s.clipboard(), (repo / "dir" / "file.txt").string());

    s.contextMenu(ref.c_str(), "Patch/Copy");
    const std::string expected = s.git(repo, {"diff", "HEAD~1", "HEAD", "--", "dir/file.txt"}).out;
    GG_CHECK(s.waitUntil([&] { return s.clipboard() == expected; }));
    // The copied patch applies with plain git.
    s.git(repo, {"apply", "--check", "-R"}, s.clipboard());

    const fs::path out = s.path("file.patch");
    ggui::setEnv("GGUI_TEST_PICK_PATH", out.string());
    s.contextMenu(ref.c_str(), "Patch/Save...");
    // Wait for ggui to finish writing before reading the file (also below).
    GG_REQUIRE(s.waitIdle());
    GG_CHECK(s.waitUntil([&] { return s.read(s.root(), "file.patch") == expected; }));

    // A multi-file selection saves one patch for all selected files.
    ctx->ItemClick(ref.c_str());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(s.session()->changes().selectedKeys().size(), static_cast<size_t>(2));
    const fs::path both = s.path("both.patch");
    ggui::setEnv("GGUI_TEST_PICK_PATH", both.string());
    s.contextMenu(ref.c_str(), "Patch/Save...");
    const std::string expectedBoth = s.git(repo, {"diff", "HEAD~1", "HEAD"}).out;
    GG_REQUIRE(s.waitIdle());
    GG_CHECK(s.waitUntil([&] { return s.read(s.root(), "both.patch") == expectedBoth; }));
    ggui::unsetEnv("GGUI_TEST_PICK_PATH");

    s.contextMenu(ref.c_str(), "Blame file");
    GG_CHECK(s.waitUntil([&] { return s.session()->blame().blame() && s.session()->blame().blame()->query.path == "dir/file.txt"; }));
}

GG_TEST("changes", "stash contents: working tree, index and untracked parts")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    const auto snapshot = s.session()->snapshot(); // keeps stashes alive while the UI refreshes
    const auto& stashes = snapshot->stashes;
    GG_REQUIRE(stashes.size() == 3);
    GG_CHECK_STR_EQ(stashes[0].message, "On main: with untracked");
    GG_CHECK_STR_EQ(stashes[1].message, "On main: index and worktree");
    GG_CHECK(stashes[0].hasUntracked && !stashes[1].hasUntracked);
    GG_CHECK(stashes[1].hasIndexChanges);
    GG_CHECK_STR_EQ(stashes[1].base.hex(), s.revParse(repo, "stash@{1}^1"));
    s.showPanel("Stashes");
    GG_CHECK(s.itemText("//Stashes/stash_1/###row").rfind("stash@{1} On main: index", 0) == 0);
    ctx->ItemClick("//Stashes/stash_1/###row");
    GG_CHECK(s.waitUntil([&] { return !paths(s, FileGroup::StashIndex).empty() && !paths(s, FileGroup::StashWorktree).empty(); }));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::StashIndex) == (V{"a.txt"}));
    GG_CHECK(paths(s, FileGroup::StashWorktree) == (V{"b.txt"}));
    ctx->ItemClick("//Stashes/stash_0/###row");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::StashUntracked) == (V{"new.txt"}); }));
    GG_CHECK(paths(s, FileGroup::StashWorktree) == (V{"b.txt"}));
    // Diff of the untracked part.
    ctx->ItemClick(fileRef(s, "Untracked files", "new.txt").c_str());
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == "new.txt" && d->query.kind == ggui::core::DiffKind::StashUntracked;
    }));
}

GG_TEST("info", "change information: message, author, committer, date, ID, parents")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    // A commit whose committer differs from its author.
    s.write(repo, "c.txt", "c\n");
    s.git(repo, {"add", "c.txt"});
    gg::RunRequest r;
    r.args = {"git", "-c", "user.name=Committer Person", "-c", "user.email=committer@example.com", "commit", "-q",
        "--author=Author Person <author@example.com>", "-m", "Subject line\n\nBody text."};
    r.cwd = repo;
    GG_REQUIRE(gg::run(r).ok());
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    auto& info = s.session()->info();
    GG_REQUIRE(s.waitUntil([&] { return info.details() != nullptr; }));
    GG_CHECK_STR_EQ(info.details()->message, "Subject line\n\nBody text.\n");
    GG_CHECK_STR_EQ(info.details()->authorName, "Author Person");
    GG_CHECK_STR_EQ(info.details()->committerName, "Committer Person");
    GG_CHECK(s.itemExists("//Change information/##message"));
    s.contextMenu("//Change information/**/###author", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "Author Person");
    s.contextMenu("//Change information/**/###author", "Copy email");
    GG_CHECK_STR_EQ(s.clipboard(), "author@example.com");
    // The author line is plain text: no hover or click effect (its context menu stays).
    ctx->ItemClick("//Change information/**/###author");
    ctx->Yield(2);
    GG_CHECK(ImGui::GetActiveID() == 0 && !s.itemDrawsBackground("//Change information/**/###author"));
    // The short ID shows its first 3 characters normally and the rest dimmed. A click on the text copies the
    // highlighted 3 characters, or the full ID from the dimmed part. There is no Copy button.
    const std::string head = s.head(repo);
    const std::string shortHead = head.substr(0, 7);
    ctx->ScrollToItemY("//Change information/**/###commit_id_text");
    GG_CHECK_STR_EQ(s.itemText("//Change information/**/###commit_id_text"), shortHead);
    GG_CHECK(s.idShownDimmed("//Change information", shortHead, 3));
    GG_CHECK(!s.itemExists("//Change information/**/###commit_id"));
    s.clickIdText("//Change information/**/###commit_id_text", 3, false);
    GG_CHECK_STR_EQ(s.clipboard(), head);
    s.clickIdText("//Change information/**/###commit_id_text", 3, true);
    GG_CHECK_STR_EQ(s.clipboard(), head.substr(0, 3));
    // Right-click offers the one copy item: the 3 characters on the prefix, the 7 on the rest, the full ID with Shift.
    const std::string copyItem = "//$FOCUSED/###copy_id";
    s.rightClickIdText("//Change information/**/###commit_id_text", 3, true);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + head.substr(0, 3) + "###copy_id");
    ctx->MenuClick(copyItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), head.substr(0, 3));
    s.rightClickIdText("//Change information/**/###commit_id_text", 3, false);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + shortHead + "###copy_id");
    ctx->MenuClick(copyItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), shortHead);
    s.rightClickIdText("//Change information/**/###commit_id_text", 3, true, true);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy full ID###copy_id");
    ctx->MenuClick(copyItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), head);
    GG_CHECK(info.details()->authorTime > 0);
    // Hovering the commit ID shows no tooltip.
    ctx->MouseMove("//Change information/**/###commit_id_text");
    ctx->SleepNoSkip(1.0f, 0.1f);
    ImGuiWindow* idTip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_CHECK(idTip == nullptr || !idTip->Active);
    // Parents: the merge has two; clicking one reveals it.
    selectCommit(s, s.revParse(repo, "HEAD~1"));
    GG_REQUIRE(s.waitUntil([&] { return info.details() && info.details()->parents.size() == 2; }));
    // (A clickable item does draw a hover highlight: the check above can fail.)
    ctx->MouseMove("//Change information/**/###parent_1");
    GG_CHECK(s.itemDrawsBackground("//Change information/**/###parent_1"));
    // A parent shows no tooltip.
    const std::string parent2 = s.revParse(repo, "HEAD~1^2");
    ctx->SleepNoSkip(1.0f, 0.1f);
    idTip = ctx->GetWindowByRef("//##Tooltip_00");
    GG_CHECK(idTip == nullptr || !idTip->Active);
    // A parent's right-click offers the same copy item, for that parent.
    s.rightClickIdText("//Change information/**/###parent_1", 3, false);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + parent2.substr(0, 7) + "###copy_id");
    ctx->MenuClick(copyItem.c_str());
    GG_CHECK_STR_EQ(s.clipboard(), parent2.substr(0, 7));
    s.rightClickIdText("//Change information/**/###parent_1", 3, true);
    GG_CHECK_STR_EQ(s.itemLabel(copyItem.c_str()), "Copy " + parent2.substr(0, 3) + "###copy_id");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    ctx->ItemClick("//Change information/**/###parent_1");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == parent2; }));
    // The root commit has no parents.
    selectCommit(s, s.revParse(repo, "HEAD~1^2~2"));
    GG_CHECK(s.waitUntil([&] { return info.details() && info.details()->parents.empty(); }));
}

GG_TEST("changes", "file tooltips wait until scrolling stops")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    for (int i = 0; i < 80; ++i)
        s.write(repo, "dir/file" + std::to_string(100 + i) + ".txt", "x\n");
    GG_REQUIRE(s.openRepository(repo));
    const std::string row = fileRef(s, "Untracked", "dir/file110.txt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    auto tipShown = [&] {
        ImGuiWindow* tip = ctx->GetWindowByRef("//##Tooltip_00");
        return tip != nullptr && tip->Active;
    };
    ctx->MouseMove(row.c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(tipShown());
    // While the wheel scrolls the list, no tooltip (whatever row is under the mouse).
    ctx->MouseWheelY(-1.0f);
    ctx->Yield(1);
    GG_CHECK(!tipShown());
    ctx->MouseWheelY(-1.0f);
    ctx->SleepNoSkip(0.15f, 0.05f);
    GG_CHECK(!tipShown());
    // Once it stops, tooltips come back.
    ctx->SleepNoSkip(1.2f, 0.1f);
    GG_CHECK(tipShown());
}

} // namespace ggtest
