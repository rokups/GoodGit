// Conflicts: native in-progress operations and first-class conflicts (§4.10).
#include <thread>
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <IconsMaterialSymbols.h>
#include <libgg/Conflicts.hpp>
#include <libgg/Git2.hpp>
#include <libgg/Markers.hpp>
#include <libgg/Rewrite.hpp>

#include <algorithm>

namespace ggtest {

namespace {

using ggui::core::Oid;

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + path + "/###file_" + path;
}

bool inProgress(Scenario&, const fs::path& repo)
{
    for (const char* f : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply"})
        if (fs::exists(repo / ".git" / f))
            return true;
    return false;
}

bool unmerged(Scenario& s, const fs::path& repo) { return !s.gitOut(repo, {"ls-files", "-u"}).empty(); }

bool conflicted(Scenario& s, const std::string& hex) { return s.session()->conflictsOf(Oid::fromHex(hex)) != nullptr; }

// Waits for the conflict scan of the loaded history.
bool scanned(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(Oid::fromHex(hex)) != nullptr && s.session()->activities().empty(); });
}

} // namespace

GG_TEST("conflicts", "native merge: three-way diff, take ours, edit the message, continue")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    const std::string theirs = s.revParse(repo, "theirs");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.itemText("//History/**/###row_wt").find("conflicts") != std::string::npos);
    GG_CHECK(s.itemExists("//###Toolbar/###tb_state"));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "f.txt").c_str()); }));
    ctx->ItemClick(fileRef(s, "Conflicted", "f.txt").c_str());
    s.showPanel("Diff");
    s.comboSelect("//Diff/##conflict_view", "Base " ICON_MS_ARROW_RIGHT_ALT " ours");
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && d->query.kind == ggui::core::DiffKind::Stages && d->query.stageA == 1 && d->query.stageB == 2;
    }));
    s.comboSelect("//Diff/##conflict_view", "Working tree");
    s.contextMenu(fileRef(s, "Conflicted", "f.txt").c_str(), "Take ours");
    GG_CHECK(s.waitUntil([&] { return !unmerged(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f.txt"), "a\nours\nc\n");
    // The merge message in progress, edited in Change information.
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/##merge_message"); }));
    s.setText("//Change information/##merge_message", "Merged by the test\n");
    ctx->ItemClick("//Change information/Save message##save_merge_message");
    GG_CHECK(s.waitUntil([&] { return s.read(repo / ".git", "MERGE_MSG").rfind("Merged by the test", 0) == 0; }));
    s.settle();
    ctx->ItemClick("//###Toolbar/Continue##tb_continue");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^2"), theirs);
    GG_CHECK(s.gitOut(repo, {"log", "-1", "--format=%s"}) == "Merged by the test");
}

GG_TEST("conflicts", "native: abort a merge, skip a rebase step")
{
    const fs::path merge = s.fixture(Recipe::MidMerge);
    const std::string head = s.head(merge);
    GG_REQUIRE(s.openRepository(merge));
    ctx->ItemClick("//###Toolbar/Abort##tb_abort");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, merge); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(merge), head);
    GG_CHECK(s.statusPorcelain(merge).empty());

    const fs::path rebase = s.fixture(Recipe::MidRebase);
    GG_REQUIRE(s.openRepository(rebase));
    GG_REQUIRE(s.itemExists("//###Toolbar/Skip##tb_skip"));
    ctx->ItemClick("//###Toolbar/Skip##tb_skip");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, rebase); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(rebase), s.revParse(rebase, "theirs")); // the only commit was skipped
}

GG_TEST("conflicts", "native: resolve by editing, mark resolved, continue")
{
    const fs::path repo = s.fixture(Recipe::MidCherryPick);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "f.txt").c_str()); }));
    s.write(repo, "f.txt", "a\nboth\nc\n");
    s.contextMenu(fileRef(s, "Conflicted", "f.txt").c_str(), "Mark resolved");
    GG_CHECK(s.waitUntil([&] { return !unmerged(s, repo); }));
    s.settle();
    ctx->ItemClick("//###Toolbar/Continue##tb_continue");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "HEAD:f.txt"}), "a\nboth\nc");
}

GG_TEST("conflicts", "native: resolve with the configured merge tool")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    s.fakeTool("merge-tool", "printf 'resolved by tool\\n' > \"$4\"\n"); // on PATH; logs its arguments
    s.git(repo, {"config", "merge.tool", "testtool"});
    s.git(repo, {"config", "mergetool.testtool.cmd", "merge-tool \"$BASE\" \"$LOCAL\" \"$REMOTE\" \"$MERGED\""});
    s.git(repo, {"config", "mergetool.testtool.trustExitCode", "true"});
    s.git(repo, {"config", "mergetool.keepBackup", "false"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "f.txt").c_str()); }));
    s.contextMenu(fileRef(s, "Conflicted", "f.txt").c_str(), "Resolve with merge tool");
    GG_CHECK(s.waitUntil([&] { return !unmerged(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f.txt"), "resolved by tool\n");
}

GG_TEST("conflicts", "commit with conflicts records diff3 regions; not offered for binary conflicts")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    const std::string theirs = s.revParse(repo, "theirs");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//###Toolbar/Commit with conflicts##tb_commit_conflicts"); }));
    ctx->ItemClick("//###Toolbar/Commit with conflicts##tb_commit_conflicts");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^2"), theirs);
    const std::string committed = s.gitOut(repo, {"show", "HEAD:f.txt"});
    GG_CHECK(committed.find("<<<<<<<") != std::string::npos);
    GG_CHECK(committed.find("|||||||") != std::string::npos);
    GG_CHECK(committed.find(">>>>>>>") != std::string::npos);
    // The new commit is marked conflicted in History.
    GG_CHECK(s.waitUntil([&] { return conflicted(s, s.head(repo)); }));

    // A binary conflict cannot be written as text regions: the action is not offered.
    const fs::path bin = s.path("binary-conflict");
    s.git(s.root(), {"init", "-q", "-b", "main", bin.string()});
    s.track(bin);
    s.write(bin, "b.bin", std::string("base\0bin", 8));
    s.git(bin, {"add", "b.bin"});
    s.git(bin, {"commit", "-q", "-m", "Base"});
    s.git(bin, {"branch", "other"});
    s.write(bin, "b.bin", std::string("ours\0bin", 8));
    s.git(bin, {"commit", "-q", "-am", "Ours"});
    s.git(bin, {"switch", "-q", "other"});
    s.write(bin, "b.bin", std::string("thrs\0bin", 8));
    s.git(bin, {"commit", "-q", "-am", "Theirs"});
    s.git(bin, {"switch", "-q", "main"});
    s.gitMayFail(bin, {"merge", "other"});
    GG_REQUIRE(unmerged(s, bin));
    GG_REQUIRE(s.openRepository(bin));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "b.bin").c_str()); }));
    ctx->Yield(3);
    GG_CHECK(!s.itemExists("//###Toolbar/Commit with conflicts##tb_commit_conflicts"));
    GG_CHECK(s.itemExists("//###Toolbar/Continue##tb_continue"));
    GG_CHECK(s.session()->status()->conflicted.front().binary);
}

