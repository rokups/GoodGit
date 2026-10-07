// Edit commit in place (§4.3): detach at a commit, amend it, the descendants and branches restack.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

// a adds a.txt, b changes line 2 of a.txt and adds b.txt, c adds c.txt (main = c);
// "feat" branches off c with d adding d.txt.
struct StackRepo {
    fs::path path;
    std::string a, b, c, d;
};

StackRepo makeStack(Scenario& s, const std::string& cA = "1\n2\n3\n")
{
    StackRepo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    s.commitFile(p, "a.txt", "1\n2\n3\n", "a add a");
    r.a = s.head(p);
    s.write(p, "a.txt", "1\nB\n3\n");
    s.write(p, "b.txt", "b\n");
    s.git(p, {"add", "a.txt", "b.txt"});
    s.git(p, {"commit", "-q", "-m", "b change a, add b"});
    r.b = s.head(p);
    s.write(p, "a.txt", cA == "1\n2\n3\n" ? "1\nB\n3\n" : cA);
    s.write(p, "c.txt", "c\n");
    s.git(p, {"add", "a.txt", "c.txt"});
    s.git(p, {"commit", "-q", "-m", "c add c"});
    r.c = s.head(p);
    s.git(p, {"switch", "-q", "-c", "feat"});
    s.commitFile(p, "d.txt", "d\n", "d add d");
    r.d = s.head(p);
    s.git(p, {"switch", "-q", "main"});
    return r;
}

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

bool rowReady(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(hex)) != nullptr; });
}

bool editing(Scenario& s, const std::string& commit, const std::string& branch)
{
    return s.waitUntil([&] {
        const auto& e = s.session()->editSession();
        return e && e->commit == commit && e->branch == branch;
    });
}

bool detached(Scenario& s, const fs::path& repo) { return !s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"}).ok(); }

// No session file left under <git dir>/gg/edit.
bool noSessionFile(const fs::path& repo)
{
    const fs::path dir = repo / ".git" / "gg" / "edit";
    std::error_code ec;
    return !fs::exists(dir, ec) || fs::is_empty(dir, ec);
}

// Refs (with symbolic HEAD) and the index.
std::string repoState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    state += "\n" + s.gitOut(repo, {"ls-files", "-s"});
    return state;
}

std::uint64_t lastToast(Scenario& s)
{
    std::uint64_t id = 0;
    for (const auto& t : s.app.toasts())
        id = std::max(id, t.id);
    return id;
}

bool noticeSays(Scenario& s, std::uint64_t after, const std::string& text, float seconds = 20.0f)
{
    return s.waitUntil([&] {
        for (const auto& t : s.app.toasts())
            if (t.id > after && t.message.find(text) != std::string::npos)
                return true;
        return false;
    }, seconds);
}

// Right-click `id` > Edit commit and wait for the session on `branch`.
bool editCommit(Scenario& s, const fs::path& repo, const std::string& id, const std::string& branch)
{
    if (!rowReady(s, id))
        return false;
    s.contextMenu(rowRef(id).c_str(), "Edit commit (checkout detached)");
    return editing(s, id, branch) && s.head(repo) == id;
}

void amendStaged(ImGuiTestContext* ctx, Scenario& s)
{
    ctx->MenuClick("//##MainMenuBar/Commit/Commit...");
    if (s.dialogOpen("Commit")) {
        s.dialogCheck("Commit", "amend", "Amend");
        s.dialogButton("Commit", "Amend");
    }
}


// f.txt of `n` lines ("1".."n") with the given lines (1-based) replaced.
std::string lines(const std::map<int, std::string>& changed, int n = 10)
{
    std::string text;
    for (int i = 1; i <= n; ++i) {
        auto it = changed.find(i);
        text += (it == changed.end() ? std::to_string(i) : it->second) + "\n";
    }
    return text;
}

std::string lineOf(const std::string& text, int n)
{
    std::istringstream in(text);
    std::string line;
    for (int i = 1; std::getline(in, line); ++i)
        if (i == n)
            return line;
    return "<missing>";
}

// b (root: f.txt 1..10) on main; "feature" from b with f1; main gets a; m merges feature into
// main; t (another file) follows m.
struct MergeRepo {
    fs::path path;
    std::string b, f1, a, m, t;
};

