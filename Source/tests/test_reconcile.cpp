// The reconciler: plain git (no hooks) becomes undoable journal operations (docs: reconciler design).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include "util/Env.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Journal.hpp>
#include <libgg/Reconcile.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace ggtest {

namespace {

std::vector<gg::journal::Operation> journalOps(const fs::path& repo)
{
    gg::journal::Journal journal{repo / ".git"};
    std::string error;
    return journal.read(&error);
}

size_t countOps(const fs::path& repo, const std::string& src, const std::string& label = {})
{
    size_t n = 0;
    for (const auto& op : journalOps(repo))
        n += op.src == src && (label.empty() || op.label == label) ? 1 : 0;
    return n;
}

size_t panelOps(Scenario& s, const std::string& src)
{
    size_t n = 0;
    for (const auto& op : s.session()->operations())
        n += op.src == src ? 1 : 0;
    return n;
}

// Everything Undo restores: refs (with symbolic HEAD).
std::string refState(Scenario& s, const fs::path& repo)
{
    std::string state = s.gitOut(repo, {"for-each-ref", "--format=%(refname) %(objectname)"});
    auto head = s.gitMayFail(repo, {"symbolic-ref", "-q", "HEAD"});
    state += "\nHEAD " + (head.ok() ? gg::trim(head.out) : s.head(repo));
    return state;
}

std::vector<gg::journal::Operation> gitOps(const fs::path& repo)
{
    std::vector<gg::journal::Operation> out;
    for (auto& op : journalOps(repo))
        if (op.src == "git")
            out.push_back(std::move(op));
    return out;
}

const gg::journal::RefChange* refChange(const gg::journal::Operation& op, const std::string& ref)
{
    for (const auto& r : op.refs)
        if (r.ref == ref && r.oldValue != r.newValue)
            return &r;
    return nullptr;
}

} // namespace

GG_TEST("reconcile", "plain git changes without hooks are journaled and Undo restores them")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "x"});
    const std::string committed = refState(s, repo);
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    const auto& ops = s.session()->operations();
    GG_CHECK_STR_EQ(ops.back().label, "git commit"); // read from HEAD's reflog
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    s.settle();
    // Redo brings the commit back.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == committed; }));
    s.settle();

    // A new branch, a tag and a deleted branch: one operation each time the journal is looked at.
    s.git(repo, {"branch", "other", "main~1"});
    const std::string beforeMany = refState(s, repo);
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    s.git(repo, {"branch", "foo"});
    s.git(repo, {"tag", "t"});
    s.git(repo, {"branch", "-D", "other"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") >= 3; }));
    s.settle();
    const std::string afterMany = refState(s, repo);
    GG_CHECK(afterMany.find("refs/tags/t") != std::string::npos);
    GG_CHECK(afterMany.find("refs/heads/foo") != std::string::npos);
    GG_CHECK(afterMany.find("refs/heads/other") == std::string::npos);
    // Undo everything the plain git did (one or more external operations).
    for (int i = 0; i < 4 && refState(s, repo) != beforeMany; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.waitUntil([&] { return refState(s, repo) != afterMany; });
        s.settle();
    }
    GG_CHECK_STR_EQ(refState(s, repo), beforeMany);
    GG_CHECK(refState(s, repo).find("refs/heads/other") != std::string::npos);
}

GG_TEST("reconcile", "changes made while ggui is closed are journaled on open")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    s.git(repo, {"tag", "closed-tag"});
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git", "git commit"), 1u);
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 1u); // the tag has no reflog coverage
    for (int i = 0; i < 2; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.settle();
    }
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "git gg undo in a terminal with ggui closed reconciles first")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok()); // establishes the baseline
    const std::string start = refState(s, repo);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(refState(s, repo) != start);
    const auto r = s.gitgg(repo, {"undo"});
    GG_CHECK(r.ok());
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_CHECK_EQ(countOps(repo, "git", "git commit"), 1u);
}

GG_TEST("reconcile", "ggui's own operations are never re-journaled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return refState(s, repo) != start; }));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "ggui") == 1; }));
    s.settle();
    ctx->Yield(30);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "ggui"), 1u);
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
    // A terminal git gg command is not re-journaled either.
    GG_REQUIRE(s.gitgg(repo, {"new", "-m", "from the command line"}).ok());
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok());
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
}

GG_TEST("reconcile", "state file deleted: no history replay, no duplicates")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    const size_t before = journalOps(repo).size();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    std::error_code ec;
    GG_REQUIRE(fs::remove(repo / ".git" / "gg" / "reconcile.json", ec));
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(journalOps(repo).size(), before);
    GG_CHECK(fs::exists(repo / ".git" / "gg" / "reconcile.json"));
}

GG_TEST("reconcile", "a deleted journal makes the baseline start over: no replay, later plain git still journaled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    std::error_code ec;
    GG_REQUIRE(fs::remove(repo / ".git" / "gg" / "journal", ec));
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 0u); // the old journal's ops are not replayed as one
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "plain again"});
    GG_CHECK(s.waitUntil([&] { return countOps(repo, "git", "git commit") == 1; }));
}

GG_TEST("reconcile", "Undo of a symbolic ref that is not HEAD restores that ref and leaves HEAD alone")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    GG_REQUIRE(s.gitMayFail(repo, {"remote", "set-head", "origin", "main"}).ok());
    const std::string symbolic = s.gitOut(repo, {"symbolic-ref", "refs/remotes/origin/HEAD"});
    GG_REQUIRE(!symbolic.empty());
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string head = s.gitOut(repo, {"symbolic-ref", "HEAD"});
    GG_REQUIRE(s.gitMayFail(repo, {"remote", "set-head", "origin", "-d"}).ok());
    GG_CHECK(!s.gitMayFail(repo, {"symbolic-ref", "-q", "refs/remotes/origin/HEAD"}).ok());
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitMayFail(repo, {"symbolic-ref", "-q", "refs/remotes/origin/HEAD"}).ok(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "refs/remotes/origin/HEAD"}), symbolic);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "HEAD"}), head);
}

// ---- the managed hooks of older versions: uninstalled silently on open ----------------------------------