GG_TEST("conflicts", "first-class conflicts: History marks, filter, F7, Change information, Changes")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string head = s.head(repo);          // "Descendant keeps the conflict"
    const std::string conflictCommit = s.revParse(repo, "HEAD~1");
    const std::string base = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(scanned(s, base));
    GG_CHECK(s.waitUntil([&] { return conflicted(s, head) && conflicted(s, conflictCommit); }));
    GG_CHECK(!conflicted(s, base));
    // Only conflicted commits.
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
    GG_CHECK(s.waitUntil([&] { return history.visibleIds().size() == 2; }));
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
    GG_CHECK(s.waitUntil([&] { return history.visibleIds().size() == 3; }));
    // F7 / Shift+F7 move between conflicted commits.
    ctx->ItemClick(("//History/**/###row_" + base).c_str());
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), conflictCommit);
    ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), head);
    ctx->KeyPress(ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), conflictCommit);
    // Change information lists the file and its sides.
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Change information/**/###conflict_0"); }));
    GG_CHECK(s.itemText("//Change information/**/###conflict_0").find("conflict.txt (2 sides)") == 0);
    // HEAD is checked out and conflicted: Changes flags the file although git status is clean.
    ctx->ItemClick("//History/**/###row_wt");
    GG_CHECK(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "conflict.txt").c_str()); }));
    GG_CHECK(s.statusPorcelain(repo).empty());
    // F7 from the working tree row: the first conflicted commit; past the last one nothing moves.
    ctx->KeyPress(ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), head);
    ctx->KeyPress(ImGuiKey_F7);
    ctx->KeyPress(ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), conflictCommit);
    // Conflicted only, while new commits are scanned, and together with a search.
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
    s.commitFile(repo, "more.txt", "more\n", "Another descendant keeps it");
    const std::string newer = s.head(repo);
    GG_CHECK(s.waitUntil([&] { return conflicted(s, newer); }));
    ctx->ItemClick(("//History/**/###row_" + newer).c_str());
    ctx->KeyPress(ImGuiKey_F7);
    GG_CHECK_STR_EQ(s.session()->selection().id.hex(), head);
    ctx->ItemInputValue("//History/##hist_filter", "keeps the conflict");
    GG_CHECK(s.waitUntil([&] { return history.visibleIds().size() == 1; }));
    ctx->ItemInputValue("//History/##hist_filter", "");
    ctx->ItemClick("//History/Conflicted only##hist_conflicted");
}

GG_TEST("conflicts", "a conflicted commit selected in History marks its files with the side count")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string conflictedId = s.revParse(repo, "HEAD~1"); // its descendant adds other.txt only
    const std::string base = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(scanned(s, base));
    GG_CHECK(s.waitUntil([&] { return conflicted(s, conflictedId); }));
    ctx->ItemClick(("//History/**/###row_" + conflictedId).c_str());
    auto conflictRow = [&]() -> const ggui::FileRow* {
        for (const auto& r : s.session()->changes().rows())
            if (r.path == "conflict.txt")
                return &r;
        return nullptr;
    };
    GG_CHECK(s.waitUntil([&] { return conflictRow() && conflictRow()->sides == 2; }));
    GG_REQUIRE(conflictRow() != nullptr);
    GG_CHECK_STR_EQ(conflictRow()->conflict, "2-sided conflict");
    GG_CHECK(!conflictRow()->firstClass); // a commit's row offers no working-tree actions
    GG_CHECK(conflictRow()->group == ggui::FileGroup::Commit);
    GG_CHECK(s.itemExists((s.child("//Changes", "##files") + "/conflict.txt/###file_conflict.txt").c_str()));
    // A commit that is not conflicted has plain rows.
    ctx->ItemClick(("//History/**/###row_" + base).c_str());
    GG_CHECK(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    for (const auto& r : s.session()->changes().rows())
        GG_CHECK_EQ(r.sides, 0);
}

GG_TEST("conflicts", "marker parsing: N sides, marker length, malformed, opt-out, disposable cache")
{
    const fs::path repo = s.fixture(Recipe::ConflictedN);
    const std::string nway = s.head(repo);
    // Longer markers declared through the attribute.
    s.commitFile(repo, ".gitattributes", "long.txt conflict-marker-size=9\noptout.txt gg-conflicts=false\n", "Attributes");
    s.commitFile(repo, "long.txt",
        "<<<<<<<<< side 1\nL1\n||||||||| base\nL0\n=========\nL2\n>>>>>>>>> side 2\n", "Long markers");
    const std::string longMarkers = s.head(repo);
    s.commitFile(repo, "broken.txt", "<<<<<<< side 1\nno end\n=======\nstill no end\n", "Malformed markers");
    const std::string malformed = s.head(repo);
    s.commitFile(repo, "optout.txt", "<<<<<<< a\n1\n||||||| b\n0\n=======\n2\n>>>>>>> c\n", "Opted out");
    const std::string optout = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(scanned(s, nway));
    GG_CHECK(s.waitUntil([&] { return conflicted(s, nway); }));
    const auto* sides = s.session()->conflictsOf(Oid::fromHex(nway));
    GG_REQUIRE(sides != nullptr);
    GG_CHECK_EQ((*sides)[0].second, 3);
    // long.txt is a conflict; later commits keep it (descendants), so compare the file lists.
    auto files = [&](const std::string& id) {
        std::set<std::string> out;
        if (const auto* c = s.session()->conflictsOf(Oid::fromHex(id)))
            for (const auto& [path, n] : *c)
                out.insert(path);
        return out;
    };
    GG_CHECK(s.waitUntil([&] { return files(longMarkers).count("long.txt") == 1; }));
    GG_CHECK(files(malformed).count("broken.txt") == 0);
    GG_CHECK(files(optout).count("optout.txt") == 0);
    const auto before = files(optout);
    // The cache under .git/gg is disposable: without it the same is reported.
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    removeAll(repo / ".git" / "gg");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(scanned(s, nway));
    GG_CHECK(s.waitUntil([&] { return files(optout) == before; }));
}

