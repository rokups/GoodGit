// Revert and cherry-pick, with and without committing (§4.3).
#include "panels/HistoryPanel.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

// main: c1 adds a.txt, c2 adds b.txt, c3 changes a.txt's second line, c4 adds c.txt.
// side (from c2): s1 adds s.txt (by Other Person), s2 changes a.txt's second line differently.
struct PickRepo {
    fs::path path;
    std::string c1, c2, c3, c4, s1, s2;
};

PickRepo makeRepo(Scenario& s)
{
    PickRepo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    s.commitFile(p, "a.txt", "one\ntwo\nthree\n", "c1 add a");
    r.c1 = s.head(p);
    s.commitFile(p, "b.txt", "b\n", "c2 add b");
    r.c2 = s.head(p);
    s.git(p, {"branch", "side"});
    s.commitFile(p, "a.txt", "one\nTWO\nthree\n", "c3 change a");
    r.c3 = s.head(p);
    s.commitFile(p, "c.txt", "c\n", "c4 add c");
    r.c4 = s.head(p);
    s.git(p, {"switch", "-q", "side"});
    s.write(p, "s.txt", "s\n");
    s.git(p, {"add", "s.txt"});
    s.git(p, {"commit", "-q", "--author=Other Person <other@example.com>", "-m", "s1 add s\n\nWith a body."});
    r.s1 = s.head(p);
    s.commitFile(p, "a.txt", "one\nSIDE\nthree\n", "s2 change a on side");
    r.s2 = s.head(p);
    s.git(p, {"switch", "-q", "main"});
    return r;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

bool rowReady(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
}

bool changed(Scenario& s, const fs::path& repo, const std::string& before)
{
    const bool ok = s.waitUntil([&] { return s.head(repo) != before; });
    s.settle();
    return ok;
}

// A clone for doing the same with plain git (the same commit ids; main checked out).
fs::path twin(Scenario& s, const fs::path& repo)
{
    const fs::path out = repo.parent_path() / (repo.filename().string() + "-plain");
    s.git(repo.parent_path(), {"clone", "-q", repo.string(), out.string()});
    return out;
}

std::string message(Scenario& s, const fs::path& repo, const std::string& rev = "HEAD")
{
    return s.gitOut(repo, {"log", "-1", "--format=%B", rev});
}

std::string revertMsg(Scenario& s, const fs::path& repo, const std::string& id)
{
    return "Revert \"" + s.gitOut(repo, {"log", "-1", "--format=%s", id}) + "\"\n\nThis reverts commit " + id + ".";
}

std::string pickMsg(Scenario& s, const fs::path& repo, const std::string& id)
{
    return message(s, repo, id) + "\n\n(cherry picked from commit " + id + ")";
}

ggui::core::RepoState state(Scenario& s) { return s.session()->snapshot()->state; }

// Opens the Commit dialog for the working tree and returns its pre-filled message.
std::string commitDialogMessage(Scenario& s)
{
    s.ctx->ItemClick("//History/**/###row_wt");
    s.ctx->ItemClick("//###Toolbar/###tb_commit");
    if (!s.dialogOpen("Commit"))
        return "<no dialog>";
    return s.app.dialogs().current()->text("message");
}

// A branch "more" from c2 with three commits (m1, m2, m3 add m1.txt, m2.txt, m3.txt, oldest first);
// main is checked out again.
std::vector<std::string> addMore(Scenario& s, const PickRepo& r)
{
    const fs::path& p = r.path;
    std::vector<std::string> ids;
    s.git(p, {"switch", "-q", "-c", "more", r.c2});
    for (const char* n : {"m1", "m2", "m3"}) {
        s.commitFile(p, std::string(n) + ".txt", std::string(n) + "\n", std::string(n) + " add file");
        ids.push_back(s.head(p));
    }
    s.git(p, {"switch", "-q", "main"});
    return ids;
}

// The label of a menu item of the open menu, without its ID part. The test engine keeps only the
// first 31 characters of a label, so `expected` is cut the same way.
std::string menuLabel(Scenario& s, const char* id, const std::string& expected, std::string* want)
{
    const std::string label = s.itemLabel((std::string("//$FOCUSED/") + id).c_str());
    *want = expected.substr(0, 31);
    return label.substr(0, label.find("###"));
}
#define CHECK_MENU_LABEL(id, expected)                \
    do {                                              \
        std::string want;                             \
        const std::string got = menuLabel(s, id, expected, &want); \
        GG_CHECK_STR_EQ(got, want);                   \
    } while (0)

// Selects the commits: the first by a click, the others with Ctrl.
void selectRows(Scenario& s, const std::vector<std::string>& ids)
{
    for (const auto& id : ids)
        GG_CHECK(rowReady(s, id));
    s.ctx->ItemClick(rowRef(ids.front()).c_str());
    s.ctx->KeyDown(ImGuiMod_Ctrl);
    for (size_t i = 1; i < ids.size(); ++i)
        s.ctx->ItemClick(rowRef(ids[i]).c_str());
    s.ctx->KeyUp(ImGuiMod_Ctrl);
    s.ctx->Yield(2);
}

} // namespace