namespace {

const std::vector<std::string> kLegacyHooks{"reference-transaction", "post-checkout", "post-merge", "post-rewrite",
    "post-commit", "pre-push", "pre-commit"};

// What the old installer wrote, byte for byte: the runner, and either hook.ggui-<name> config entries
// or wrapper scripts over the hooks (the user's own one moved to <name>.gg-previous).
void installLegacyHooks(Scenario& s, const fs::path& repo, bool config, fs::path hooksDir = {})
{
    if (hooksDir.empty())
        hooksDir = repo / ".git" / "hooks";
    const fs::path runner = repo / ".git" / "gg" / "hooks" / "run";
    s.write(runner.parent_path(), "run", R"(#!/bin/sh
# ggui managed hooks runner (installed by "git gg hooks install", removed by uninstall).
# Does nothing when git-gg is not installed, so plain git never breaks because of ggui.
name="$1"
shift
if command -v git-gg >/dev/null 2>&1; then
    exec git-gg hook "$name" "$@"
fi
if [ "$name" = "pre-push" ]; then
    echo "ggui hooks: git-gg not found on PATH; skipping the first-class conflict check" >&2
fi
exit 0
)");
    fs::permissions(runner, fs::perms::owner_all);
    const std::string quoted = "'" + runner.generic_string() + "'";
    for (const auto& name : kLegacyHooks) {
        if (config) {
            s.git(repo, {"config", "--local", "hook.ggui-" + name + ".command", quoted + " " + name});
            s.git(repo, {"config", "--local", "hook.ggui-" + name + ".event", name});
            continue;
        }
        const fs::path hook = hooksDir / name;
        if (fs::exists(hook))
            fs::rename(hook, hooksDir / (name + ".gg-previous"));
        s.write(hooksDir, name,
            "#!/bin/sh\n# ggui managed hook: runs the ggui hooks runner, then the previous hook (if any).\n"
            "# \"git gg hooks uninstall\" restores the previous hook.\n"
            "input=$(cat; printf x)\n"
            "input=${input%x}\n"
            "status=0\n"
            "printf '%s' \"$input\" | "
                + quoted + " " + name
                + " \"$@\" || status=$?\n"
                  "prev=\"$0.gg-previous\"\n"
                  "if [ -x \"$prev\" ]; then\n"
                  "    printf '%s' \"$input\" | \"$prev\" \"$@\" || status=$?\n"
                  "fi\n"
                  "exit $status\n");
        fs::permissions(hook, fs::perms::owner_all);
    }
}

// Every file in .git/hooks with its bytes.
std::map<std::string, std::string> hookFiles(Scenario& s, const fs::path& dir)
{
    std::map<std::string, std::string> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file())
            files[e.path().filename().string()] = s.read(dir, e.path().filename().string());
    return files;
}

bool legacyGone(Scenario& s, const fs::path& repo)
{
    return s.gitMayFail(repo, {"config", "--get-regexp", "^hook\\."}).out.empty()
        && !fs::exists(repo / ".git" / "gg" / "hooks");
}

} // namespace

GG_TEST("reconcile", "legacy managed hooks (config mode) are uninstalled silently on open, then plain git is journaled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string configBefore = s.read(repo / ".git", "config");
    installLegacyHooks(s, repo, true);
    GG_REQUIRE(!legacyGone(s, repo));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return legacyGone(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore);
    GG_CHECK(s.app.dialogs().current() == nullptr);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the migration"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 1u);
    GG_CHECK_EQ(countOps(repo, "git", "external changes"), 0u);
}

GG_TEST("reconcile", "legacy managed hooks (wrapper scripts) are uninstalled on open, the user's hook restored byte-exact")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo / ".git" / "hooks", "pre-push", "#!/bin/sh\n# the user's own hook\necho 'pushing' >&2\nexit 0\n");
    fs::permissions(repo / ".git" / "hooks" / "pre-push", fs::perms::owner_all);
    const auto before = hookFiles(s, repo / ".git" / "hooks");
    installLegacyHooks(s, repo, false);
    GG_REQUIRE(hookFiles(s, repo / ".git" / "hooks") != before);
    GG_REQUIRE(fs::exists(repo / ".git" / "hooks" / "pre-push.gg-previous"));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return legacyGone(s, repo) && hookFiles(s, repo / ".git" / "hooks") == before; }));
    s.settle();
    GG_CHECK(hookFiles(s, repo / ".git" / "hooks") == before);
#ifndef _WIN32
    // Windows has no executable bit: libstdc++ reports one only for .exe/.bat/.cmd/.com names.
    const auto perms = fs::status(repo / ".git" / "hooks" / "pre-push").permissions();
    GG_CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
#endif
    GG_CHECK(s.app.dialogs().current() == nullptr);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the migration"});
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 1; }));
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 1u);
}

GG_TEST("reconcile", "legacy wrapper scripts under a relative core.hooksPath are uninstalled on open, byte-exact")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path dir = repo / ".githooks";
    s.git(repo, {"config", "core.hooksPath", ".githooks"});
    s.write(dir, "pre-push", "#!/bin/sh\n# the user's own hook\nexit 0\n");
    fs::permissions(dir / "pre-push", fs::perms::owner_all);
    const std::string configBefore = s.read(repo / ".git", "config");
    const auto before = hookFiles(s, dir);
    installLegacyHooks(s, repo, false, dir);
    GG_REQUIRE(hookFiles(s, dir) != before);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return legacyGone(s, repo) && hookFiles(s, dir) == before; }));
    s.settle();
    GG_CHECK(hookFiles(s, dir) == before);
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore); // core.hooksPath stays
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("reconcile", "legacy config-mode hooks are uninstalled when ggui opens a linked worktree")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path wt = s.root() / "linked";
    s.git(repo, {"worktree", "add", "-q", "-b", "linked", wt.string()});
    s.track(wt);
    const std::string configBefore = s.read(repo / ".git", "config");
    installLegacyHooks(s, repo, true);
    GG_REQUIRE(!legacyGone(s, repo));
    GG_REQUIRE(s.openRepository(wt));
    GG_CHECK(s.waitUntil([&] { return legacyGone(s, repo); }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore);
    GG_CHECK(s.app.dialogs().current() == nullptr);
}