GG_TEST("conflicts", "gg.sameChange setting and conflict-marker-size attribute are applied by writes")
{
    // gg::conflicts::writeOptions(repo, path) is what every write site (Rewrite.cpp, Actions.cpp)
    // calls before gg::markers::mergeFiles/materialize; check it end to end on a real repo, then
    // that its options actually change what mergeFiles writes (docs/spec/conflict-markers.md §7.3
    // rule 3, §3.3).
    const fs::path repo = s.fixture(Recipe::Empty, "same-change");
    s.track(repo);
    gg::git2::Repository r = gg::git2::openRepository(repo);
    // A file with two separate hunks (anchor lines keep them apart): the "x" hunk both sides
    // change identically, the "y" hunk they change differently (always a real conflict, so the
    // whole file never trivially resolves and the "x" hunk is diffed on its own, §7.4).
    const std::string base = "top\nx=0\nmid\ny=0\nbottom\n";
    const std::string ours = "top\nx=1\nmid\ny=1\nbottom\n";
    const std::string theirs = "top\nx=1\nmid\ny=2\nbottom\n";
    // Default (gg.sameChange unset): accept, like Git — the "x" hunk both sides changed the same
    // way resolves plainly; only "y" stays a region.
    GG_CHECK(gg::conflicts::writeOptions(r.get(), "f.txt").sameChangeResolves);
    {
        const std::string merged = gg::markers::mergeFiles(base, ours, theirs, gg::conflicts::writeOptions(r.get(), "f.txt"));
        GG_CHECK(merged.find("top\nx=1\nmid\n") != std::string::npos); // "x" resolved, out of any region
        GG_CHECK_EQ(gg::markers::parse(merged).regions.size(), static_cast<size_t>(1)); // only "y" conflicts
    }
    // gg.sameChange=keep: the exact term algebra (a - r + a is not a) keeps "x" a conflict too.
    s.git(repo, {"config", "gg.sameChange", "keep"});
    GG_CHECK(!gg::conflicts::writeOptions(r.get(), "f.txt").sameChangeResolves);
    {
        const std::string merged = gg::markers::mergeFiles(base, ours, theirs, gg::conflicts::writeOptions(r.get(), "f.txt"));
        GG_CHECK_EQ(gg::markers::parse(merged).regions.size(), static_cast<size_t>(2)); // "x" and "y" both conflict
    }
    // The whole file changed the same way on both sides: a − r + a stays a conflict too.
    GG_CHECK(gg::markers::isConflicted(
        gg::markers::mergeFiles(base, ours, ours, gg::conflicts::writeOptions(r.get(), "f.txt"))));
    // An unknown value falls back to accept.
    s.git(repo, {"config", "gg.sameChange", "bogus"});
    GG_CHECK(gg::conflicts::writeOptions(r.get(), "f.txt").sameChangeResolves);
    s.git(repo, {"config", "--unset", "gg.sameChange"});
    // conflict-marker-size attribute: absent, the default; set, applied to a genuine conflict's
    // marker length (base x=0, ours x=2, theirs x=1 disagree, so it stays a region).
    GG_CHECK_EQ(gg::conflicts::writeOptions(r.get(), "f.txt").markerSize, 7);
    s.write(repo, ".gitattributes", "f.txt conflict-marker-size=11\n");
    {
        const auto options = gg::conflicts::writeOptions(r.get(), "f.txt");
        GG_CHECK_EQ(options.markerSize, 11);
        const std::string merged = gg::markers::mergeFiles("x=0\n", "x=1\n", "x=2\n", options);
        const auto parsed = gg::markers::parse(merged);
        GG_REQUIRE(parsed.conflicted());
        GG_CHECK_EQ(parsed.regions.front().markerLength, 11);
    }
    // End to end through the rewrite engine: a real Pick recreates this same conflict with the
    // attribute's marker length (Rewrite.cpp's write sites use gg::conflicts::writeOptions too).
    s.commitFile(repo, "f.txt", "x=0\n", "base");
    const std::string baseCommit = s.head(repo);
    s.commitFile(repo, "f.txt", "x=1\n", "child");
    gg::rewrite::Plan plan = gg::rewrite::replayPlan(r.get(), {baseCommit});
    bool found = false;
    for (auto& step : plan.steps)
        if (step.source == baseCommit) {
            step.setFiles.push_back({"f.txt", "x=2\n"});
            found = true;
        }
    GG_REQUIRE(found);
    gg::rewrite::Rewriter rewriter(repo);
    gg::rewrite::Result result = rewriter.compute(plan);
    GG_REQUIRE(result.ok && result.unresolved.empty());
    std::string error;
    GG_REQUIRE(rewriter.apply(plan, result, error));
    const auto rewritten = gg::markers::parse(s.gitOut(repo, {"show", "HEAD:f.txt"}));
    GG_REQUIRE(rewritten.conflicted());
    GG_CHECK_EQ(rewritten.regions.front().markerLength, 11);

    // A plain file (no regions yet) that libgit2 merges cleanly only by Git's same-change rule:
    // accept keeps libgit2's merge, keep makes the same-change hunk a first-class conflict.
    // (Hunks further apart than in the file above: git's merge joins changes one line apart.)
    const std::string gOld = "top\nx=0\n1\n2\n3\n4\ny=0\nbottom\n";
    const std::string gNew = "top\nx=1\n1\n2\n3\n4\ny=1\nbottom\n";
    auto sameChangeRewrite = [&]() -> std::string {
        s.commitFile(repo, "g.txt", gOld, "g base");
        const std::string gBase = s.head(repo);
        s.commitFile(repo, "g.txt", gNew, "g child"); // x=1 and y=1
        gg::rewrite::Plan p = gg::rewrite::replayPlan(r.get(), {gBase});
        for (auto& step : p.steps)
            if (step.source == gBase)
                step.setFiles.push_back({"g.txt", "top\nx=1\n1\n2\n3\n4\ny=0\nbottom\n"}); // the same x change
        gg::rewrite::Rewriter w(repo);
        gg::rewrite::Result computed = w.compute(p);
        std::string err;
        const bool applied = computed.ok && w.apply(p, computed, err);
        GG_CHECK(applied);
        return applied ? s.git(repo, {"show", "HEAD:g.txt"}).out : std::string();
    };
    GG_CHECK_EQ(sameChangeRewrite(), gNew);
    s.git(repo, {"config", "gg.sameChange", "keep"});
    const std::string kept = sameChangeRewrite();
    GG_CHECK(gg::markers::isConflicted(kept));
    s.git(repo, {"config", "--unset", "gg.sameChange"});
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflicts", "engine: a rewrite that leaves a conflicted file's value unchanged reuses its exact blob")
{
    // docs/spec/conflict-markers.md §7.4a: a writer that produces a value equal to an input's
    // value keeps that input's bytes. Build a stack where the child already holds a first-class
    // conflict (hand-written, with labels and a marker length the writer itself would never
    // choose), then rewrite the ancestor in an unrelated hunk so that the replayed child's value
    // does not change at all: the rewritten commit's blob for that file must be byte-identical
    // to the original, not a freshly materialised (and differently formatted) equivalent.
    const fs::path repo = s.fixture(Recipe::Empty, "reuse-unchanged");
    s.track(repo);
    gg::git2::Repository r = gg::git2::openRepository(repo);

    // Two independent hunks, far enough apart to anchor separately: "x" (plain) and "y" (becomes
    // a conflict in the child).
    const std::string base = "top\nx=0\nmid\nPLAINY\nbottom\n";
    s.commitFile(repo, "f.txt", base, "base");
    const std::string baseCommit = s.head(repo);

    // The child: "x" already edited (x=0 -> x=1) and "y" already a hand-written first-class
    // conflict — labels "mine"/"orig"/"theirs", marker length 9 though the content (no leading
    // marker-like runs) only needs 7.
    const std::string child = "top\nx=1\nmid\n"
        "<<<<<<<<< mine\nY-A\n||||||||| orig\nPLAINY\n=========\nY-B\n>>>>>>>>> theirs\n"
        "bottom\n";
    GG_REQUIRE(gg::markers::isConflicted(child));
    GG_REQUIRE(gg::markers::parse(child).regions.front().markerLength == 9);
    s.commitFile(repo, "f.txt", child, "child (already conflicted)");
    const std::string childCommit = s.head(repo);
    const std::string originalBlob = s.gitOut(repo, {"rev-parse", childCommit + ":f.txt"});

    // Rewrite the ancestor: apply the very same "x" edit the child already carries (an edit
    // unrelated to the "y" conflict region). Replaying the child on top recomputes f.txt through
    // the marker algebra (both the new ancestor and the child changed it relative to the old
    // ancestor), but the resulting value is exactly what the child already had.
    gg::rewrite::Plan plan = gg::rewrite::replayPlan(r.get(), {baseCommit});
    bool found = false;
    for (auto& step : plan.steps)
        if (step.source == baseCommit) {
            step.setFiles.push_back({"f.txt", "top\nx=1\nmid\nPLAINY\nbottom\n"});
            found = true;
        }
    GG_REQUIRE(found);
    gg::rewrite::Rewriter rewriter(repo);
    gg::rewrite::Result result = rewriter.compute(plan);
    GG_REQUIRE(result.ok && result.unresolved.empty());
    std::string error;
    GG_REQUIRE(rewriter.apply(plan, result, error));

    const std::string rewrittenBlob = s.gitOut(repo, {"rev-parse", "HEAD:f.txt"});
    GG_CHECK_EQ(rewrittenBlob, originalBlob); // reused the original blob, not a fresh equivalent
    const std::string rewrittenText = s.git(repo, {"show", "HEAD:f.txt"}).out;
    GG_CHECK_EQ(rewrittenText, child); // same exact bytes: labels and marker length preserved

    // libgg-level check of sameValue itself (docs/spec/conflict-markers.md §7.3, strict).
    GG_CHECK(gg::markers::sameValue(child, child)); // equal values written the same way
    const std::string reformatted = gg::markers::mergeFiles(base, "top\nx=1\nmid\nPLAINY\nbottom\n", child);
    GG_CHECK(gg::markers::sameValue(reformatted, child)); // equal values, written differently
    const std::string differentValue = "top\nx=1\nmid\n"
        "<<<<<<<<< mine\nY-A\n||||||||| orig\nPLAINY\n=========\nZ-DIFFERENT\n>>>>>>>>> theirs\n"
        "bottom\n";
    GG_CHECK(!gg::markers::sameValue(child, differentValue)); // different values
    // Git's same-change rule ("all adds agree") is excluded: x - r + x is not x under sameValue.
    gg::markers::WriteOptions strict;
    strict.sameChangeResolves = false;
    const std::string changed = "top\nx=1\nmid\nPLAINY\nbottom\n";
    const std::string sameChangeBothSides = gg::markers::mergeFiles(base, changed, changed, strict);
    GG_REQUIRE(gg::markers::isConflicted(sameChangeBothSides)); // stays a region, not resolved
    GG_CHECK(!gg::markers::sameValue(sameChangeBothSides, changed));
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflicts", "marker grammar edge cases (docs/spec/conflict-markers.md)")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // file → content; the files expected to be reported conflicted are listed below.
    const std::vector<std::pair<std::string, std::string>> cases{
        {"crlf.txt", "<<<<<<< a\r\n1\r\n||||||| b\r\n0\r\n=======\r\n2\r\n>>>>>>> c\r\n"},
        {"short.txt", "<<<<<< a\n1\n|||||| b\n0\n======\n2\n>>>>>> c\n"},
        {"eqlabel.txt", "<<<<<<< a\n1\n||||||| b\n0\n======= x\n2\n>>>>>>> c\n"},
        {"restart-ours.txt", "<<<<<<< a\njunk\n<<<<<<< b\n1\n||||||| c\n0\n=======\n2\n>>>>>>> d\n"},
        {"restart-base.txt", "<<<<<<< a\n1\n||||||| b\n<<<<<<< x\n1\n||||||| y\n0\n=======\n2\n>>>>>>> z\n"},
        {"restart-theirs.txt", "<<<<<<< a\n1\n||||||| b\n0\n=======\n<<<<<<< x\n1\n||||||| y\n0\n=======\n2\n>>>>>>> z\n"},
        {"twoway.txt", "<<<<<<< a\n1\n=======\n2\n>>>>>>> c\n"},
        {"noend.txt", "<<<<<<< a\n1\n||||||| b\n0\n=======\n2\n"},
        {"wrongorder.txt", "<<<<<<< a\n1\n||||||| b\n0\n>>>>>>> c\n=======\n"},
        {"eof.txt", "<<<<<<< a\n1\n"},
        {"noeol.txt", "x\n<<<<<<< side 1 [no newline]\na\n||||||| base\nb\n=======\nc\n>>>>>>> side 2 [no newline]\n"},
        {"long8.txt", "<<<<<<<< a\n1\n|||||||| b\n0\n========\n2\n>>>>>>>> c\n"},
        {"mixedlen.txt", "<<<<<<< a\n1\n|||||||| b\n0\n=======\n2\n>>>>>>> c\n"},
        {"nway1.txt", "<<<<<<< gg 1-sided conflict\n+++++++ side 1\nx\n>>>>>>> end of conflict\n"},
        {"nwaytail.txt", "<<<<<<< gg 3-sided conflicts\n+++++++ side 1\nx\n------- base 1\ny\n+++++++ side 2\nz\n"
                         "------- base 2\ny\n+++++++ side 3\nw\n>>>>>>> end\n"},
        {"nwaycount.txt", "<<<<<<< gg 3-sided conflict\n+++++++ side 1\nx\n------- base 1\ny\n+++++++ side 2\nz\n>>>>>>> end\n"},
        {"nwayorder.txt", "<<<<<<< gg 3-sided conflict\n+++++++ side 1\nx\n+++++++ side 2\nz\n------- base\ny\n"
                          "+++++++ side 3\nw\n>>>>>>> end\n"},
        {"nwayrestart.txt", "<<<<<<< gg 2-sided conflict\n+++++++ side 1\nx\n<<<<<<< a\n1\n||||||| b\n0\n=======\n2\n>>>>>>> c\n"},
        {"nwayok.txt", "<<<<<<< gg 2-sided conflict extra text\n+++++++ side 1\nx\n------- base 1\ny\n+++++++ side 2\nz\n>>>>>>> end\n"},
        {"nwayhuge.txt", "<<<<<<< gg 999999-sided conflict\n+++++++ side 1\nx\n>>>>>>> end\n"},
    };
    for (const auto& [name, content] : cases)
        s.write(repo, name, content);
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Marker cases"});
    const auto r = s.gitgg(repo, {"conflicts"});
    GG_CHECK_EQ(r.exitCode, 1);
    const std::string expected = "crlf.txt (2 sides)\nlong8.txt (2 sides)\nnoeol.txt (2 sides)\nnwayok.txt (2 sides)\n"
                                 "nwayrestart.txt (2 sides)\nrestart-base.txt (2 sides)\nrestart-ours.txt (2 sides)\n"
                                 "restart-theirs.txt (2 sides)\n";
    GG_CHECK_STR_EQ(r.out, expected);
    // The UI agrees.
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] {
        const auto* c = s.session()->conflictsOf(ggui::core::Oid::fromHex(s.head(repo)));
        return c && c->size() == 8;
    }));
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflicts", "checking out a conflicted commit: clean status by default, index stages when asked")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string conflictedCommit = s.revParse(repo, "HEAD~1");
    s.git(repo, {"switch", "-q", "--detach", "HEAD~2"}); // start on the clean base
    GG_REQUIRE(s.openRepository(repo));
    // Default: only the file content, index = HEAD, git status clean.
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(conflictedCommit)) != nullptr; }));
    s.contextMenu(("//History/**/###row_" + conflictedCommit).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == conflictedCommit; }));
    s.settle();
    GG_CHECK(s.read(repo, "conflict.txt").find("<<<<<<<") != std::string::npos);
    GG_CHECK(s.statusPorcelain(repo).empty());
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    // With the setting: stages 1–3 from the region, so git mergetool can work on it.
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    ctx->ItemCheck("//Settings/##settings_tabs/Git/Expand to index stages on checkout##expand_stages");
    GG_CHECK(s.app.settings().data().expandConflictStages);
    ctx->WindowClose("//Settings");
    const std::string main = s.revParse(repo, "main");
    s.contextMenu(("//History/**/###row_" + main).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == main; }));
    s.settle();
    const auto stages = gg::splitLines(s.gitOut(repo, {"ls-files", "-u", "--", "conflict.txt"}));
    GG_REQUIRE(stages.size() == 3);
    const std::string theirs = s.gitOut(repo, {"show", ":3:conflict.txt"});
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", ":1:conflict.txt"}), "top\nx=0\nbottom");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", ":2:conflict.txt"}), "top\nx=1\nbottom");
    GG_CHECK_STR_EQ(theirs, "top\nx=2\nbottom");
    // Switching away collapses them back first (git would refuse with an unmerged index).
    s.contextMenu(("//History/**/###row_" + s.revParse(repo, "HEAD~2")).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "main~2"); }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());
    // Checking out the child (the conflicted commit) expands too; the parent collapses.
    s.contextMenu(("//History/**/###row_" + conflictedCommit).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == conflictedCommit; }));
    s.settle();
    GG_CHECK_EQ(gg::splitLines(s.gitOut(repo, {"ls-files", "-u", "--", "conflict.txt"})).size(), 3u);
    s.contextMenu(("//History/**/###row_" + s.revParse(repo, "main~2")).c_str(), "Check out/Detached HEAD");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == s.revParse(repo, "main~2"); }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
}

} // namespace ggtest

