// Edit commit in place (§4.3): detach at a commit, amend it, the descendants and branches restack.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

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
    s.contextMenu(rowRef(id).c_str(), "Edit commit");
    return editing(s, id, branch) && s.head(repo) == id;
}

void amendStaged(ImGuiTestContext* ctx, Scenario& s)
{
    ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
    if (s.dialogOpen("Amend"))
        s.dialogButton("Amend", "Amend");
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

} // namespace

GG_TEST("edit-in-place", "edit a mid-stack commit: descendants and branches restack, Return goes back")
{
    const StackRepo r = makeStack(s);
    const fs::path& p = r.path;
    GG_REQUIRE(s.openRepository(p));
    GG_REQUIRE(editCommit(s, p, r.b, "main"));
    GG_CHECK(detached(s, p));
    GG_CHECK_EQ(s.session()->editSession()->descendants, 2);
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//##Toolbar/###tb_edit"); }));

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

    ctx->ItemClick("//##Toolbar/Return to main##tb_edit_return");
    GG_CHECK(s.waitUntil([&] { return !detached(s, p) && !s.session()->editSession(); }));
    s.settle();
    GG_CHECK_STR_EQ(gg::trim(s.gitOut(p, {"symbolic-ref", "HEAD"})), "refs/heads/main");
    GG_CHECK_STR_EQ(s.head(p), s.revParse(p, "main"));
    GG_CHECK(noSessionFile(p));
    GG_CHECK(!s.itemExists("//##Toolbar/###tb_edit"));
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
    ctx->ItemClick("//##Toolbar/Stop editing##tb_edit_stop");
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

} // namespace ggtest
