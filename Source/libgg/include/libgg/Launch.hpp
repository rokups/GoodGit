// Starting a ggui window as its own process (shared by ggui's "Open in new window" and
// `git gg sequence-editor`, which starts ggui when none has the repository open).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gg {

// The running program's own executable ("" when unknown).
std::filesystem::path selfExecutable();
// GG_GGUI, else the ggui next to this program (ggui itself, or the one beside git-gg), else ggui
// on PATH ("" when there is none).
std::filesystem::path gguiProgram();

struct DetachedProcess {
#ifdef _WIN32
    void* process = nullptr; // HANDLE
#else
    long long pid = -1;
#endif
};

// Starts `args` (args[0] is the program's path) detached: its own session / process group,
// stdin/stdout/stderr on the null device, without the GIT_DIR-style variables git sets for its
// hooks and editors. It keeps running after this process ends.
bool spawnDetached(const std::vector<std::string>& args, DetachedProcess& child, std::string& error);
// Whether the child is still running (reaps it when it has exited).
bool stillRunning(DetachedProcess& child);
// Stops tracking the child; on POSIX a background thread reaps it when it exits.
void release(DetachedProcess& child);

} // namespace gg
