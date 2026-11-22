#include "libgg/GitRunner.hpp"

#include "libgg/Thread.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
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
std::atomic<long long> g_slowGitSleeps{0};

std::mutex g_logMutex;
bool g_logEnabled = false;
std::vector<CommandLogEntry> g_log;
struct Running {
    const RunRequest* request;
    std::chrono::steady_clock::time_point start;
};
std::vector<Running> g_running;

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

} // namespace

std::string quoteWindowsArg(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
        return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes before a quote are doubled and the quote is escaped. Others stay as they are.
        out.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
        out.push_back(c);
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\'); // before the closing quote
    out.push_back('"');
    return out;
}

bool windowsCommandLineFits(size_t length)
{
    return length <= kMaxWindowsCommandLine;
}

bool isBatchFileName(const std::string& name)
{
    if (name.size() < 4)
        return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".cmd" || ext == ".bat";
}

std::string batchShellArgs(const std::string& script, const std::vector<std::string>& args, std::string& error)
{
    if (script.find_first_of("%\"\r\n") != std::string::npos) {
        error = "cannot run a batch file with '%' or '\"' or a line break in its path";
        return {};
    }
    // The outer quotes around the whole command are removed by `/s`.
    std::string line = "/d /e:on /v:off /s /c \"\"" + script + "\"";
    for (const auto& a : args) {
        // Inside quotes `^&|<>` and a tab are plain text. A line break or another control character cannot be encoded.
        // `/e:on`: `%cd:~,%` needs command extensions, which the registry can turn off.
        for (const char c : a) {
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
                error = std::string("cannot pass an argument with ") +
                    (c == '\n' ? "a line break" : c == '\r' ? "a carriage return" : "a control character") +
                    " to a batch file";
                return {};
            }
        }
        // Like Rust std: `"` becomes `""` (backslashes before it are doubled), and `%` becomes `%%cd:~,%`
        // so cmd.exe cannot expand %VAR% inside the quotes. Trailing backslashes are doubled before the
        // closing quote, so the program that the batch file starts does not read them as an escape.
        line += " \"";
        size_t backslashes = 0;
        for (const char c : a) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            if (c == '"')
                line.append(backslashes * 2, '\\');
            else
                line.append(backslashes, '\\');
            backslashes = 0;
            line += c == '"' ? "\"\"" : c == '%' ? "%%cd:~,%" : std::string(1, c);
        }
        line.append(backslashes * 2, '\\');
        line += '"';
    }
    line += "\"";
    return line;
}

std::vector<std::string> programCandidates(const std::string& program, bool windows)
{
    if (!windows)
        return {program};
    // CreateProcess cannot run a file without a known extension, so the bare name is not a candidate.
    std::vector<std::string> out{program + ".exe", program + ".cmd", program + ".bat"};
    std::string ext = program.size() >= 4 ? program.substr(program.size() - 4) : std::string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".exe" || ext == ".com" || isBatchFileName(program))
        out.push_back(program);
    return out;
}

namespace {

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

} // namespace

bool buildWindowsCommandLine(const std::vector<std::string>& args, const std::string& program,
    std::wstring& application, std::wstring& cmd, std::string& error)
{
    application.clear();
    cmd.clear();
    if (isBatchFileName(program)) {
        // A batch file never goes to CreateProcess as the program: cmd.exe runs it.
        wchar_t buf[MAX_PATH * 4];
        DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf)) {
            application.assign(buf, n);
        } else {
            n = GetEnvironmentVariableW(L"SystemRoot", buf, static_cast<DWORD>(std::size(buf)));
            if (n > 0 && n < std::size(buf))
                application = std::wstring(buf, n) + L"\\System32\\cmd.exe";
        }
        if (application.empty()) {
            error = "cannot find cmd.exe";
            return false;
        }
        const std::string tail = batchShellArgs(program, std::vector<std::string>(args.begin() + 1, args.end()), error);
        if (tail.empty())
            return false;
        cmd = L"\"" + application + L"\" " + widen(tail);
    } else {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i)
                cmd.push_back(L' ');
            cmd += widen(quoteWindowsArg(i == 0 ? program : args[i]));
        }
    }
    if (!windowsCommandLineFits(cmd.size())) {
        error = "command line too long";
        return false;
    }
    return true;
}