// `f1Lines` / `aLines` are the lines each side changes; `resolution` (when given) is the text the
// merge is resolved to by hand.
MergeRepo makeMerge(Scenario& s, const std::map<int, std::string>& f1Lines, const std::map<int, std::string>& aLines,
    const std::optional<std::string>& resolution = std::nullopt)
{
    MergeRepo r;
    r.path = s.fixture(Recipe::Empty);
    const fs::path& p = r.path;
    s.commitFile(p, "f.txt", lines({}), "b add f");
    r.b = s.head(p);
    s.git(p, {"switch", "-q", "-c", "feature"});
    s.commitFile(p, "f.txt", lines(f1Lines), "f1 feature change");
    r.f1 = s.head(p);
    s.git(p, {"switch", "-q", "main"});
    s.commitFile(p, "f.txt", lines(aLines), "a main change");
    r.a = s.head(p);
    if (resolution) {
        s.gitMayFail(p, {"merge", "--no-ff", "-q", "-m", "m merge feature", "feature"});
        s.write(p, "f.txt", *resolution);
        s.git(p, {"add", "f.txt"});
        s.git(p, {"commit", "-q", "-m", "m merge feature"});
    } else {
        s.git(p, {"merge", "--no-ff", "-q", "-m", "m merge feature", "feature"});
    }
    r.m = s.head(p);
    s.commitFile(p, "t.txt", "t\n", "t after merge");
    r.t = s.head(p);
    return r;
}

std::string fileAt(Scenario& s, const fs::path& p, const std::string& rev) { return s.gitOut(p, {"show", rev + ":f.txt"}); }

// A clone with "origin/feature" two commits (f1, f2) past main and no local "feature" yet.
struct RemoteRepo {
    fs::path path;
    std::string f1, f2;
};

RemoteRepo makeRemoteFeature(Scenario& s)
{
    RemoteRepo r;
    r.path = s.fixture(Recipe::WithRemote);
    const fs::path& p = r.path;
    s.git(p, {"switch", "-q", "-c", "feature", "main"});
    s.commitFile(p, "f1.txt", "f1\n", "f1 feature");
    r.f1 = s.head(p);
    s.git(p, {"push", "-q", "origin", "feature"});
    s.commitFile(p, "local-only.txt", "f2\n", "f2 feature");
    r.f2 = s.head(p);
    s.git(p, {"push", "-q", "origin", "feature"});
    s.git(p, {"switch", "-q", "main"});
    s.git(p, {"branch", "-q", "-D", "feature"});
    return r;
}

// A branch step that fails after the switch: the local feature is behind origin/feature, and the
// worktree is in the middle of a rebase of it, so "git branch -f" refuses.
void rebasingWorktree(Scenario& s, const RemoteRepo& r)
{
    const fs::path& p = r.path;
    s.git(p, {"branch", "-q", "feature", r.f1});
    s.commitFile(p, "m.txt", "m\n", "m main");
    const fs::path wt = p.string() + "-feature-wt";
    s.git(p, {"worktree", "add", "-q", wt.string(), "feature"});
    s.gitMayFail(wt, {"rebase", "-x", "false", "main"});
}

// A regular file where the directory of the session files is, so the session file cannot be
// written (after the switch and the branch step).
void blockSessionFile(const fs::path& repo)
{
    const fs::path dir = repo / ".git" / "gg";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove(dir / "edit", ec); // an empty directory
    std::ofstream(dir / "edit").put('x');
}

// The harness checks the entries under <git dir>/gg at the end of a test.
void unblockSessionFile(const fs::path& repo)
{
    std::error_code ec;
    fs::remove(repo / ".git" / "gg" / "edit", ec);
}

} // namespace

