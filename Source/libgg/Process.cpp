#include "libgg/Process.hpp"

#include <fstream>
#include <map>
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

long long parentOf(long long pid)
{
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string stat;
    if (!std::getline(f, stat))
        return 0;
    // Field 4 (ppid), after the ")" that closes the command name and field 3 (state).
    const auto close = stat.rfind(')');
    if (close == std::string::npos)
        return 0;
    std::istringstream rest(stat.substr(close + 2));
    std::string state;
    long long ppid = 0;
    rest >> state >> ppid;
    return ppid;
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

// A git process: "git", "git.exe" or a dashed "git-<command>" (git-receive-pack, ...), but not
// git-gg, which is what runs the hook.
bool isGitProgram(std::string program)
{
    const auto slash = program.find_last_of("/\\");
    if (slash != std::string::npos)
        program = program.substr(slash + 1);
    if (program.size() > 4 && program.compare(program.size() - 4, 4, ".exe") == 0)
        program.resize(program.size() - 4);
    if (program == "git")
        return true;
    return program.rfind("git-", 0) == 0 && program != "git-gg";
}

// How far up to look for the git command. A hook runs as git → hook (config command, or the
// wrapper script and its pipeline subshell) → runner → git-gg.
constexpr int kMaxAncestors = 8;

} // namespace

ProcessInfo parentProcess()
{
    ProcessInfo info;
#ifdef _WIN32
    // Best effort: the parent pid from a toolhelp snapshot; start time from its creation time.
    // The nearest git.exe ancestor (the hook's shell and runner sit in between), else the parent.
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        struct Entry {
            DWORD parent;
            std::string exe;
        };
        std::map<DWORD, Entry> procs;
        PROCESSENTRY32W e{};
        e.dwSize = sizeof(e);
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
            std::string exe;
            for (const wchar_t* c = e.szExeFile; *c; ++c)
                exe.push_back(*c < 128 ? static_cast<char>(*c) : '?');
            procs[e.th32ProcessID] = {e.th32ParentProcessID, exe};
        }
        CloseHandle(snap);
        const auto me = procs.find(self);
        if (me != procs.end()) {
            info.pid = me->second.parent;
            DWORD pid = me->second.parent;
            for (int i = 0; i < kMaxAncestors && pid != 0; ++i) {
                const auto it = procs.find(pid);
                if (it == procs.end())
                    break;
                if (isGitProgram(it->second.exe)) {
                    info.pid = pid;
                    break;
                }
                pid = it->second.parent;
            }
        }
    }
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(info.pid))) {
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(h, &created, &exited, &kernel, &user))
            info.start = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        CloseHandle(h);
    }
    info.commandLine = "git";
#else
    // The nearest git ancestor (the hook's shell and runner sit in between), else the parent.
    info.pid = getppid();
    for (long long pid = info.pid, i = 0; i < kMaxAncestors && pid > 1; ++i, pid = parentOf(pid)) {
        std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
        std::string program;
        std::getline(f, program, '\0');
        if (isGitProgram(program)) {
            info.pid = pid;
            break;
        }
    }
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