namespace ggtest {

namespace {

std::string wtFile(Scenario& s, const std::string& path)
{
    return s.child("//Changes", "##files") + "/Conflicted/" + path + "/###file_" + path;
}

// Conflicted2 with the conflicted commit checked out (detached), its descendant on main.
fs::path conflictedCheckout(Scenario& s, std::string& conflicted)
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    conflicted = s.revParse(repo, "HEAD~1");
    s.git(repo, {"switch", "-q", "--detach", conflicted});
    return repo;
}

} // namespace

GG_TEST("conflicts", "first-class: term view, take a side, Mark resolved, Amend resolves the descendants too")
{
    std::string conflicted;
    const fs::path repo = conflictedCheckout(s, conflicted);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "conflict.txt").c_str()); }));
    // Regions remain: Mark resolved is refused.
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Mark resolved");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("conflict regions") != std::string::npos);
    // The Diff panel shows what side 1 did against the base.
    ctx->ItemClick(wtFile(s, "conflict.txt").c_str());
    s.showPanel("Diff");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Diff/##term_view"); }));
    s.comboSelect("//Diff/##term_view", "Base " ICON_MS_ARROW_RIGHT_ALT " side 1");
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && d->query.kind == ggui::core::DiffKind::Term && !d->files.empty() && !d->files[0].hunks.empty()
            && d->files[0].hunks[0].lines.size() >= 2;
    }));
    const auto& hunk = s.session()->diff().diff()->files[0].hunks[0];
    bool removed = false, added = false;
    for (const auto& l : hunk.lines) {
        removed |= l.origin == '-' && l.text == "x=0";
        added |= l.origin == '+' && l.text == "x=1";
    }
    GG_CHECK(removed && added);
    // Take side 2 for the whole file, mark resolved, amend.
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Take side/Side 2 (whole file)###wholeside1");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "conflict.txt") == "top\nx=2\nbottom\n"; }));
    s.settle();
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "conflict.txt").c_str()) || s.session()->status()->unstaged.size() == 1; }));
    s.git(repo, {"add", "conflict.txt"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.size() == 1; }));
    ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != conflicted; }));
    s.settle();
    // The amended commit and main's descendant are both resolved now.
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts", "HEAD"}).exitCode, 0);
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts", "main"}).exitCode, 0);
    GG_CHECK_STR_EQ(s.revParse(repo, "main^"), s.head(repo));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "main:conflict.txt"}), "top\nx=2\nbottom");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "main:other.txt"}), "descendant");
}