GG_TEST("edit-in-place", "edit a mid-stack commit: descendants and branches restack, Return goes back")
{
    const StackRepo r = makeStack(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    GG_CHECK(detached(s, p));
    GG_CHECK_EQ(s.session()->editSession()->descendants, 2);
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//###Toolbar/###tb_edit"); }));

    s.write(p, "b.txt", "b amended\n");
    s.git(p, {"add", "b.txt"});
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.head(p) != r.b && s.revParse(p, "main") != r.c; }));
    s.settle();
    const std::string nb = s.head(p);
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "HEAD~1"), r.a);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1"), nb);
    GG_CHECK_STR_EQ(s.revParse(p, "feat~1"), s.revParse(p, "main"));
    GG_CHECK_STR_EQ(s.gitOut(p, {"show", "main:b.txt"}), "b amended");
    GG_CHECK_STR_EQ(s.gitOut(p, {"show", "feat:d.txt"}), "d");
    GG_CHECK_STR_EQ(s.gitOut(p, {"log", "-1", "--format=%s", "main"}), "c add c");
    GG_CHECK(editing(s, nb, "main"));

    ctx->ItemClick("//###Toolbar/Return to main##tb_edit_return");
    GG_CHECK(s.waitUntil([&] { return !detached(s, p) && !s.session()->editSession(); }));
    s.settle();
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(p, {"symbolic-ref", "HEAD"})), "refs/heads/main");
    GG_CHECK_STR_EQ(s.head(p), s.revParse(p, "main"));
    GG_CHECK(noSessionFile(p));
    GG_CHECK(!s.itemExists("//###Toolbar/###tb_edit"));
}

GG_TEST("edit-in-place", "a conflicting descendant restacks with a first-class conflict")
{
    // c changes the line b changed; amending b's line conflicts with c.
    const StackRepo r = makeStack(s, "1\nC\n3\n");
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    s.write(p, "a.txt", "1\nX\n3\n");
    s.git(p, {"add", "a.txt"});
    const std::uint64_t seen = lastToast(s);
    amendStaged(ctx, s);
    GG_CHECK(noticeSays(s, seen, "now have first-class conflicts"));
    s.settle();
    const std::string nb = s.head(p);
    GG_CHECK(nb != r.b);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1"), nb);
    GG_CHECK_STR_EQ(s.revParse(p, "feat~1"), s.revParse(p, "main"));
    GG_CHECK(s.revParse(p, "main") != r.c);
    GG_CHECK(editing(s, nb, "main"));
}

GG_TEST("edit-in-place", "a restack that cannot complete leaves the repository unchanged")
{
    // b adds a binary file c changes; amending it gives a conflict the restack cannot record.
    const fs::path p = s.fixture(Recipe::Empty);
    s.commitFile(p, "a.txt", "a\n", "a");
    s.write(p, "bin.dat", std::string("B\0\1\2", 4));
    s.git(p, {"add", "bin.dat"});
    s.git(p, {"commit", "-q", "-m", "b add bin"});
    const std::string b = s.head(p);
    s.write(p, "bin.dat", std::string("C\0\1\2", 4));
    s.git(p, {"add", "bin.dat"});
    s.git(p, {"commit", "-q", "-m", "c change bin"});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, b, "main"));
    s.write(p, "bin.dat", std::string("X\0\1\2", 4));
    s.git(p, {"add", "bin.dat"});
    const std::string before = repoState(s, p);
    amendStaged(ctx, s);
    GG_CHECK(s.waitUntil([&] { return s.app.errorMessage().find("binary conflict in bin.dat") != std::string::npos; }));
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(editing(s, b, "main"));
}

GG_TEST("edit-in-place", "one Undo reverts the amend and the restack")
{
    const StackRepo r = makeStack(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    s.write(p, "b.txt", "b amended\n");
    s.git(p, {"add", "b.txt"});
    s.settle();
    const std::string before = repoState(s, p);
    const size_t ops = s.session()->operations().size();
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(p, "main") != r.c; }));
    s.settle();
    GG_CHECK_EQ(s.session()->operations().size(), ops + 1);
    ctx->MenuClick("//##MainMenuBar/Edit/Undo");
    GG_CHECK(s.waitUntil([&] { return repoState(s, p) == before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(p, "main"), r.c);
    GG_CHECK_STR_EQ(s.revParse(p, "feat"), r.d);
    GG_CHECK_STR_EQ(s.head(p), r.b);
}

GG_TEST("edit-in-place", "E on a mid-stack commit starts an edit session")
{
    const StackRepo r = makeStack(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.b));
    ctx->ItemClick(rowRef(r.b).c_str());
    ctx->KeyPress(ImGuiKey_E);
    GG_CHECK(editing(s, r.b, "main"));
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.head(p), r.b);
}

