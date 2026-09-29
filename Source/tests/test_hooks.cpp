// Managed hooks (§4.12 B, §5 H1).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "shell/Settings.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/GitRunner.hpp>
#include <libgg/Journal.hpp>

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace ggtest {

namespace {

std::vector<gg::journal::Operation> journalOps(const fs::path& repo, const std::string& src = {})
{
    gg::journal::Journal journal{repo / ".git"};
    std::string error;
    auto ops = journal.read(&error);
    if (!src.empty())
        std::erase_if(ops, [&](const auto& op) { return op.src != src; });
    return ops;
}

// Everything Undo restores: refs (with symbolic HEAD) and the index.
std::string repoState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    state += "\n" + s.gitOut(repo, {"ls-files", "-s"});
    return state;
}

// Hooks directory contents (name → mode and bytes).
std::map<std::string, std::string> hooksDir(const fs::path& repo)
{
    std::map<std::string, std::string> out;
    for (const auto& e : fs::directory_iterator(repo / ".git" / "hooks")) {
        std::ifstream f(e.path(), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        out[e.path().filename().string()] = std::to_string(static_cast<int>(e.status().permissions())) + ":" + ss.str();
    }
    return out;
}

// git with PATH lacking the directory of git-gg (hooks must then do nothing / warn).
gg::RunResult gitWithoutGitGg(const fs::path& repo, std::vector<std::string> args)
{
    const fs::path binDir = gg::findInPath("git-gg").parent_path();
    std::string path;
    std::stringstream parts(ggui::getEnv("PATH"));
    for (std::string part; std::getline(parts, part, kPathSep[0]);) {
        // Every spelling of the directory (Windows: case, slashes, a trailing separator).
        std::error_code ec;
        if (part.empty() || fs::equivalent(part, binDir, ec))
            continue;
        path += (path.empty() ? "" : kPathSep) + part;
    }
    gg::RunRequest r;
    args.insert(args.begin(), "git");
    r.args = std::move(args);
    r.cwd = repo;
    r.env.emplace_back("PATH", path);
    return gg::run(r);
}

void writeHook(Scenario& s, const fs::path& repo, const std::string& name, const std::string& body)
{
    s.write(repo / ".git" / "hooks", name, "#!/bin/sh\n" + body);
    fs::permissions(repo / ".git" / "hooks" / name, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
}

} // namespace

GG_TEST("hooks", "git gg hooks install/status/uninstall: config-defined hooks from git 2.54, wrapper scripts before")
{
    // Hooks defined in the configuration need git 2.54; older git gets wrapper scripts.
    const bool config = s.gitAtLeast(2, 54);
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string configBefore = s.read(repo / ".git", "config");
    auto r = s.gitgg(repo, {"hooks", "status"});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.out.rfind("not installed", 0) == 0);
    r = s.gitgg(repo, {"hooks", "install"});
    GG_REQUIRE(r.ok());
    GG_CHECK(r.out.find(config ? "config-defined" : "wrapper scripts") != std::string::npos);
    r = s.gitgg(repo, {"hooks", "status"});
    GG_CHECK(r.ok());
    GG_CHECK(r.out.rfind(config ? "installed (config-defined)" : "installed (wrapper scripts in ", 0) == 0);
    if (config)
        GG_CHECK(!s.gitOut(repo, {"config", "--get", "hook.ggui-reference-transaction.command"}).empty());
    GG_CHECK(fs::exists(repo / ".git" / "gg" / "hooks" / "run"));
    r = s.gitgg(repo, {"hooks", "uninstall"});
    GG_CHECK(r.ok());
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore);
    GG_CHECK_EQ(s.gitgg(repo, {"hooks", "status"}).exitCode, 1);
}