GG_TEST("reconcile", "legacy managed hooks are uninstalled silently by git gg too")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string configBefore = s.read(repo / ".git", "config");
    s.write(repo / ".git" / "hooks", "pre-push", "#!/bin/sh\n# the user's own hook\nexit 0\n");
    fs::permissions(repo / ".git" / "hooks" / "pre-push", fs::perms::owner_all);
    const auto before = hookFiles(s, repo / ".git" / "hooks");
    installLegacyHooks(s, repo, false);
    installLegacyHooks(s, repo, true); // a stale install of both kinds
    const auto r = s.gitgg(repo, {"op", "log"});
    GG_REQUIRE(r.ok());
    GG_CHECK(r.err.empty());
    GG_CHECK(legacyGone(s, repo));
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore);
    GG_CHECK(hookFiles(s, repo / ".git" / "hooks") == before);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the migration"});
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok());
    GG_CHECK_EQ(countOps(repo, "git"), 1u);
}

GG_TEST("reconcile", "git gg hooks uninstall removes a legacy install and says so")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string configBefore = s.read(repo / ".git", "config");
    installLegacyHooks(s, repo, true);
    const auto r = s.gitgg(repo, {"hooks", "uninstall"});
    GG_CHECK(r.ok());
    GG_CHECK(r.out.find("Removed") != std::string::npos);
    GG_CHECK(legacyGone(s, repo));
    GG_CHECK_STR_EQ(s.read(repo / ".git", "config"), configBefore);
}

GG_TEST("reconcile", "another worktree's HEAD is never treated as deleted or created")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    GG_REQUIRE(s.openRepository(wt1));
    s.settle();
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(countOps(repo, "git"), 0u);
}

GG_TEST("reconcile", "one operation per plain git command, labelled")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string mainRef = "refs/heads/main", featRef = "refs/heads/feat";
    // Each command: exactly one new operation with the expected label and ref changes, nothing
    // left over for an "external changes" operation.
    auto step = [&](std::vector<std::string> args, const std::string& label, std::vector<std::string> refs) {
        const size_t before = gitOps(repo).size();
        s.git(repo, args);
        GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= before + 1; }));
        s.settle();
        ctx->Yield(10);
        const auto ops = gitOps(repo);
        if (ops.size() != before + 1)
            ctx->LogError("'git %s' recorded %zu operations", args.front().c_str(), ops.size() - before);
        GG_REQUIRE(ops.size() == before + 1);
        GG_CHECK_STR_EQ(ops.back().label, label);
        GG_CHECK(!ops.back().cmd.empty());
        for (const auto& ref : refs)
            if (!refChange(ops.back(), ref))
                ctx->LogError("'%s': no change of %s", label.c_str(), ref.c_str());
        size_t changed = 0;
        for (const auto& r : ops.back().refs)
            changed += r.oldValue != r.newValue ? 1 : 0;
        GG_CHECK_EQ(changed, refs.size());
    };
    step({"commit", "-q", "--allow-empty", "-m", "c1"}, "git commit", {mainRef});
    step({"switch", "-q", "-c", "feat"}, "git checkout feat", {"HEAD", featRef});
    step({"commit", "-q", "--allow-empty", "-m", "c2"}, "git commit", {featRef});
    const std::string featTip = s.head(repo);
    step({"checkout", "-q", "main"}, "git checkout main", {"HEAD"});
    step({"merge", "-q", "--no-ff", "-m", "merge feat", "feat"}, "git merge feat", {mainRef});
    step({"reset", "-q", "--hard", "HEAD~1"}, "git reset HEAD~1", {mainRef});
    step({"cherry-pick", "--allow-empty", featTip}, "git cherry-pick", {mainRef});
    for (const auto& op : gitOps(repo))
        GG_CHECK(op.label != "external changes");
}

GG_TEST("reconcile", "switch -c records HEAD's branch change")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    const auto* head = refChange(ops.front(), "HEAD");
    GG_REQUIRE(head != nullptr);
    GG_CHECK_STR_EQ(head->oldValue, "ref:refs/heads/main");
    GG_CHECK_STR_EQ(head->newValue, "ref:refs/heads/feat");
    // Undo puts HEAD back on main (and removes the branch).
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"branch", "--show-current"}) == "main"; }));
}

GG_TEST("reconcile", "commit then reset --hard while ggui is closed journals both")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    const std::string committed = refState(s, repo);
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    GG_CHECK_STR_EQ(refState(s, repo), start);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return panelOps(s, "git") == 2; }));
    s.settle();
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 2);
    GG_CHECK_STR_EQ(ops[0].label, "git commit");
    GG_CHECK_STR_EQ(ops[1].label, "git reset HEAD~1");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // undoes the reset: the commit is back
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == committed; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // undoes the commit
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "stash push leaves no HEAD operation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.write(repo, "f1.txt", "changed for the stash\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    // "reset: moving to HEAD" in HEAD's reflog changes nothing; the stash ref's own reflog says what
    // happened.
    for (const auto& op : gitOps(repo)) {
        GG_CHECK_STR_EQ(op.label, "git stash");
        GG_CHECK(refChange(op, "HEAD") == nullptr);
        GG_CHECK(refChange(op, "refs/heads/main") == nullptr);
        GG_CHECK(refChange(op, "refs/stash") != nullptr);
    }
}

GG_TEST("reconcile", "expired reflog falls back to the snapshot and never duplicates")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "while closed"});
    s.git(repo, {"reflog", "expire", "--expire=now", "--all"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ctx->Yield(10);
    GG_CHECK_EQ(gitOps(repo).size(), 1u);
    GG_CHECK_STR_EQ(gitOps(repo).front().label, "external changes");
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(gitOps(repo).size(), 1u);
    // The empty reflog is remembered: the next plain command is labelled again.
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the expiry"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    GG_CHECK_STR_EQ(gitOps(repo).back().label, "git commit");
}

GG_TEST("reconcile", "undo of a plain reset --hard carries the clean tree, of a plain commit keeps the changes")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string startHead = s.head(repo);
    s.write(repo, "added.txt", "added\n");
    s.git(repo, {"add", "added.txt"});
    s.git(repo, {"commit", "-q", "-m", "add a file"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / "added.txt"));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // the reset: the file comes back with the commit
    GG_CHECK(s.waitUntil([&] { return fs::exists(repo / "added.txt"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), "");
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); // the commit: HEAD moves back, the file stays (staged)
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == startHead; }));
    s.settle();
    GG_CHECK(fs::exists(repo / "added.txt"));
    GG_CHECK_STR_EQ(s.read(repo, "added.txt"), "added\n");
}

