// Frame-time probe (test hook, product spec §8.1): measures per frame the app's own work
// (App::frame) and, separately, presenting (render and swap), excluding time the test coroutine
// spends in test steps. The budget is on the app's work; presenting is measured on its own
// because on a software renderer it is the machine's time, not repository work.
// The budget uses the CPU time of the UI thread: the wall time also holds the time that the
// kernel gives the core to other threads when the machine is loaded.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <time.h>
#endif

namespace ggui {

// CPU time of the calling thread in ms. A negative value means the platform has no such clock.
inline double threadCpuMs()
{
#if defined(__linux__) || defined(__APPLE__)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return -1.0;
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
#else
    return -1.0;
#endif
}

// CPU time between two threadCpuMs() values. Without a CPU clock it is the wall time.
inline double cpuDeltaMs(double cpuStart, double cpuEnd, double wallMs)
{
    return cpuStart >= 0.0 && cpuEnd >= cpuStart ? cpuEnd - cpuStart : wallMs;
}

struct FrameProbe {
    double maxAppMs = 0.0;      // CPU time of the UI thread in the app part
    double maxWallMs = 0.0;     // wall time of the app part
    double maxPresentMs = 0.0;
    double maxTotalMs = 0.0;
    long long frames = 0;
    long long slowFrames = 0;   // app part > 33 ms
    double worstAtMs = 0.0;     // wall time of the worst app frame, from reset()
    long long worstFrame = 0;   // number of the worst app frame (1 is the first after reset())
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // App frame times since reset(). Only the UI thread calls record() and reset(). The test
    // coroutine reads the probe while the UI thread waits for it, so there is no parallel access.
    std::vector<double> appSamples;  // CPU time of the app part
    std::vector<double> wallSamples; // wall time of the app part
    void reset()
    {
        appSamples.clear();
        appSamples.reserve(100000); // record() does not allocate while a test measures
        wallSamples.clear();
        wallSamples.reserve(100000);
        maxWallMs = 0.0;
        maxAppMs = maxPresentMs = maxTotalMs = 0.0;
        frames = slowFrames = 0;
        worstAtMs = 0.0;
        worstFrame = 0;
        start = std::chrono::steady_clock::now();
    }
    // appWallMs and appMs (CPU time) are for the app part. They are equal without a CPU clock.
    void record(double appWallMs, double presentMs, double appMs)
    {
        ++frames;
        if (appSamples.size() < appSamples.capacity()) // only after reset(): no growth in the product
            appSamples.push_back(appMs);
        if (wallSamples.size() < wallSamples.capacity())
            wallSamples.push_back(appWallMs);
        maxWallMs = std::max(maxWallMs, appWallMs);
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
    // The p-th percentile (0 to 100) of the app frame CPU times since reset(). 0 when there is none.
    double percentileAppMs(double p) const { return percentileOf(appSamples, p); }
    // The same for the app frame wall times.
    double percentileWallMs(double p) const { return percentileOf(wallSamples, p); }
    static double percentileOf(const std::vector<double>& samples, double p)
    {
        if (samples.empty())
            return 0.0;
        std::vector<double> copy = samples;
        const double clamped = std::clamp(p, 0.0, 100.0);
        const size_t index = static_cast<size_t>(clamped / 100.0 * static_cast<double>(copy.size() - 1) + 0.5);
        std::nth_element(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(index), copy.end());
        return copy[index];
    }
};

inline FrameProbe& frameProbe()
{
    static FrameProbe probe;
    return probe;
}

} // namespace ggui
