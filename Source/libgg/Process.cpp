#include "libgg/Process.hpp"

#include <cstdlib>
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

#ifdef _WIN32
// Another process's command line (NtQueryInformationProcess, ProcessCommandLineInformation),
// with the program path replaced by its name without .exe: "git commit -m x".
std::string commandLineOf(DWORD pid)
{
    using Query = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<Query>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h || !query) {
        if (h)
            CloseHandle(h);
        return {};
    }
    constexpr ULONG kCommandLine = 60; // ProcessCommandLineInformation
    ULONG size = 0;
    query(h, kCommandLine, nullptr, 0, &size);
    std::string buffer(size ? size : 1, '\0');
    struct Unicode {
        USHORT length;
        USHORT maximum;
        PWSTR text;
    };
    std::wstring line;
    if (size && query(h, kCommandLine, buffer.data(), size, &size) >= 0) {
        const auto* u = reinterpret_cast<const Unicode*>(buffer.data());
        line.assign(u->text, u->length / sizeof(wchar_t));
    }
    CloseHandle(h);
    // The program: quoted or up to the first space.
    size_t rest = 0;
    if (!line.empty() && line[0] == L'"') {
        const auto close = line.find(L'"', 1);
        rest = close == std::wstring::npos ? line.size() : close + 1;
    } else {
        rest = line.find(L' ');
        if (rest == std::wstring::npos)
            rest = line.size();
    }
    std::wstring program = line.substr(0, rest);
    std::erase(program, L'"');
    if (const auto slash = program.find_last_of(L"/\\"); slash != std::wstring::npos)
        program = program.substr(slash + 1);
    if (program.size() > 4 && _wcsicmp(program.c_str() + program.size() - 4, L".exe") == 0)
        program.resize(program.size() - 4);
    const std::wstring text = program + line.substr(rest);
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}
#endif

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
// wrapper script and its pipeline subshell) → runner → git-gg; on Windows every MSYS fork and
// exec in between is a process of its own, so the chain is longer there.
constexpr int kMaxAncestors = 32;

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
        // Diagnostics: GG_DEBUG_PROCESS=<file> appends the ancestor chain this lookup saw.
        if (const char* debug = std::getenv("GG_DEBUG_PROCESS"); debug && *debug) {
            std::ofstream d(debug, std::ios::app);
            d << self;
            for (DWORD p = procs.count(self) ? procs[self].parent : 0, i = 0; i < 40 && p; ++i) {
                const auto it = procs.find(p);
                if (it == procs.end()) {
                    d << " <- " << p << " (gone)";
                    break;
                }
                d << " <- " << p << " " << it->second.exe;
                p = it->second.parent;
            }
            d << "\n";
        }
        // The hook's first shell, when it named itself (GG_HOOK_SHELL): its parent is git.
        DWORD start = 0;
        if (const char* shell = std::getenv("GG_HOOK_SHELL"); shell && *shell)
            start = static_cast<DWORD>(std::strtoul(shell, nullptr, 10));
        const auto me = procs.find(start && procs.count(start) ? start : self);
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
    info.commandLine = commandLineOf(static_cast<DWORD>(info.pid));
    if (info.commandLine.empty())
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