GG_TEST("reconcile", "a plain commit in a linked worktree is journaled from that worktree's own reflog")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path wt1 = s.root() / (repo.filename().string() + "-wt1");
    GG_REQUIRE(s.openRepository(wt1));
    s.settle();
    s.git(wt1, {"commit", "-q", "--allow-empty", "-m", "in the worktree"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git commit");
    GG_CHECK(!ops.front().wt.empty() && ops.front().wt != "main");
}

GG_TEST("reconcile", "a finished plain rebase is one operation and Undo restores the branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f2"});
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "m1"});
    s.git(repo, {"switch", "-q", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 6; }));
    s.settle();
    const std::string before = refState(s, repo);
    s.git(repo, {"rebase", "-q", "main"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 7; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 7);
    GG_CHECK_STR_EQ(ops.back().label, "git rebase");
    GG_CHECK(refChange(ops.back(), "refs/heads/feat") != nullptr);
    GG_CHECK(refChange(ops.back(), "HEAD") == nullptr); // back on the branch it started on
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == before; }));
}

GG_TEST("reconcile", "git rebase <upstream> <branch> on an up-to-date branch adds no operation")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f1"});
    s.git(repo, {"switch", "-q", "main"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 3; }));
    s.settle();
    s.git(repo, {"rebase", "-q", "main", "main"}); // up to date: "rebase: checkout main", nothing else
    s.waitUntil([] { return false; }, 0.4f); // the watcher's debounce, and the pass after it
    s.settle();
    GG_CHECK_EQ(gitOps(repo).size(), 3u);
    s.git(repo, {"switch", "-q", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 4; }));
    s.settle();
    s.git(repo, {"rebase", "-q", "feat", "feat"});
    s.waitUntil([] { return false; }, 0.4f); // the watcher's debounce, and the pass after it
    s.settle();
    GG_CHECK_EQ(gitOps(repo).size(), 4u);
    for (const auto& op : gitOps(repo))
        GG_CHECK(op.label != "git rebase");
}

GG_TEST("reconcile", "a plain rebase whose finish entry a pass reads only after the rebase directory is gone is still one operation")
{
    // What the race looks like: a pass read the reflog up to the last pick, then git wrote the
    // finish and removed rebase-merge/ before the pass looked for it. Staged by hand: the files a
    // finished rebase leaves are put back to that moment, and then forward again.
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f2"});
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "m1"});
    s.git(repo, {"switch", "-q", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 6; }));
    s.settle();
    const std::string before = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);

    const fs::path gitDir = repo / ".git";
    const std::vector<std::string> names = {"logs/HEAD", "logs/refs/heads/feat", "refs/heads/feat", "HEAD"};
    auto read = [&](const std::string& name) {
        std::ifstream in(gitDir / name, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    };
    auto write = [&](const std::string& name, const std::string& text) {
        std::ofstream out(gitDir / name, std::ios::binary | std::ios::trunc);
        out << text;
    };
    std::map<std::string, std::string> started, finished;
    for (const auto& name : names)
        started[name] = read(name);
    s.git(repo, {"rebase", "-q", "main"});
    for (const auto& name : names)
        finished[name] = read(name);
    const std::string tip = finished["refs/heads/feat"]; // the last replayed commit
    std::string headLog = finished["logs/HEAD"];
    const size_t lastLine = headLog.rfind('\n', headLog.size() - 2);
    GG_REQUIRE(lastLine != std::string::npos);
    GG_REQUIRE(headLog.find("rebase (finish)", lastLine) != std::string::npos);
    headLog.resize(lastLine + 1); // the finish entry is not written yet

    // The moment: HEAD detached on the last pick, the branch where it was, no rebase directory.
    // The two passes are run here, one after the other, with no watcher to run one in between.
    write("refs/heads/feat", started["refs/heads/feat"]);
    write("logs/refs/heads/feat", started["logs/refs/heads/feat"]);
    write("logs/HEAD", headLog);
    write("HEAD", tip);
    GG_REQUIRE(!fs::exists(gitDir / "rebase-merge"));
    {
        gg::git2::Repository r = gg::git2::openRepository(repo);
        std::string error;
        gg::reconcile::run(r.get(), &error);
        GG_CHECK(error.empty());
        GG_REQUIRE(gitOps(repo).size() == 7);
        GG_CHECK_STR_EQ(gitOps(repo).back().label, "git rebase");
        GG_CHECK(!gitOps(repo).back().ended); // the finish is still to come

        // Then git is done: the branch moves, HEAD goes back on it, the finish entry is there.
        for (const auto& name : {"refs/heads/feat", "logs/refs/heads/feat", "HEAD", "logs/HEAD"})
            write(name, finished[name]);
        gg::reconcile::run(r.get(), &error);
        GG_CHECK(error.empty());
        const auto ops = gitOps(repo);
        GG_REQUIRE(ops.size() == 7);
        GG_CHECK_STR_EQ(ops.back().label, "git rebase");
        GG_CHECK(ops.back().ended);
        GG_CHECK(refChange(ops.back(), "refs/heads/feat") != nullptr);
        GG_CHECK(refChange(ops.back(), "HEAD") == nullptr); // back on the branch it started on
        GG_CHECK(!fs::exists(gitDir / "gg" / "rebase"));
    }
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->operations().size() == 7; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == before; }));
}