GG_TEST("hooks", "config-defined hooks: a repository path with a quote, a partial installation completed")
{
    // Forced config mode, in a repository whose path has a quote in it (the hook commands quote it);
    // one hook removed by hand leaves the installation partial.
    GG_REQUIRE_GIT(2, 54, "hooks defined in the configuration");
    const fs::path quoted = s.path("it's here");
    s.git(s.root(), {"init", "-q", "-b", "main", quoted.string()});
    s.track(quoted);
    s.commitFile(quoted, "a.txt", "a\n", "first");
    ggui::setEnv("GG_HOOKS_MODE", "config");
    auto r = s.gitgg(quoted, {"hooks", "install"});
    GG_REQUIRE(r.ok());
    GG_CHECK(r.out.find("config-defined") != std::string::npos);
    ggui::unsetEnv("GG_HOOKS_MODE");
    s.git(quoted, {"commit", "-q", "--allow-empty", "-m", "Journaled through a quoted path"});
    GG_CHECK_EQ(journalOps(quoted, "git").size(), static_cast<size_t>(1));
    s.git(quoted, {"config", "--local", "--remove-section", "hook.ggui-pre-push"});
    r = s.gitgg(quoted, {"hooks", "status"});
    GG_CHECK_EQ(r.exitCode, 1);
    GG_CHECK(r.out.rfind("partially installed", 0) == 0);
    GG_REQUIRE(s.gitgg(quoted, {"hooks", "install"}).ok()); // completes it
    GG_CHECK(s.gitgg(quoted, {"hooks", "status"}).ok());
    GG_CHECK(s.gitgg(quoted, {"hooks", "uninstall"}).ok());
}

GG_TEST("hooks", "wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path marker = s.path("post-commit-ran");
    writeHook(s, repo, "post-commit", "echo chained > '" + marker.string() + "'\n");
    writeHook(s, repo, "pre-push", "echo 'user pre-push says no' >&2\nexit 3\n");
    const auto before = hooksDir(repo);
    ggui::setEnv("GG_HOOKS_MODE", "wrapper");
    auto r = s.gitgg(repo, {"hooks", "install"});
    GG_REQUIRE(r.ok());
    GG_CHECK(r.out.find("wrapper scripts") != std::string::npos);
    GG_CHECK(s.read(repo / ".git" / "hooks", "post-commit").find("# ggui managed hook") != std::string::npos);
    GG_CHECK(fs::exists(repo / ".git" / "hooks" / "post-commit.gg-previous"));
    // Both the ggui runner (journal) and the user's hook run.
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "Through the wrapper"});
    GG_CHECK(fs::exists(marker));
    auto ops = journalOps(repo, "git");
    GG_CHECK_EQ(ops.size(), static_cast<size_t>(1));
    // The operation is the git command's, not the wrapper script's (the hook's shell and the
    // runner sit between git and git-gg); a command with several ref transactions is one.
    auto gitLabel = [](const std::string& label) {
        return label.rfind("git", 0) == 0 && label.find("hooks/") == std::string::npos;
    };
    if (!ops.empty())
        GG_CHECK(gitLabel(ops.back().label));
    const fs::path chains = s.path("process-chains.txt"); // Windows: which ancestors the hooks saw
    ggui::setEnv("GG_DEBUG_PROCESS", chains.string());
    s.git(repo, {"checkout", "-q", "-b", "wrapped"});
    ggui::unsetEnv("GG_DEBUG_PROCESS");
    ops = journalOps(repo, "git");
    if (ops.size() != 2)
        ctx->LogInfo("hook process chains:\n%s", s.read(chains.parent_path(), chains.filename().string()).c_str());
    GG_CHECK_EQ(ops.size(), static_cast<size_t>(2));
    if (!ops.empty())
        GG_CHECK(gitLabel(ops.back().label));
    // The user's pre-push exit status still decides.
    r = s.gitMayFail(repo, {"push", "-q", "origin", "main"});
    GG_CHECK(!r.ok());
    GG_CHECK(r.err.find("user pre-push says no") != std::string::npos);
    r = s.gitgg(repo, {"hooks", "uninstall"});
    GG_CHECK(r.ok());
    GG_CHECK(hooksDir(repo) == before);
    ggui::unsetEnv("GG_HOOKS_MODE");
}

