// UI-thread registration and the "not on the UI thread" assertion (product spec §3.1).
//
// ggui registers its UI thread at startup. Every libgit2 helper and the git runner call
// assertNotUiThread(), so a UI-thread call to libgit2 or git is caught in debug and test builds.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace gg {

enum class UiThreadCheck {
    Off,    // no checking (release builds, git-gg)
    Abort,  // log and abort the process (default in debug/test builds)
    Record, // record the violation so a test can observe it
};

// Marks the calling thread as the UI thread.
void registerUiThread();
bool isUiThread();

// Test-only switch (§8.1). Returns the previous mode.
UiThreadCheck setUiThreadCheck(UiThreadCheck mode);
UiThreadCheck uiThreadCheck();

// Violations recorded in Record mode, oldest first.
std::vector<std::string> uiThreadViolations();
void clearUiThreadViolations();

// True when the thread checks are compiled in (GGUI_THREAD_CHECKS).
bool threadChecksCompiled();

// Called by libgit2 helpers and the git runner. `what` names the call site.
void assertNotUiThread(const char* what);

// Wakes a sleeping UI loop. The UI registers the waker at startup. A worker thread calls wakeUi()
// after it hands a result to the UI thread, outside any lock. It does nothing when no waker is set.
// setUiWaker() waits until the calls that run now end. The waker must not call these functions.
void setUiWaker(std::function<void()> waker);
void wakeUi();

} // namespace gg
