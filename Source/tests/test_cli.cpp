// The git-gg executable (§6; P2-26): each command runs as a test step, results checked in the UI.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>

namespace ggtest {

namespace {

using ggui::core::Oid;

bool rowShown(Scenario& s, const std::string& hex)
{
    return s.waitUntil([&] { return s.session()->history().row(Oid::fromHex(hex)) != nullptr; });
}

} // namespace

GG_TEST("cli", "git gg new: on HEAD, with a message, detached, merge", "CLI-NEW", "CLI-NEW-MSG", "CLI-NEW-DETACH",
    "CLI-NEW-MERGE")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    const std::string topic = s.revParse(repo, "topic");
    GG_REQUIRE(s.openRepository(repo));
    auto r = s.gitgg(repo, {"new"});
    GG_REQUIRE(r.ok());
    const std::string plain = gg::trim(r.out);
    GG_CHECK_STR_EQ(s.head(repo), plain);
    GG_CHECK(rowShown(s, plain));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");

    r = s.gitgg(repo, {"new", "-m", "Described from the CLI"});
    GG_REQUIRE(r.ok());
    const std::string described = gg::trim(r.out);
    GG_CHECK(rowShown(s, described));
    GG_CHECK_STR_EQ(s.session()->history().row(Oid::fromHex(described))->subject, "Described from the CLI");

    r = s.gitgg(repo, {"new", "--detach", "-m", "Detached"});
    GG_REQUIRE(r.ok());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->headDetached; }));
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), described);

    s.git(repo, {"switch", "-q", "main"});
    r = s.gitgg(repo, {"new", "-m", "Merge from the CLI", "main", "topic"});
    GG_REQUIRE(r.ok());
    const std::string merge = gg::trim(r.out);
    GG_CHECK(rowShown(s, merge));
    GG_CHECK_STR_EQ(s.revParse(repo, merge + "^1"), described);
    GG_CHECK_STR_EQ(s.revParse(repo, merge + "^2"), topic);
    GG_CHECK_EQ(s.session()->history().row(Oid::fromHex(merge))->parents.size(), static_cast<size_t>(2));
}

GG_TEST("cli", "git gg undo, redo and op log", "CLI-UNDO", "CLI-REDO", "CLI-OP-LOG")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string before = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N); // an operation made in ggui
    GG_REQUIRE(s.waitUntil([&] { return s.head(repo) != before; }));
    s.settle();
    const std::string made = s.head(repo);
    auto r = s.gitgg(repo, {"undo"});
    GG_CHECK(r.ok());
    GG_CHECK(r.out.find("Undone: \"new commit\"") != std::string::npos);
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->head.hex() == before; }));
    r = s.gitgg(repo, {"redo"});
    GG_CHECK(r.ok());
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->head.hex() == made; }));
    r = s.gitgg(repo, {"op", "log"});
    GG_CHECK(r.ok());
    const auto lines = gg::splitLines(r.out);
    GG_REQUIRE(!lines.empty());
    GG_CHECK(lines[0].find("[git-gg] redo \"new commit\"") != std::string::npos); // newest first
    GG_CHECK(r.out.find("[git-gg] undo \"new commit\"") != std::string::npos);
    GG_CHECK(r.out.find("[ggui] new commit") != std::string::npos);
    GG_CHECK(r.out.find("refs/heads/main: ") != std::string::npos);
    // The UI lists the same operations.
    GG_CHECK(s.waitUntil([&] { return s.session()->operations().size() == 3; }));
}

