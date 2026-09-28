// Conflicts: native in-progress operations and first-class conflicts (§4.10; P2-18, P2-19, P2-24).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

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

GG_TEST("conflicts", "native merge: three-way diff, take ours, edit the message, continue", "HIST-WT-NATIVE-CONFLICT",
    "CONF-NATIVE-3WAY-DIFF", "CONF-NATIVE-TAKE-SIDE", "CONF-MERGE-MSG", "CONF-NATIVE-CONTINUE", "TB-STATE-CONTINUE")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    const std::string theirs = s.revParse(repo, "theirs");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.itemText("//History/**/###row_wt").find("conflicts") != std::string::npos);
    GG_CHECK(s.itemExists("//##Toolbar/###tb_state"));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "f.txt").c_str()); }));
    ctx->ItemClick(fileRef(s, "Conflicted", "f.txt").c_str());
    s.showPanel("Diff");
    s.comboSelect("//Diff/##conflict_view", "Base \xe2\x86\x92 ours");
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
    ctx->ItemClick("//##Toolbar/Continue##tb_continue");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^2"), theirs);
    GG_CHECK(s.gitOut(repo, {"log", "-1", "--format=%s"}) == "Merged by the test");
}

GG_TEST("conflicts", "native: abort a merge, skip a rebase step", "CONF-NATIVE-ABORT", "TB-STATE-ABORT", "CONF-NATIVE-SKIP",
    "TB-STATE-SKIP")
{
    const fs::path merge = s.fixture(Recipe::MidMerge);
    const std::string head = s.head(merge);
    GG_REQUIRE(s.openRepository(merge));
    ctx->ItemClick("//##Toolbar/Abort##tb_abort");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, merge); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(merge), head);
    GG_CHECK(s.statusPorcelain(merge).empty());

    const fs::path rebase = s.fixture(Recipe::MidRebase);
    GG_REQUIRE(s.openRepository(rebase));
    GG_REQUIRE(s.itemExists("//##Toolbar/Skip##tb_skip"));
    ctx->ItemClick("//##Toolbar/Skip##tb_skip");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, rebase); }));
    s.settle();
    GG_CHECK_STR_EQ(s.head(rebase), s.revParse(rebase, "theirs")); // the only commit was skipped
}

GG_TEST("conflicts", "native: resolve by editing, mark resolved, continue", "CHG-MARK-RESOLVED", "CHG-CTX-MARK-RESOLVED",
    "CONF-NATIVE-MARK-RESOLVED")
{
    const fs::path repo = s.fixture(Recipe::MidCherryPick);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Conflicted", "f.txt").c_str()); }));
    s.write(repo, "f.txt", "a\nboth\nc\n");
    s.contextMenu(fileRef(s, "Conflicted", "f.txt").c_str(), "Mark resolved");
    GG_CHECK(s.waitUntil([&] { return !unmerged(s, repo); }));
    s.settle();
    ctx->ItemClick("//##Toolbar/Continue##tb_continue");
    GG_CHECK(s.waitUntil([&] { return !inProgress(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "HEAD:f.txt"}), "a\nboth\nc");
}

GG_TEST("conflicts", "native: resolve with the configured merge tool", "APP-EXT-MERGETOOL", "CHG-CTX-MERGETOOL",
    "CONF-NATIVE-MERGETOOL")
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

GG_TEST("conflicts", "commit with conflicts records diff3 regions; not offered for binary conflicts",
    "CONF-COMMIT-WITH-CONFLICTS", "CONF-COMMIT-WITH-CONFLICTS-BINARY-REFUSE", "HIST-CONFLICT-MARK")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    const std::string theirs = s.revParse(repo, "theirs");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//##Toolbar/Commit with conflicts##tb_commit_conflicts"); }));
    ctx->ItemClick("//##Toolbar/Commit with conflicts##tb_commit_conflicts");
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
    GG_CHECK(!s.itemExists("//##Toolbar/Commit with conflicts##tb_commit_conflicts"));
    GG_CHECK(s.itemExists("//##Toolbar/Continue##tb_continue"));
    GG_CHECK(s.session()->status()->conflicted.front().binary);
}

