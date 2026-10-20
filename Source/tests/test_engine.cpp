// Engine contract, watcher and responsiveness (§3.1).
#include "core/Worker.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/FrameProbe.hpp"

#include <libgg/GitRunner.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ggtest {

GG_TEST("engine", "overlapping diff requests: only the newest result is shown")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    gg::setSlowGitLatency(std::chrono::milliseconds(400));
    // Three quick selections: a.txt, c.txt, e.txt. Only e.txt may ever be displayed.
    ctx->KeyPress(ImGuiKey_F6);
    ctx->KeyPress(ImGuiKey_F6);
    ctx->KeyPress(ImGuiKey_F6);
    bool sawStale = false;
    const bool done = s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        if (d && !d->files.empty() && d->files[0].path() != "e.txt")
            sawStale = true;
        return d && !d->files.empty() && d->files[0].path() == "e.txt";
    }, 10.0f);
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(done);
    GG_CHECK(!sawStale);
}

GG_TEST("engine", "watcher: plain git steps update the UI")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    const size_t rows = history.rows().size();
    // A commit made with plain git appears without pressing Refresh.
    s.commitFile(repo, "watched.txt", "hello\n", "Made outside ggui");
    GG_CHECK(s.waitUntil([&] { return history.rows().size() == rows + 1; }));
    GG_CHECK_STR_EQ(s.session()->snapshot()->head.hex(), s.head(repo));
    // A new branch (refs/) and an edited file (worktree).
    s.git(repo, {"branch", "made-outside"});
    GG_CHECK(s.waitUntil([&] { return s.session()->snapshot()->findBranch("made-outside") != nullptr; }));
    s.write(repo, "f1.txt", "edited outside\n");
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->unstaged.size() == 1; }));
    // Staging with plain git moves it to Staged (index change).
    s.git(repo, {"add", "f1.txt"});
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.size() == 1; }));
}

GG_TEST("engine", "partial status on a huge worktree is marked scanning")
{
    const fs::path big = s.largeFixture();
    const fs::path repo = s.path("big-clone");
    s.git(s.root(), {"clone", "-q", "--shared", "--no-checkout", big.string(), repo.string()});
    s.git(repo, {"checkout", "-q", "main"});
    // Modify files all over the tree so changes keep arriving while the scan runs.
    for (int i = 0; i < 50000; i += 25) {
        char name[64];
        std::snprintf(name, sizeof(name), "dir%03d/file%05d.txt", i % 500, i);
        s.write(repo, name, "modified\n");
    }
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->partial
                                    && s.session()->status()->unstaged.size() >= 1900; }, 120.0f));
    GG_CHECK(s.session()->changes().everScanned());
}

GG_TEST("engine", "responsiveness on the large repository")
{
    const fs::path repo = s.largeFixture();
    gg::setSlowGitLatency(std::chrono::milliseconds(50)); // slow-git mode
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 120.0f));
    auto& probe = ggui::frameProbe();
    ImGuiWindow* table = ctx->GetWindowByRef(s.child("//History", "##hist_table").c_str());
    GG_REQUIRE(table != nullptr);
    // Warm-up outside the measured window: the first draw of each UI path costs more than later
    // draws (glyphs, caches, first reads of a cold worktree). Do each path once.
    for (int i = 0; i <= 20; ++i) {
        ctx->ScrollToY(table->ID, table->ScrollMax.y * static_cast<float>(i) / 20.0f);
        ctx->Yield();
    }
    ctx->ScrollToTop(table->ID);
    ctx->ItemClick("//History/**/###row_wt");
    ctx->KeyPress(ImGuiKey_DownArrow, 1);
    s.waitUntil([&] { return !s.session()->changes().rows().empty(); }, 10.0f);
    ctx->KeyPress(ImGuiKey_F6);
    s.waitIdle(30.0f);
    ctx->ItemInputValue("//History/##hist_filter", "Merge side branch 1");
    s.waitIdle(30.0f);
    ctx->ItemInputValue("//History/##hist_filter", "");
    s.waitIdle(30.0f);
    ctx->ItemClick("//History/**/###row_wt");
    ctx->ScrollToTop(table->ID);
    s.waitIdle(30.0f);
    probe.reset();
    // Scroll, select commits, look at diffs and blame, filter.
    for (int i = 0; i < 20; ++i) {
        ctx->ScrollToY(table->ID, table->ScrollMax.y * static_cast<float>(i) / 20.0f);
        ctx->Yield();
    }
    ctx->ScrollToTop(table->ID);
    for (int i = 0; i < 10; ++i) {
        ctx->ItemClick("//History/**/###row_wt");
        ctx->KeyPress(ImGuiKey_DownArrow, i + 1);
        s.waitUntil([&] { return !s.session()->changes().rows().empty(); }, 10.0f);
        ctx->KeyPress(ImGuiKey_F6);
        s.waitIdle(30.0f);
    }
    ctx->ItemInputValue("//History/##hist_filter", "Merge side branch 1");
    s.waitIdle(30.0f);
    ctx->ItemInputValue("//History/##hist_filter", "");
    s.waitIdle(30.0f);
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    ctx->LogInfo("frames %lld, app max %.1f ms (frame %lld at %.0f ms), present max %.1f ms, total max %.1f ms, "
                 "app > 33 ms: %lld",
        probe.frames, probe.maxAppMs, probe.worstFrame, probe.worstAtMs, probe.maxPresentMs, probe.maxTotalMs,
        probe.slowFrames);
    GG_CHECK(probe.frames > 50);
    const bool fast = probe.maxAppMs < timeBudgetMs(33.0);
    if (!fast)
        ctx->LogError("slowest app frame: %.1f ms, frame %lld of %lld, %.0f ms after the start of the measured window",
            probe.maxAppMs, probe.worstFrame, probe.frames, probe.worstAtMs);
    GG_CHECK(fast);
}

