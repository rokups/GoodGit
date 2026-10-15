// Frame-time probe (test hook, product spec §8.1): measures per frame the app's own work
// (App::frame) and, separately, presenting (render and swap), excluding time the test coroutine
// spends in test steps. The budget is on the app's work; presenting is measured on its own
// because on a software renderer it is the machine's time, not repository work.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

namespace ggui {

struct FrameProbe {
    double maxAppMs = 0.0;
    double maxPresentMs = 0.0;
    double maxTotalMs = 0.0;
    long long frames = 0;
    long long slowFrames = 0;   // app part > 33 ms
    double worstAtMs = 0.0;     // wall time of the worst app frame, from reset()
    long long worstFrame = 0;   // number of the worst app frame (1 is the first after reset())
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    void reset()
    {
        maxAppMs = maxPresentMs = maxTotalMs = 0.0;
        frames = slowFrames = 0;
        worstAtMs = 0.0;
        worstFrame = 0;
        start = std::chrono::steady_clock::now();
    }
    void record(double appMs, double presentMs)
    {
        ++frames;
        if (appMs > maxAppMs) {
            worstFrame = frames;
            worstAtMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        maxAppMs = std::max(maxAppMs, appMs);
        maxPresentMs = std::max(maxPresentMs, presentMs);
        maxTotalMs = std::max(maxTotalMs, appMs + presentMs);
        if (appMs > 33.0)
            ++slowFrames;
    }
};

inline FrameProbe& frameProbe()
{
    static FrameProbe probe;
    return probe;
}

} // namespace ggui
