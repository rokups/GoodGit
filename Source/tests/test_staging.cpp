// File-level staging and external tools (§4.4; P2-06, P2-08).
#include "panels/ChangesPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

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

std::string groupRef(Scenario& s, const char* group) { return s.child("//Changes", "##files") + "/" + group; }

// Porcelain v2 "XY" code of `path` ("" when clean / unknown; "??" for untracked).
std::string xy(Scenario& s, const fs::path& repo, const std::string& path)
{
    for (const auto& entry : gg::splitNul(s.statusPorcelain(repo))) {
        if (entry.size() > 2 && entry[0] == '?' && entry.substr(2) == path)
            return "??";
        if (entry.size() > 5 && (entry[0] == '1' || entry[0] == '2')) {
            const auto last = entry.rfind(' ');
            if (entry.substr(last + 1) == path)
                return entry.substr(2, 2);
        }
    }
    return "";
}

bool waitXY(Scenario& s, const fs::path& repo, const std::string& path, const std::string& expected)
{
    const bool ok = s.waitUntil([&] { return xy(s, repo, path) == expected; }, 20.0f);
    if (!ok)
        s.ctx->LogError("%s: status '%s', expected '%s'", path.c_str(), xy(s, repo, path).c_str(), expected.c_str());
    s.settle();
    return ok;
}

bool rowsReady(Scenario& s, size_t n)
{
    return s.waitUntil([&] { return s.session()->changes().rows().size() == n; });
}

} // namespace

GG_TEST("staging", "stage, unstage and discard files", "CHG-STAGE", "CHG-UNSTAGE", "CHG-DISCARD")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 7));
    s.contextMenu(fileRef(s, "Unstaged", "b.txt").c_str(), "Stage");
    GG_CHECK(waitXY(s, repo, "b.txt", "M."));
    s.contextMenu(fileRef(s, "Staged", "a.txt").c_str(), "Unstage");
    GG_CHECK(waitXY(s, repo, "a.txt", ".M"));
    // Discard the unstaged part of c.txt (the staged part stays).
    s.contextMenu(fileRef(s, "Unstaged", "c.txt").c_str(), "Discard...");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Discard");
    GG_CHECK(waitXY(s, repo, "c.txt", "M."));
    GG_CHECK_STR_EQ(s.read(repo, "c.txt"), "c staged\n");
    // Discarding an untracked file deletes it (after confirmation).
    s.contextMenu(fileRef(s, "Untracked", "u.txt").c_str(), "Discard...");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Discard");
    GG_CHECK(s.waitUntil([&] { return !fs::exists(repo / "u.txt"); }));
    s.settle();
}

GG_TEST("staging", "Space and Enter toggle staging", "CHG-KEY-TOGGLE-SPACE", "CHG-KEY-TOGGLE-ENTER")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 7));
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str());
    ctx->KeyPress(ImGuiKey_Space);
    GG_CHECK(waitXY(s, repo, "b.txt", "M."));
    ctx->ItemClick(fileRef(s, "Staged", "b.txt").c_str());
    ctx->KeyPress(ImGuiKey_Enter);
    GG_CHECK(waitXY(s, repo, "b.txt", ".M"));
}

GG_TEST("staging", "stage all, unstage all, stage modified", "CHG-STAGE-ALL", "CHG-UNSTAGE-ALL", "CHG-STAGE-MODIFIED",
    "HIST-WT-CTX-STAGE-ALL", "HIST-WT-CTX-UNSTAGE-ALL")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 7));
    ctx->ItemClick((groupRef(s, "Staged") + "/Unstage all##unstage_all").c_str());
    GG_CHECK(waitXY(s, repo, "a.txt", ".M"));
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    ctx->ItemClick((groupRef(s, "Unstaged") + "/Stage modified##stage_modified").c_str());
    GG_CHECK(waitXY(s, repo, "b.txt", "M."));
    GG_CHECK_STR_EQ(xy(s, repo, "u.txt"), "??"); // untracked stays untracked
    // Working tree row menu: Unstage all, Stage all (includes untracked).
    s.contextMenu("//History/**/###row_wt", "Unstage all");
    GG_CHECK(waitXY(s, repo, "b.txt", ".M"));
    s.contextMenu("//History/**/###row_wt", "Stage all");
    GG_CHECK(waitXY(s, repo, "u.txt", "A."));
    s.contextMenu("//History/**/###row_wt", "Unstage all");
    GG_CHECK(waitXY(s, repo, "u.txt", "??"));
    ctx->ItemClick((groupRef(s, "Unstaged") + "/Stage all##stage_all").c_str());
    GG_CHECK(waitXY(s, repo, "u.txt", "A."));
}