GG_TEST("revert-pick", "revert and cherry-pick without committing: index, working tree and the Commit dialog's message")
{
    const PickRepo r = makeRepo(s);
    const fs::path plain = twin(s, r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c2));
    // Revert c2: b.txt goes from the index and the working tree; git's REVERT_HEAD state.
    s.contextMenu(rowRef(r.c2).c_str(), "###revert_no_commit");
    GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::Reverting; }));
    s.settle();
    s.git(plain, {"revert", "--no-commit", r.c2});
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"write-tree"}), s.gitOut(plain, {"write-tree"}));
    GG_CHECK(!fs::exists(r.path / "b.txt"));
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
    GG_CHECK_STR_EQ(s.read(r.path / ".git", "MERGE_MSG"), revertMsg(s, r.path, r.c2) + "\n");
    GG_CHECK_STR_EQ(commitDialogMessage(s), revertMsg(s, r.path, r.c2));
    s.dialogButton("Commit", "Commit");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(message(s, r.path), revertMsg(s, r.path, r.c2));
    GG_CHECK(state(s) == ggui::core::RepoState::None);
    s.git(plain, {"commit", "-q", "--no-edit"});
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), s.revParse(plain, "HEAD^{tree}"));

    // Cherry-pick s1: s.txt staged, no state (as plain git), the message waiting for Commit.
    const std::string before = s.head(r.path);
    GG_REQUIRE(rowReady(s, r.s1));
    s.contextMenu(rowRef(r.s1).c_str(), "###cherry_pick_no_commit");
    GG_CHECK(s.waitUntil([&] { return fs::exists(r.path / "s.txt"); }));
    s.settle();
    s.git(plain, {"cherry-pick", "--no-commit", r.s1});
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"write-tree"}), s.gitOut(plain, {"write-tree"}));
    GG_CHECK(state(s) == ggui::core::RepoState::None);
    GG_CHECK_STR_EQ(s.head(r.path), before);
    GG_CHECK_STR_EQ(commitDialogMessage(s), pickMsg(s, r.path, r.s1));
    s.dialogButton("Commit", "Commit");
    GG_CHECK(changed(s, r.path, before));
    GG_CHECK_STR_EQ(message(s, r.path), pickMsg(s, r.path, r.s1));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK(!fs::exists(r.path / ".git" / "MERGE_MSG"));

    // The appended line is not added twice.
    const std::string id = r.s1;
    const std::string once = "Subject\n\nBody\n\n(cherry picked from commit " + id + ")";
    GG_CHECK_STR_EQ(ggui::cherryPickMessage("Subject\n\nBody\n", id), once);
    GG_CHECK_STR_EQ(ggui::cherryPickMessage(once + "\n", id), once);
    GG_CHECK_STR_EQ(ggui::revertMessage("Subject\n\nBody\n", id), "Revert \"Subject\"\n\nThis reverts commit " + id + ".");
    GG_CHECK_STR_EQ(ggui::revertPartMessage("Subject\n\nBody\n", id, "a.txt, b.txt"),
        "Revert \"Subject\"\n\nThis reverts part of commit " + id + ": a.txt, b.txt.");
}

