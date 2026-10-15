// gg.firstClassConflicts: off (the default) disables all first-class conflict handling.
#include "panels/HistoryPanel.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

using ggui::core::Oid;

bool hasFirstClass(Scenario& s)
{
    const auto st = s.session()->status();
    if (!st)
        return false;
    for (const auto& e : st->conflicted)
        if (e.firstClass)
            return true;
    return false;
}

} // namespace

GG_TEST("first_class", "gg.firstClassConflicts off: no first-class entries and no conflict badges")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string conflictedId = s.revParse(repo, "HEAD~1");
    // The conflicted text, edited in the working tree: its markers stay in the file.
    s.write(repo, "conflict.txt",
        "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\n=======\nx=2\n>>>>>>> side 2\nbottom\nmore\n");
    GG_REQUIRE(s.openRepository(repo));
    // On (the harness default): a badge on the commit and a first-class entry in the status.
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) != nullptr; }));
    GG_CHECK(s.waitUntil([&] { return hasFirstClass(s); }));
    // Off: after a refresh neither exists.
    s.setFirstClassConflicts(repo, false);
    s.session()->refresh();
    s.session()->history().reload();
    GG_CHECK(s.waitUntil([&] { return !hasFirstClass(s) && s.session()->status() != nullptr; }));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) == nullptr; }));
    // On again: both return.
    s.setFirstClassConflicts(repo, true);
    s.session()->refresh();
    s.session()->history().reload();
    GG_CHECK(s.waitUntil([&] { return hasFirstClass(s); }));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) != nullptr; }));
}

GG_TEST("first_class", "Settings: the First-class conflicts checkbox writes the repository config")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const std::string conflictedId = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) != nullptr; }));
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    const char* box = "//Settings/##settings_tabs/Git/First-class conflicts (experimental)##first_class";
    auto checked = [&] { return s.itemExists(box) && (ctx->ItemInfo(box).StatusFlags & ImGuiItemStatusFlags_Checked); };
    auto enabled = [&] { return s.itemExists(box) && !(ctx->ItemInfo(box).ItemFlags & ImGuiItemFlags_Disabled); };
    GG_REQUIRE(s.waitUntil([&] { return enabled() && checked(); }));
    ctx->ItemClick(box); // off
    GG_CHECK(s.waitUntil([&] { return gg::trim(s.gitMayFail(repo, {"config", "--local", "gg.firstClassConflicts"}).out) == "false"; }));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) == nullptr; }));
    GG_CHECK(s.waitUntil([&] { return enabled() && !checked(); }));
    ctx->ItemClick(box); // on
    GG_CHECK(s.waitUntil([&] { return gg::trim(s.gitMayFail(repo, {"config", "--local", "gg.firstClassConflicts"}).out) == "true"; }));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflictedId)) != nullptr; }));
}

GG_TEST("first_class", "off: a drop whose descendant would conflict is refused, on: it proceeds")
{
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "a.txt", "one\ntwo\nthree\n", "c1 add a");
    s.commitFile(repo, "a.txt", "one\nTWO\nthree\n", "c2 change a");
    const std::string c2 = s.head(repo);
    s.commitFile(repo, "a.txt", "one\nTHREE\nthree\n", "c3 change a again");
    const std::string c3 = s.head(repo);
    s.setFirstClassConflicts(repo, false);
    GG_REQUIRE(s.openRepository(repo));
    const std::string refs = s.gitOut(repo, {"for-each-ref"});
    s.session()->actions().abandon(Oid::fromHex(c2), false);
    GG_REQUIRE(s.dismissError());
    GG_CHECK_STR_EQ(s.app.errorTitle(), "drop commit " + c2.substr(0, 10));
    const std::string& msg = s.app.errorMessage();
    GG_CHECK(msg.find("would conflict in:\n" + c3.substr(0, 10) + " c3 change a again: a.txt\n") != std::string::npos);
    GG_CHECK(msg.find("First-class conflicts are off (Settings > Git).") != std::string::npos);
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), c3);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"for-each-ref"}), refs);
    GG_CHECK(s.statusPorcelain(repo).empty());
    for (const char* dir : {"rebase-merge", "rebase-apply", "sequencer"})
        GG_CHECK(!fs::exists(repo / ".git" / dir));
    // On: the same drop goes through and stores the conflict in the commit.
    s.setFirstClassConflicts(repo, true);
    s.session()->requestConfig();
    GG_REQUIRE(s.waitUntil([&] { return s.session()->firstClassConflicts(); }));
    s.session()->actions().abandon(Oid::fromHex(c2), false);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != c3; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s"}), "c3 change a again");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-list", "--count", "HEAD"}), "2");
}

GG_TEST("first_class", "off: a merge is a plain git merge that stops at conflicts; Abort restores the tree")
{
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "a.txt", "one\ntwo\nthree\n", "c1 add a");
    s.git(repo, {"branch", "side"});
    s.commitFile(repo, "a.txt", "one\nMAIN\nthree\n", "main change");
    s.git(repo, {"switch", "-q", "side"});
    s.commitFile(repo, "a.txt", "one\nSIDE\nthree\n", "side change");
    s.git(repo, {"switch", "-q", "main"});
    const std::string head = s.head(repo);
    s.setFirstClassConflicts(repo, false);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().mergeIntoHead("side", {});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::Merging; }));
    s.settle();
    GG_CHECK(fs::exists(repo / ".git" / "MERGE_HEAD"));
    GG_CHECK(!s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.read(repo, "a.txt").find("<<<<<<<") != std::string::npos);
    GG_CHECK_STR_EQ(s.head(repo), head);
    ctx->ItemClick("//###Toolbar/Abort##tb_abort");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::None; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / ".git" / "MERGE_HEAD"));
    GG_CHECK(s.gitOut(repo, {"ls-files", "-u"}).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());
    GG_CHECK_STR_EQ(s.read(repo, "a.txt"), "one\nMAIN\nthree\n");
    GG_CHECK_STR_EQ(s.head(repo), head);
}

} // namespace ggtest
