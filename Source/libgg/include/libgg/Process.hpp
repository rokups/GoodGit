// Parent-process information used to label and group plain git operations in the journal.
#pragma once

#include <cstdint>
#include <string>

namespace gg {

struct ProcessInfo {
    long long pid = 0;
    std::uint64_t start = 0;    // process start time (platform units), 0 when unknown
    std::string commandLine;    // "git rebase -i main" when known
};

// The parent of the current process (for a hook: the git command that runs it).
ProcessInfo parentProcess();
bool processAlive(long long pid, std::uint64_t start);

} // namespace gg
