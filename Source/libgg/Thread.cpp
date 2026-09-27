#include "libgg/Thread.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace gg {
namespace {

std::atomic<std::thread::id> g_uiThread{};
#ifdef GGUI_THREAD_CHECKS
std::atomic<UiThreadCheck> g_mode{UiThreadCheck::Abort};
#else
std::atomic<UiThreadCheck> g_mode{UiThreadCheck::Off};
#endif
std::mutex g_violationsMutex;
std::vector<std::string> g_violations;

} // namespace

void registerUiThread() { g_uiThread = std::this_thread::get_id(); }

bool isUiThread() { return g_uiThread.load() == std::this_thread::get_id(); }

UiThreadCheck setUiThreadCheck(UiThreadCheck mode) { return g_mode.exchange(mode); }

UiThreadCheck uiThreadCheck() { return g_mode.load(); }

std::vector<std::string> uiThreadViolations()
{
    std::lock_guard lock(g_violationsMutex);
    return g_violations;
}

void clearUiThreadViolations()
{
    std::lock_guard lock(g_violationsMutex);
    g_violations.clear();
}

bool threadChecksCompiled()
{
#ifdef GGUI_THREAD_CHECKS
    return true;
#else
    return false;
#endif
}

void assertNotUiThread([[maybe_unused]] const char* what)
{
#ifdef GGUI_THREAD_CHECKS
    if (!isUiThread())
        return;
    switch (g_mode.load()) {
    case UiThreadCheck::Off:
        return;
    case UiThreadCheck::Record: {
        std::lock_guard lock(g_violationsMutex);
        g_violations.emplace_back(what);
        return;
    }
    case UiThreadCheck::Abort:
        std::fprintf(stderr, "ggui: repository work on the UI thread: %s\n", what);
        std::fflush(stderr);
        std::abort();
    }
#endif
}

} // namespace gg