GG_TEST("hooks", "plain git commands are journaled one operation each and Undo restores them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->hooksInstalled(); }));
    // Each command: exactly one new "git" operation labelled with the command, undone by Ctrl+Z.
    auto step = [&](std::vector<std::string> args) {
        s.settle();
        const std::string before = repoState(s, repo);
        const size_t ops = journalOps(repo, "git").size();
        s.git(repo, args);
        const auto after = journalOps(repo, "git");
        std::string cmd = "git";
        for (const auto& a : args)
            cmd += " " + a;
        if (after.size() != ops + 1)
            ctx->LogError("'%s' recorded %zu operations", cmd.c_str(), after.size() - ops);
        GG_CHECK_EQ(after.size(), ops + 1);
        if (!after.empty())
            GG_CHECK(after.back().label.find(args.front()) != std::string::npos);
        GG_CHECK(s.waitUntil([&] { return s.session()->operations().size() >= journalOps(repo).size(); }));
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        const bool restored = s.waitUntil([&] { return repoState(s, repo) == before; });
        if (!restored)
            ctx->LogError("undo of '%s' did not restore the repository", cmd.c_str());
        GG_CHECK(restored);
        s.settle();
    };
    step({"commit", "-q", "--allow-empty", "-m", "Plain commit"});
    step({"branch", "plain-branch"});
    step({"checkout", "-q", "-b", "plain-switch"});
    step({"reset", "-q", "--hard", "HEAD~2"});
    // Global options before the command; reset modes that do and do not touch the working tree.
    step({"-c", "core.abbrev=12", "--no-pager", "reset", "-q", "--keep", "HEAD~1"});
    step({"-C", repo.string(), "reset", "-q", "--merge", "HEAD~1"});
    step({"reset", "-q", "--soft", "HEAD~1"});
    // A fast-forward merge (post-merge runs).
    s.git(repo, {"branch", "ahead", s.gitOut(repo, {"commit-tree", "HEAD^{tree}", "-p", "HEAD", "-m", "ahead"})});
    step({"merge", "-q", "--ff-only", "ahead"});
    // GG_NO_JOURNAL set: the hooks leave the command out of the journal.
    {
        const size_t ops = journalOps(repo, "git").size();
        ggui::setEnv("GG_NO_JOURNAL", "1");
        s.git(repo, {"branch", "unjournaled"});
        ggui::unsetEnv("GG_NO_JOURNAL");
        GG_CHECK_EQ(journalOps(repo, "git").size(), ops);
        s.git(repo, {"branch", "-D", "unjournaled"});
        s.settle();
    }
    // Switching to another commit: Undo carries the (clean) working tree back too.
    s.git(repo, {"branch", "plain-older", "HEAD~1"});
    step({"switch", "-q", "plain-older"});
    step({"checkout", "-q", "--detach", "HEAD~1"});
    // With a local change Undo moves only HEAD back; the change stays.
    s.git(repo, {"switch", "-q", "plain-older"});
    s.settle();
    const std::string onOlder = s.head(repo);
    s.write(repo, "f1.txt", "a local change\n");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != onOlder; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "a local change\n");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    s.git(repo, {"reset", "-q", "--hard"}); // (the index stayed where it was too)
    step({"tag", "-a", "-m", "annotated", "plain-tag"});
    // post-rewrite adds the rewritten commits to the amend's operation.
    s.git(repo, {"commit", "-q", "--amend", "-m", "Amended plainly"});
    const auto amend = journalOps(repo, "git").back();
    GG_CHECK_EQ(amend.rewrites.size(), static_cast<size_t>(1));
    s.settle();
    // ggui's own git commands join its operation (GG_OPERATION): no extra "git" operation.
    const size_t gitOps = journalOps(repo, "git").size();
    const size_t gguiOps = journalOps(repo, "ggui").size();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_CHECK(s.waitUntil([&] { return journalOps(repo, "ggui").size() == gguiOps + 1; }));
    s.settle();
    GG_CHECK_EQ(journalOps(repo, "git").size(), gitOps);
    // With hooks, the "no hooks" note is gone.
    s.showPanel("Operations");
    GG_CHECK(!s.textShown("//Operations", "Undo covers ggui and git gg only"));
}

