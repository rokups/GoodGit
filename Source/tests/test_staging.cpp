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

GG_TEST("staging", "Space, Enter and double-click toggle staging", "CHG-KEY-TOGGLE-SPACE", "CHG-KEY-TOGGLE-ENTER",
    "CHG-DBLCLICK-STAGE")
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
    ctx->ItemDoubleClick(fileRef(s, "Untracked", "u.txt").c_str());
    GG_CHECK(waitXY(s, repo, "u.txt", "A."));
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

GG_TEST("staging", "intent to add, track, untrack (and ignore), delete", "CHG-INTENT-TO-ADD", "CHG-TRACK", "CHG-UNTRACK",
    "CHG-UNTRACK-IGNORE", "CHG-CTX-DELETE")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    s.write(repo, "v.txt", "another untracked\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(rowsReady(s, 8));
    s.contextMenu(fileRef(s, "Untracked", "u.txt").c_str(), "Intent to add");
    GG_CHECK(waitXY(s, repo, "u.txt", ".A"));
    s.contextMenu(fileRef(s, "Untracked", "v.txt").c_str(), "Track");
    GG_CHECK(waitXY(s, repo, "v.txt", "A."));
    // Untrack a committed file: git rm --cached, file stays on disk.
    s.contextMenu(fileRef(s, "Unstaged", "b.txt").c_str(), "Untrack...");
    GG_REQUIRE(s.dialogOpen("Untrack"));
    s.dialogButton("Untrack", "Untrack");
    // Deleted from the index, still on disk as an untracked file.
    GG_CHECK(waitXY(s, repo, "b.txt", "D."));
    GG_CHECK(s.statusPorcelain(repo).find(std::string("? b.txt")) != std::string::npos);
    GG_CHECK(fs::exists(repo / "b.txt"));
    s.contextMenu(fileRef(s, "Staged", "a.txt").c_str(), "Untrack...");
    GG_REQUIRE(s.dialogOpen("Untrack"));
    s.dialogCheck("Untrack", "ignore", "Also add them to .gitignore");
    s.dialogButton("Untrack", "Untrack");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, ".gitignore").find("/a.txt") != std::string::npos; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"ls-files", "a.txt"}).empty());
    // Delete a file from the working tree.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Untracked", "b.txt").c_str()); }));
    s.contextMenu(fileRef(s, "Untracked", "b.txt").c_str(), "Delete file...");
    GG_REQUIRE(s.dialogOpen("Delete files"));
    s.dialogButton("Delete files", "Delete");
    GG_CHECK(s.waitUntil([&] { return !fs::exists(repo / "b.txt"); }));
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

} // namespace ggtest
