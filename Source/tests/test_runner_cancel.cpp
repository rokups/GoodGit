// GitRunner cancel and kill (a running child and its children), and HooksLegacy::uninstall (GG-98).
#include "libgg/Git2.hpp"
#include "libgg/GitRunner.hpp"
#include "libgg/HooksLegacy.hpp"
#include "tests/Harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

namespace ggtest {

namespace fs = std::filesystem;

namespace {

#ifndef _WIN32
// Runs `sh -c script` on a thread and cancels it after `cancelAfterMs`.
// `elapsedMs` gets the time from the cancel to the return of run().
gg::RunResult runThenCancel(const std::string& script, int cancelAfterMs, double& elapsedMs)
{
    gg::RunRequest r;
    r.args = {"sh", "-c", script};
    r.cancel = gg::CancelToken(); // the default is a copy of the shared never-cancelled token
    gg::RunResult result;
    std::thread worker([&] { result = gg::run(r); });
    std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
    const auto t0 = std::chrono::steady_clock::now();
    r.cancel.cancel(); // the token is shared with the copy that run() polls
    worker.join();
    elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return result;
}

// Whether the process is gone (init reaps a zombie in a short time).
bool processGone(long pid)
{
    for (int i = 0; i < 100; ++i) {
        if (::kill(static_cast<pid_t>(pid), 0) != 0)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
#endif

// A hook wrapper as the old installer wrote it (the marker line is what uninstall looks for).
const char* kWrapper =
    "#!/bin/sh\n# ggui managed hook: runs the ggui hooks runner, then the previous hook (if any).\nexit 0\n";

} // namespace

#ifndef _WIN32

GG_TEST("runner", "cancel ends a running child and its child soon and reports cancelled")
{
    double ms = 0;
    // The shell prints the pid of its background child, then waits for it.
    const auto r = runThenCancel("sleep 60 & echo $!; wait", 300, ms);
    GG_CHECK(r.cancelled);
    GG_CHECK(!r.ok());
    GG_CHECK(ms < timeBudgetMs(1500));
    GG_CHECK_STR_EQ(r.message(), "Cancelled");
    const long pid = std::atol(r.out.c_str());
    GG_REQUIRE(pid > 1);
    GG_CHECK(processGone(pid));
}

GG_TEST("runner", "cancel kills a child that ignores SIGTERM after the grace time")
{
    double ms = 0;
    const auto r = runThenCancel("trap '' TERM; echo $$; while :; do sleep 1; done", 500, ms);
    GG_CHECK(r.cancelled);
    GG_CHECK_EQ(r.exitCode, 128 + SIGKILL);
    GG_CHECK(ms < timeBudgetMs(4000));
    const long pid = std::atol(r.out.c_str());
    GG_REQUIRE(pid > 1);
    GG_CHECK(processGone(pid));
}

GG_TEST("runner", "a token cancelled before the run still stops the child")
{
    gg::RunRequest r;
    r.args = {"sh", "-c", "sleep 60"};
    r.cancel = gg::CancelToken();
    r.cancel.cancel();
    const auto t0 = std::chrono::steady_clock::now();
    const auto result = gg::run(r);
    GG_CHECK(result.cancelled);
    GG_CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(static_cast<int>(timeBudgetMs(1500))));
}

#endif

GG_TEST("hooks", "uninstall leaves a hook that is not ours and succeeds")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string mine = "#!/bin/sh\n# the user's own hook\nexit 0\n";
    s.write(repo / ".git" / "hooks", "pre-push", mine);
    const auto r = gg::git2::openRepository(repo);
    GG_CHECK(!gg::hooks::installed(r.get()));
    std::string error;
    GG_CHECK(gg::hooks::uninstall(r.get(), error));
    GG_CHECK(error.empty());
    GG_CHECK_STR_EQ(s.read(repo / ".git", "hooks/pre-push"), mine);
}

GG_TEST("hooks", "uninstall removes our wrapper and restores the previous hook")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string theirs = "#!/bin/sh\n# the user's own hook\nexit 3\n";
    const fs::path hooks = repo / ".git" / "hooks";
    s.write(hooks, "pre-push", kWrapper);
    s.write(hooks, "pre-push.gg-previous", theirs);
    s.write(hooks, "post-commit", kWrapper); // a wrapper without a previous hook
    const auto r = gg::git2::openRepository(repo);
    GG_CHECK(gg::hooks::installed(r.get()));
    std::string error;
    GG_CHECK(gg::hooks::uninstall(r.get(), error));
    GG_CHECK(error.empty());
    GG_CHECK_STR_EQ(s.read(repo / ".git", "hooks/pre-push"), theirs);
    GG_CHECK(!fs::exists(hooks / "pre-push.gg-previous"));
    GG_CHECK(!fs::exists(hooks / "post-commit"));
    GG_CHECK(!gg::hooks::installed(r.get()));
}

GG_TEST("hooks", "uninstall with no hooks directory succeeds")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    fs::remove_all(repo / ".git" / "hooks");
    const auto r = gg::git2::openRepository(repo);
    GG_CHECK(!gg::hooks::installed(r.get()));
    std::string error;
    GG_CHECK(gg::hooks::uninstall(r.get(), error));
    GG_CHECK(error.empty());
}

GG_TEST("hooks", "uninstall removes the config entries and the runner, and keeps other config")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"config", "--local", "hook.ggui-pre-push.command", "true"});
    s.git(repo, {"config", "--local", "hook.ggui-pre-push.event", "pre-push"});
    s.git(repo, {"config", "--local", "hook.other.command", "true"});
    s.write(repo / ".git" / "gg" / "hooks", "run", "#!/bin/sh\nexit 0\n");
    const auto r = gg::git2::openRepository(repo);
    GG_CHECK(gg::hooks::installed(r.get()));
    std::string error;
    GG_CHECK(gg::hooks::uninstall(r.get(), error));
    GG_CHECK(error.empty());
    GG_CHECK(!fs::exists(repo / ".git" / "gg" / "hooks"));
    GG_CHECK(s.gitMayFail(repo, {"config", "--get", "hook.ggui-pre-push.command"}).out.empty());
    GG_CHECK(!s.gitMayFail(repo, {"config", "--get", "hook.other.command"}).out.empty());
}

} // namespace ggtest
