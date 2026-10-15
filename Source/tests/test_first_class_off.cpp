// gg.firstClassConflicts: off (the default) disables all first-class conflict handling.
#include "panels/HistoryPanel.hpp"
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

} // namespace ggtest
