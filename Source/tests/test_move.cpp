// Moving changes between commits (§4.3 Move files/hunks/lines, §4.4, §4.5; P3-11).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <functional>

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }
std::string fileRef(Scenario& s, const std::string& path) { return s.child("//Changes", "##files") + "/" + path + "/###file_" + path; }
std::string body(Scenario& s) { return s.child("//Diff", "##diff_body"); }

std::string changedFiles(Scenario& s, const fs::path& repo, const std::string& rev)
{
    std::string out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"show", "--name-only", "--format=", rev})))
        if (!l.empty())
            out += (out.empty() ? "" : " ") + l;
    return out;
}

// base adds a.txt (20 lines); X adds b.txt and c.txt and changes lines 2 and 18 of a.txt; Y adds d.txt.
struct MoveRepo {
    fs::path path;
    std::string base, x, y;
};

MoveRepo makeRepo(Scenario& s)
{
    MoveRepo r;
    r.path = s.fixture(Recipe::Empty);
    std::string a;
    for (int i = 1; i <= 20; ++i)
        a += "line " + std::to_string(i) + "\n";
    s.commitFile(r.path, "a.txt", a, "base");
    r.base = s.head(r.path);
    std::string changed = a;
    changed.replace(changed.find("line 2\n"), 7, "LINE 2\n");
    changed.replace(changed.find("line 18\n"), 8, "LINE 18\n");
    s.write(r.path, "a.txt", changed);
    s.write(r.path, "b.txt", "b\n");
    s.write(r.path, "c.txt", "c\n");
    s.git(r.path, {"add", "."});
    s.git(r.path, {"commit", "-q", "-m", "X"});
    r.x = s.head(r.path);
    s.commitFile(r.path, "d.txt", "d\n", "Y");
    r.y = s.head(r.path);
    return r;
}

void selectCommit(Scenario& s, const std::string& hex, size_t files)
{
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
    s.ctx->ItemClick(rowRef(hex).c_str());
    s.waitUntil([&] { return s.session()->changes().rows().size() == files; });
}

bool moved(Scenario& s, const fs::path& repo, const std::string& from)
{
    const bool ok = s.waitUntil([&] { return s.head(repo) != from; });
    s.settle();
    return ok;
}

} // namespace

GG_TEST("move", "files: to the parent, to the child, to the working tree, revert", "ACT-MOVE-CHANGES-PARENT",
    "ACT-MOVE-CHANGES-CHILD", "ACT-MOVE-CHANGES-WORKTREE", "CHG-CTX-MOVE-PARENT", "CHG-CTX-MOVE-CHILD", "CHG-CTX-REVERT")
{
    const MoveRepo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "HEAD^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    // c.txt to the parent: base now adds it, X no longer does; the tip is unchanged.
    selectCommit(s, r.x, 3);
    s.contextMenu(fileRef(s, "c.txt").c_str(), "Move to parent");
    GG_CHECK(moved(s, r.path, r.y));
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD~2"), "a.txt c.txt");
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD~1"), "a.txt b.txt");
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), tree);
    // b.txt to the child: Y now adds b.txt and d.txt.
    const std::string x2 = s.revParse(r.path, "HEAD~1");
    const std::string beforeChild = s.head(r.path);
    selectCommit(s, x2, 2);
    s.contextMenu(fileRef(s, "b.txt").c_str(), "Move to child");
    GG_CHECK(moved(s, r.path, beforeChild));
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD"), "b.txt d.txt");
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD~1"), "a.txt");
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), tree);
    // d.txt out of HEAD into the working tree: uncommitted, still on disk.
    const std::string tip = s.head(r.path);
    selectCommit(s, tip, 2);
    s.contextMenu(fileRef(s, "d.txt").c_str(), "Move to the working tree");
    GG_CHECK(moved(s, r.path, tip));
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD"), "b.txt");
    GG_CHECK_STR_EQ(s.read(r.path, "d.txt"), "d\n");
    GG_CHECK(s.statusPorcelain(r.path).find("? d.txt") != std::string::npos); // untracked (porcelain v2)
    // Revert a.txt's change in the middle commit: gone from history (and the working tree).
    fs::remove(r.path / "d.txt");
    const std::string mid = s.revParse(r.path, "HEAD~1");
    const std::string beforeRevert = s.head(r.path);
    selectCommit(s, mid, 1);
    s.contextMenu(fileRef(s, "a.txt").c_str(), "Revert");
    GG_CHECK(moved(s, r.path, beforeRevert));
    GG_CHECK(s.read(r.path, "a.txt").find("LINE") == std::string::npos);
    GG_CHECK(s.statusPorcelain(r.path).empty());
}