GG_TEST("reconcile", "a rebase whose branch moved before HEAD's finish entry is one operation")
{
    // The other order of the same race: git moves the branch (its reflog gets "rebase (finish):
    // refs/heads/feat onto ...") before it writes the finish entry of HEAD's reflog. Staged by hand
    // as above: pass 1 sees the moved branch and HEAD's log without the finish.
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f1"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "f2"});
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "m1"});
    s.git(repo, {"switch", "-q", "feat"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 6; }));
    s.settle();
    const std::string before = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);

    const fs::path gitDir = repo / ".git";
    const std::vector<std::string> names = {"logs/HEAD", "logs/refs/heads/feat", "refs/heads/feat", "HEAD"};
    auto read = [&](const std::string& name) {
        std::ifstream in(gitDir / name, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    };
    auto write = [&](const std::string& name, const std::string& text) {
        std::ofstream out(gitDir / name, std::ios::binary | std::ios::trunc);
        out << text;
    };
    const std::string oldTip = read("refs/heads/feat");
    std::map<std::string, std::string> finished;
    s.git(repo, {"rebase", "-q", "main"});
    for (const auto& name : names)
        finished[name] = read(name);
    const std::string tip = finished["refs/heads/feat"]; // the last replayed commit
    std::string headLog = finished["logs/HEAD"];
    const size_t lastLine = headLog.rfind('\n', headLog.size() - 2);
    GG_REQUIRE(lastLine != std::string::npos);
    GG_REQUIRE(headLog.find("rebase (finish)", lastLine) != std::string::npos);
    headLog.resize(lastLine + 1); // HEAD's finish entry is not written yet

    // The moment: the branch has moved (and its log has the finish), HEAD is detached on the last
    // pick, its log ends there, and there is no rebase directory.
    write("refs/heads/feat", finished["refs/heads/feat"]);
    write("logs/refs/heads/feat", finished["logs/refs/heads/feat"]);
    write("logs/HEAD", headLog);
    write("HEAD", tip);
    GG_REQUIRE(!fs::exists(gitDir / "rebase-merge"));
    {
        gg::git2::Repository r = gg::git2::openRepository(repo);
        std::string error;
        gg::reconcile::run(r.get(), &error);
        GG_CHECK(error.empty());
        const auto first = gitOps(repo);
        GG_REQUIRE(first.size() == 7);
        GG_CHECK_STR_EQ(first.back().label, "git rebase");
        GG_CHECK(!first.back().ended); // the finish is still to come
        const auto* moved = refChange(first.back(), "refs/heads/feat");
        GG_REQUIRE(moved != nullptr);
        GG_CHECK(moved->oldValue == oldTip.substr(0, oldTip.find('\n'))); // where the branch was before the rebase

        // Then HEAD goes back on the branch, the finish entry is there.
        for (const auto& name : {"HEAD", "logs/HEAD"})
            write(name, finished[name]);
        gg::reconcile::run(r.get(), &error);
        GG_CHECK(error.empty());
        const auto ops = gitOps(repo);
        GG_REQUIRE(ops.size() == 7);
        GG_CHECK_STR_EQ(ops.back().label, "git rebase");
        GG_CHECK(ops.back().ended);
        GG_CHECK(refChange(ops.back(), "refs/heads/feat") != nullptr);
        GG_CHECK(refChange(ops.back(), "HEAD") == nullptr); // back on the branch it started on
        GG_CHECK(!fs::exists(gitDir / "gg" / "rebase"));
    }
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->operations().size() == 7; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == before; }));
}

GG_TEST("reconcile", "a rebase finished in another worktree is not part of the rebase stopped in this one")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"switch", "-q", "-c", "other"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "o1"});
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "m1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 4; }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);

    // This worktree: a rebase stopped at an edit. A linked worktree: the rebase of "other" is done.
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    const fs::path linked = repo.parent_path() / "linked";
    s.git(repo, {"worktree", "add", "-q", linked.string(), "other"});
    s.git(linked, {"rebase", "-q", "main"});
    gg::git2::Repository r = gg::git2::openRepository(repo);
    std::string error;
    gg::reconcile::run(r.get(), &error);
    GG_CHECK(error.empty());
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 6);
    GG_CHECK_STR_EQ(ops[4].label, "git rebase"); // this worktree's rebase, still open
    GG_CHECK(!ops[4].ended);
    GG_CHECK(refChange(ops[4], "refs/heads/other") == nullptr);
    GG_CHECK(refChange(ops[5], "refs/heads/other") != nullptr); // the other worktree's: its own operation
}

GG_TEST("reconcile", "a rebase started and aborted in a terminal, then a commit: the commit keeps its branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    s.git(repo, {"rebase", "--abort"});
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "after the abort"});
    const std::string committed = refState(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1); // no no-op "git rebase", no "external changes"
    GG_CHECK_STR_EQ(ops.front().label, "git commit");
    GG_CHECK(refChange(ops.front(), "refs/heads/main") != nullptr);
    GG_CHECK(refChange(ops.front(), "HEAD") == nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    GG_CHECK(committed != start);
}