GG_TEST("conflicts", "first-class: take a side in one region; resolve in the editor and commit on top")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string two = "a\n<<<<<<< side 1\nA1\n||||||| base\nA0\n=======\nA2\n>>>>>>> side 2\nmiddle\n"
                            "<<<<<<< side 1\nB1\n||||||| base\nB0\n=======\nB2\n>>>>>>> side 2\nz\n";
    s.commitFile(repo, "two.txt", two, "Two regions");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "two.txt").c_str()); }));
    s.contextMenu(wtFile(s, "two.txt").c_str(), "Take side/In one region...");
    GG_REQUIRE(s.dialogOpen("Take side in a region"));
    s.dialogText("Take side in a region", "region", "2");
    s.dialogText("Take side in a region", "side", "1");
    s.dialogButton("Take side in a region", "Take");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "two.txt").find("B1\nz\n") != std::string::npos; }));
    s.settle();
    GG_CHECK(s.read(repo, "two.txt").find("<<<<<<< side 1\nA1") != std::string::npos); // region 1 kept
    // An "editor" resolves the rest; then a new commit on top resolves the conflict.
    const fs::path editorLog = s.fakeTool("resolving-editor", "printf 'a\\nA2\\nmiddle\\nB1\\nz\\n' > \"$1\"\n");
    (void)editorLog;
    s.git(repo, {"config", "core.editor", "resolving-editor"});
    ggui::unsetEnv("GIT_EDITOR"); // the test runner sets GIT_EDITOR=true, which beats core.editor
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "two.txt").c_str()); }));
    s.contextMenu(wtFile(s, "two.txt").c_str(), "Open working-copy file");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "two.txt") == "a\nA2\nmiddle\nB1\nz\n"; }));
    s.settle();
    s.git(repo, {"add", "two.txt"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.size() == 1; }));
    ctx->ItemClick("//###Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Resolve two.txt");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"log", "-1", "--format=%s"}) == "Resolve two.txt"; }));
    s.settle();
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts"}).exitCode, 0);
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts", "HEAD~1"}).exitCode, 1);
}