GG_TEST("edit-in-place", "the session is cleared when HEAD is no longer detached or the branch is gone")
{
    const StackRepo r = makeStack(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    // Switching back outside ggui ends the session.
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    GG_CHECK(!noSessionFile(p));
    s.git(p, {"switch", "-q", "main"});
    GG_CHECK(s.waitUntil([&] { return !s.session()->editSession(); }));
    GG_CHECK(noSessionFile(p));
    // Deleting the branch while detached ends it too.
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    s.git(p, {"branch", "-q", "-D", "main"});
    GG_CHECK(s.waitUntil([&] { return !s.session()->editSession(); }));
    GG_CHECK(noSessionFile(p));
    // Stop editing drops a live session and keeps HEAD where it is.
    GG_REQUIRE(editCommit(s, p, r.d, "feat"));
    ctx->ItemClick("//###Toolbar/Stop editing##tb_edit_stop");
    GG_CHECK(s.waitUntil([&] { return !s.session()->editSession(); }));
    GG_CHECK(noSessionFile(p));
    GG_CHECK_STR_EQ(s.head(p), r.d);
}


GG_TEST("edit-in-place", "amending a commit on a merged branch carries the change through the merge")
{
    const MergeRepo r = makeMerge(s, {{3, "F1"}}, {{8, "A"}});
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.f1, "main"));
    s.write(p, "f.txt", lines({{3, "F1-amended"}}));
    s.git(p, {"add", "f.txt"});
    s.settle();
    const std::string before = repoState(s, p);
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(p, "main") != r.t; }));
    s.settle();
    const std::string nf1 = s.revParse(p, "feature");
    GG_CHECK(nf1 != r.f1);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^1"), r.a);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^2"), nf1);
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "main~1^3"}).ok());
    GG_CHECK_STR_EQ(lineOf(fileAt(s, p, "main~1"), 3), "F1-amended");
    GG_CHECK_STR_EQ(lineOf(fileAt(s, p, "main"), 3), "F1-amended");
    GG_CHECK_STR_EQ(lineOf(fileAt(s, p, "main"), 8), "A");
    GG_CHECK_STR_EQ(s.gitOut(p, {"show", "main:t.txt"}), "t");
    // One Undo restores every ref.
    ctx->MenuClick("//##MainMenuBar/Edit/Undo");
    GG_CHECK(s.waitUntil([&] { return repoState(s, p) == before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(p, "main"), r.t);
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f1);
}

GG_TEST("edit-in-place", "amending the first-parent side of a merge still reaches the tip")
{
    const MergeRepo r = makeMerge(s, {{3, "F1"}}, {{8, "A"}});
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.a, "main"));
    s.write(p, "f.txt", lines({{8, "A-amended"}}));
    s.git(p, {"add", "f.txt"});
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(p, "main") != r.t; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^1"), s.head(p));
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^2"), r.f1);
    GG_CHECK_STR_EQ(lineOf(fileAt(s, p, "main"), 8), "A-amended");
    GG_CHECK_STR_EQ(lineOf(fileAt(s, p, "main"), 3), "F1");
}

GG_TEST("edit-in-place", "a merge's hand resolution survives an amend of a merged commit elsewhere")
{
    const std::string resolved = lines({{5, "resolved"}, {11, "evil"}}, 11);
    const MergeRepo r = makeMerge(s, {{5, "feat"}}, {{5, "main"}}, resolved);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.f1, "main"));
    s.write(p, "f.txt", lines({{2, "F1-line2"}, {5, "feat"}}));
    s.git(p, {"add", "f.txt"});
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(p, "main") != r.t; }));
    s.settle();
    const std::string tip = fileAt(s, p, "main");
    GG_CHECK_STR_EQ(lineOf(tip, 2), "F1-line2");
    GG_CHECK_STR_EQ(lineOf(tip, 5), "resolved");
    GG_CHECK_STR_EQ(lineOf(tip, 11), "evil");
    GG_CHECK(tip.find("<<<<<<<") == std::string::npos);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^2"), s.revParse(p, "feature"));
}