GG_TEST("reconcile", "a plain rebase -i with an edit stop is one open op until it finishes; Undo refuses meanwhile, then restores it all")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    const std::string id = ops.front().id;
    GG_CHECK_STR_EQ(ops.front().src, "git");
    GG_CHECK_STR_EQ(ops.front().label, "git rebase");
    GG_CHECK(!ops.front().ended);
    GG_CHECK(ops.front().spansRebase);
    GG_CHECK(fs::exists(repo / ".git" / "gg" / "rebase" / "main" / "operation"));

    // Undo does not reach into the stopped rebase.
    const std::string stopped = refState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    s.settle();
    ctx->Yield(10);
    GG_CHECK(s.dismissError()); // "Nothing to undo": the open rebase is not a candidate
    GG_CHECK_STR_EQ(refState(s, repo), stopped);
    GG_CHECK_EQ(journalOps(repo).size(), 1u);
    GG_CHECK(fs::exists(repo / ".git" / "rebase-merge"));

    // Amend at the stop, go on to the next stop, amend again, finish: still the same operation.
    s.git(repo, {"commit", "-q", "--amend", "--allow-empty", "-m", "amended once"});
    s.git(repo, {"rebase", "--continue"});
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    s.git(repo, {"commit", "-q", "--amend", "--allow-empty", "-m", "amended twice"});
    GG_CHECK(s.waitUntil([&] { return journalOps(repo).size() == 1 && !journalOps(repo).front().ended; }));
    s.git(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty() && journalOps(repo).front().ended; }));
    s.settle();
    ctx->Yield(10);
    ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().id, id);
    GG_CHECK(ops.front().ok);
    GG_CHECK(!fs::exists(repo / ".git" / "gg" / "rebase"));
    GG_CHECK(refChange(ops.front(), "refs/heads/main") != nullptr);
    GG_CHECK(refState(s, repo) != start);
    GG_CHECK(s.waitUntil([&] {
        const auto snap = s.session()->snapshot();
        return snap && !snap->rebase;
    }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
}

GG_TEST("reconcile", "a plain rebase run entirely while ggui is closed is one op")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    s.git(repo, {"commit", "-q", "--amend", "--allow-empty", "-m", "amended"});
    s.git(repo, {"rebase", "--continue"});
    s.git(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-merge"));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK(ops.front().ended);
    GG_CHECK(!ops.front().spansRebase);
    GG_CHECK(!fs::exists(repo / ".git" / "gg" / "rebase"));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "a plain rebase --abort ends its group; there is nothing to undo from it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.waitUntil([&] { return journalOps(repo).size() == 1 && !journalOps(repo).front().ended; }));
    s.git(repo, {"rebase", "--abort"});
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty() && journalOps(repo).front().ended; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK(ops.front().ok);
    GG_CHECK(!ops.front().restorable()); // HEAD and main are where they were
    GG_CHECK(!fs::exists(repo / ".git" / "gg" / "rebase"));
    GG_CHECK_STR_EQ(refState(s, repo), start);
    // Nothing to undo: no undo operation is written.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.dismissError());
    s.settle();
    ctx->Yield(10);
    GG_CHECK_EQ(journalOps(repo).size(), 1u);
    GG_CHECK_STR_EQ(refState(s, repo), start);
}

GG_TEST("reconcile", "a stale cursor does not duplicate an operation the journal has")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "once"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    // Put the HEAD cursor back one entry (as after a crash between the journal append and the
    // cursor update): the commit's entry is consumed again.
    std::vector<std::string> log;
    {
        std::ifstream in(repo / ".git" / "logs" / "HEAD");
        for (std::string line; std::getline(in, line);)
            if (!line.empty())
                log.push_back(line);
    }
    GG_REQUIRE(log.size() >= 2);
    const std::string& prev = log[log.size() - 2];
    const size_t tab = prev.find('\t');
    GG_REQUIRE(tab != std::string::npos);
    const std::string head = prev.substr(0, tab);
    const size_t gt = head.find('>');
    GG_REQUIRE(gt != std::string::npos);
    std::istringstream when(head.substr(gt + 1));
    long long time = 0;
    when >> time;
    const fs::path statePath = repo / ".git" / "gg" / "reconcile.json";
    nlohmann::json state;
    {
        std::ifstream in(statePath);
        in >> state;
    }
    state["cursors"]["HEAD"] = {{"n", log.size() - 1}, {"old", prev.substr(0, 40)}, {"new", prev.substr(41, 40)}, {"time", time},
        {"msg", prev.substr(tab + 1)}};
    std::ofstream(statePath, std::ios::trunc) << state.dump();
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    ctx->Yield(20);
    s.settle();
    GG_CHECK_EQ(gitOps(repo).size(), 1u);
    GG_CHECK_EQ(journalOps(repo).size(), 1u);
}

GG_TEST("reconcile", "two resets away and back in one pass are two ops, each undoable")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "b"}); // journaled: main A -> B
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const std::string a = s.gitOut(repo, {"rev-parse", "HEAD~1"});
    const std::string atB = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"reset", "-q", "--hard", a});
    const std::string atA = refState(s, repo);
    s.git(repo, {"reset", "-q", "--hard", "ORIG_HEAD"});
    GG_CHECK_STR_EQ(refState(s, repo), atB);
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    ctx->Yield(10);
    GG_CHECK_EQ(gitOps(repo).size(), 3u);
    GG_CHECK_STR_EQ(refState(s, repo), atB);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == atA; }));
    s.settle();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == atB; }));
}

GG_TEST("reconcile", "Undo refuses a plain git operation while a rebase is in progress")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"commit", "-q", "--allow-empty", "-m", "before the rebase"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    ggui::setEnv("GIT_SEQUENCE_EDITOR", "sed -i 's/^pick/edit/'");
    s.git(repo, {"rebase", "-q", "-i", "HEAD~2"});
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-merge"));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    s.settle();
    const std::string state = refState(s, repo);
    const size_t before = journalOps(repo).size();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    s.settle();
    ctx->Yield(10);
    // Not the open rebase, and not the commit before it either: git owns the refs until it ends.
    GG_CHECK(s.app.dialogs().current() != nullptr);
    if (s.app.dialogs().current())
        GG_CHECK(s.app.dialogs().current()->message.find("finish or abort the rebase first") != std::string::npos);
    GG_CHECK(s.dismissError());
    GG_CHECK_STR_EQ(refState(s, repo), state);
    GG_CHECK_EQ(journalOps(repo).size(), before); // no undo operation was written
    GG_CHECK(fs::exists(repo / ".git" / "rebase-merge"));
    s.git(repo, {"rebase", "--abort"});
}