namespace {

bool branchExists(Scenario& s, const fs::path& repo, const std::string& name)
{
    return s.gitMayFail(repo, {"show-ref", "--verify", "-q", "refs/heads/" + name}).ok();
}

// Runs a custom mutation through the app and waits for its event.
std::optional<ggui::core::MutationFinishedEvent> runMutation(
    Scenario& s, std::function<void(ggui::core::MutationContext&)> fn)
{
    std::optional<ggui::core::MutationFinishedEvent> event;
    s.session()->actions().run("rollback test", std::move(fn),
        [&](const ggui::core::MutationFinishedEvent& e) { event = e; });
    if (!s.waitUntil([&] { return event.has_value(); }))
        return std::nullopt;
    return event;
}

} // namespace

GG_TEST("engine", "a mutation that fails runs its rollback steps in reverse order")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    std::vector<std::string> order;
    const auto event = runMutation(s, [&](ggui::core::MutationContext& mc) {
        for (const std::string name : {"rollback-a", "rollback-b"}) {
            mc.git({"branch", name});
            mc.rollback.push_back([name, &order](ggui::core::MutationContext& c) {
                order.push_back(name);
                c.gitMayFail({"branch", "-D", name});
                return std::string();
            });
        }
        throw ggui::core::MutationError{ggui::core::Outcome::Refused, "stop here", {}};
    });
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Refused);
    GG_CHECK_STR_EQ(event->message, "stop here");
    GG_CHECK(!branchExists(s, repo, "rollback-a"));
    GG_CHECK(!branchExists(s, repo, "rollback-b"));
    GG_REQUIRE(order.size() == 2);
    GG_CHECK_STR_EQ(order[0], "rollback-b");
    GG_CHECK_STR_EQ(order[1], "rollback-a");
    GG_CHECK(event->message.find("The rollback stopped") == std::string::npos);
}

GG_TEST("engine", "a mutation that is cancelled runs its rollback steps: the cancel does not stop their git commands")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const auto event = runMutation(s, [&](ggui::core::MutationContext& mc) {
        mc.git({"branch", "rollback-a"});
        mc.rollback.push_back([](ggui::core::MutationContext& c) {
            const auto res = c.gitMayFail({"branch", "-D", "rollback-a"});
            return res.ok() ? std::string() : res.message();
        });
        mc.token().cancel();
        mc.git({"status"});
    });
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Cancelled);
    GG_CHECK(event->message.find("The rollback stopped") == std::string::npos);
    GG_CHECK(!branchExists(s, repo, "rollback-a"));
}

GG_TEST("engine", "a rollback step that fails stops the rollback and its text is in the message")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const auto event = runMutation(s, [&](ggui::core::MutationContext& mc) {
        mc.git({"branch", "rollback-first"});
        mc.rollback.push_back([](ggui::core::MutationContext& c) {
            c.gitMayFail({"branch", "-D", "rollback-first"});
            return std::string();
        });
        mc.rollback.push_back([](ggui::core::MutationContext&) { return std::string("your changes are in stash@{0}"); });
        throw ggui::core::MutationError{ggui::core::Outcome::Failed, "it failed", {}};
    });
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Failed);
    GG_CHECK_STR_EQ(event->message, "it failed\nThe rollback stopped: your changes are in stash@{0}");
    GG_CHECK(branchExists(s, repo, "rollback-first"));
}