GG_TEST("conflicts", "first-class conflicts: History marks, filter, F7, Change information, Changes",
    "CONF-PARSE-DIFF3", "CONF-DISPLAY-HISTORY", "HIST-FILTER-CONFLICTED", "HIST-KEY-NEXT-CONFLICT",
    "HIST-KEY-PREV-CONFLICT", "INFO-CONFLICTED-FILES", "CONF-DISPLAY-INFO", "CHG-FIRSTCLASS-CONFLICTED")
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
}

GG_TEST("conflicts", "marker parsing: N sides, marker length, malformed, opt-out, disposable cache", "CONF-PARSE-NWAY",
    "CONF-MARKER-LENGTH", "CONF-WELLFORMED-ONLY", "CONF-OPTOUT", "CONF-CACHE-DISPOSABLE")
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
    fs::remove_all(repo / ".git" / "gg");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(scanned(s, nway));
    GG_CHECK(s.waitUntil([&] { return files(optout) == before; }));
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflicts", "marker grammar edge cases (docs/spec/conflict-markers.md)", "CONF-PARSE-DIFF3", "CONF-PARSE-NWAY",
    "CONF-WELLFORMED-ONLY", "CONF-MARKER-LENGTH", "CLI-CONFLICTS")
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

GG_TEST("conflicts", "checking out a conflicted commit: clean status by default, index stages when asked",
    "CONF-CHECKOUT-CLEAN", "CONF-CHECKOUT-EXPAND-STAGES")
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

GG_TEST("conflicts", "first-class: term view, take a side, Mark resolved, Amend resolves the descendants too",
    "DIFF-TERM-VIEW", "CONF-RESOLVE-TAKE-SIDE", "CONF-MARK-RESOLVED-REFUSE", "CONF-RESOLVE-AMEND",
    "CHG-FIRSTCLASS-RESOLVE-AMEND")
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
    s.comboSelect("//Diff/##term_view", "Base \xe2\x86\x92 side 1");
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
    s.contextMenu(wtFile(s, "conflict.txt").c_str(), "Take side/Side 2 (whole file)");
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

GG_TEST("conflicts", "first-class: take a side in one region; resolve in the editor and commit on top",
    "CONF-RESOLVE-EDITOR", "CONF-RESOLVE-NEW-COMMIT")
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
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Resolve two.txt");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"log", "-1", "--format=%s"}) == "Resolve two.txt"; }));
    s.settle();
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts"}).exitCode, 0);
    GG_CHECK_EQ(s.gitgg(repo, {"conflicts", "HEAD~1"}).exitCode, 1);
}

GG_TEST("conflicts", "first-class: resolve with the merge tool (stages from the regions)", "CONF-RESOLVE-MERGETOOL")
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

GG_TEST("conflicts", "toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict",
    "CONF-NATIVE-DETECT", "CONF-NATIVE-CONTINUE", "CONF-NATIVE-MERGETOOL", "TB-STATE-BADGE")
{
    auto clean = [&](const fs::path&) {
        return s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::None; });
    };
    const fs::path revert = s.fixture(Recipe::MidRevert);
    GG_REQUIRE(s.openRepository(revert));
    ctx->ItemClick("//##Toolbar/Abort##tb_abort");
    GG_CHECK(clean(revert));
    GG_CHECK(!fs::exists(revert / ".git" / "REVERT_HEAD"));
    s.settle();
    const fs::path apply = s.fixture(Recipe::MidRebaseApply);
    GG_REQUIRE(s.openRepository(apply));
    GG_CHECK(s.session()->snapshot()->state == ggui::core::RepoState::Rebasing);
    ctx->ItemClick("//##Toolbar/Abort##tb_abort");
    GG_CHECK(clean(apply));
    GG_CHECK(!fs::exists(apply / ".git" / "rebase-apply"));
    s.settle();
    const fs::path bisect = s.fixture(Recipe::Bisecting);
    GG_REQUIRE(s.openRepository(bisect));
    const std::string before = s.head(bisect);
    ctx->ItemClick("//##Toolbar/Skip##tb_skip");
    GG_CHECK(s.waitUntil([&] { return s.head(bisect) != before; }));
    s.settle();
    ctx->ItemClick("//##Toolbar/Reset##tb_abort");
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