GG_TEST("revert-pick", "Revert and Cherry-pick make a commit: a new commit on HEAD, the author kept, one Undo")
{
    const PickRepo r = makeRepo(s);
    const fs::path plain = twin(s, r.path);
    const std::string me = s.gitOut(r.path, {"config", "user.name"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c3));
    s.contextMenu(rowRef(r.c3).c_str(), "###revert");
    GG_CHECK(changed(s, r.path, r.c4));
    const std::string reverted = s.head(r.path);
    s.git(plain, {"revert", "--no-edit", r.c3});
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), s.revParse(plain, "HEAD^{tree}"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), r.c4);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), reverted); // the attached branch advanced
    GG_CHECK_STR_EQ(message(s, r.path), revertMsg(s, r.path, r.c3));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%an"}), me);
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "one\ntwo\nthree\n");
    GG_CHECK(s.statusPorcelain(r.path).empty());

    // Cherry-pick and commit s1 (a local change elsewhere stays): Other Person stays the author.
    s.write(r.path, "c.txt", "local\n");
    GG_REQUIRE(rowReady(s, r.s1));
    s.contextMenu(rowRef(r.s1).c_str(), "###cherry_pick");
    GG_CHECK(changed(s, r.path, reverted));
    const std::string picked = s.head(r.path);
    s.git(plain, {"cherry-pick", r.s1});
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^{tree}"), s.revParse(plain, "HEAD^{tree}"));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), reverted);
    GG_CHECK_STR_EQ(message(s, r.path), pickMsg(s, r.path, r.s1));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%an <%ae>"}), "Other Person <other@example.com>");
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%cn"}), me);
    GG_CHECK_STR_EQ(s.read(r.path, "s.txt"), "s\n");
    GG_CHECK_STR_EQ(s.read(r.path, "c.txt"), "local\n");

    // One Undo takes the pick back: ref, index and working tree.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == reverted; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), reverted);
    GG_CHECK(!fs::exists(r.path / "s.txt"));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "");
    GG_CHECK_STR_EQ(s.read(r.path, "c.txt"), "local\n");
    s.git(r.path, {"checkout", "--", "c.txt"});

    // On a detached HEAD, from Commit ▸ Selected commit: HEAD advances, branches stay.
    s.git(r.path, {"switch", "-q", "--detach"});
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headDetached; }));
    s.settle();
    ctx->ItemClick(rowRef(r.s1).c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Selected commit/###cherry_pick");
    GG_CHECK(changed(s, r.path, reverted));
    GG_CHECK(s.session()->snapshot()->headDetached);
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), reverted);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), reverted);
}