GG_TEST("engine", "a rollback step that throws stops the rollback and the error of its command is in the message")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    const auto event = runMutation(s, [&](ggui::core::MutationContext& mc) {
        mc.rollback.push_back([](ggui::core::MutationContext& c) {
            c.git({"branch", "-D", "no-such-branch"});
            return std::string();
        });
        throw ggui::core::MutationError{ggui::core::Outcome::Refused, "stop here", {}};
    });
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Refused);
    GG_CHECK(event->message.starts_with("stop here\nThe rollback stopped: "));
    GG_CHECK(event->message.find("no-such-branch") != std::string::npos);
}

GG_TEST("engine", "a mutation that succeeds runs no rollback step")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.settle();
    int runs = 0;
    const auto event = runMutation(s, [&](ggui::core::MutationContext& mc) {
        mc.git({"branch", "rollback-kept"});
        mc.rollback.push_back([&runs](ggui::core::MutationContext&) {
            ++runs;
            return std::string();
        });
    });
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Ok);
    GG_CHECK_EQ(runs, 0);
    GG_CHECK(branchExists(s, repo, "rollback-kept"));
}

GG_TEST("engine", "a long content job does not delay a diff")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    ggui::core::Engine engine(ggui::core::Engine::Options{repo, false});
    // The blame job sleeps on the Content queue for 5 s unless the test cancels it.
    gg::setSlowGitLatency(std::chrono::milliseconds(5000));
    ggui::core::BlameQuery blame;
    blame.path = "f1.txt";
    const auto blameId = engine.blame(blame);
    const bool started = s.waitUntil([&] { return engine.running(ggui::core::Queue::Content); }, 10.0f);
    GG_REQUIRE(started);
    // The job reads the latency a short time after it starts. Wait until it is in its sleep.
    // Then a new job does not sleep.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    ggui::core::DiffQuery query;
    query.kind = ggui::core::DiffKind::Unstaged;
    const auto diffId = engine.diff(query, 1);
    bool gotDiff = false;
    const bool done = s.waitUntil([&] {
        for (auto& event : engine.poll(64))
            if (const auto* d = std::get_if<ggui::core::DiffEvent>(&event); d && d->request == diffId)
                gotDiff = true;
        return gotDiff;
    }, 4.0f);
    // The blame job is still in its sleep: the diff did not wait for it.
    bool blamePending = false;
    for (const auto& a : engine.activities())
        blamePending = blamePending || a.id == blameId;
    engine.cancelAll();
    GG_CHECK(done);
    GG_CHECK(blamePending);
}

namespace {

// A fake slow status scan on a Worker: it runs until `release` is set or its token is cancelled.
struct SlotProbe {
    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    std::atomic<bool> sawCancel{false};
    std::atomic<int> followUpRuns{0};
    std::atomic<int> droppedCount{0};
};

ggui::core::Worker::Task slowScan(SlotProbe& probe, std::uint64_t id, bool cancelRunning)
{
    ggui::core::Worker::Task task;
    task.id = id;
    task.slot = 2;
    task.cancelRunning = cancelRunning;
    task.fn = [&probe](ggui::core::Worker&, const gg::CancelToken& token) {
        probe.started = true;
        while (!probe.release && !token.cancelled())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        probe.sawCancel = token.cancelled();
    };
    task.token = gg::CancelToken();
    return task;
}

ggui::core::Worker::Task followUp(SlotProbe& probe, std::uint64_t id, bool cancelRunning)
{
    ggui::core::Worker::Task task;
    task.id = id;
    task.slot = 2;
    task.cancelRunning = cancelRunning;
    task.fn = [&probe](ggui::core::Worker&, const gg::CancelToken&) { ++probe.followUpRuns; };
    return task;
}

} // namespace

GG_TEST("engine", "a status request from the watcher does not cancel the running scan")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    SlotProbe probe;
    std::atomic<std::uint64_t> lastDropped{0};
    ggui::core::Worker worker("test", repo, [&](std::uint64_t id) {
        lastDropped = id;
        ++probe.droppedCount;
    });
    worker.post(slowScan(probe, 1, true));
    GG_REQUIRE(s.waitUntil([&] { return probe.started.load(); }, 10.0f));
    // Two watcher requests during the scan: the first is replaced, one follow-up stays.
    worker.post(followUp(probe, 2, false));
    worker.post(followUp(probe, 3, false));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    GG_CHECK(worker.running());
    GG_CHECK(!probe.sawCancel.load());
    GG_CHECK_EQ(probe.followUpRuns.load(), 0);
    probe.release = true;
    GG_CHECK(s.waitUntil([&] { return !worker.busy(); }, 10.0f));
    GG_CHECK(!probe.sawCancel.load());
    GG_CHECK_EQ(probe.followUpRuns.load(), 1);
    GG_CHECK_EQ(probe.droppedCount.load(), 1);
    GG_CHECK_EQ(lastDropped.load(), std::uint64_t(2));
}