// Ctrl+Z while git owns the refs through rebase-apply/: refused with `message`, nothing changes.
static void expectUndoRefused(Scenario& s, ImGuiTestContext* ctx, const fs::path& repo, const std::string& message)
{
    s.settle();
    const std::string state = refState(s, repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    s.settle();
    ctx->Yield(10);
    GG_CHECK(s.app.dialogs().current() != nullptr);
    if (s.app.dialogs().current())
        GG_CHECK(s.app.dialogs().current()->message.find(message) != std::string::npos);
    GG_CHECK(s.dismissError());
    GG_CHECK_STR_EQ(refState(s, repo), state);
    // The reconciler may journal the plain git steps before Undo plans; no undo operation is written.
    for (const auto& op : journalOps(repo))
        GG_CHECK(op.undoes.empty());
}

GG_TEST("reconcile", "Undo refuses while an apply-backend rebase (git rebase --apply) is stopped at a conflict")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.commitFile(repo, "c.txt", "a\nb\nc\n", "Base c");
    s.git(repo, {"switch", "-q", "-c", "side"});
    s.commitFile(repo, "c.txt", "a\nside\nc\n", "Side c");
    s.git(repo, {"switch", "-q", "main"});
    s.commitFile(repo, "c.txt", "a\nmain\nc\n", "Main c");
    s.git(repo, {"switch", "-q", "side"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    GG_CHECK(!s.gitMayFail(repo, {"rebase", "--apply", "main"}).ok());
    GG_REQUIRE(fs::is_directory(repo / ".git" / "rebase-apply"));
    GG_CHECK(!fs::exists(repo / ".git" / "rebase-apply" / "applying"));
    GG_CHECK(!fs::exists(repo / ".git" / "rebase-merge"));
    expectUndoRefused(s, ctx, repo, "finish or abort the rebase first");
    GG_CHECK(fs::is_directory(repo / ".git" / "rebase-apply"));
    s.git(repo, {"rebase", "--abort"});
}

// A feat branch and main both change c.txt, so rebasing feat onto main stops at a conflict.
static void conflictingBranches(Scenario& s, const fs::path& repo)
{
    s.commitFile(repo, "c.txt", "a\nb\nc\n", "Base c");
    s.git(repo, {"switch", "-q", "-c", "feat"});
    s.commitFile(repo, "c.txt", "a\nfeat\nc\n", "Feat c");
    s.git(repo, {"switch", "-q", "main"});
    s.commitFile(repo, "c.txt", "a\nmain\nc\n", "Main c");
    s.git(repo, {"switch", "-q", "feat"});
}

GG_TEST("reconcile", "a plain git rebase --apply stopped at a conflict and continued is one operation; Undo restores the branch")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    conflictingBranches(s, repo);
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    ctx->Yield(10);
    const size_t opsBefore = gitOps(repo).size();
    const std::string before = refState(s, repo);
    GG_CHECK(!s.gitMayFail(repo, {"rebase", "--apply", "main"}).ok());
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-apply" / "rebasing"));
    GG_CHECK(s.waitUntil([&] { return journalOps(repo).size() > opsBefore; }));
    s.settle();
    ctx->Yield(10);
    GG_CHECK(!journalOps(repo).back().ended); // open while the rebase is stopped
    std::ofstream(repo / "c.txt", std::ios::trunc) << "a\nresolved\nc\n";
    s.git(repo, {"add", "c.txt"});
    s.git(repo, {"rebase", "--continue"});
    GG_REQUIRE(!fs::exists(repo / ".git" / "rebase-apply"));
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty() && journalOps(repo).back().ended; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == opsBefore + 1);
    GG_CHECK_STR_EQ(ops.back().label, "git rebase");
    GG_CHECK(refChange(ops.back(), "refs/heads/feat") != nullptr);
    GG_CHECK(refChange(ops.back(), "HEAD") == nullptr); // back on the branch it started on
    GG_CHECK(refState(s, repo) != before);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == before; }));
}

GG_TEST("reconcile", "a plain git rebase --apply stopped and aborted ends its group; there is nothing to undo from it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    conflictingBranches(s, repo);
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    ctx->Yield(10);
    const std::string start = refState(s, repo);
    const size_t opsBefore = journalOps(repo).size();
    GG_CHECK(!s.gitMayFail(repo, {"rebase", "--apply", "main"}).ok());
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-apply" / "rebasing"));
    GG_CHECK(s.waitUntil([&] { return journalOps(repo).size() == opsBefore + 1 && !journalOps(repo).back().ended; }));
    s.git(repo, {"rebase", "--abort"});
    GG_CHECK(s.waitUntil([&] { return !journalOps(repo).empty() && journalOps(repo).back().ended; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = journalOps(repo);
    GG_REQUIRE(ops.size() == opsBefore + 1);
    GG_CHECK(ops.back().ok);
    GG_CHECK(!ops.back().restorable()); // HEAD and feat are where they were
    GG_CHECK(!fs::exists(repo / ".git" / "gg" / "rebase"));
    GG_CHECK_STR_EQ(refState(s, repo), start);
}

GG_TEST("reconcile", "Undo refuses while git am is stopped at a conflict")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.commitFile(repo, "c.txt", "a\nb\nc\n", "Base c");
    s.commitFile(repo, "c.txt", "a\npatched\nc\n", "Patched c");
    const std::string patch = s.gitOut(repo, {"format-patch", "-1", "--stdout"});
    s.git(repo, {"reset", "-q", "--hard", "HEAD~1"});
    s.commitFile(repo, "c.txt", "a\nother\nc\n", "Other c");
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    GG_CHECK(!s.gitMayFail(repo, {"am"}, patch).ok());
    GG_REQUIRE(fs::exists(repo / ".git" / "rebase-apply" / "applying"));
    expectUndoRefused(s, ctx, repo, "finish or abort git am first");
    GG_CHECK(fs::is_directory(repo / ".git" / "rebase-apply"));
    s.git(repo, {"am", "--abort"});
}

GG_TEST("reconcile", "git branch -f on another branch is its own op")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"branch", "x", "main~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    GG_CHECK_STR_EQ(gitOps(repo).back().label, "git branch x");
    const std::string created = refState(s, repo);
    const std::string head = s.head(repo);
    s.git(repo, {"branch", "-f", "x", "main"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 2);
    GG_CHECK_STR_EQ(ops.back().label, "git branch -f x");
    GG_CHECK(refChange(ops.back(), "refs/heads/x") != nullptr);
    GG_CHECK(refChange(ops.back(), "HEAD") == nullptr);
    GG_CHECK(refChange(ops.back(), "refs/heads/main") == nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == created; }));
    GG_CHECK_STR_EQ(s.head(repo), head);
}