GG_TEST("revert-pick", "conflicts: and commit lands first-class conflicts; without committing git stops (Abort)")
{
    const PickRepo r = makeRepo(s);
    // c5 changes the line c3 changed: reverting c3 conflicts, and so does picking s2.
    s.commitFile(r.path, "a.txt", "one\nTWO!\nthree\n", "c5 change a again");
    const std::string c5 = s.head(r.path);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.s2));
    // A "now have first-class conflicts" notification newer than toast `after`.
    auto lastToast = [&] { return s.app.toasts().empty() ? std::uint64_t{0} : s.app.toasts().back().id; };
    auto conflictToast = [&](std::uint64_t after) {
        return s.waitUntil([&] {
            for (const auto& t : s.app.toasts())
                if (t.id > after && t.message.find("now have first-class conflicts") != std::string::npos)
                    return true;
            return false;
        });
    };
    std::uint64_t seen = lastToast();
    // Cherry-pick and commit: a commit with first-class conflicts; plain git sees a clean tree.
    s.contextMenu(rowRef(r.s2).c_str(), "###cherry_pick");
    GG_CHECK(changed(s, r.path, c5));
    GG_CHECK(conflictToast(seen));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), c5);
    GG_CHECK_STR_EQ(message(s, r.path), pickMsg(s, r.path, r.s2));
    const std::string file = s.gitOut(r.path, {"show", "HEAD:a.txt"});
    GG_CHECK(file.find("<<<<<<<") != std::string::npos && file.find("SIDE") != std::string::npos
        && file.find("TWO!") != std::string::npos);
    GG_CHECK(s.read(r.path, "a.txt").find("<<<<<<<") != std::string::npos);
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK(s.waitUntil([&] {
        const auto* row = s.session()->history().row(ggui::core::Oid::fromHex(s.head(r.path)));
        return row && row->conflicted;
    }));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == c5; }));
    s.settle();

    // Revert and commit c3: the same.
    seen = lastToast();
    s.contextMenu(rowRef(r.c3).c_str(), "###revert");
    GG_CHECK(changed(s, r.path, c5));
    GG_CHECK(conflictToast(seen));
    GG_CHECK_STR_EQ(message(s, r.path), revertMsg(s, r.path, r.c3));
    GG_CHECK(s.gitOut(r.path, {"show", "HEAD:a.txt"}).find("<<<<<<<") != std::string::npos);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) == c5; }));
    s.settle();

    // Without committing: git's native conflict, the in-progress state and its banner; Abort.
    for (const bool revert : {false, true}) {
        const std::string id = revert ? r.c3 : r.s2;
        s.contextMenu(rowRef(id).c_str(), revert ? "###revert_no_commit" : "###cherry_pick_no_commit");
        const auto expected = revert ? ggui::core::RepoState::Reverting : ggui::core::RepoState::CherryPicking;
        GG_CHECK(s.waitUntil([&] { return state(s) == expected; }));
        s.settle();
        GG_CHECK(!s.gitOut(r.path, {"ls-files", "-u"}).empty());
        GG_CHECK(s.gitOut(r.path, {"status"}).find(revert ? "reverting" : "cherry-picking") != std::string::npos);
        GG_CHECK_STR_EQ(s.read(r.path / ".git", "MERGE_MSG"),
            (revert ? revertMsg(s, r.path, id) : pickMsg(s, r.path, id)) + "\n");
        GG_CHECK(s.itemExists("//###Toolbar/Abort##tb_abort"));
        ctx->ItemClick("//###Toolbar/Abort##tb_abort");
        GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::None; }));
        s.settle();
        GG_CHECK_STR_EQ(s.head(r.path), c5);
        GG_CHECK(s.statusPorcelain(r.path).empty());
    }
}

GG_TEST("revert-pick", "merge commits are reverted and picked against their first parent (-m 1)")
{
    const PickRepo r = makeRepo(s);
    const fs::path& p = r.path;
    // main merges feat (f.txt); "other" (from c2) merges feat2 (g.txt).
    s.git(p, {"switch", "-q", "-c", "feat", r.c2});
    s.commitFile(p, "f.txt", "f\n", "f1 add f");
    s.git(p, {"switch", "-q", "-c", "other", r.c2});
    s.git(p, {"switch", "-q", "-c", "feat2", r.c2});
    s.commitFile(p, "g.txt", "g\n", "g1 add g");
    s.git(p, {"switch", "-q", "other"});
    s.git(p, {"merge", "-q", "--no-ff", "-m", "merge feat2", "feat2"});
    const std::string m2 = s.head(p);
    s.git(p, {"switch", "-q", "main"});
    s.git(p, {"merge", "-q", "--no-ff", "-m", "merge feat", "feat"});
    const std::string m1 = s.head(p);
    s.commitFile(p, "h.txt", "h\n", "c6 add h");
    const std::string c6 = s.head(p);
    const fs::path plain = twin(s, p);
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, m1));
    s.contextMenu(rowRef(m1).c_str(), "###revert");
    GG_CHECK(changed(s, p, c6));
    s.git(plain, {"revert", "--no-edit", "-m", "1", m1});
    GG_CHECK_STR_EQ(s.revParse(p, "HEAD^{tree}"), s.revParse(plain, "HEAD^{tree}"));
    GG_CHECK(!fs::exists(p / "f.txt"));
    GG_CHECK_STR_EQ(message(s, p), revertMsg(s, p, m1));
    const std::string reverted = s.head(p);
    GG_REQUIRE(rowReady(s, m2));
    s.contextMenu(rowRef(m2).c_str(), "###cherry_pick_no_commit");
    GG_CHECK(s.waitUntil([&] { return fs::exists(p / "g.txt"); }));
    s.settle();
    s.git(plain, {"cherry-pick", "--no-commit", "-m", "1", m2});
    GG_CHECK_STR_EQ(s.gitOut(p, {"write-tree"}), s.gitOut(plain, {"write-tree"}));
    GG_CHECK_STR_EQ(s.head(p), reverted);
}