GG_TEST("engine", "a status request that is not from the watcher cancels the running scan")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    SlotProbe probe;
    ggui::core::Worker worker("test", repo, [&](std::uint64_t) { ++probe.droppedCount; });
    worker.post(slowScan(probe, 1, true));
    GG_REQUIRE(s.waitUntil([&] { return probe.started.load(); }, 10.0f));
    worker.post(followUp(probe, 2, true));
    // The scan ends by the cancel, without the release flag.
    GG_CHECK(s.waitUntil([&] { return !worker.busy(); }, 10.0f));
    GG_CHECK(probe.sawCancel.load());
    GG_CHECK_EQ(probe.followUpRuns.load(), 1);
}

namespace {

// Counts the status scans of an engine: one final StatusEvent for each scan request.
struct ScanCounter {
    ggui::core::Engine& engine;
    std::set<ggui::core::RequestId> scans;

    int count()
    {
        for (auto& event : engine.poll(64))
            if (const auto* st = std::get_if<ggui::core::StatusEvent>(&event))
                scans.insert(st->request);
        return static_cast<int>(scans.size());
    }
};

ggui::core::WatchEvent worktreeEvent()
{
    ggui::core::WatchEvent e;
    e.worktree = true;
    return e;
}

} // namespace

GG_TEST("engine", "window state: a focused window scans for every watcher event")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    ggui::core::Engine::Options options{repo, false};
    ggui::core::Engine engine(options);
    ScanCounter scans{engine};
    for (int i = 1; i <= 3; ++i) {
        engine.onWatchEvent(worktreeEvent());
        GG_CHECK(s.waitUntil([&] { return scans.count() == i; }, 10.0f));
    }
    engine.cancelAll();
}

GG_TEST("engine", "window state: an unfocused window scans once in each interval")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    ggui::core::Engine::Options options{repo, false};
    options.unfocusedStatusInterval = std::chrono::milliseconds(600);
    ggui::core::Engine engine(options);
    ScanCounter scans{engine};
    engine.setWindowState(ggui::core::WindowState::Unfocused);
    // A burst: the first event scans at once, the others wait for the end of the interval.
    for (int i = 0; i < 6; ++i)
        engine.onWatchEvent(worktreeEvent());
    GG_CHECK(s.waitUntil([&] { return scans.count() == 1; }, 10.0f));
    // poll() runs the held scan when the interval ends: one scan for the whole burst.
    GG_CHECK(s.waitUntil([&] { return scans.count() == 2; }, 10.0f));
    s.waitUntil([] { return false; }, 1.5f);
    GG_CHECK_EQ(scans.count(), 2);
    // A focus gain with nothing held does not scan.
    engine.setWindowState(ggui::core::WindowState::Focused);
    s.waitUntil([] { return false; }, 0.5f);
    GG_CHECK_EQ(scans.count(), 2);
    engine.cancelAll();
}

GG_TEST("engine", "window state: a minimized window holds the scans until it has focus")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    ggui::core::Engine::Options options{repo, false};
    options.unfocusedStatusInterval = std::chrono::milliseconds(200);
    ggui::core::Engine engine(options);
    ScanCounter scans{engine};
    engine.setWindowState(ggui::core::WindowState::Minimized);
    for (int i = 0; i < 5; ++i)
        engine.onWatchEvent(worktreeEvent());
    s.waitUntil([] { return false; }, 1.0f);
    GG_CHECK_EQ(scans.count(), 0);
    // Focus gain: exactly one scan.
    engine.setWindowState(ggui::core::WindowState::Focused);
    GG_CHECK(s.waitUntil([&] { return scans.count() == 1; }, 10.0f));
    s.waitUntil([] { return false; }, 0.5f);
    GG_CHECK_EQ(scans.count(), 1);
    // A restore without focus runs one scan too, and no interval is needed for it.
    engine.setWindowState(ggui::core::WindowState::Minimized);
    engine.onWatchEvent(worktreeEvent());
    engine.onWatchEvent(worktreeEvent());
    s.waitUntil([] { return false; }, 0.5f);
    GG_CHECK_EQ(scans.count(), 1);
    engine.setWindowState(ggui::core::WindowState::Unfocused);
    GG_CHECK(s.waitUntil([&] { return scans.count() == 2; }, 10.0f));
    s.waitUntil([] { return false; }, 0.8f);
    GG_CHECK_EQ(scans.count(), 2);
    engine.cancelAll();
}

} // namespace ggtest
