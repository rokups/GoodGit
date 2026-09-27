// Frame-time probe (test hook, REBUILD_PLAN §8.1): measures the app's own work per frame
// (App::frame plus rendering), excluding time the test coroutine spends in test steps.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ggui {

struct FrameProbe {
    double maxMs = 0.0;
    double lastMs = 0.0;
    long long frames = 0;
    long long slowFrames = 0;   // > 33 ms
    void reset()
    {
        maxMs = lastMs = 0.0;
        frames = slowFrames = 0;
    }
    void record(double ms)
    {
        lastMs = ms;
        maxMs = std::max(maxMs, ms);
        ++frames;
        if (ms > 33.0)
            ++slowFrames;
    }
};

inline FrameProbe& frameProbe()
{
    static FrameProbe probe;
    return probe;
}

} // namespace ggui
