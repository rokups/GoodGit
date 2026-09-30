#include "libgg/Process.hpp"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace gg {

namespace {

#ifndef _WIN32
std::uint64_t startTime(long long pid)
{
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string stat;
    if (!std::getline(f, stat))
        return 0;
    // Field 22 (starttime) counted after the ")" that closes the command name.
    const auto close = stat.rfind(')');
    if (close == std::string::npos)
        return 0;
    std::istringstream rest(stat.substr(close + 2));
    std::string field;
    for (int i = 3; i <= 22 && rest >> field; ++i)
        if (i == 22)
            return std::stoull(field);
    return 0;
}
#endif

} // namespace

ProcessInfo selfProcess()
{
    ProcessInfo info;
#ifdef _WIN32
    info.pid = GetCurrentProcessId();
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        info.start = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
#else
    info.pid = getpid();
    info.start = startTime(info.pid);
#endif
    return info;
}

bool processAlive(long long pid, std::uint64_t start)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    (void)start;
    return alive;
#else
    const std::uint64_t now = startTime(pid);
    return now != 0 && (start == 0 || now == start);
#endif
}

} // namespace gg