GG_TEST("conflicts", "first-class: resolve with the merge tool (stages from the regions)")
{
    std::string conflicted;
    const fs::path repo = conflictedCheckout(s, conflicted);
    s.fakeTool("fc-merge-tool", "printf 'top\\nx=merged\\nbottom\\n' > \"$4\"\n");
    s.git(repo, {"config", "merge.tool", "fctool"});
    s.git(repo, {"config", "mergetool.fctool.cmd", "fc-merge-tool \"$BASE\" \"$LOCAL\" \"$REMOTE\" \"$MERGED\""});
    s.git(repo, {"config", "mergetool.fctool.trustExitCode", "true"});
    s.git(repo, {"config", "mergetool.keepBackup", "false"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "conflict.txt").c_str()); }));
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Resolve with merge tool");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "conflict.txt") == "top\nx=merged\nbottom\n"; }));
    s.settle();
    // The tool saw base, ours and theirs taken from the region, and the result is staged.
    const std::string args = s.read(s.root(), "fc-merge-tool.log");
    GG_CHECK(args.find("_BASE_") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "conflict.txt");
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
}

GG_TEST("conflicts", "first-class: resolve one pair of sides of an N-sided conflict with the merge tool")
{
    const fs::path repo = s.fixture(Recipe::ConflictedN); // HEAD: 3-sided conflict.txt (x=3/x=1/x=2, bases x=0/x=0)
    const std::string original = s.read(repo, "conflict.txt");
    const fs::path contentLog = s.root() / "fc-merge-tool-n-content.log";
    s.fakeTool("fc-merge-tool-n",
        "cat \"$1\" \"$2\" \"$3\" > \"" + contentLog.generic_string() + "\"\nprintf 'merged\\n' > \"$4\"\n");
    s.git(repo, {"config", "merge.tool", "fctooln"});
    s.git(repo, {"config", "mergetool.fctooln.cmd", "fc-merge-tool-n \"$BASE\" \"$LOCAL\" \"$REMOTE\" \"$MERGED\""});
    s.git(repo, {"config", "mergetool.fctooln.trustExitCode", "true"});
    s.git(repo, {"config", "mergetool.keepBackup", "false"});
    s.git(repo, {"config", "mergetool.gives-up.cmd", "false"});
    s.git(repo, {"config", "mergetool.gives-up.trustExitCode", "true"});
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtFile(s, "conflict.txt").c_str()); }));

    // A failing tool on one pair leaves the file byte-identical and the index at HEAD.
    s.git(repo, {"config", "merge.tool", "gives-up"});
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Resolve with merge tool/Sides 1 and 2###pair0");
    GG_CHECK(s.dismissError());
    GG_CHECK_STR_EQ(s.read(repo, "conflict.txt"), original);
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());

    // Resolving sides 1 and 2 (pair 0): the tool saw base 1 (x=0), side 1 (x=3), side 2 (x=1).
    s.git(repo, {"config", "merge.tool", "fctooln"});
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Resolve with merge tool/Sides 1 and 2###pair0");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "conflict.txt") != original; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(s.root(), "fc-merge-tool-n-content.log"), "x=0\nx=3\nx=1\n");

    // The file is now a two-sided conflict: terms {merged, x=2}, base r2 (x=0).
    const std::string after = s.read(repo, "conflict.txt");
    const gg::markers::Merge m = gg::markers::toMerge(after);
    GG_REQUIRE(m.adds.size() == 2);
    GG_CHECK_STR_EQ(m.adds[0], "merged\n");
    GG_CHECK_STR_EQ(m.adds[1], "x=2\n");
    GG_REQUIRE(m.removes.size() == 1);
    GG_CHECK_STR_EQ(m.removes[0], "x=0\n");

    // The index is back to HEAD for the path: no stages, nothing staged.
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
}

GG_TEST("conflicts", "toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict")
{
    auto clean = [&](const fs::path&) {
        return s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::None; });
    };
    const fs::path revert = s.fixture(Recipe::MidRevert);
    GG_REQUIRE(s.openRepository(revert));
    ctx->ItemClick("//###Toolbar/Abort##tb_abort");
    GG_CHECK(clean(revert));
    GG_CHECK(!fs::exists(revert / ".git" / "REVERT_HEAD"));
    s.settle();
    const fs::path apply = s.fixture(Recipe::MidRebaseApply);
    GG_REQUIRE(s.openRepository(apply));
    GG_CHECK(s.session()->snapshot()->state == ggui::core::RepoState::Rebasing);
    ctx->ItemClick("//###Toolbar/Abort##tb_abort");
    GG_CHECK(clean(apply));
    GG_CHECK(!fs::exists(apply / ".git" / "rebase-apply"));
    s.settle();
    const fs::path bisect = s.fixture(Recipe::Bisecting);
    GG_REQUIRE(s.openRepository(bisect));
    const std::string before = s.head(bisect);
    ctx->ItemClick("//###Toolbar/Skip##tb_skip");
    GG_CHECK(s.waitUntil([&] { return s.head(bisect) != before; }));
    s.settle();
    ctx->ItemClick("//###Toolbar/Reset##tb_abort");
    GG_CHECK(clean(bisect));
    GG_CHECK(!fs::exists(bisect / ".git" / "BISECT_LOG"));
    s.settle();

    // A merge tool on a first-class conflict: stages 1–3 from the regions; the tool's result is
    // staged. A tool that gives up leaves the index as it was.
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    s.git(repo, {"config", "merge.tool", "fake"});
    s.git(repo, {"config", "mergetool.fake.cmd", "cp \"$REMOTE\" \"$MERGED\""});
    s.git(repo, {"config", "mergetool.fake.trustExitCode", "true"});
    s.git(repo, {"config", "mergetool.keepBackup", "false"});
    s.git(repo, {"config", "mergetool.gives-up.cmd", "false"});
    s.git(repo, {"config", "mergetool.gives-up.trustExitCode", "true"});
    GG_REQUIRE(s.openRepository(repo));
    const std::string row = s.child("//Changes", "##files") + "/Conflicted/conflict.txt/###file_conflict.txt";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    s.git(repo, {"config", "merge.tool", "gives-up"});
    s.contextMenu(row.c_str(), "Resolve with merge tool");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());
    s.git(repo, {"config", "merge.tool", "fake"});
    s.contextMenu(row.c_str(), "Resolve with merge tool");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "conflict.txt") == "top\nx=2\nbottom\n"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "conflict.txt");
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflicts", "brokenMarkers: unit cases for the broken-region diagnostic (§4.10, §8)")
{
    using gg::markers::brokenMarkers;
    const std::string region = "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\n=======\nx=2\n>>>>>>> side 2\nbottom\n";
    // Region intact: no warning.
    GG_CHECK(brokenMarkers(region, region).empty());
    // Separator deleted: the opening and closing marker lines (1-based, in `after`) are reported.
    const std::string separatorDeleted = "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\nx=2\n>>>>>>> side 2\nbottom\n";
    GG_CHECK((brokenMarkers(region, separatorDeleted) == std::vector<size_t>{2, 7}));
    // Properly resolved (every marker line removed): no warning.
    const std::string resolved = "top\nx=2\nbottom\n";
    GG_CHECK(brokenMarkers(region, resolved).empty());
    // Marker-like text of another length: not reported (it does not match any region's length).
    const std::string otherLength = region + "<<<<<<<<\nfoo\n>>>>>>>>\n";
    GG_CHECK(brokenMarkers(region, otherLength).empty());
    // `before` without regions: always empty, whatever `after` looks like.
    const std::string plain = "no conflict here\n";
    const std::string strayMarkers = "<<<<<<< a\nx\n>>>>>>> b\n";
    GG_CHECK(brokenMarkers(plain, strayMarkers).empty());
}