GG_TEST("hooks", "without git-gg on PATH the hooks do nothing, pre-push warns")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    auto r = gitWithoutGitGg(repo, {"commit", "-q", "--allow-empty", "-m", "No git-gg"});
    GG_CHECK(r.ok());
    GG_CHECK(gg::trim(r.err).empty());
    GG_CHECK(journalOps(repo, "git").empty());
    r = gitWithoutGitGg(repo, {"push", "-q", "origin", "HEAD:refs/heads/pushed"});
    GG_CHECK(r.ok());
    GG_CHECK(r.err.find("git-gg not found") != std::string::npos);
}

GG_TEST("hooks", "hooks work in linked worktrees")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    const auto list = gg::splitLines(s.gitOut(repo, {"worktree", "list", "--porcelain"}));
    fs::path linked;
    for (const auto& line : list)
        if (line.rfind("worktree ", 0) == 0 && fs::path(line.substr(9)) != fs::canonical(repo)
            && fs::exists(fs::path(line.substr(9)) / ".git") && linked.empty())
            linked = line.substr(9); // (the fixture also has a prunable one whose directory is gone)
    GG_REQUIRE(!linked.empty());
    GG_CHECK(s.gitgg(linked, {"hooks", "status"}).ok());
    s.git(linked, {"commit", "-q", "--allow-empty", "-m", "In the linked worktree"});
    const auto ops = journalOps(repo, "git");
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK(ops.back().wt != "main");
    GG_CHECK(!ops.back().wt.empty());
    // And its undo works from there.
    auto r = s.gitgg(linked, {"undo"});
    GG_CHECK(r.ok());
    GG_CHECK(s.gitOut(linked, {"log", "-1", "--format=%s"}) != "In the linked worktree");
}

GG_TEST("hooks", "a fetch of thousands of refs stays fast with the hooks")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path origin = s.root() / (repo.filename().string() + "-origin.git");
    const std::string tip = s.gitOut(origin, {"rev-parse", "main"});
    // git before 2.51 updates fetched refs one transaction (two hook calls) at a time: slow with
    // any hook, so only the one operation is checked there, with fewer refs (still more records
    // than the journal tail search reads).
    const bool batched = s.gitAtLeast(2, 51);
    const int count = batched ? 3000 : 600;
    std::string input;
    for (int i = 0; i < count; ++i)
        input += "create refs/heads/many/b" + std::to_string(i) + " " + tip + "\n";
    GG_REQUIRE(s.git(origin, {"update-ref", "--stdin"}, input).ok());
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    const auto start = std::chrono::steady_clock::now();
    s.git(repo, {"fetch", "-q", "origin"});
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    ctx->LogInfo("fetch of %d refs with hooks: %lld ms", count, static_cast<long long>(ms));
    if (batched)
        GG_CHECK(ms < timeBudgetMs(5000));
    const auto ops = journalOps(repo, "git");
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK(ops.back().refs.size() >= static_cast<size_t>(count));
}

