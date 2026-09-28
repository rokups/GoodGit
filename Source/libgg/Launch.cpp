#include "libgg/Launch.hpp"

#include "libgg/GitRunner.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gg {

namespace fs = std::filesystem;

fs::path selfExecutable()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf))
        return fs::path(std::wstring(buf, n));
#else
    std::error_code ec;
    if (auto p = fs::read_symlink("/proc/self/exe", ec); !ec)
        return p;
#endif
    return {};
}

fs::path gguiProgram()
{
    if (const char* env = std::getenv("GG_GGUI"); env && *env)
        return findInPath(env);
#ifdef _WIN32
    const char* name = "ggui.exe";
#else
    const char* name = "ggui";
#endif
    std::error_code ec;
    if (const fs::path self = selfExecutable(); !self.empty() && fs::is_regular_file(self.parent_path() / name, ec))
        return self.parent_path() / name;
    return findInPath("ggui");
}

bool spawnDetached(const std::vector<std::string>& args, DetachedProcess& child, std::string& error)
{
#ifdef _WIN32
    std::wstring cmd;
    for (const auto& a : args) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, a.data(), static_cast<int>(a.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, a.data(), static_cast<int>(a.size()), w.data(), n);
        if (!cmd.empty())
            cmd += L' ';
        cmd += L'"';
        size_t slashes = 0;
        for (wchar_t c : w) {
            if (c == L'\\') {
                ++slashes;
            } else {
                if (c == L'"')
                    cmd.append(slashes + 1, L'\\');
                slashes = 0;
            }
            cmd += c;
        }
        cmd.append(slashes, L'\\');
        cmd += L'"';
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
            nullptr, &si, &pi)) {
        error = "cannot start " + args.front();
        return false;
    }
    CloseHandle(pi.hThread);
    child.process = pi.hProcess;
    return true;
#else
    std::vector<char*> argv;
    for (const auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    // What git set up for a hook or an editor is not for a whole ggui session.
    std::vector<char*> envp;
    for (char** e = environ; e && *e; ++e) {
        bool skip = false;
        for (const char* name : {"GIT_DIR=", "GIT_WORK_TREE=", "GIT_INDEX_FILE=", "GIT_PREFIX=", "GIT_OBJECT_DIRECTORY="})
            skip = skip || std::strncmp(*e, name, std::strlen(name)) == 0;
        if (!skip)
            envp.push_back(*e);
    }
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int fd = 0; fd <= 2; ++fd)
        posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &all);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, argv[0], &actions, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        error = "cannot start " + args.front() + ": " + std::strerror(rc);
        return false;
    }
    child.pid = pid;
    return true;
#endif
}

bool stillRunning(DetachedProcess& child)
{
#ifdef _WIN32
    return child.process && WaitForSingleObject(static_cast<HANDLE>(child.process), 0) == WAIT_TIMEOUT;
#else
    if (child.pid <= 0)
        return false;
    int status = 0;
    const pid_t r = waitpid(static_cast<pid_t>(child.pid), &status, WNOHANG);
    if (r == 0)
        return true;
    child.pid = -1;
    return false;
#endif
}

void release(DetachedProcess& child)
{
#ifdef _WIN32
    if (child.process)
        CloseHandle(static_cast<HANDLE>(child.process));
    child.process = nullptr;
#else
    if (child.pid > 0) {
        const pid_t pid = static_cast<pid_t>(child.pid);
        std::thread([pid] {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    }
    child.pid = -1;
#endif
}

} // namespace gg
