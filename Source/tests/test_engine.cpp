// Engine contract, watcher and responsiveness (§3.1).
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/FrameProbe.hpp"

#include <libgg/GitRunner.hpp>

namespace ggtest {

GG_TEST("engine", "overlapping diff requests: only the newest result is shown", "APP-CANCEL-LONG-OPS")
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

GG_TEST("engine", "watcher: plain git steps update the UI", "APP-WATCH-WORKTREE", "APP-WATCH-GITDIR")
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

GG_TEST("engine", "partial status on a huge worktree is marked scanning", "CHG-SCANNING")
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

GG_TEST("engine", "responsiveness on the large repository", "APP-RESPONSIVE")
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
    ctx->LogInfo("frames %lld, max %.1f ms, > 33 ms: %lld", probe.frames, probe.maxMs, probe.slowFrames);
    GG_CHECK(probe.frames > 50);
    GG_CHECK(probe.maxMs < timeBudgetMs(33.0));
}

} // namespace ggtest
