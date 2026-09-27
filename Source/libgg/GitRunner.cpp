#include "libgg/GitRunner.hpp"

#include "libgg/Thread.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gg {
namespace {

std::mutex g_envMutex;
std::string g_askpass;
std::string g_askpassEndpoint;
thread_local std::string t_operation;
std::atomic<long long> g_slowGitMs{0};

std::mutex g_logMutex;
bool g_logEnabled = false;
std::vector<CommandLogEntry> g_log;

using EnvMap = std::map<std::string, std::string>;

EnvMap currentEnvironment()
{
    EnvMap env;
#ifdef _WIN32
    wchar_t* block = GetEnvironmentStringsW();
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        const std::wstring entry(p);
        const auto eq = entry.find(L'=', 1); // skip "=C:" style entries' leading '='
        if (eq == std::wstring::npos)
            continue;
        const int n1 = WideCharToMultiByte(CP_UTF8, 0, entry.c_str(), static_cast<int>(eq), nullptr, 0, nullptr, nullptr);
        std::string key(static_cast<size_t>(n1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, entry.c_str(), static_cast<int>(eq), key.data(), n1, nullptr, nullptr);
        const std::wstring wv = entry.substr(eq + 1);
        const int n2 = WideCharToMultiByte(CP_UTF8, 0, wv.c_str(), static_cast<int>(wv.size()), nullptr, 0, nullptr, nullptr);
        std::string value(static_cast<size_t>(n2), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wv.c_str(), static_cast<int>(wv.size()), value.data(), n2, nullptr, nullptr);
        env[key] = value;
    }
    FreeEnvironmentStringsW(block);
#else
    for (char** e = environ; e && *e; ++e) {
        const char* eq = std::strchr(*e, '=');
        if (!eq)
            continue;
        env[std::string(*e, static_cast<size_t>(eq - *e))] = std::string(eq + 1);
    }
#endif
    return env;
}

EnvMap buildEnvironment(const RunRequest& request)
{
    EnvMap env = currentEnvironment();
    if (request.cLocale) {
        env["LC_ALL"] = "C";
        env["LANGUAGE"] = "C";
    }
    if (request.gitEnvironment) {
        env["GIT_TERMINAL_PROMPT"] = "0";
        const std::string askpass = askpassProgram();
        if (!askpass.empty()) {
            env["GIT_ASKPASS"] = askpass;
            env["SSH_ASKPASS"] = askpass;
            env["SSH_ASKPASS_REQUIRE"] = "force";
            env["GG_ASKPASS_ENDPOINT"] = askpassEndpoint();
        }
        if (!t_operation.empty())
            env["GG_OPERATION"] = t_operation;
    }
    for (const auto& [key, value] : request.env) {
        if (value)
            env[key] = *value;
        else
            env.erase(key);
    }
    return env;
}

// Splits stderr chunks into progress lines (terminated by '\r' or '\n').
class ProgressSplitter {
public:
    explicit ProgressSplitter(const std::function<void(const GitProgress&)>& cb) : m_cb(cb) { }
    void feed(const char* data, size_t n)
    {
        if (!m_cb)
            return;
        for (size_t i = 0; i < n; ++i) {
            const char c = data[i];
            if (c == '\r' || c == '\n') {
                if (auto p = parseProgressLine(m_line))
                    m_cb(*p);
                m_line.clear();
            } else {
                m_line.push_back(c);
            }
        }
    }

private:
    const std::function<void(const GitProgress&)>& m_cb;
    std::string m_line;
};

void logCommand(const RunRequest& request, const RunResult& result)
{
    std::lock_guard lock(g_logMutex);
    if (!g_logEnabled)
        return;
    g_log.push_back(CommandLogEntry{request.args, request.cwd, result.exitCode, result.duration, result.err});
}

#ifdef _WIN32

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Quotes one argument following the MSVCRT CommandLineToArgvW rules.
void appendQuoted(std::wstring& cmd, const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd.push_back(L'"');
    for (auto it = arg.begin();; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd.push_back(*it);
        } else {
            cmd.append(backslashes, L'\\');
            cmd.push_back(*it);
        }
    }
    cmd.push_back(L'"');
}