GG_TEST("hooks", "first-open prompt (Install / Not now / Never) and the Settings Hooks tab")
{
    s.app.settings().data().askHooksOnOpen = true;
    const fs::path repo = s.fixture(Recipe::Linear);
    auto reopen = [&](const fs::path& path) {
        if (s.session())
            ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
        ctx->Yield(2);
        ctx->ItemInputValue("//Welcome/##welcome_path", path.string().c_str());
    };
    reopen(repo);
    GG_REQUIRE(s.dialogOpen("Install ggui hooks?"));
    s.dialogButton("Install ggui hooks?", "Not now");
    GG_CHECK(s.app.settings().repo(repo.string()).hooks == ggui::HooksAnswer::NotNow);
    // "Not now" asks again next time; "Never" does not.
    reopen(repo);
    GG_REQUIRE(s.dialogOpen("Install ggui hooks?"));
    s.dialogButton("Install ggui hooks?", "Never");
    GG_CHECK(s.app.settings().repo(repo.string()).hooks == ggui::HooksAnswer::Never);
    reopen(repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->opened() && s.session()->hooksStatus(); }));
    s.settle();
    GG_CHECK(s.app.dialogs().current() == nullptr);
    // Install from the prompt in another repository.
    const fs::path other = s.fixture(Recipe::Merges);
    reopen(other);
    GG_REQUIRE(s.dialogOpen("Install ggui hooks?"));
    s.dialogButton("Install ggui hooks?", "Install");
    GG_CHECK(s.waitUntil([&] { return s.gitgg(other, {"hooks", "status"}).ok(); }));
    GG_CHECK(s.waitUntil([&] { return s.session()->hooksInstalled(); }));
    // Settings ▸ Hooks: remove, then install again.
    s.settle(); // the install finishes (buttons are disabled while ggui is busy)
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Hooks");
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "Status: installed"); }));
    ctx->ItemClick("//Settings/##settings_tabs/Hooks/Remove hooks##remove_hooks");
    GG_CHECK(s.waitUntil([&] { return s.gitgg(other, {"hooks", "status"}).exitCode == 1; }));
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "Status: not installed"); }));
    s.settle();
    ctx->ItemClick("//Settings/##settings_tabs/Hooks/Install hooks##install_hooks");
    GG_CHECK(s.waitUntil([&] { return s.gitgg(other, {"hooks", "status"}).ok(); }));
    ctx->WindowClose("//Settings");
}

} // namespace ggtest

namespace ggtest {

GG_TEST("hooks", "managed pre-push refuses plain git pushes of conflicted commits")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const fs::path bare = s.path("prepush-remote.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", "file://" + bare.generic_string()});
    s.git(repo, {"push", "-q", "origin", "main~2:refs/heads/main"}); // the clean base only
    s.git(repo, {"fetch", "-q", "origin"});
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    auto r = s.gitMayFail(repo, {"push", "origin", "main"});
    GG_CHECK(!r.ok());
    GG_CHECK(r.err.find("refusing to push commits with first-class conflicts") != std::string::npos);
    GG_CHECK(r.err.find("conflict.txt") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main~2"));
    // Only --no-verify bypasses it (git's own switch).
    r = s.gitMayFail(repo, {"push", "-q", "--no-verify", "origin", "main"});
    GG_CHECK(r.ok());
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main"));
    // Deleting a remote branch pushes no commits: allowed.
    s.git(repo, {"push", "-q", "--no-verify", "origin", "main:refs/heads/doomed"});
    r = s.gitMayFail(repo, {"push", "-q", "origin", "--delete", "doomed"});
    GG_CHECK(r.ok());
    GG_CHECK(!s.gitMayFail(bare, {"rev-parse", "-q", "--verify", "refs/heads/doomed"}).ok());
    // Clean commits the remote does not have yet (and it has one we do not): allowed.
    s.git(bare, {"update-ref", "refs/heads/other", s.gitOut(bare, {"commit-tree", "main~2^{tree}", "-m", "only on the remote"})});
    const std::string clean = s.gitOut(repo, {"commit-tree", "main~2^{tree}", "-p", "main~2", "-m", "clean"});
    r = s.gitMayFail(repo, {"push", "-q", "origin", "+" + clean + ":refs/heads/other"});
    GG_CHECK(r.ok());
    // Installing twice is fine; a user's hook that replaced a wrapper is left alone by uninstall.
    GG_CHECK(s.gitgg(repo, {"hooks", "install"}).ok());
    GG_CHECK(s.gitgg(repo, {"hooks", "uninstall"}).ok());
    ggui::setEnv("GG_HOOKS_MODE", "wrapper");
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    ggui::unsetEnv("GG_HOOKS_MODE");
    s.write(repo / ".git" / "hooks", "post-commit", "#!/bin/sh\necho mine\n");
    GG_CHECK(s.gitgg(repo, {"hooks", "uninstall"}).ok());
    GG_CHECK_STR_EQ(s.read(repo / ".git" / "hooks", "post-commit"), "#!/bin/sh\necho mine\n");
    // Outside a repository.
    GG_CHECK_EQ(s.gitgg(s.root(), {"hooks", "status"}).exitCode, 1);
}