GG_TEST("revert-pick", "refusals: HEAD itself, an ancestor of HEAD, staged changes; a revert that changes nothing")
{
    const PickRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.c4));
    const std::string before = s.gitOut(r.path, {"for-each-ref"});
    // HEAD: nothing to pick (the items are disabled); reverting it is fine.
    ctx->ItemClick(rowRef(r.c4).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/###cherry_pick").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(ctx->ItemInfo("//$FOCUSED/###cherry_pick_no_commit").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/###revert").ItemFlags & ImGuiItemFlags_Disabled));
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/###revert_no_commit").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->KeyPress(ImGuiKey_Escape);
    auto refused = [&](const char* why) {
        GG_CHECK(s.dismissError());
        const bool expected = s.app.errorMessage().find(why) != std::string::npos;
        if (!expected)
            ctx->LogError("expected \"%s\", got \"%s\"", why, s.app.errorMessage().c_str());
        GG_CHECK(expected);
        GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), before);
        GG_CHECK(s.statusPorcelain(r.path).empty());
    };
    // An ancestor of HEAD is already there.
    s.contextMenu(rowRef(r.c2).c_str(), "###cherry_pick");
    refused("already in HEAD's history");
    s.contextMenu(rowRef(r.c2).c_str(), "###cherry_pick_no_commit");
    refused("already in HEAD's history");
    // Staged changes: Abort would drop them, so no-commit runs refuse.
    s.write(r.path, "c.txt", "staged\n");
    s.git(r.path, {"add", "c.txt"});
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->staged.empty(); }));
    s.settle();
    s.contextMenu(rowRef(r.s1).c_str(), "###cherry_pick_no_commit");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("staged changes") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "c.txt");
    s.git(r.path, {"reset", "-q", "--hard"});
    // The index row goes away and the history rows shift: wait for both before clicking.
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.empty(); }));
    s.settle();
    GG_REQUIRE(rowReady(s, r.c2));
    ctx->Yield(3);
    // Reverting c2 twice: the second changes nothing ("Drop them": nothing to change).
    s.contextMenu(rowRef(r.c2).c_str(), "###revert");
    GG_CHECK(changed(s, r.path, r.c4));
    const std::string once = s.head(r.path);
    s.contextMenu(rowRef(r.c2).c_str(), "###revert");
    GG_REQUIRE(s.dialogOpen("Commits become empty"));
    s.dialogButton("Commits become empty", "Drop them");
    GG_CHECK(s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.message.find("Nothing to change") != std::string::npos)
                return true;
        return false;
    }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), once);
}