GG_TEST("move", "lines: a hunk to the parent and to the active commit", "DIFF-CTX-MOVE-PARENT", "DIFF-CTX-MOVE-CHILD",
    "DIFF-CTX-MOVE-ACTIVE", "DIFF-CTX-MOVE-WORKTREE", "DIFF-CTX-REVERT")
{
    const MoveRepo r = makeRepo(s);
    const std::string tree = s.revParse(r.path, "HEAD^{tree}");
    GG_REQUIRE(s.openRepository(r.path));
    selectCommit(s, r.x, 3);
    ctx->ItemClick(fileRef(s, "a.txt").c_str());
    s.showPanel("Diff");
    GG_REQUIRE(s.waitUntil([&] { const auto& d = s.session()->diff().diff(); return d && !d->files.empty() && d->files[0].hunks.size() == 2; }));
    // The second hunk (line 18) to the parent.
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_1").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_1").c_str());
    s.contextMenu((body(s) + "/###hunk_1").c_str(), "Move line(s) to parent");
    GG_CHECK(moved(s, r.path, r.y));
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD~2:a.txt"}).find("LINE 18") != std::string::npos);
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD~2:a.txt"}).find("LINE 2") == std::string::npos);
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD~1:a.txt"}).find("LINE 2") != std::string::npos);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), tree);
    // Line 2's hunk from the (new) X to the checked-out commit (Y): X keeps b and c only.
    const std::string x2 = s.revParse(r.path, "HEAD~1");
    selectCommit(s, x2, 3);
    ctx->ItemClick(fileRef(s, "a.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] { const auto& d = s.session()->diff().diff(); return d && !d->files.empty() && d->files[0].hunks.size() == 1 && d->query.a.hex() == x2; }));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_0").c_str());
    const std::string beforeActive = s.head(r.path);
    s.contextMenu((body(s) + "/###hunk_0").c_str(), "Move line(s) to active commit");
    GG_CHECK(moved(s, r.path, beforeActive));
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD~1"), "b.txt c.txt");
    GG_CHECK_STR_EQ(changedFiles(s, r.path, "HEAD"), "a.txt d.txt");
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), tree);
    // Revert that line in HEAD, then (after Undo) move it to the working tree instead.
    const std::string tip = s.head(r.path);
    selectCommit(s, tip, 2);
    ctx->ItemClick(fileRef(s, "a.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_0").c_str());
    s.contextMenu((body(s) + "/###hunk_0").c_str(), "Revert line(s)");
    GG_CHECK(moved(s, r.path, tip));
    GG_CHECK(s.read(r.path, "a.txt").find("LINE 2") == std::string::npos);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == tip; }));
    s.settle();
    selectCommit(s, tip, 2);
    ctx->ItemClick(fileRef(s, "a.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_0").c_str());
    s.contextMenu((body(s) + "/###hunk_0").c_str(), "Move line(s) to working tree");
    GG_CHECK(moved(s, r.path, tip));
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD:a.txt"}).find("LINE 2") == std::string::npos);
    GG_CHECK(s.read(r.path, "a.txt").find("LINE 2") != std::string::npos); // uncommitted now
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--name-only"}), "a.txt");
    // And to the child: HEAD~1 (X) has no line changes left; nothing selectable is fine.
}

