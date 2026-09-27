#include "libgg/Process.hpp"

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
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

std::string commandLine(long long pid)
{
    std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string out;
    for (char c : raw)
        out.push_back(c == '\0' ? ' ' : c);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}
#endif

} // namespace

ProcessInfo parentProcess()
{
    ProcessInfo info;
#ifdef _WIN32
    // Best effort: the parent pid from a toolhelp snapshot; start time from its creation time.
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{};
        e.dwSize = sizeof(e);
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
            if (e.th32ProcessID == self)
                info.pid = e.th32ParentProcessID;
        CloseHandle(snap);
    }
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(info.pid))) {
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(h, &created, &exited, &kernel, &user))
            info.start = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        CloseHandle(h);
    }
    info.commandLine = "git";
#else
    info.pid = getppid();
    info.start = startTime(info.pid);
    info.commandLine = commandLine(info.pid);
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