GG_TEST("conflicts", "an edit that breaks a conflict region warns (Status, Changes) instead of silently resolving")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    // Break the region: delete the "=======" separator. HEAD is still conflicted; the working
    // tree edit is not (it parses as plain text with stray "<<<<<<<"/">>>>>>>" lines).
    s.write(repo, "conflict.txt", "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\nx=2\n>>>>>>> side 2\nbottom\n");
    GG_REQUIRE(s.openRepository(repo));
    const std::string row = s.child("//Changes", "##files") + "/Unstaged/conflict.txt/###file_conflict.txt";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    GG_CHECK(s.waitUntil([&] {
        const auto st = s.session()->status();
        if (!st)
            return false;
        const auto it = std::find_if(st->unstaged.begin(), st->unstaged.end(),
            [](const ggui::core::StatusEntry& e) { return e.path == "conflict.txt"; });
        return it != st->unstaged.end() && it->brokenMarkerLines == std::vector<size_t>{2, 7};
    }));
    // Not shown under Conflicted: it is an ordinary Modified row carrying the warning.
    GG_CHECK(!s.itemExists((s.child("//Changes", "##files") + "/Conflicted/conflict.txt/###file_conflict.txt").c_str()));
    ctx->MouseMove(row.c_str());
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "Conflict markers left at line 2, 7"));
}

GG_TEST("conflicts", "materialize labels sides/base from termLabels; labels round-trip, sanitize, and truncate")
{
    // Two hunks far enough apart to anchor separately (docs/spec/conflict-markers.md §7.4 rule
    // 4): a genuine 2-region conflict, so a whole-file term's label must show up the same way in
    // every region.
    const std::string base = "top\nx=0\nmid\ny=0\nbottom\n";
    const std::string ours = "top\nx=1\nmid\ny=1\nbottom\n";
    const std::string theirs = "top\nx=2\nmid\ny=2\nbottom\n";
    gg::markers::WriteOptions options;
    options.termLabels[ours] = "1a2b3c4 Fix the parser";
    options.termLabels[theirs] = "9f8e7d6 Alternative fix";
    options.termLabels[base] = "deadbee Base commit";
    const std::string merged = gg::markers::mergeFiles(base, ours, theirs, options);
    const auto parsed = gg::markers::parse(merged);
    GG_REQUIRE(parsed.regions.size() == static_cast<size_t>(2));
    for (const auto& region : parsed.regions) {
        GG_REQUIRE(region.sides.size() == static_cast<size_t>(2));
        GG_CHECK(region.sides[0].label == "1a2b3c4 Fix the parser");
        GG_CHECK(region.sides[1].label == "9f8e7d6 Alternative fix");
        GG_REQUIRE(region.bases.size() == static_cast<size_t>(1));
        GG_CHECK(region.bases[0].label == "deadbee Base commit");
    }
    GG_CHECK(merged.find("<<<<<<< 1a2b3c4 Fix the parser") != std::string::npos);
    // §3.1: the closing marker of the two-sided form labels side B, not a separate "end" label.
    GG_CHECK(merged.find(">>>>>>> 9f8e7d6 Alternative fix") != std::string::npos);

    // termLabels(parse back) round-trips: the whole-file terms map to the same labels.
    const auto recovered = gg::markers::termLabels(merged);
    GG_REQUIRE(recovered.count(ours) == 1);
    GG_CHECK(recovered.at(ours) == "1a2b3c4 Fix the parser");
    GG_REQUIRE(recovered.count(theirs) == 1);
    GG_CHECK(recovered.at(theirs) == "9f8e7d6 Alternative fix");
    GG_REQUIRE(recovered.count(base) == 1);
    GG_CHECK(recovered.at(base) == "deadbee Base commit");

    // Labels with newlines are sanitized (a label lives on one marker line); long subjects
    // truncated so a marker line stays readable.
    gg::markers::WriteOptions dirty;
    dirty.termLabels[ours] = "abc1234 Multi\nline\rsubject";
    dirty.termLabels[theirs] = "def5678 " + std::string(100, 'x'); // well past the ~72-byte cap
    const std::string merged2 = gg::markers::mergeFiles(base, ours, theirs, dirty);
    const auto parsed2 = gg::markers::parse(merged2);
    GG_REQUIRE(parsed2.conflicted());
    const std::string label0 = parsed2.regions.front().sides[0].label;
    GG_CHECK(label0.find('\n') == std::string::npos);
    GG_CHECK(label0.find('\r') == std::string::npos);
    GG_CHECK(label0 == "abc1234 Multi line subject");
    const std::string label1 = parsed2.regions.front().sides[1].label;
    GG_CHECK(label1.size() <= 72);
    GG_REQUIRE(label1.size() >= 3);
    GG_CHECK(label1.substr(label1.size() - 3) == "...");
    // Truncation never splits a UTF-8 character ("é" is two bytes; 34 of them cross the cap).
    gg::markers::WriteOptions wide;
    std::string accents;
    for (int i = 0; i < 40; ++i)
        accents += "\xC3\xA9";
    wide.termLabels[theirs] = accents; // the cut at byte 69 falls inside a character
    const auto parsed3 = gg::markers::parse(gg::markers::mergeFiles(base, ours, theirs, wide));
    GG_REQUIRE(parsed3.conflicted());
    const std::string label3 = parsed3.regions.front().sides[1].label;
    GG_CHECK(label3.size() <= 72);
    GG_CHECK_EQ(label3.size(), static_cast<size_t>(68 + 3)); // 34 whole characters, then "..."
    GG_CHECK(label3 == accents.substr(0, 68) + "...");

    // The "[no newline]" flag still round-trips together with a label.
    gg::markers::WriteOptions noeol;
    noeol.sideLabels = {"mine", "theirs"};
    const std::string noeolMerged = gg::markers::mergeFiles("a\nb\n", "a\nB1", "a\nB2", noeol);
    const auto parsedNoeol = gg::markers::parse(noeolMerged);
    GG_REQUIRE(parsedNoeol.conflicted());
    GG_CHECK(parsedNoeol.regions.front().sides[0].label == "mine");
    GG_CHECK(parsedNoeol.regions.front().sides[0].noEol);
    GG_CHECK(parsedNoeol.regions.front().sides[0].value() == "B1");
    GG_CHECK(parsedNoeol.regions.front().sides[1].label == "theirs");
    GG_CHECK(parsedNoeol.regions.front().sides[1].noEol);
    GG_CHECK(parsedNoeol.regions.front().sides[1].value() == "B2");
}