GG_TEST("revert-pick", "the four items: Cherry-pick and Revert commit, the (no commit) items change the index only")
{
    const PickRepo r = makeRepo(s);
    const auto more = addMore(s, r);
    const std::string me = s.gitOut(r.path, {"config", "user.name"});
    GG_REQUIRE(s.openRepository(r.path));
    GG_REQUIRE(rowReady(s, r.s1));
    // All four exist at once, without Shift, with the count left out for one commit.
    ctx->ItemClick(rowRef(r.s1).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    CHECK_MENU_LABEL("###cherry_pick", "Cherry-pick");
    CHECK_MENU_LABEL("###cherry_pick_no_commit", "Cherry-pick (no commit)");
    CHECK_MENU_LABEL("###revert", "Revert");
    CHECK_MENU_LABEL("###revert_no_commit", "Revert (no commit)");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    // Cherry-pick: a commit on HEAD.
    s.contextMenu(rowRef(r.s1).c_str(), "###cherry_pick");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), r.c4);
    GG_CHECK(fs::exists(r.path / "s.txt"));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%an"}), "Other Person");
    GG_CHECK(s.statusPorcelain(r.path).empty());
    // Revert: a commit on HEAD.
    const std::string picked = s.head(r.path);
    s.contextMenu(rowRef(r.c2).c_str(), "###revert");
    GG_CHECK(changed(s, r.path, picked));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD^"), picked);
    GG_CHECK(!fs::exists(r.path / "b.txt"));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-1", "--format=%an"}), me);
    const std::string reverted = s.head(r.path);
    // Cherry-pick (no commit): the index changes, HEAD stays.
    GG_REQUIRE(rowReady(s, more[0]));
    s.contextMenu(rowRef(more[0]).c_str(), "###cherry_pick_no_commit");
    GG_CHECK(s.waitUntil([&] { return fs::exists(r.path / "m1.txt"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), reverted);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "m1.txt");
    s.git(r.path, {"reset", "-q", "--hard"});
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.empty(); }));
    s.settle();
    GG_REQUIRE(rowReady(s, r.c3));
    ctx->Yield(3);
    // Revert (no commit): the index changes (c3's change to a.txt goes), HEAD stays.
    s.contextMenu(rowRef(r.c3).c_str(), "###revert_no_commit");
    GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::Reverting; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), reverted);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "a.txt");
    GG_CHECK_STR_EQ(s.read(r.path, "a.txt"), "one\ntwo\nthree\n");
}

GG_TEST("revert-pick", "Cherry-pick of three selected commits: three commits, the oldest first, one Undo")
{
    const PickRepo r = makeRepo(s);
    const auto more = addMore(s, r); // oldest first
    GG_REQUIRE(s.openRepository(r.path));
    // The selection and the click order do not matter, and the menu names the count.
    selectRows(s, {more[2], more[0], more[1]});
    ctx->ItemClick(rowRef(more[1]).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    CHECK_MENU_LABEL("###cherry_pick", "Cherry-pick 3 commits");
    CHECK_MENU_LABEL("###cherry_pick_no_commit", "Cherry-pick 3 commits (no commit)");
    GG_CHECK(!(ctx->ItemInfo("//$FOCUSED/###cherry_pick").ItemFlags & ImGuiItemFlags_Disabled));
    ctx->MenuClick("//$FOCUSED/###cherry_pick");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~3"), r.c4);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), s.head(r.path));
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-3", "--format=%s", "--reverse"}), "m1 add file\nm2 add file\nm3 add file");
    GG_CHECK_STR_EQ(message(s, r.path, "HEAD~2"), pickMsg(s, r.path, more[0]));
    GG_CHECK_STR_EQ(message(s, r.path), pickMsg(s, r.path, more[2]));
    GG_CHECK(s.statusPorcelain(r.path).empty());
    // One Undo takes all three back.
    const std::string after = s.head(r.path);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) != after; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
    GG_CHECK_STR_EQ(s.revParse(r.path, "main"), r.c4);
    GG_CHECK(!fs::exists(r.path / "m1.txt") && !fs::exists(r.path / "m3.txt"));
}

GG_TEST("revert-pick", "Revert of two selected commits: two commits, the newest first; one that is refused refuses all")
{
    const PickRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    // c4 (HEAD) and c2, not adjacent: c4 is reverted first, so the revert of c2 is the last commit.
    selectRows(s, {r.c4, r.c2});
    ctx->ItemClick(rowRef(r.c4).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    CHECK_MENU_LABEL("###revert", "Revert 2 commits");
    ctx->MenuClick("//$FOCUSED/###revert");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.c4);
    GG_CHECK_STR_EQ(message(s, r.path), revertMsg(s, r.path, r.c2));
    GG_CHECK_STR_EQ(message(s, r.path, "HEAD~1"), revertMsg(s, r.path, r.c4));
    GG_CHECK(!fs::exists(r.path / "b.txt") && !fs::exists(r.path / "c.txt"));
    // A cherry-pick with a commit that HEAD already has (c2, not the first row): all are refused.
    const std::string before = s.gitOut(r.path, {"for-each-ref"});
    selectRows(s, {r.s1, r.c2});
    ctx->ItemClick(rowRef(r.s1).c_str(), ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/###cherry_pick");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("already in HEAD's history") != std::string::npos);
    GG_CHECK(s.app.errorMessage().find(r.c2.substr(0, 10)) != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"for-each-ref"}), before);
}