GG_TEST("hooks", "managed pre-commit warns about a broken conflict region without blocking the commit")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    // Break the region: delete the "=======" separator, leaving the opening/closing markers as
    // plain text. HEAD still holds the conflict; the staged edit does not.
    s.write(repo, "conflict.txt", "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\nx=2\n>>>>>>> side 2\nbottom\n");
    s.git(repo, {"add", "conflict.txt"});
    const std::string before = s.head(repo);
    const auto r = s.gitMayFail(repo, {"commit", "-q", "-m", "Break the region"});
    GG_CHECK(r.ok()); // the warning never blocks the commit
    GG_CHECK(r.err.find("ggui: conflict.txt: conflict markers left at line 2, 7 (the edit broke a conflict region)")
        != std::string::npos);
    GG_CHECK(s.head(repo) != before);
}

GG_TEST("hooks", "managed pre-commit warns about a staged first-class conflict without blocking the commit")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    // A new staged file that is itself a first-class conflict: warns, commit still succeeds.
    s.write(repo, "new_conflict.txt", "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\n=======\nx=2\n>>>>>>> side 2\nbottom\n");
    s.git(repo, {"add", "new_conflict.txt"});
    auto r = s.gitMayFail(repo, {"commit", "-q", "-m", "Add a conflicted file"});
    GG_CHECK(r.ok());
    GG_CHECK(r.err.find(
        "ggui: new_conflict.txt: committing a first-class conflict (2-sided); resolve it before pushing")
        != std::string::npos);
    // An unrelated commit that does not touch conflict.txt: HEAD already holds that conflict
    // unchanged, so it is not this commit's doing and nothing is printed about it.
    s.write(repo, "unrelated.txt", "hello\n");
    s.git(repo, {"add", "unrelated.txt"});
    r = s.gitMayFail(repo, {"commit", "-q", "-m", "Unrelated change"});
    GG_CHECK(r.ok());
    GG_CHECK(r.err.find("conflict.txt") == std::string::npos);
}

GG_TEST("hooks", "managed pre-push refuses commits that left broken conflict markers")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    const fs::path bare = s.path("broken-push-remote.git");
    s.git(s.root(), {"init", "-q", "--bare", "-b", "main", bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", "file://" + bare.generic_string()});
    // Push the conflicted history itself first (before hooks are installed), so the new commit
    // below is the only outgoing one: it must be refused for broken markers, not for still
    // carrying the (already pushed) first-class conflict.
    s.git(repo, {"push", "-q", "origin", "main"});
    s.git(repo, {"fetch", "-q", "origin"});
    GG_REQUIRE(s.gitgg(repo, {"hooks", "install"}).ok());
    // HEAD (main) still holds conflict.txt's conflict unchanged; break its markers instead of
    // resolving it.
    s.write(repo, "conflict.txt", "top\n<<<<<<< side 1\nx=1\n||||||| base\nx=0\nx=2\n>>>>>>> side 2\nbottom\n");
    s.git(repo, {"add", "conflict.txt"});
    s.git(repo, {"commit", "-q", "-m", "Break the region"});
    auto r = s.gitMayFail(repo, {"push", "origin", "main"});
    GG_CHECK(!r.ok());
    GG_CHECK(r.err.find("refusing to push commits that left broken conflict markers") != std::string::npos);
    GG_CHECK(r.err.find("conflict.txt line 2, 7") != std::string::npos);
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main~1"));
    // Only --no-verify bypasses it.
    r = s.gitMayFail(repo, {"push", "-q", "--no-verify", "origin", "main"});
    GG_CHECK(r.ok());
    GG_CHECK_STR_EQ(s.gitOut(bare, {"rev-parse", "main"}), s.revParse(repo, "main"));
}

} // namespace ggtest
