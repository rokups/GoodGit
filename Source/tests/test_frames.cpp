// Frame requests (GG-73): a widget that animates asks for frames, so an idle loop can sleep.
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "platform/Platform.hpp"
#include "tests/Harness.hpp"
#include "util/FrameScheduler.hpp"
#include "util/Frames.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Thread.hpp>

#include <chrono>
#include <thread>

namespace ggtest {

GG_TEST("frames", "requests are kept and taken once")
{
    ggui::takeFrameRequests();
    ggui::requestFrames(3);
    ggui::requestFrames(1);
    GG_CHECK(ggui::takeFrameRequests() == 3);
    GG_CHECK(ggui::takeFrameRequests() == 0);
    ggui::requestAnimation();
    GG_CHECK(ggui::takeFrameRequests() == 1);
}

GG_TEST("frames", "a frame that draws a spinner requests a frame")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    gg::setSlowGitLatency(std::chrono::milliseconds(5000));
    ctx->ItemClick("//Welcome/##welcome_path");
    ctx->KeyCharsReplace(repo.string().c_str());
    ctx->ItemClick("//Welcome/Open");
    // The Welcome screen draws a spinner while the repository opens.
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Welcome/Cancel##open"); }, 15.0f));
    ggui::takeFrameRequests();
    ctx->Yield(2);
    GG_CHECK(ggui::takeFrameRequests() > 0);
    ctx->ItemClick("//Welcome/Cancel##open");
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    GG_CHECK(s.waitIdle());
    // Without a spinner, toast or scroll, the next frames request nothing.
    ctx->Yield(10);
    ggui::takeFrameRequests();
    ctx->Yield(2);
    GG_CHECK(ggui::takeFrameRequests() == 0);
}

GG_TEST("frames", "scheduler waits when idle")
{
    using Clock = std::chrono::steady_clock;
    using ggui::FrameScheduler;
    const Clock::time_point t0 = Clock::now();
    const auto at = [&](int ms) { return t0 + std::chrono::milliseconds(ms); };
    const auto input = [&](int ms, bool event, bool frames, bool text, bool focused, bool minimized,
                           bool woken = false, bool mouseDown = false) {
        FrameScheduler::Input in;
        in.eventSeen = event;
        in.woken = woken;
        in.anyMouseDown = mouseDown;
        in.framesRequested = frames;
        in.wantTextInput = text;
        in.focused = focused;
        in.minimized = minimized;
        in.now = at(ms);
        return in;
    };

    {
        FrameScheduler sched;
        // An event opens the active window of 500 ms: no wait, also with nothing else to show.
        GG_CHECK(sched.waitMs(input(0, true, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(499, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(500, false, false, false, true, false)) == 1000);
        // A new event opens the window again.
        GG_CHECK(sched.waitMs(input(600, true, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1099, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1100, false, false, false, true, false)) == 1000);
    }
    {
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, false, true, false)) == 1000);
        // An animation in the focused window draws at once.
        GG_CHECK(sched.waitMs(input(0, false, true, false, true, false)) == 0);
        // An animation in an unfocused window runs at a slow rate.
        GG_CHECK(sched.waitMs(input(0, false, true, false, false, false)) == 100);
        // A text cursor blinks only in a focused window.
        GG_CHECK(sched.waitMs(input(0, false, false, true, true, false)) == 100);
        GG_CHECK(sched.waitMs(input(0, false, false, true, false, false)) == 1000);
        // A minimized or hidden window ignores animation requests and the active window.
        GG_CHECK(sched.waitMs(input(0, false, true, true, true, true)) == 1000);
        GG_CHECK(sched.waitMs(input(0, true, true, false, true, true)) == 1000);
    }
    {
        // A wake from a worker opens a shorter window than input does.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, false, true, false, true)) == 0);
        GG_CHECK(sched.waitMs(input(199, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(200, false, false, false, true, false)) == 1000);
        // A wake does not shorten the window that input opened.
        GG_CHECK(sched.waitMs(input(1000, true, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1100, false, false, false, true, false, true)) == 0);
        GG_CHECK(sched.waitMs(input(1450, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1500, false, false, false, true, false)) == 1000);
    }
    {
        // A held mouse button needs frames in a focused window only.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, false, true, false, false, true)) == 0);
        GG_CHECK(sched.waitMs(input(0, false, false, false, false, false, false, true)) == 1000);
        GG_CHECK(sched.waitMs(input(0, false, false, false, true, true, false, true)) == 1000);
    }
}

GG_TEST("frames", "scheduler handles focus, minimize and text input")
{
    using Clock = std::chrono::steady_clock;
    using ggui::FrameScheduler;
    const Clock::time_point t0 = Clock::now();
    const auto at = [&](int ms) { return t0 + std::chrono::milliseconds(ms); };
    const auto input = [&](int ms, bool event, bool frames, bool text, bool focused, bool minimized,
                           bool mouseDown = false) {
        FrameScheduler::Input in;
        in.eventSeen = event;
        in.anyMouseDown = mouseDown;
        in.framesRequested = frames;
        in.wantTextInput = text;
        in.focused = focused;
        in.minimized = minimized;
        in.now = at(ms);
        return in;
    };

    {
        // A spinner in a window without focus runs at the slow rate, not at once.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, true, false, false, false)) == FrameScheduler::kSlowWaitMs);
    }
    {
        // A minimized window sleeps, also with an animation request, a mouse button and an event.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, true, true, false, true, true, true)) == FrameScheduler::kIdleWaitMs);
    }
    {
        // A restore: the first input opens the active window. After the window, the loop sleeps.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, false, false, true)) == FrameScheduler::kIdleWaitMs);
        GG_CHECK(sched.waitMs(input(1000, true, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1100, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1499, false, false, false, true, false)) == 0);
        GG_CHECK(sched.waitMs(input(1500, false, false, false, true, false)) == FrameScheduler::kIdleWaitMs);
    }
    {
        // A text cursor blinks at the slow rate in a focused window. An unfocused window sleeps.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, true, true, false)) == FrameScheduler::kSlowWaitMs);
        GG_CHECK(sched.waitMs(input(0, false, false, true, false, false)) == FrameScheduler::kIdleWaitMs);
    }
    {
        // A mouse button in a window without focus gives no frames.
        FrameScheduler sched;
        GG_CHECK(sched.waitMs(input(0, false, false, false, false, false, true)) == FrameScheduler::kIdleWaitMs);
    }
}

GG_TEST("frames", "worker wake reaches the loop")
{
    ggui::Platform& platform = s.app.platform();
    // Other producers can set the flag at any time. The counter shows that the loop handled a wake.
    const std::uint64_t before = platform.wakesHandled();
    std::thread worker([] { gg::wakeUi(); });
    worker.join();
    ctx->Yield(2);
    GG_CHECK(platform.wakesHandled() > before);
}

} // namespace ggtest