GG_TEST("revert-pick", "Cherry-pick (no commit) of two selected commits: both changes in the index, both messages waiting")
{
    const PickRepo r = makeRepo(s);
    const auto more = addMore(s, r);
    GG_REQUIRE(s.openRepository(r.path));
    selectRows(s, {more[2], more[0]});
    ctx->ItemClick(rowRef(more[0]).c_str(), ImGuiMouseButton_Right);
    ctx->Yield(2);
    CHECK_MENU_LABEL("###cherry_pick_no_commit", "Cherry-pick 2 commits (no commit)");
    ctx->MenuClick("//$FOCUSED/###cherry_pick_no_commit");
    GG_CHECK(s.waitUntil([&] { return fs::exists(r.path / "m1.txt") && fs::exists(r.path / "m3.txt"); }));
    s.settle();
    GG_CHECK(!fs::exists(r.path / "m2.txt"));
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "m1.txt\nm3.txt");
    // The oldest first, a blank line between.
    GG_CHECK_STR_EQ(s.read(r.path / ".git", "MERGE_MSG"), pickMsg(s, r.path, more[0]) + "\n\n" + pickMsg(s, r.path, more[2]) + "\n");
    GG_CHECK_STR_EQ(commitDialogMessage(s), pickMsg(s, r.path, more[0]) + "\n\n" + pickMsg(s, r.path, more[2]));
}

GG_TEST("revert-pick", "Cherry-pick (no commit) of three selected commits stops at the one with a conflict and says so; Abort restores HEAD")
{
    const PickRepo r = makeRepo(s);
    // c5 changes the line s2 changes: s1 applies, s2 conflicts, s3 (after it on side) is not applied.
    s.commitFile(r.path, "a.txt", "one\nTWO!\nthree\n", "c5 change a again");
    const std::string c5 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "side"});
    s.commitFile(r.path, "s3.txt", "s3\n", "s3 add s3");
    const std::string s3 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    selectRows(s, {s3, r.s2, r.s1});
    ctx->ItemClick(rowRef(r.s2).c_str(), ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/###cherry_pick_no_commit");
    GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::CherryPicking; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), c5);
    GG_CHECK(fs::exists(r.path / "s.txt")); // the first commit (s1) is in
    GG_CHECK(!fs::exists(r.path / "s3.txt")); // the third is not applied
    GG_CHECK(!s.gitOut(r.path, {"ls-files", "-u"}).empty());
    GG_CHECK_STR_EQ(s.revParse(r.path, "CHERRY_PICK_HEAD"), r.s2); // the commit that stopped
    GG_CHECK_STR_EQ(s.read(r.path / ".git", "MERGE_MSG"), pickMsg(s, r.path, r.s1) + "\n\n" + pickMsg(s, r.path, r.s2) + "\n");
    // The notice names the commit and the commits left.
    const std::string expected = "The cherry-pick has conflicts in commit " + r.s2.substr(0, 10)
        + " (2 of 3). 1 commit is not applied. Resolve the conflicts, then Continue (or commit), or Abort.";
    bool told = false;
    for (const auto& t : s.app.toasts())
        told = told || t.message == expected;
    GG_CHECK(told);
    ctx->ItemClick("//###Toolbar/Abort##tb_abort");
    GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::None; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), c5);
    GG_CHECK(s.statusPorcelain(r.path).empty());
    GG_CHECK(!fs::exists(r.path / "s.txt"));
}

