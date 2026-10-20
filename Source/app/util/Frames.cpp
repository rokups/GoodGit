#include "util/Frames.hpp"

#include <algorithm>

namespace ggui {

namespace {
// Not thread-safe. Only the UI thread reads and writes it.
int g_pendingFrames = 0;
} // namespace

void requestFrames(int n) { g_pendingFrames = std::max(g_pendingFrames, n); }

void requestAnimation() { requestFrames(1); }

int takeFrameRequests()
{
    const int n = g_pendingFrames;
    g_pendingFrames = 0;
    return n;
}

} // namespace ggui