GG_TEST("edit-in-place", "an amend that clashes with a merge's resolution records a first-class conflict")
{
    const std::string resolved = lines({{5, "resolved"}, {11, "evil"}}, 11);
    const MergeRepo r = makeMerge(s, {{5, "feat"}}, {{5, "main"}}, resolved);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.f1, "main"));
    s.write(p, "f.txt", lines({{5, "feat-amended"}}));
    s.git(p, {"add", "f.txt"});
    const std::uint64_t seen = lastToast(s);
    amendStaged(ctx, s);
    GG_CHECK(noticeSays(s, seen, "now have first-class conflicts"));
    s.settle();
    GG_CHECK(s.revParse(p, "main") != r.t);
    const std::string merged = fileAt(s, p, "main~1");
    GG_CHECK(merged.find("<<<<<<<") != std::string::npos);
    GG_CHECK(merged.find("resolved") != std::string::npos);
    GG_CHECK(merged.find("feat-amended") != std::string::npos);
    GG_CHECK(merged.find("evil") != std::string::npos);
    GG_CHECK_STR_EQ(s.revParse(p, "main~1^2"), s.revParse(p, "feature"));
}

GG_TEST("edit-in-place", "amending below a merge base reaches the tip once, without a conflict")
{
    const MergeRepo r = makeMerge(s, {{3, "F1"}}, {{8, "A"}});
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    s.write(p, "f.txt", lines({{10, "B-amended"}}));
    s.git(p, {"add", "f.txt"});
    amendStaged(ctx, s);
    GG_REQUIRE(s.waitUntil([&] { return s.revParse(p, "main") != r.t; }));
    s.settle();
    const std::string tip = fileAt(s, p, "main");
    GG_CHECK_STR_EQ(lineOf(tip, 10), "B-amended");
    GG_CHECK_STR_EQ(lineOf(tip, 3), "F1");
    GG_CHECK_STR_EQ(lineOf(tip, 8), "A");
    GG_CHECK(tip.find("<<<<<<<") == std::string::npos);
    GG_CHECK(s.revParse(p, "main~1^1") != r.a);
    GG_CHECK(s.revParse(p, "main~1^2") != r.f1);
}

GG_TEST("edit-in-place", "Edit commit on a commit only a remote-tracking branch has makes the local branch; Undo removes it")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/feature"}).ok());
    s.settle();
    const std::string before = repoState(s, p);
    GG_REQUIRE(editCommit(s, p, r.f2, "feature"));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f2);
    GG_CHECK_STR_EQ(s.gitOut(p, {"rev-parse", "--abbrev-ref", "feature@{upstream}"}), "origin/feature");
    ctx->MenuClick("//##MainMenuBar/Edit/Undo");
    GG_CHECK(s.waitUntil([&] { return repoState(s, p) == before; }));
    s.settle();
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/feature"}).ok());
    GG_CHECK(!detached(s, p));
}

GG_TEST("edit-in-place", "Edit commit moves a local branch that is behind the remote-tracking branch")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.git(p, {"branch", "-q", "feature", r.f1});
    GG_REQUIRE(s.openRepository(p));
    s.settle();
    const std::string before = repoState(s, p);
    GG_REQUIRE(editCommit(s, p, r.f2, "feature"));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f2);
    // The upstream of an existing branch is not set by the fast-forward.
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "--abbrev-ref", "feature@{upstream}"}).ok());
    ctx->MenuClick("//##MainMenuBar/Edit/Undo");
    GG_CHECK(s.waitUntil([&] { return repoState(s, p) == before; }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f1);
}

GG_TEST("edit-in-place", "Edit commit on a branch that has diverged from the remote-tracking branch is refused")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.git(p, {"switch", "-q", "-c", "feature", r.f1});
    s.commitFile(p, "g.txt", "g\n", "g local");
    s.git(p, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    GG_REQUIRE(s.dialogOpen(("edit " + r.f2.substr(0, 10)).c_str()));
    GG_CHECK_STR_EQ(s.app.errorMessage(), "The commit is on origin/feature, but the local branch feature has diverged from it");
    s.dialogButton(("edit " + r.f2.substr(0, 10)).c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!s.session()->editSession());
    GG_CHECK(noSessionFile(p));
}