GG_TEST("staging", "intent to add, delete; no Track / Untrack", "CHG-INTENT-TO-ADD", "CHG-NO-TRACK-UNTRACK",
    "CHG-CTX-DELETE")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    s.write(repo, "v.txt", "another untracked\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 8));
    s.contextMenu(fileRef(s, "Untracked", "u.txt").c_str(), "Intent to add");
    GG_CHECK(waitXY(s, repo, "u.txt", ".A"));
    // The menu has no Track / Untrack items (Stage and plain git cover them).
    for (const char* group : {"Untracked", "Unstaged"}) {
        ctx->ItemClick(fileRef(s, group, group == std::string("Untracked") ? "v.txt" : "b.txt").c_str(), ImGuiMouseButton_Right);
        ctx->Yield(2);
        GG_CHECK(ctx->ItemExists("//$FOCUSED/Stage"));
        GG_CHECK(!ctx->ItemExists("//$FOCUSED/Track") && !ctx->ItemExists("//$FOCUSED/Untrack..."));
        ctx->KeyPress(ImGuiKey_Escape);
    }
    // Delete a file from the working tree.
    s.contextMenu(fileRef(s, "Untracked", "v.txt").c_str(), "Delete file...");
    GG_REQUIRE(s.dialogOpen("Delete files"));
    s.dialogButton("Delete files", "Delete");
    GG_CHECK(s.waitUntil([&] { return !fs::exists(repo / "v.txt"); }));
    s.settle();
}

GG_TEST("staging", "drag files between Staged and Unstaged", "CHG-DRAG-STAGE", "CHG-DRAG-UNSTAGE")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 7));
    ctx->ItemDragAndDrop(fileRef(s, "Unstaged", "b.txt").c_str(), (groupRef(s, "Staged") + "/###group").c_str());
    GG_CHECK(waitXY(s, repo, "b.txt", "M."));
    ctx->ItemDragAndDrop(fileRef(s, "Staged", "a.txt").c_str(), (groupRef(s, "Unstaged") + "/###group").c_str());
    GG_CHECK(waitXY(s, repo, "a.txt", ".M"));
}

GG_TEST("staging", "discard all changes from the Working tree menu", "HIST-WT-CTX-DISCARD")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 7));
    s.contextMenu("//History/**/###row_wt", "Discard changes...");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    ctx->ItemCheck("//Discard changes/Also delete untracked files (1)##untracked");
    s.dialogButton("Discard changes", "Discard");
    GG_CHECK(s.waitUntil([&] { return s.statusPorcelain(repo).empty(); }));
    s.settle();
}

GG_TEST("staging", "external editor, folder and diff tools", "CHG-CTX-OPEN", "CHG-CTX-OPEN-FOLDER", "CHG-CTX-EXTDIFF-HEAD",
    "CHG-CTX-EXTDIFF-PARENT", "APP-EXT-EDITOR", "APP-EXT-FOLDER", "APP-EXT-DIFF-HEAD", "APP-EXT-DIFF-PARENT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "edited\n");
    const fs::path editorLog = s.fakeTool("fake-editor");
    const fs::path folderLog = s.fakeTool("xdg-open");
    const fs::path diffLog = s.fakeTool("fake-difftool");
    ggui::unsetEnv("GIT_EDITOR");
    ggui::unsetEnv("EDITOR");
    s.git(repo, {"config", "core.editor", "fake-editor"});
    s.git(repo, {"config", "diff.tool", "fake"});
    s.git(repo, {"config", "difftool.fake.cmd", "fake-difftool \"$LOCAL\" \"$REMOTE\""});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 1));
    auto logHas = [&](const fs::path& log, const std::string& text) {
        return s.waitUntil([&] { return s.read(log.parent_path(), log.filename().string()).find(text) != std::string::npos; });
    };
    s.contextMenu(fileRef(s, "Unstaged", "f1.txt").c_str(), "Open working-copy file");
    GG_CHECK(logHas(editorLog, "f1.txt"));
    s.contextMenu(fileRef(s, "Unstaged", "f1.txt").c_str(), "Open containing folder");
    GG_CHECK(logHas(folderLog, repo.string()));
    s.contextMenu(fileRef(s, "Unstaged", "f1.txt").c_str(), "External diff/vs HEAD");
    GG_CHECK(logHas(diffLog, "f1.txt"));
    s.settle();
    // vs parent, from a commit.
    const size_t before = gg::splitLines(s.read(diffLog.parent_path(), diffLog.filename().string())).size();
    ctx->ItemClick(("//History/**/###row_" + s.head(repo)).c_str());
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    s.contextMenu(fileRef(s, nullptr, "f5.txt").c_str(), "External diff/vs parent");
    GG_CHECK(s.waitUntil([&] {
        return gg::splitLines(s.read(diffLog.parent_path(), diffLog.filename().string())).size() >= before + 2;
    }));
    s.settle();
}