namespace {

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
    std::wstring application;
    std::string buildError;
    if (!buildWindowsCommandLine(request.args, program.string(), application, cmd, buildError)) {
        result.startFailed = true;
        result.err = request.args.at(0) + ": " + buildError;
        return result;
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    HANDLE job = nullptr;
    std::vector<char> attrBuf;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    const auto closeAll = [&] {
        for (HANDLE* h : {&inR, &inW, &outR, &outW, &errR, &errW, &job}) {
            if (*h)
                CloseHandle(*h);
            *h = nullptr;
        }
        if (attrs)
            DeleteProcThreadAttributeList(attrs);
        attrs = nullptr;
    };
    const auto fail = [&](const char* what) {
        const DWORD e = GetLastError();
        closeAll();
        result.startFailed = true;
        result.err = request.args.at(0) + ": " + what + " (error " + std::to_string(e) + ")";
    };
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0)) {
        fail("cannot create pipe");
        return result;
    }
    if (!SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0)
        || !SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0)) {
        fail("cannot set handle flags");
        return result;
    }

    // Only the three child pipe ends are inherited, so a run() on another thread cannot leak them to its child.
    // The three handles are distinct, as the handle list requires.
    HANDLE inherited[3] = {inR, outW, errW};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    attrBuf.resize(attrSize);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (attrSize == 0 || !InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize)) {
        fail("cannot init attribute list");
        return result;
    }
    attrs = attrList;
    if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
        fail("cannot set handle list");
        return result;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = inR;
    si.StartupInfo.hStdOutput = outW;
    si.StartupInfo.hStdError = errW;
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        fail("cannot create job object");
        return result;
    }

    const std::wstring cwd = request.cwd.empty() ? std::wstring() : request.cwd.wstring();
    const BOOL created = CreateProcessW(application.empty() ? nullptr : application.c_str(), cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, envBlock.data(),
        cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    if (!created) {
        fail("cannot start process");
        return result;
    }
    DeleteProcThreadAttributeList(attrs);
    attrs = nullptr;
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(errW);
    inR = outW = errW = nullptr;
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        // Run without the job, as before: cancel then ends only the child.
        std::fprintf(stderr, "gg: cannot assign process to job (error %lu)\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    std::atomic<int> pumpsDone{0}; // the writer and the two readers
    std::thread writer([&] {
        DWORD written = 0;
        size_t off = 0;
        while (off < request.input.size()) {
            if (!WriteFile(inW, request.input.data() + off, static_cast<DWORD>(request.input.size() - off), &written, nullptr))
                break;
            off += written;
        }
        CloseHandle(inW);
        ++pumpsDone;
    });
    std::thread outReader([&] {
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(outR, buf, sizeof(buf), &n, nullptr) && n > 0)
            result.out.append(buf, n);
        ++pumpsDone;
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
        ++pumpsDone;
    });

    const auto killTree = [&] {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
    };
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
        if (request.cancel.cancelled() && !result.cancelled) {
            killTree();
            result.cancelled = true;
        }
    }
    // A grandchild can hold the pipes after the child exits. Like POSIX, wait 500 ms, then end the job.
    // This also unblocks a writer that waits on a grandchild which holds stdin. Without a job, killTree ends only the child, so the deadline does not unblock the pumps.
    const auto exitDrainEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    bool drainKilled = false;
    while (pumpsDone.load() < 3) {
        if (!drainKilled && std::chrono::steady_clock::now() >= exitDrainEnd) {
            killTree();
            drainKilled = true;
        }
        Sleep(10);
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
    if (job)
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

    // A child that closes stdin early must give EPIPE, not kill this process.
    static const bool sigpipeIgnored = (std::signal(SIGPIPE, SIG_IGN), true);
    (void)sigpipeIgnored;
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
    bool killed = false, sentKill = false;
    using Clock = std::chrono::steady_clock;
    Clock::time_point killAt, drainEnd, exitDrainEnd;
    int status = 0;
    bool reaped = false;
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
        poll(fds, static_cast<nfds_t>(n), 20);
        if (!reaped && request.cancel.cancelled() && !killed) {
            kill(-pid, SIGTERM);
            killed = true;
            result.cancelled = true;
            killAt = Clock::now() + std::chrono::seconds(2);
        }
        if (killed && !sentKill && Clock::now() >= killAt) {
            kill(-pid, SIGKILL);
            sentKill = true;
            drainEnd = Clock::now() + std::chrono::milliseconds(500);
        }
        if (sentKill && Clock::now() >= drainEnd) {
            // A grandchild that left the group can still hold a pipe open.
            if (outFd >= 0)
                close(outFd);
            if (errFd >= 0)
                close(errFd);
            outFd = errFd = -1;
            break;
        }
        if (!reaped && waitpid(pid, &status, WNOHANG) == pid) {
            // The direct child is gone. A grandchild can keep the pipes open, so wait only a short time.
            reaped = true;
            exitDrainEnd = Clock::now() + std::chrono::milliseconds(500);
        }
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
            // Write only on POLLOUT; errno is stale otherwise. Any other event means the reader is gone.
            bool done = !(fds[iIn].revents & POLLOUT);
            if (!done) {
                const ssize_t w = write(inFd, request.input.data() + inOff, request.input.size() - inOff);
                if (w > 0)
                    inOff += static_cast<size_t>(w);
                done = (w < 0 && errno != EAGAIN && errno != EINTR) || inOff >= request.input.size();
            }
            if (done) {
                close(inFd);
                inFd = -1;
            }
        }
        // Check the deadline after the reads, so data that is ready at the deadline is kept.
        if (reaped && Clock::now() >= exitDrainEnd) {
            if (outFd >= 0)
                close(outFd);
            if (errFd >= 0)
                close(errFd);
            outFd = errFd = -1;
            break;
        }
    }
    if (inFd >= 0)
        close(inFd);
    while (!reaped) {
        // Poll, so a child that closed its pipes still sees cancel and a child that ignores SIGTERM cannot block.
        if (request.cancel.cancelled() && !killed) {
            kill(-pid, SIGTERM);
            killed = true;
            result.cancelled = true;
            killAt = Clock::now() + std::chrono::seconds(2);
        }
        if (killed && !sentKill && Clock::now() >= killAt) {
            kill(-pid, SIGKILL);
            sentKill = true;
        }
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid || (w < 0 && errno != EINTR))
            break;
        if (w == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        {
            std::lock_guard lock(g_logMutex);
            if (g_logEnabled)
                g_running.push_back(Running{&request, start});
        }
        result = runProcess(request);
        std::lock_guard lock(g_logMutex);
        std::erase_if(g_running, [&](const Running& r) { return r.request == &request; });
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

int gitVersion(const std::filesystem::path& cwd)
{
    const RunResult r = git(cwd, {"version"});
    int major = 0, minor = 0;
    const auto pos = r.out.find_first_of("0123456789");
    if (!r.ok() || pos == std::string::npos || std::sscanf(r.out.c_str() + pos, "%d.%d", &major, &minor) != 2)
        return 0;
    return major * 100 + minor;
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
void noteSlowGitSleep() { ++g_slowGitSleeps; }
long long slowGitSleeps() { return g_slowGitSleeps.load(); }

void setCommandLogEnabled(bool enabled)
{
    std::lock_guard lock(g_logMutex);
    g_logEnabled = enabled;
}

std::vector<CommandLogEntry> runningCommands()
{
    std::lock_guard lock(g_logMutex);
    std::vector<CommandLogEntry> out;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& r : g_running)
        out.push_back(CommandLogEntry{r.request->args, r.request->cwd, -1,
            std::chrono::duration_cast<std::chrono::milliseconds>(now - r.start), {}});
    return out;
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
    // "<phase>: <n>% (...)" possibly prefixed by "remote: " (the server's progress).
    const size_t from = line.rfind("remote: ", 0) == 0 ? 8 : 0;
    const auto colon = line.find(": ", from);
    const auto pct = line.find('%', from);
    if (colon == std::string::npos || pct == std::string::npos || pct < colon)
        return std::nullopt;
    size_t start = pct;
    while (start > colon + 2 && line[start - 1] >= '0' && line[start - 1] <= '9')
        --start;
    if (start == pct)
        return std::nullopt;
    GitProgress progress;
    progress.phase = line.substr(from, colon - from);
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

#ifdef _WIN32
bool widenUtf8(const std::string& s, std::wstring& out)
{
    out.clear();
    if (s.empty())
        return true;
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return false;
    out.assign(static_cast<std::size_t>(n), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), out.data(), n)
        == n;
}
#endif

std::vector<std::string> lsRemoteNames(const std::string& output, const std::string& prefix)
{
    std::vector<std::string> names;
    for (const auto& line : splitLines(output)) {
        const auto tab = line.find('\t');
        if (tab != std::string::npos && line.compare(tab + 1, prefix.size(), prefix) == 0)
            names.push_back(line.substr(tab + 1 + prefix.size()));
    }
    return names;
}

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::filesystem::path findInPath(const std::string& program)
{
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef _WIN32
    constexpr bool windows = true;
    const char sep = ';';
#else
    constexpr bool windows = false;
    const char sep = ':';
#endif
    // A batch file found here still goes through cmd.exe with checked arguments at the start.
    const std::vector<std::string> names = programCandidates(program, windows);
    const auto usable = [&](const fs::path& candidate) {
#ifdef _WIN32
        return fs::is_regular_file(candidate, ec);
#else
        return fs::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0;
#endif
    };
    if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos) {
        if (!windows)
            return fs::exists(program, ec) ? fs::path(program) : fs::path();
        for (const auto& name : names) {
            if (fs::is_regular_file(name, ec))
                return fs::path(name);
        }
        return {};
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv)
        return {};
    const std::string path(pathEnv);
    size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find(sep, start);
        if (end == std::string::npos)
            end = path.size();
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            for (const auto& name : names) {
                const fs::path candidate = fs::path(dir) / name;
                if (usable(candidate))
                    return candidate;
            }
        }
        start = end + 1;
    }
    return {};
}

} // namespace gg