GG_TEST("reconcile", "git branch x then git branch -D y: creation is its own op, deletion goes to the lump")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"branch", "y", "main~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"branch", "x"});
    s.git(repo, {"branch", "-D", "y"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 3; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 3);
    GG_CHECK_STR_EQ(ops[1].label, "git branch x");
    GG_CHECK(refChange(ops[1], "refs/heads/x") != nullptr);
    GG_CHECK(refChange(ops[1], "refs/heads/y") == nullptr);
    GG_CHECK_STR_EQ(ops[2].label, "external changes");
    GG_CHECK(refChange(ops[2], "refs/heads/y") != nullptr);
    GG_CHECK(refChange(ops[2], "refs/heads/x") == nullptr);
    for (int i = 0; i < 2; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.settle();
    }
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "git branch -m a b is one op, undo renames back")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"branch", "a", "main~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"branch", "-m", "a", "b"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 2; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 2);
    GG_CHECK_STR_EQ(ops.back().label, "git branch -m");
    GG_CHECK(refChange(ops.back(), "refs/heads/a") != nullptr);
    GG_CHECK(refChange(ops.back(), "refs/heads/b") != nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK(!s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/b"}).ok());
    GG_CHECK(s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/a"}).ok());
}

GG_TEST("reconcile", "git stash with ggui open is one git stash op; Undo restores refs/stash")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    s.write(repo, "f1.txt", "changed for the stash\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git stash");
    GG_CHECK(refChange(ops.front(), "refs/stash") != nullptr);
    GG_CHECK(refChange(ops.front(), "HEAD") == nullptr);
    GG_CHECK(refChange(ops.front(), "refs/heads/main") == nullptr);
    const std::string head = s.head(repo);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.head(repo), head);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"stash", "list"}), "");
}

GG_TEST("reconcile", "stash drop is journaled as one op from the snapshot")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.write(repo, "f1.txt", "first stash\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    s.write(repo, "f1.txt", "second stash\n");
    s.git(repo, {"stash", "push", "-q"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 2; }));
    s.settle();
    const std::string stashed = refState(s, repo);
    s.git(repo, {"stash", "drop", "-q"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 3; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 3);
    GG_CHECK_STR_EQ(ops.back().label, "external changes"); // the log was rewritten: no chain
    GG_CHECK(refChange(ops.back(), "refs/stash") != nullptr);
    size_t changed = 0;
    for (const auto& r : ops.back().refs)
        changed += r.oldValue != r.newValue ? 1 : 0;
    GG_CHECK_EQ(changed, 1u);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == stashed; }));
}

GG_TEST("reconcile", "a tag created and a branch reset in one pass while closed: two ops (branch -f, lump with the tag)")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    s.git(repo, {"branch", "x", "main~1"});
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() == 1; }));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"tag", "t"});
    s.git(repo, {"branch", "-f", "x", "main"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return gitOps(repo).size() >= 3; }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 3);
    GG_CHECK_STR_EQ(ops[1].label, "git branch -f x");
    GG_CHECK(refChange(ops[1], "refs/heads/x") != nullptr);
    GG_CHECK(refChange(ops[1], "refs/tags/t") == nullptr);
    GG_CHECK_STR_EQ(ops[2].label, "external changes");
    GG_CHECK(refChange(ops[2], "refs/tags/t") != nullptr);
    for (int i = 0; i < 2; ++i) {
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
        s.settle();
    }
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
}

GG_TEST("reconcile", "fetch of thousands of refs is one op and reconcile stays fast")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    const fs::path origin = s.root() / (repo.filename().string() + "-origin.git");
    const std::string tip = s.gitOut(origin, {"rev-parse", "main"});
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok()); // the baseline
    const int count = 3000;
    std::string input;
    for (int i = 0; i < count; ++i)
        input += "create refs/heads/many/b" + std::to_string(i) + " " + tip + "\n";
    GG_REQUIRE(s.git(origin, {"update-ref", "--stdin"}, input).ok());
    s.git(repo, {"fetch", "-q", "origin"});
    const auto start = std::chrono::steady_clock::now();
    GG_REQUIRE(s.gitgg(repo, {"op", "log"}).ok()); // reconciles (process start-up included)
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    ctx->LogInfo("reconcile of a fetch of %d refs: %lld ms", count, static_cast<long long>(ms));
    GG_CHECK(ms < timeBudgetMs(2000));
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.back().label, "git fetch");
    GG_CHECK(ops.back().refs.size() >= static_cast<size_t>(count));
}

GG_TEST("reconcile", "renaming the checked-out branch with ggui closed is one op, undo puts HEAD back on the old name")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);
    s.git(repo, {"branch", "-m", "main", "renamed"});
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git branch -m");
    const auto* head = refChange(ops.front(), "HEAD");
    GG_REQUIRE(head != nullptr);
    GG_CHECK_STR_EQ(head->oldValue, "ref:refs/heads/main");
    GG_CHECK_STR_EQ(head->newValue, "ref:refs/heads/renamed");
    GG_CHECK(refChange(ops.front(), "refs/heads/main") != nullptr);
    GG_CHECK(refChange(ops.front(), "refs/heads/renamed") != nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
}

GG_TEST("reconcile", "renaming the checked-out branch with ggui open is one op")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    s.git(repo, {"branch", "-m", "main", "renamed"});
    GG_CHECK(s.waitUntil([&] {
        const auto ops = gitOps(repo);
        return !ops.empty() && refChange(ops.back(), "refs/heads/renamed") != nullptr;
    }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git branch -m");
    GG_CHECK(refChange(ops.front(), "HEAD") != nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
}

GG_TEST("reconcile", "git branch -c copies the reflog: one op creating the copy, undo deletes only it")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const std::string start = refState(s, repo);
    s.git(repo, {"branch", "-c", "main", "copy"});
    GG_CHECK(s.waitUntil([&] { return !gitOps(repo).empty(); }));
    s.settle();
    ctx->Yield(10);
    const auto ops = gitOps(repo);
    GG_REQUIRE(ops.size() == 1);
    GG_CHECK_STR_EQ(ops.front().label, "git branch -c");
    size_t changed = 0;
    for (const auto& r : ops.front().refs)
        changed += r.oldValue != r.newValue ? 1 : 0;
    GG_CHECK_EQ(changed, 1u);
    GG_CHECK(refChange(ops.front(), "refs/heads/copy") != nullptr);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return refState(s, repo) == start; }));
    GG_CHECK(s.gitMayFail(repo, {"rev-parse", "-q", "--verify", "refs/heads/main"}).ok());
}

} // namespace ggtest
