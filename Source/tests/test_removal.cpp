// REBUILD_PLAN §9 removal checklist (P4-05; docs/removal-audit.md): the jj-style parts of the old
// gg must not come back. The post-test hook checks refs/gg and .git/gg after every test; this
// scenario does so on purpose after a representative mix of ggui, git gg and plain git (with the
// managed hooks) operations, then deletes .git/gg and shows that nothing changes meaning.
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <libgg/GitRunner.hpp>

#include <set>

namespace ggtest {

namespace {

using ggui::core::Oid;

std::string subjectOf(Scenario& s, const fs::path& repo, const std::string& rev)
{
    return s.gitOut(repo, {"log", "-1", "--format=%s", rev});
}

// `git gg ARGS` as a user runs it from a shell, not as one of ggui's git children (where a lone
// unknown argument is an askpass prompt for the running ggui).
gg::RunResult gitggAsUser(const fs::path& repo, const std::vector<std::string>& args)
{
    gg::RunRequest req;
    req.args = {"git", "gg"};
    req.args.insert(req.args.end(), args.begin(), args.end());
    req.cwd = repo;
    req.gitEnvironment = false;
    req.env.emplace_back("GG_ASKPASS_ENDPOINT", std::nullopt);
    return gg::run(req);
}

} // namespace

GG_TEST("removal", "ggui, git gg and plain git leave no refs/gg; .git/gg is only journal and caches, deleting it changes nothing",
    "REMOVAL-NO-GG-STATE")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // main and side both edit shared.txt: rebasing main onto side gives a text conflict.
    s.commitFile(repo, "shared.txt", "base\n", "Add shared");
    s.git(repo, {"branch", "side"});
    s.commitFile(repo, "shared.txt", "main\n", "Main edits shared");
    s.git(repo, {"switch", "-q", "side"});
    s.commitFile(repo, "shared.txt", "side\n", "Side edits shared");
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->hooksInstalled(); }));

    // ggui: in-memory rebase whose text conflict becomes first-class (it lives in the file only).
    const std::string before = s.head(repo);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_side/###branch_side"); }));
    s.contextMenu("//Branches/branch_side/###branch_side", "Rebase HEAD onto branch");
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != before; }));
    s.settle();
    auto r = s.gitgg(repo, {"conflicts"});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.out.find("shared.txt") != std::string::npos);

    // ggui: stash and pop from the toolbar.
    s.write(repo, "shared.txt", "resolved\n");
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    ctx->ItemClick("//##Toolbar/###tb_stash");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return s.statusPorcelain(repo).empty(); }));
    s.settle();
    ctx->ItemClick("//##Toolbar/###tb_pop");
    GG_CHECK(s.waitUntil([&] { return !s.statusPorcelain(repo).empty(); }));
    s.settle();
    s.git(repo, {"checkout", "-q", "--", "shared.txt"});
    s.settle();

    // ggui: reword the parent (the conflicted HEAD is rebased onto it), Undo and Redo.
    const std::string parent = s.revParse(repo, "HEAD~1");
    ctx->ItemClick(("//History/**/###row_" + parent).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/##message"); }));
    s.setText("//Change information/##message", "Reworded by ggui");
    ctx->ItemClick("//Change information/###save_message");
    GG_CHECK(s.waitUntil([&] { return subjectOf(s, repo, "HEAD~1") == "Reworded by ggui"; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.revParse(repo, "HEAD~1") == parent; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(s.waitUntil([&] { return subjectOf(s, repo, "HEAD~1") == "Reworded by ggui"; }));
    s.settle();

    // git gg and plain git (journaled by the managed hooks).
    GG_REQUIRE(s.gitgg(repo, {"new", "-m", "From git gg"}).ok());
    GG_REQUIRE(s.gitgg(repo, {"undo"}).ok());
    GG_REQUIRE(s.gitgg(repo, {"redo"}).ok());
    GG_CHECK(s.gitgg(repo, {"op", "log"}).ok());
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "Plain commit"});
    s.git(repo, {"tag", "plain-tag"});
    s.git(repo, {"branch", "plain-branch", "HEAD~1"});
    s.settle();

    // Only refs git itself uses; nothing under refs/gg.
    GG_CHECK(s.gitOut(repo, {"for-each-ref", "refs/gg"}).empty());
    for (const auto& ref : gg::splitLines(s.gitOut(repo, {"for-each-ref", "--format=%(refname)"}))) {
        const bool gitOwn = ref.rfind("refs/heads/", 0) == 0 || ref.rfind("refs/tags/", 0) == 0 || ref == "refs/stash";
        if (!gitOwn)
            ctx->LogError("unexpected ref %s", ref.c_str());
        GG_CHECK(gitOwn);
    }
    // .git/gg: the journal, the conflict-scan cache and the managed-hook runner, nothing else.
    std::set<std::string> entries;
    for (const auto& e : fs::directory_iterator(repo / ".git" / "gg"))
        entries.insert(e.path().filename().string());
    GG_CHECK(entries.count("journal") == 1);
    for (const auto& name : entries) {
        const bool allowed = name == "journal" || name == "cache" || name == "hooks";
        if (!allowed)
            ctx->LogError("unexpected entry .git/gg/%s", name.c_str());
        GG_CHECK(allowed);
    }

    // Deleting .git/gg loses undo history and nothing else: same refs, files and conflicts.
    const std::string conflicted = s.revParse(repo, ":/Main edits shared");
    const std::string conflictsBefore = s.gitgg(repo, {"conflicts", conflicted}).out;
    const auto refsBefore = s.refs(repo);
    const std::string statusBefore = s.statusPorcelain(repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "uninstall"}).ok());
    fs::remove_all(repo / ".git" / "gg");
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_CHECK(s.refs(repo) == refsBefore);
    GG_CHECK_STR_EQ(s.statusPorcelain(repo), statusBefore);
    r = s.gitgg(repo, {"conflicts", conflicted});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK_STR_EQ(r.out, conflictsBefore);
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(conflicted)) != nullptr; }));
    GG_CHECK(s.session()->operations().empty());
}

GG_TEST("removal", "git gg has none of the old gg command families", "REMOVAL-NO-OLD-CLI")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string head = s.head(repo);
    const auto refsBefore = s.refs(repo);
    // The old families and a revset argument: usage errors, and nothing changes.
    const std::vector<std::vector<std::string>> old{{"branch", "list"}, {"file", "list"}, {"util", "gc"},
        {"workspace", "list"}, {"config", "list"}, {"operation", "restore", "--what", "repo"}, {"op", "restore"},
        {"next"}, {"prev"}, {"log", "-r", "all()"}, {"new", "-r", "@-"}};
    for (const auto& args : old) {
        const auto r = gitggAsUser(repo, args);
        if (r.ok())
            ctx->LogError("git gg %s succeeded", args.front().c_str());
        GG_CHECK_EQ(r.exitCode, 129); // git's usage-error status
        GG_CHECK(!r.err.empty());
    }
    GG_CHECK_STR_EQ(s.head(repo), head);
    GG_CHECK(s.refs(repo) == refsBefore);
    // The help lists only the §6 commands.
    const auto help = gitggAsUser(repo, {"help"});
    GG_REQUIRE(help.ok());
    for (const char* gone : {"branch", "workspace", "util", "next", "prev", "restore", "revset", "fileset"})
        GG_CHECK(help.out.find(gone) == std::string::npos);
}

} // namespace ggtest