GG_TEST("edit-in-place", "a failed switch of Edit commit leaves no local branch; Stash and switch then makes it")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.write(p, "local-only.txt", "my local edit\n");
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    GG_REQUIRE(s.dialogOpen("Stash and switch"));
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/feature"}).ok());
    GG_CHECK(!detached(s, p));
    s.dialogButton("Stash and switch", "Stash and switch");
    GG_CHECK(editing(s, r.f2, "feature"));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f2);
}

GG_TEST("edit-in-place", "Edit commit on the tip of origin/main moves main, which is behind it, and ignores origin/HEAD")
{
    const fs::path p = s.fixture(Recipe::WithRemote);
    s.git(p, {"remote", "set-head", "origin", "main"});
    s.git(p, {"reset", "-q", "--hard", "origin/main~1"});
    const std::string tip = s.revParse(p, "origin/main");
    GG_REQUIRE(s.revParse(p, "origin/HEAD") == tip);
    const std::string upstream = s.gitOut(p, {"rev-parse", "--abbrev-ref", "main@{upstream}"});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, tip, "main"));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "main"), tip);
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/HEAD"}).ok());
    GG_CHECK_STR_EQ(s.gitOut(p, {"rev-parse", "--abbrev-ref", "main@{upstream}"}), upstream);
}

GG_TEST("edit-in-place", "Edit commit is refused when another worktree has the local branch behind the remote-tracking branch")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    const fs::path wt = p.string() + "-feature-wt";
    s.git(p, {"branch", "-q", "feature", r.f1});
    s.git(p, {"worktree", "add", "-q", wt.string(), "feature"});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK(s.app.errorMessage().find("another worktree has the local branch feature") != std::string::npos);
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!s.session()->editSession());
}

GG_TEST("edit-in-place", "Edit commit rolls back when another worktree rebases the local branch")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    rebasingWorktree(s, r);
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    const std::string headBefore = s.head(p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK(s.app.errorMessage().find("cannot force update") != std::string::npos);
    GG_CHECK(s.app.errorMessage().find("The rollback stopped") == std::string::npos);
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK_STR_EQ(s.head(p), headBefore);
    GG_CHECK(!detached(s, p));
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(p, {"symbolic-ref", "--short", "HEAD"})), "main");
    GG_CHECK(!s.session()->editSession());
    GG_CHECK(noSessionFile(p));
    GG_CHECK(s.gitOut(p, {"stash", "list"}).empty());
}

GG_TEST("edit-in-place", "Edit commit rolls back the stash when the branch step fails after Stash and switch")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    rebasingWorktree(s, r);
    s.write(p, "local-only.txt", "my local edit\n");
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    const std::string headBefore = s.head(p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    GG_REQUIRE(s.dialogOpen("Stash and switch"));
    s.dialogButton("Stash and switch", "Stash and switch");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK(s.app.errorMessage().find("cannot force update") != std::string::npos);
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK_STR_EQ(s.head(p), headBefore);
    GG_CHECK(!detached(s, p));
    GG_CHECK_STR_EQ(s.read(p, "local-only.txt"), "my local edit\n");
    GG_CHECK(s.gitOut(p, {"stash", "list"}).empty());
    GG_CHECK(!s.session()->editSession());
    GG_CHECK(noSessionFile(p));
}

GG_TEST("edit-in-place", "Edit commit: a branch that a hook refuses leaves no branch")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    const fs::path hooks = p.string() + "-hooks";
    fs::create_directories(hooks);
    s.write(hooks, "reference-transaction",
        "#!/bin/sh\n[ \"$1\" = prepared ] || exit 0\nwhile read old new ref; do\n  [ \"$ref\" = refs/heads/feature ] && exit 1\ndone\nexit 0\n");
    fs::permissions(hooks / "reference-transaction", fs::perms::owner_all);
    s.git(p, {"config", "core.hooksPath", hooks.generic_string()});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK(!s.app.errorMessage().empty());
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!detached(s, p));
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/feature"}).ok());
    GG_CHECK(!s.session()->editSession());
    GG_CHECK(noSessionFile(p));
}

