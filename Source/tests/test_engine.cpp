// Engine contract, watcher and responsiveness (§3.1).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/FrameProbe.hpp"

#include <libgg/GitRunner.hpp>

#include <functional>
#include <optional>
#include <string>
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
    probe.reset();
    // Scroll, select commits, look at diffs and blame, filter.
    ImGuiWindow* table = ctx->GetWindowByRef(s.child("//History", "##hist_table").c_str());
    GG_REQUIRE(table != nullptr);
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
    ctx->LogInfo("frames %lld, app max %.1f ms, present max %.1f ms, total max %.1f ms, app > 33 ms: %lld",
        probe.frames, probe.maxAppMs, probe.maxPresentMs, probe.maxTotalMs, probe.slowFrames);
    GG_CHECK(probe.frames > 50);
    GG_CHECK(probe.maxAppMs < timeBudgetMs(33.0));
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

} // namespace ggtest