GG_TEST("cli", "git gg conflicts, help and exit codes", "CLI-CONFLICTS", "CLI-HELP", "CLI-EXIT-CODES")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    auto r = s.gitgg(repo, {"conflicts"});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.out.find("conflict.txt (2 sides)") != std::string::npos);
    r = s.gitgg(repo, {"conflicts", "HEAD~2"});
    GG_CHECK_EQ(r.exitCode, 0);
    GG_CHECK(r.out.empty());
    // Git-style errors: 128 for fatal (with "fatal:" on stderr), 129 for usage.
    r = s.gitgg(repo, {"conflicts", "no-such-rev"});
    GG_CHECK_EQ(r.exitCode, 128);
    GG_CHECK(r.err.rfind("fatal: ", 0) == 0);
    r = s.gitgg(repo, {"new", "--no-such-option"});
    GG_CHECK_EQ(r.exitCode, 129);
    r = s.gitgg(s.root(), {"undo"}); // not a repository
    GG_CHECK_EQ(r.exitCode, 128);
    GG_CHECK(r.err.find("fatal:") != std::string::npos);
    // Help: overview and per command.
    r = s.gitgg(repo, {"help"});
    GG_CHECK(r.ok());
    for (const char* cmd : {"new", "undo", "redo", "op", "conflicts", "hooks", "ui"})
        GG_CHECK(r.out.find(cmd) != std::string::npos);
    r = s.gitgg(repo, {"help", "new"});
    GG_CHECK(r.ok() && r.out.rfind("usage: git gg new", 0) == 0);
    // ("git gg --help" is git's own: it opens the git-gg man page. The binary answers directly.)
    r = s.run(repo, {"git-gg", "--help"});
    GG_CHECK(r.ok() && r.out.rfind("usage: git gg", 0) == 0);
    r = s.gitgg(repo, {"help", "bogus"});
    GG_CHECK_EQ(r.exitCode, 1);
}

GG_TEST("cli", "git gg ui starts ggui on the repository", "CLI-UI")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    // A git-gg next to a stand-in ggui that records how it was started.
    const fs::path dir = s.path("ui-bin");
    fs::create_directories(dir);
    const fs::path real = gg::findInPath("git-gg");
    GG_REQUIRE(!real.empty());
    fs::copy_file(real, dir / "git-gg");
    fs::permissions(dir / "git-gg", fs::perms::owner_all);
    const fs::path log = s.path("ggui-started.log");
    s.write(dir, "ggui", "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + log.string() + "'\nexit 7\n");
    fs::permissions(dir / "ggui", fs::perms::owner_all);
    gg::RunRequest r;
    r.args = {(dir / "git-gg").string(), "ui", "."};
    r.cwd = repo;
    r.env.emplace_back("PATH", dir.string() + ":" + ggui::getEnv("PATH"));
    const auto res = gg::run(r);
    GG_CHECK_EQ(res.exitCode, 7); // ggui's exit code is passed on
    GG_CHECK_STR_EQ(s.read(log.parent_path(), log.filename().string()), fs::canonical(repo).string() + "\n");
}

} // namespace ggtest

namespace ggtest {

GG_TEST("cli", "git gg new --before/--after inserts and rebases the descendants", "CLI-NEW-BEFORE", "CLI-NEW-AFTER")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string mid = s.revParse(repo, "HEAD~2");
    const std::string tree = s.revParse(repo, "HEAD^{tree}");
    GG_REQUIRE(s.openRepository(repo));
    auto r = s.gitgg(repo, {"new", "--after", mid, "-m", "Inserted after"});
    GG_REQUIRE(r.ok());
    const std::string after = gg::trim(r.out);
    GG_CHECK_STR_EQ(s.revParse(repo, after + "^"), mid);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~2"), after);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^{tree}"), tree);
    GG_CHECK(s.statusPorcelain(repo).empty());
    GG_CHECK(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(after)) != nullptr; }));
    r = s.gitgg(repo, {"new", "--before", mid, "-m", "Inserted before"});
    GG_REQUIRE(r.ok());
    const std::string before = gg::trim(r.out);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~4"), before);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"log", "-1", "--format=%s", before}), "Inserted before");
    GG_CHECK_EQ(std::stoi(s.gitOut(repo, {"rev-list", "--count", "HEAD"})), 7);
    // One journal operation each, undone by git gg undo.
    GG_CHECK(s.gitgg(repo, {"undo"}).ok());
    GG_CHECK_EQ(std::stoi(s.gitOut(repo, {"rev-list", "--count", "HEAD"})), 6);
    // Usage errors.
    GG_CHECK_EQ(s.gitgg(repo, {"new", "--before", mid, "--after", mid}).exitCode, 129);
    GG_CHECK_EQ(s.gitgg(repo, {"new", "--after", "no-such"}).exitCode, 128);
}