GG_TEST("revert-pick", "Cherry-pick (no commit): a commit that git refuses without a conflict undoes the commits before it")
{
    const PickRepo r = makeRepo(s);
    // "two": u1 adds u1.txt, u2 changes b.txt. A local change to b.txt makes git refuse u2.
    s.git(r.path, {"switch", "-q", "-c", "two", r.c2});
    s.commitFile(r.path, "u1.txt", "u1\n", "u1 add file");
    const std::string u1 = s.head(r.path);
    s.commitFile(r.path, "b.txt", "b2\n", "u2 change b");
    const std::string u2 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    s.write(r.path, "b.txt", "local\n");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->unstaged.empty(); }));
    s.settle();
    selectRows(s, {u2, u1});
    ctx->ItemClick(rowRef(u2).c_str(), ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/###cherry_pick_no_commit");
    GG_CHECK(s.dismissError());
    const std::string why = "commit " + u2.substr(0, 10) + " (2 of 2) failed: ";
    const bool named = s.app.errorMessage().find(why) != std::string::npos
        && s.app.errorMessage().find("The earlier commits are undone.") != std::string::npos;
    if (!named)
        ctx->LogError("unexpected error \"%s\"", s.app.errorMessage().c_str());
    GG_CHECK(named);
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "");
    GG_CHECK_STR_EQ(s.read(r.path, "b.txt"), "local\n"); // the local change is intact
    GG_CHECK(!fs::exists(r.path / "u1.txt"));            // the first commit is undone
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
    GG_CHECK(state(s) == ggui::core::RepoState::None);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"status", "--porcelain"}), "M b.txt"); // unstaged only (gitOut trims the leading space)
}

GG_TEST("revert-pick", "Revert (no commit) of two selected commits: both inverse changes in the index, both messages waiting")
{
    const PickRepo r = makeRepo(s);
    GG_REQUIRE(s.openRepository(r.path));
    selectRows(s, {r.c4, r.c2});
    ctx->ItemClick(rowRef(r.c2).c_str(), ImGuiMouseButton_Right);
    CHECK_MENU_LABEL("###revert_no_commit", "Revert 2 commits (no commit)");
    ctx->MenuClick("//$FOCUSED/###revert_no_commit");
    GG_CHECK(s.waitUntil([&] { return state(s) == ggui::core::RepoState::Reverting; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
    GG_CHECK(s.gitOut(r.path, {"ls-files", "-u"}).empty());
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"diff", "--cached", "--name-only"}), "b.txt\nc.txt");
    GG_CHECK(!fs::exists(r.path / "b.txt") && !fs::exists(r.path / "c.txt"));
    // The newest first, a blank line between.
    GG_CHECK_STR_EQ(s.read(r.path / ".git", "MERGE_MSG"), revertMsg(s, r.path, r.c4) + "\n\n" + revertMsg(s, r.path, r.c2) + "\n");
}

GG_TEST("revert-pick", "Cherry-pick of three selected commits where the middle one becomes empty: the question, then two commits")
{
    const PickRepo r = makeRepo(s);
    // "dup": d1 adds d1.txt, d2 adds c.txt as c4 does (empty on main), d3 adds d3.txt.
    s.git(r.path, {"switch", "-q", "-c", "dup", r.c2});
    s.commitFile(r.path, "d1.txt", "d1\n", "d1 add file");
    const std::string d1 = s.head(r.path);
    s.commitFile(r.path, "c.txt", "c\n", "d2 add c as c4 does");
    const std::string d2 = s.head(r.path);
    s.commitFile(r.path, "d3.txt", "d3\n", "d3 add file");
    const std::string d3 = s.head(r.path);
    s.git(r.path, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(r.path));
    selectRows(s, {d3, d2, d1});
    ctx->ItemClick(rowRef(d2).c_str(), ImGuiMouseButton_Right);
    ctx->MenuClick("//$FOCUSED/###cherry_pick");
    GG_REQUIRE(s.dialogOpen("Commits become empty"));
    s.dialogButton("Commits become empty", "Drop them");
    GG_CHECK(changed(s, r.path, r.c4));
    GG_CHECK_STR_EQ(s.revParse(r.path, "HEAD~2"), r.c4);
    GG_CHECK_STR_EQ(s.gitOut(r.path, {"log", "-2", "--format=%s", "--reverse"}), "d1 add file\nd3 add file");
    GG_CHECK(fs::exists(r.path / "d1.txt") && fs::exists(r.path / "d3.txt"));
    // One Undo takes both back.
    const std::string after = s.head(r.path);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(r.path) != after; }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(r.path), r.c4);
}

} // namespace ggtest