GG_TEST("edit-in-place", "Edit commit: a session file that cannot be written removes the branch that it made")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    blockSessionFile(p);
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK_STR_EQ(s.app.errorMessage(), "The edit session file was not written");
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!detached(s, p));
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/feature"}).ok());
    GG_CHECK(!s.session()->editSession());
    unblockSessionFile(p);
}

GG_TEST("edit-in-place", "Edit commit: a session file that cannot be written puts the fast-forwarded branch back")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.git(p, {"branch", "-q", "feature", r.f1});
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    blockSessionFile(p);
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK_STR_EQ(s.app.errorMessage(), "The edit session file was not written");
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f1);
    GG_CHECK(!s.session()->editSession());
    unblockSessionFile(p);
}

namespace {

// Edit commit on r.f2 is refused with `text`; HEAD, the refs and the session are as before.
void refusedEdit(Scenario& s, const RemoteRepo& r, const std::string& text)
{
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    const std::string before = repoState(s, p);
    s.contextMenu(rowRef(r.f2).c_str(), "Edit commit (checkout detached)");
    const std::string title = "edit " + r.f2.substr(0, 10);
    GG_REQUIRE(s.dialogOpen(title.c_str()));
    GG_CHECK_STR_EQ(s.app.errorMessage(), text);
    s.dialogButton(title.c_str(), "OK");
    s.settle();
    GG_CHECK_STR_EQ(repoState(s, p), before);
    GG_CHECK(!detached(s, p));
    GG_CHECK(!s.session()->editSession());
    GG_CHECK(noSessionFile(p));
}

} // namespace

GG_TEST("edit-in-place", "Edit commit is refused when a branch below the new branch name exists")
{
    const RemoteRepo r = makeRemoteFeature(s);
    s.git(r.path, {"branch", "-q", "feature/x", "main"});
    refusedEdit(s, r, "The commit is on origin/feature, but the branch feature/x is in the way of feature");
}

GG_TEST("edit-in-place", "Edit commit is refused when a branch at a prefix of the new branch name exists")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.git(p, {"push", "-q", "origin", ":feature"});
    s.git(p, {"push", "-q", "origin", r.f2 + ":refs/heads/feature/x"});
    s.git(p, {"branch", "-q", "feature", "main"});
    GG_REQUIRE(s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/remotes/origin/feature/x"}).ok());
    refusedEdit(s, r, "The commit is on origin/feature/x, but the branch feature is in the way of feature/x");
}

GG_TEST("edit-in-place", "Edit commit on a remote with a slash in its name makes the branch without the remote")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    s.git(p, {"remote", "rename", "origin", "up/stream"});
    GG_REQUIRE(s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/remotes/up/stream/feature"}).ok());
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.f2, "feature"));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f2);
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "-q", "--verify", "refs/heads/stream/feature"}).ok());
    GG_CHECK_STR_EQ(s.gitOut(p, {"rev-parse", "--abbrev-ref", "feature@{upstream}"}), "up/stream/feature");
}

GG_TEST("edit-in-place", "Edit commit: an upstream that cannot be set is a notice, not an error")
{
    const RemoteRepo r = makeRemoteFeature(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(rowReady(s, r.f2));
    s.settle();
    // Each write of the configuration fails while the lock file exists.
    const fs::path lock = p / ".git" / "config.lock";
    std::ofstream(lock).put('\n');
    const std::uint64_t seen = lastToast(s);
    const bool started = editCommit(s, p, r.f2, "feature");
    std::error_code ec;
    fs::remove(lock, ec);
    GG_REQUIRE(started);
    GG_CHECK(noticeSays(s, seen, "The upstream of feature was not set: "));
    s.settle();
    GG_CHECK(detached(s, p));
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), r.f2);
    GG_CHECK_STR_EQ(s.revParse(p, "feature"), s.revParse(p, "origin/feature"));
    GG_CHECK(!s.gitMayFail(p, {"rev-parse", "--abbrev-ref", "feature@{upstream}"}).ok());
    GG_CHECK(!noSessionFile(p));
}

} // namespace ggtest