RunResult runProcess(const RunRequest& request)
{
    RunResult result;
    const EnvMap env = buildEnvironment(request);
    std::wstring envBlock;
    for (const auto& [k, v] : env) {
        envBlock += widen(k) + L"=" + widen(v);
        envBlock.push_back(L'\0');
    }
    envBlock.push_back(L'\0');

    std::wstring cmd;
    std::filesystem::path program = findInPath(request.args.at(0));
    if (program.empty()) {
        result.startFailed = true;
        result.err = request.args.at(0) + ": command not found";
        return result;
    }
    for (size_t i = 0; i < request.args.size(); ++i) {
        if (i)
            cmd.push_back(L' ');
        appendQuoted(cmd, i == 0 ? program.wstring() : widen(request.args[i]));
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    CreatePipe(&inR, &inW, &sa, 0);
    CreatePipe(&outR, &outW, &sa, 0);
    CreatePipe(&errR, &errW, &sa, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = errW;
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    const std::wstring cwd = request.cwd.empty() ? std::wstring() : request.cwd.wstring();
    const BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, envBlock.data(),
        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(errW);
    if (!created) {
        CloseHandle(inW);
        CloseHandle(outR);
        CloseHandle(errR);
        CloseHandle(job);
        result.startFailed = true;
        result.err = request.args.at(0) + ": cannot start process";
        return result;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    std::thread writer([&] {
        DWORD written = 0;
        size_t off = 0;
        while (off < request.input.size()) {
            if (!WriteFile(inW, request.input.data() + off, static_cast<DWORD>(request.input.size() - off), &written, nullptr))
                break;
            off += written;
        }
        CloseHandle(inW);
    });
    std::thread outReader([&] {
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(outR, buf, sizeof(buf), &n, nullptr) && n > 0)
            result.out.append(buf, n);
    });
    std::string errData;
    std::mutex errMutex;
    std::thread errReader([&] {
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(errR, buf, sizeof(buf), &n, nullptr) && n > 0) {
            std::lock_guard lock(errMutex);
            errData.append(buf, n);
        }
    });

    ProgressSplitter splitter(request.onProgress);
    size_t fed = 0;
    for (;;) {
        const DWORD w = WaitForSingleObject(pi.hProcess, 20);
        {
            std::lock_guard lock(errMutex);
            if (errData.size() > fed) {
                splitter.feed(errData.data() + fed, errData.size() - fed);
                fed = errData.size();
            }
        }
        if (w == WAIT_OBJECT_0)
            break;
        if (request.cancel.cancelled()) {
            TerminateJobObject(job, 1);
            result.cancelled = true;
        }
    }
    writer.join();
    outReader.join();
    errReader.join();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    result.err = std::move(errData);
    CloseHandle(outR);
    CloseHandle(errR);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return result;
}

#else

void setNonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

RunResult runProcess(const RunRequest& request)
{
    RunResult result;
    const EnvMap env = buildEnvironment(request);
    std::vector<std::string> envStrings;
    envStrings.reserve(env.size());
    for (const auto& [k, v] : env)
        envStrings.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : envStrings)
        envp.push_back(s.data());
    envp.push_back(nullptr);
    std::vector<std::string> argStrings = request.args;
    std::vector<char*> argv;
    for (auto& a : argStrings)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    const std::filesystem::path program = findInPath(request.args.at(0));
    if (program.empty()) {
        result.startFailed = true;
        result.err = request.args.at(0) + ": command not found";
        return result;
    }

    int inPipe[2], outPipe[2], errPipe[2];
    if (pipe2(inPipe, O_CLOEXEC) != 0 || pipe2(outPipe, O_CLOEXEC) != 0 || pipe2(errPipe, O_CLOEXEC) != 0) {
        result.startFailed = true;
        result.err = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, inPipe[0], 0);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, errPipe[1], 2);
    if (!request.cwd.empty())
        posix_spawn_file_actions_addchdir_np(&actions, request.cwd.c_str());
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Own process group so cancellation can kill the whole tree.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    posix_spawnattr_setpgroup(&attr, 0);
    sigset_t all, none;
    sigfillset(&all);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attr, &all);
    posix_spawnattr_setsigmask(&attr, &none);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, program.c_str(), &actions, &attr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(inPipe[0]);
    close(outPipe[1]);
    close(errPipe[1]);
    if (rc != 0) {
        close(inPipe[1]);
        close(outPipe[0]);
        close(errPipe[0]);
        result.startFailed = true;
        result.err = request.args.at(0) + ": " + std::strerror(rc);
        return result;
    }

    setNonBlocking(inPipe[1]);
    setNonBlocking(outPipe[0]);
    setNonBlocking(errPipe[0]);
    int inFd = inPipe[1];
    size_t inOff = 0;
    if (request.input.empty()) {
        close(inFd);
        inFd = -1;
    }
    int outFd = outPipe[0];
    int errFd = errPipe[0];
    ProgressSplitter splitter(request.onProgress);
    bool killed = false;
    char buf[65536];
    while (outFd >= 0 || errFd >= 0) {
        pollfd fds[3];
        int n = 0;
        int iOut = -1, iErr = -1, iIn = -1;
        if (outFd >= 0) {
            iOut = n;
            fds[n++] = pollfd{outFd, POLLIN, 0};
        }
        if (errFd >= 0) {
            iErr = n;
            fds[n++] = pollfd{errFd, POLLIN, 0};
        }
        if (inFd >= 0) {
            iIn = n;
            fds[n++] = pollfd{inFd, POLLOUT, 0};
        }
        const int pr = poll(fds, static_cast<nfds_t>(n), 20);
        if (request.cancel.cancelled() && !killed) {
            kill(-pid, SIGTERM);
            killed = true;
            result.cancelled = true;
        }
        if (pr <= 0)
            continue;
        if (iOut >= 0 && fds[iOut].revents) {
            const ssize_t r = read(outFd, buf, sizeof(buf));
            if (r > 0)
                result.out.append(buf, static_cast<size_t>(r));
            else if (r == 0 || errno != EAGAIN) {
                close(outFd);
                outFd = -1;
            }
        }
        if (iErr >= 0 && fds[iErr].revents) {
            const ssize_t r = read(errFd, buf, sizeof(buf));
            if (r > 0) {
                result.err.append(buf, static_cast<size_t>(r));
                splitter.feed(buf, static_cast<size_t>(r));
            } else if (r == 0 || errno != EAGAIN) {
                close(errFd);
                errFd = -1;
            }
        }
        if (iIn >= 0 && fds[iIn].revents) {
            const ssize_t w = (fds[iIn].revents & POLLOUT)
                ? write(inFd, request.input.data() + inOff, request.input.size() - inOff)
                : -1;
            if (w > 0)
                inOff += static_cast<size_t>(w);
            if (w < 0 && errno == EAGAIN)
                continue;
            if (w < 0 || inOff >= request.input.size()) {
                close(inFd);
                inFd = -1;
            }
        }
    }
    if (inFd >= 0)
        close(inFd);
    int status = 0;
    for (int waited = 0;; ++waited) {
        const pid_t w = waitpid(pid, &status, killed ? WNOHANG : 0);
        if (w == pid)
            break;
        if (w < 0 && errno != EINTR)
            break;
        if (killed && w == 0) {
            if (waited == 50)
                kill(-pid, SIGKILL);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (killed)
        kill(-pid, SIGKILL); // leftover grandchildren
    if (WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exitCode = 128 + WTERMSIG(status);
    return result;
}

#endif

} // namespace

std::string RunResult::message() const
{
    if (cancelled)
        return "Cancelled";
    std::string text = trim(err);
    if (text.empty())
        text = trim(out);
    if (text.empty())
        text = "exit code " + std::to_string(exitCode);
    return text;
}

RunResult run(const RunRequest& request)
{
    assertNotUiThread("gg::run");
    const auto start = std::chrono::steady_clock::now();
    if (const auto latency = slowGitLatency(); latency.count() > 0) {
        // Test switch: simulate a slow git without blocking cancellation.
        const auto until = start + latency;
        while (std::chrono::steady_clock::now() < until && !request.cancel.cancelled())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RunResult result;
    if (request.cancel.cancelled()) {
        result.cancelled = true;
    } else {
        result = runProcess(request);
    }
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    logCommand(request, result);
    return result;
}

RunResult git(const std::filesystem::path& cwd, std::vector<std::string> args, std::string input)
{
    RunRequest request;
    request.args.reserve(args.size() + 1);
    request.args.emplace_back("git");
    for (auto& a : args)
        request.args.push_back(std::move(a));
    request.cwd = cwd;
    request.input = std::move(input);
    return run(request);
}

void setAskpassProgram(std::string program, std::string endpoint)
{
    std::lock_guard lock(g_envMutex);
    g_askpass = std::move(program);
    g_askpassEndpoint = std::move(endpoint);
}

std::string askpassEndpoint()
{
    std::lock_guard lock(g_envMutex);
    return g_askpassEndpoint;
}

std::string askpassProgram()
{
    std::lock_guard lock(g_envMutex);
    return g_askpass;
}

void setCurrentOperation(std::string id) { t_operation = std::move(id); }
std::string currentOperation() { return t_operation; }

void setSlowGitLatency(std::chrono::milliseconds latency) { g_slowGitMs = latency.count(); }
std::chrono::milliseconds slowGitLatency() { return std::chrono::milliseconds(g_slowGitMs.load()); }

void setCommandLogEnabled(bool enabled)
{
    std::lock_guard lock(g_logMutex);
    g_logEnabled = enabled;
}

std::vector<CommandLogEntry> commandLog()
{
    std::lock_guard lock(g_logMutex);
    return g_log;
}

void clearCommandLog()
{
    std::lock_guard lock(g_logMutex);
    g_log.clear();
}

std::optional<GitProgress> parseProgressLine(const std::string& line)
{
    // "<phase>: <n>% (...)" possibly prefixed by "remote: ".
    const auto colon = line.find(": ");
    const auto pct = line.find('%');
    if (colon == std::string::npos || pct == std::string::npos || pct < colon)
        return std::nullopt;
    size_t start = pct;
    while (start > colon + 2 && line[start - 1] >= '0' && line[start - 1] <= '9')
        --start;
    if (start == pct)
        return std::nullopt;
    GitProgress progress;
    progress.phase = line.substr(0, colon);
    if (progress.phase.rfind("remote: ", 0) == 0)
        progress.phase = progress.phase.substr(8);
    progress.percent = std::clamp(std::atoi(line.c_str() + start), 0, 100);
    progress.line = line;
    return progress;
}

std::vector<std::string> splitNul(const std::string& text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\0', start);
        if (end == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string trim(std::string text)
{
    const auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    size_t i = 0;
    while (i < text.size() && isSpace(static_cast<unsigned char>(text[i])))
        ++i;
    return text.substr(i);
}

std::filesystem::path findInPath(const std::string& program)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos)
        return fs::exists(program, ec) ? fs::path(program) : fs::path();
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv)
        return {};
#ifdef _WIN32
    const char sep = ';';
    const std::vector<std::string> exts{".exe", ".cmd", ".bat", ""};
#else
    const char sep = ':';
    const std::vector<std::string> exts{""};
#endif
    const std::string path(pathEnv);
    size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find(sep, start);
        if (end == std::string::npos)
            end = path.size();
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            for (const auto& ext : exts) {
                const fs::path candidate = fs::path(dir) / (program + ext);
#ifdef _WIN32
                if (fs::is_regular_file(candidate, ec))
                    return candidate;
#else
                if (fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0)
                    return candidate;
#endif
            }
        }
        start = end + 1;
    }
    return {};
}

} // namespace gg
