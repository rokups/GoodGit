// Process identity (pid and start time), used to tell a live journal writer from a dead one.
#pragma once

#include <cstdint>
#include <string>

namespace gg {

struct ProcessInfo {
    long long pid = 0;
    std::uint64_t start = 0;    // process start time (platform units), 0 when unknown
};

// The current process (pid and start time; no command line).
ProcessInfo selfProcess();
bool processAlive(long long pid, std::uint64_t start);

} // namespace gg
