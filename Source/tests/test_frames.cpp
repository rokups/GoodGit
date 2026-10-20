// Frame requests (GG-73): a widget that animates asks for frames, so an idle loop can sleep.
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Frames.hpp"

#include <libgg/Git2.hpp>

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

} // namespace ggtest