GG_TEST("conflicts", "engine: a rewrite that creates a conflict labels ours/theirs/base with the right commits")
{
    const fs::path repo = s.fixture(Recipe::Empty, "side-labels");
    s.track(repo);
    gg::git2::Repository r = gg::git2::openRepository(repo);

    s.commitFile(repo, "f.txt", "x=0\n", "Base commit");
    const std::string baseCommit = s.head(repo);
    s.commitFile(repo, "f.txt", "x=1\n", "Ours change");
    const std::string oursCommit = s.head(repo);

    // A commit on another line that changes the same place differently, replayed onto oursCommit:
    // base = baseCommit, ours = oursCommit (the new parent), theirs = the replayed commit itself.
    s.git(repo, {"switch", "-q", "-c", "side", baseCommit});
    s.commitFile(repo, "f.txt", "x=2\n", "Theirs change");
    const std::string theirsCommit = s.head(repo);
    s.git(repo, {"switch", "-q", "main"});

    gg::rewrite::Plan plan = gg::rewrite::replayPlan(r.get(), {theirsCommit});
    GG_REQUIRE(plan.steps.size() == 1);
    plan.steps[0].parents = {oursCommit};
    plan.steps[0].sourceParents = false;
    gg::rewrite::Rewriter rewriter(repo);
    gg::rewrite::Result result = rewriter.compute(plan);
    GG_REQUIRE(result.ok && result.unresolved.empty());
    std::string error;
    GG_REQUIRE(rewriter.apply(plan, result, error));

    const std::string newCommit = result.steps.at(plan.steps[0].key.empty() ? theirsCommit : plan.steps[0].key);
    const std::string text = s.git(repo, {"show", newCommit + ":f.txt"}).out;
    GG_REQUIRE(gg::markers::isConflicted(text));
    const auto region = gg::markers::parse(text).regions.front();
    GG_REQUIRE(region.sides.size() == static_cast<size_t>(2));
    GG_CHECK(region.sides[0].label == oursCommit.substr(0, 7) + " Ours change");
    GG_CHECK(region.sides[1].label == theirsCommit.substr(0, 7) + " Theirs change");
    GG_REQUIRE(region.bases.size() == static_cast<size_t>(1));
    GG_CHECK(region.bases[0].label == baseCommit.substr(0, 7) + " Base commit");
}

GG_TEST("conflicts", "engine: rebasing an already-conflicted commit keeps old labels and labels the new term")
{
    const fs::path repo = s.fixture(Recipe::Empty, "side-labels-stack");
    s.track(repo);
    gg::git2::Repository r = gg::git2::openRepository(repo);

    s.commitFile(repo, "f.txt", "x=0\n", "Base commit");
    const std::string baseCommit = s.head(repo);
    // The commit whose replay creates the conflict already carries one (hand-written, labelled),
    // simulating a first-class conflict that survived an earlier rewrite. Distinct side/base
    // content so nothing here coincides byte-for-byte with the new base/parent below (a plain
    // input's label only overwrites a parsed one for the exact same whole-file-term content).
    const std::string alreadyConflicted = "<<<<<<< old-side-a\nx=old-a\n||||||| old-base\nx=old-base\n"
                                           "=======\nx=old-b\n>>>>>>> old-side-b\n";
    s.commitFile(repo, "f.txt", alreadyConflicted, "Already conflicted");
    const std::string conflictedCommit = s.head(repo);

    // A new ancestor edit that conflicts with the whole (single-line) file the child changes.
    s.git(repo, {"switch", "-q", "-c", "side", baseCommit});
    s.commitFile(repo, "f.txt", "x=2\n", "New base edit");
    const std::string newBaseCommit = s.head(repo);
    s.git(repo, {"switch", "-q", "main"});

    // Replay conflictedCommit onto newBaseCommit (original parent baseCommit): the resulting
    // merge value is 3-sided — the new base's own change (a fresh term, labelled from its
    // commit) plus the two surviving terms of the old conflict (still labelled from before).
    gg::rewrite::Plan plan = gg::rewrite::replayPlan(r.get(), {conflictedCommit});
    GG_REQUIRE(plan.steps.size() == 1);
    plan.steps[0].parents = {newBaseCommit};
    plan.steps[0].sourceParents = false;
    gg::rewrite::Rewriter rewriter(repo);
    gg::rewrite::Result result = rewriter.compute(plan);
    GG_REQUIRE(result.ok && result.unresolved.empty());
    std::string error;
    GG_REQUIRE(rewriter.apply(plan, result, error));

    const std::string newCommit = result.steps.at(plan.steps[0].key.empty() ? conflictedCommit : plan.steps[0].key);
    const std::string text = s.git(repo, {"show", newCommit + ":f.txt"}).out;
    GG_REQUIRE(gg::markers::isConflicted(text));
    const auto region = gg::markers::parse(text).regions.front();
    GG_REQUIRE(region.sides.size() == static_cast<size_t>(3));
    GG_CHECK(region.sides[0].label == newBaseCommit.substr(0, 7) + " New base edit"); // the fresh term
    GG_CHECK(region.sides[1].label == "old-side-a"); // survived from the old conflict, unchanged
    GG_CHECK(region.sides[2].label == "old-side-b");
    GG_REQUIRE(region.bases.size() == static_cast<size_t>(2));
    GG_CHECK(region.bases[0].label == "old-base");
    GG_CHECK(region.bases[1].label == baseCommit.substr(0, 7) + " Base commit");
}

GG_TEST("conflicts", "engine: conflict labels name original commits, so a rewrite's trees do not depend on the time")
{
    const fs::path repo = s.fixture(Recipe::Empty, "side-labels-stable");
    s.commitFile(repo, "f.txt", "x=0\n", "Base");
    const std::string base = s.head(repo);
    s.commitFile(repo, "f.txt", "x=1\n", "One");
    s.commitFile(repo, "f.txt", "x=2\n", "Two");
    const fs::path copy = repo.parent_path() / "side-labels-stable-copy";
    fs::copy(repo, copy, fs::copy_options::recursive);
    // Editing the base makes both descendants conflict; each lands on a rewritten parent whose
    // new id depends on the commit time. The same rewrite a second later gives the same trees.
    auto rewrite = [&](const fs::path& where) {
        gg::git2::Repository r = gg::git2::openRepository(where);
        gg::rewrite::Plan plan = gg::rewrite::replayPlan(r.get(), {base});
        for (auto& step : plan.steps)
            if (step.source == base)
                step.setFiles.push_back({"f.txt", "x=9\n"});
        gg::rewrite::Rewriter rewriter(where);
        gg::rewrite::Result result = rewriter.compute(plan);
        std::string error;
        GG_CHECK(result.ok && rewriter.apply(plan, result, error));
        std::vector<std::string> trees;
        for (const char* rev : {"HEAD~2^{tree}", "HEAD~1^{tree}", "HEAD^{tree}"})
            trees.push_back(s.revParse(where, rev));
        return trees;
    };
    const auto first = rewrite(repo);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // another second: other commit ids
    const auto second = rewrite(copy);
    GG_CHECK(s.head(repo) != s.head(copy));
    GG_CHECK(first == second);
    GG_CHECK(gg::markers::isConflicted(s.git(repo, {"show", "HEAD:f.txt"}).out));
}

} // namespace ggtest