GG_TEST("move", "lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit",
    "DIFF-CTX-REVERT", "DIFF-CTX-MOVE-PARENT")
{
    const fs::path repo = s.fixture(Recipe::Empty);
    s.write(repo, "crlf.txt", "one\r\ntwo\r\nthree\r\n");
    s.write(repo, "old.txt", "rename me\nline two\nline three\nline four\nline five\n");
    s.write(repo, "run.sh", "#!/bin/sh\necho one\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "base"});
    const std::string base = s.head(repo);
    s.write(repo, "crlf.txt", "one\r\nTWO\r\nthree\r\n");
    s.write(repo, "added.txt", "new 1\nnew 2\n");
    s.git(repo, {"mv", "old.txt", "renamed.txt"});
    s.write(repo, "renamed.txt", "rename me\nline two\nline three\nline four\nline FIVE\n");
    s.write(repo, "run.sh", "#!/bin/sh\necho two\n");
    fs::permissions(repo / "run.sh", fs::perms::owner_exec, fs::perm_options::add);
    s.git(repo, {"add", "-A"});
    s.git(repo, {"commit", "-q", "-m", "X"});
    const std::string x = s.head(repo);
    s.commitFile(repo, "later.txt", "later\n", "Y");
    const std::string y = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));

    // Each file's hunk reverted in X (Y rebased on top), checked, then undone.
    auto revert = [&](const std::string& path, const std::function<void(const std::string& newX)>& check) {
        selectCommit(s, x, 4);
        ctx->ItemClick(fileRef(s, path).c_str());
        s.showPanel("Diff");
        GG_REQUIRE(s.waitUntil([&] {
            const auto& d = s.session()->diff().diff();
            return d && !d->files.empty() && d->files[0].path() == path && !d->files[0].hunks.empty();
        }));
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
        ctx->ItemClick((body(s) + "/###hunk_0").c_str());
        s.contextMenu((body(s) + "/###hunk_0").c_str(), "Revert line(s)");
        const bool ok = moved(s, repo, y);
        if (!ok)
            ctx->LogError("reverting the lines of %s changed nothing", path.c_str());
        GG_REQUIRE(ok);
        GG_CHECK(s.fsck(repo));
        check(s.revParse(repo, "HEAD~1"));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        GG_CHECK(s.waitUntil([&] { return s.head(repo) == y; }));
        s.settle();
    };
    revert("crlf.txt", [&](const std::string& nx) {
        GG_CHECK_STR_EQ(s.revParse(repo, nx + ":crlf.txt"), s.revParse(repo, base + ":crlf.txt"));
    });
    revert("added.txt", [&](const std::string& nx) {
        GG_CHECK(!s.gitMayFail(repo, {"cat-file", "-e", nx + ":added.txt"}).ok());
    });
    revert("renamed.txt", [&](const std::string& nx) {
        GG_CHECK_STR_EQ(s.revParse(repo, nx + ":renamed.txt"), s.revParse(repo, base + ":old.txt"));
        GG_CHECK(!s.gitMayFail(repo, {"cat-file", "-e", nx + ":old.txt"}).ok());
    });
    revert("run.sh", [&](const std::string& nx) {
        GG_CHECK_STR_EQ(s.gitOut(repo, {"show", nx + ":run.sh"}), "#!/bin/sh\necho one");
        GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-tree", nx, "run.sh"}).substr(0, 6), "100755"); // lines only
    });
    // The renamed file's line to the parent: the parent changes the file at its old path; X keeps
    // the rename, and the tip's tree stays the same.
    const std::string tipTree = s.revParse(repo, "HEAD^{tree}");
    selectCommit(s, x, 4);
    ctx->ItemClick(fileRef(s, "renamed.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_0").c_str());
    s.contextMenu((body(s) + "/###hunk_0").c_str(), "Move line(s) to parent");
    GG_REQUIRE(moved(s, repo, y));
    GG_CHECK(s.gitOut(repo, {"show", "HEAD~2:old.txt"}).find("line FIVE") != std::string::npos);
    GG_CHECK(!s.gitMayFail(repo, {"cat-file", "-e", "HEAD~1:old.txt"}).ok());
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^{tree}"), tipTree);
    // The CRLF file's line to the child (Y): X no longer changes it, Y does; the tip is the same.
    const std::string x2 = s.revParse(repo, "HEAD~1"), tip2 = s.head(repo);
    selectCommit(s, x2, 4);
    ctx->ItemClick(fileRef(s, "crlf.txt").c_str());
    GG_REQUIRE(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == "crlf.txt" && d->query.a.hex() == x2;
    }));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists((body(s) + "/###hunk_0").c_str()); }));
    ctx->ItemClick((body(s) + "/###hunk_0").c_str());
    s.contextMenu((body(s) + "/###hunk_0").c_str(), "Move line(s) to child");
    GG_REQUIRE(moved(s, repo, tip2));
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1:crlf.txt"), s.revParse(repo, base + ":crlf.txt"));
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^{tree}"), tipTree);
}

} // namespace ggtest