GG_TEST("staging", "double-click opens new files in the editor, others in the diff tool", "CHG-DBLCLICK-OPEN")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.commitFile(repo, "f2.txt", "changed\n", "Change f2");
    s.write(repo, "f1.txt", "edited\n");
    s.write(repo, "new.txt", "new\n");
    const fs::path editorLog = s.fakeTool("fake-editor");
    const fs::path diffLog = s.fakeTool("fake-difftool");
    ggui::unsetEnv("GIT_EDITOR");
    ggui::unsetEnv("EDITOR");
    s.git(repo, {"config", "core.editor", "fake-editor"});
    s.git(repo, {"config", "diff.tool", "fake"});
    s.git(repo, {"config", "difftool.fake.cmd", "fake-difftool \"$LOCAL\" \"$REMOTE\""});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 2));
    auto lines = [&](const fs::path& log) { return gg::splitLines(s.read(log.parent_path(), log.filename().string())); };
    auto logHas = [&](const fs::path& log, const std::string& text) {
        return s.waitUntil([&] { return s.read(log.parent_path(), log.filename().string()).find(text) != std::string::npos; });
    };
    // A new file opens in the editor; staging does not change.
    ctx->ItemDoubleClick(fileRef(s, "Untracked", "new.txt").c_str());
    GG_CHECK(logHas(editorLog, "new.txt"));
    s.settle();
    GG_CHECK_STR_EQ(xy(s, repo, "new.txt"), "??");
    GG_CHECK(lines(diffLog).empty());
    // A modified file opens in the diff tool: HEAD's version against the working tree.
    ctx->ItemDoubleClick(fileRef(s, "Unstaged", "f1.txt").c_str());
    GG_CHECK(s.waitUntil([&] { return lines(diffLog).size() >= 2; }));
    s.settle();
    GG_CHECK_STR_EQ(xy(s, repo, "f1.txt"), ".M");
    // In a commit: the parent's version against the commit's.
    const size_t before = lines(diffLog).size();
    ctx->ItemClick(("//History/**/###row_" + s.head(repo)).c_str());
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    ctx->ItemDoubleClick(fileRef(s, nullptr, "f2.txt").c_str());
    GG_CHECK(s.waitUntil([&] { return lines(diffLog).size() >= before + 2; }));
    s.settle();
    // A staged file, and a stash's working tree and index parts.
    s.write(repo, "f4.txt", "in the index\n");
    s.git(repo, {"add", "f4.txt"});
    s.write(repo, "f4.txt", "and in the working tree\n");
    s.git(repo, {"stash", "push", "-q"});
    s.write(repo, "f3.txt", "staged\n");
    s.git(repo, {"add", "f3.txt"});
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Staged", "f3.txt").c_str()); }));
    size_t n = lines(diffLog).size();
    ctx->ItemDoubleClick(fileRef(s, "Staged", "f3.txt").c_str());
    GG_CHECK(s.waitUntil([&] { return lines(diffLog).size() >= n + 2; }));
    s.settle();
    s.showPanel("Stashes");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    ctx->ItemClick("//Stashes/stash_0/###row");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Index", "f4.txt").c_str()); }));
    for (const char* part : {"Working tree", "Index"}) {
        n = lines(diffLog).size();
        ctx->ItemDoubleClick(fileRef(s, part, "f4.txt").c_str());
        GG_CHECK(s.waitUntil([&] { return lines(diffLog).size() >= n + 2; }));
        s.settle();
    }
}

} // namespace ggtest