GG_TEST("cli", "git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH",
    "CLI-UNDO", "CLI-REDO", "CLI-EXIT-CODES", "HOOK-CLI-INSTALL", "CLI-UI")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    auto r = s.gitgg(repo, {"undo"});
    GG_CHECK(!r.ok());
    GG_CHECK(r.err.find("Nothing to undo") != std::string::npos);
    r = s.gitgg(repo, {"redo"});
    GG_CHECK(r.err.find("Nothing to redo") != std::string::npos);
    // Undoing a checkout would overwrite a local change: refused with a hint.
    GG_REQUIRE(s.gitgg(repo, {"new", "-m", "one more"}).ok());
    s.write(repo, "f1.txt", "local change\n");
    GG_REQUIRE(s.gitgg(repo, {"undo"}).ok()); // a new commit: its files stay, nothing is lost
    s.git(repo, {"checkout", "-q", "-b", "other"});
    s.commitFile(repo, "f1.txt", "on other\n", "Other f1");
    s.git(repo, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_other/###branch_other"); }));
    s.contextMenu("//Branches/branch_other/###branch_other", "Check out");
    GG_REQUIRE(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "other"; }));
    s.settle();
    s.write(repo, "f1.txt", "edited after the checkout\n");
    r = s.gitgg(repo, {"undo"});
    GG_CHECK_EQ(r.exitCode, 128);
    GG_CHECK(r.err.find("hint: stash your changes first") != std::string::npos);
    s.git(repo, {"checkout", "-q", "--", "f1.txt"});
    // Contradictory insert options.
    GG_CHECK_EQ(s.gitgg(repo, {"new", "--before", "HEAD~1", "HEAD"}).exitCode, 129);
    GG_CHECK_EQ(s.gitgg(repo, {"new", "--after", "HEAD~1", "--detach"}).exitCode, 129);
    // Hooks outside a repository.
    GG_CHECK_EQ(s.gitgg(s.root(), {"hooks", "install"}).exitCode, 128);
    GG_CHECK_EQ(s.gitgg(s.root(), {"hooks", "uninstall"}).exitCode, 128);
    // The git-gg binary started by its path, with neither git-gg nor ggui on PATH: the hooks
    // status warns, and git gg ui cannot start ggui.
    const fs::path gitgg = gg::findInPath("git-gg");
    GG_REQUIRE(!gitgg.empty());
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    const fs::path gitDir = gg::findInPath("git").parent_path();
    auto bare = [&](std::vector<std::string> args) {
        gg::RunRequest req;
        req.args = {gitgg.string()};
        req.args.insert(req.args.end(), args.begin(), args.end());
        req.cwd = repo;
        req.env.emplace_back("PATH", gitDir.string());
        return gg::run(req);
    };
    r = bare({"hooks", "status"});
    GG_CHECK(r.out.find("git-gg is not on PATH") != std::string::npos);
    r = bare({"ui"});
    GG_CHECK_EQ(r.exitCode, 128);
    GG_CHECK(r.err.find("cannot start ggui") != std::string::npos);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "uninstall"}).ok());
    // git-gg as GIT_ASKPASS (git runs it with the prompt) when no ggui answers: no answer (exit 1).
    for (const char* endpoint : {"no-port-here", "1:token"}) {
        gg::RunRequest req;
        req.args = {gitgg.string(), "Password for x:"};
        req.cwd = repo;
        req.gitEnvironment = false; // (not ggui's own endpoint)
        req.env.emplace_back("GG_ASKPASS_ENDPOINT", endpoint);
        GG_CHECK_EQ(gg::run(req).exitCode, 1);
    }
}

} // namespace ggtest
