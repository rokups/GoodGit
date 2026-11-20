#include "libgg/SequenceEditorLink.hpp"

#include "libgg/Files.hpp"
#include "libgg/Git2.hpp"
#include "libgg/LocalSocket.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gg::seqlink {

namespace fs = std::filesystem;

fs::path registryDir()
{
    fs::path dir;
    std::error_code ec;
#ifdef _WIN32
    // The user's own temp directory. libstdc++ throws when it does not exist: use the error_code form.
    dir = fs::temp_directory_path(ec) / "ggui";
    ec.clear();
#else
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
        dir = fs::path(runtime) / "ggui";
    else
        dir = fs::temp_directory_path(ec) / ("ggui-" + std::to_string(static_cast<long long>(getuid())));
#endif
    ec.clear();
#ifndef _WIN32
    // The directory must be ours and private. Another owner or a symlink: use a fresh private
    // directory (other processes cannot find it, so they fall back to git's own editor).
    // mkdir with 0700: no window where another user can plant a file (the parent exists).
    ::mkdir(dir.c_str(), 0700);
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
        static const fs::path fresh = [] {
            std::error_code e;
            static std::string tmpl; // static: the exit handler below reads it
            tmpl = (fs::temp_directory_path(e) / "ggui-XXXXXX").string();
            if (!mkdtemp(tmpl.data()))
                return fs::path(); // empty: the registry is off
            std::atexit([] {
                std::error_code rm;
                fs::remove_all(tmpl, rm);
            });
            return fs::path(tmpl);
        }();
        return fresh;
    }
    if ((st.st_mode & 0777) != 0700)
        ::chmod(dir.c_str(), 0700);
#else
    if (fs::create_directories(dir, ec))
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    return dir;
}

std::string gitDirKey(const fs::path& gitDir)
{
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(gitDir, ec), ec);
    if (ec)
        p = fs::absolute(gitDir, ec).lexically_normal();
    std::string key = p.generic_string();
    while (key.size() > 1 && key.back() == '/')
        key.pop_back();
#ifdef _WIN32
    for (auto& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return key;
}

namespace {

fs::path instanceFile(long long pid) { return registryDir() / (std::to_string(pid) + ".instance"); }

bool processAlive(long long pid)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
#endif
}

} // namespace

void removeDeadInstances(long long exceptPid)
{
    std::error_code ec;
    const fs::path dir = registryDir();
    if (dir.empty())
        return;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const long long pid = std::atoll(p.stem().string().c_str());
        if (p.extension() == ".instance" && pid > 0 && pid != exceptPid && !processAlive(pid)) {
            std::error_code rm;
            fs::remove(p, rm);
        }
    }
}

bool writeInstance(const Instance& instance)
{
    if (registryDir().empty())
        return false;
    const fs::path file = instanceFile(instance.pid);
    const fs::path tmp = file.string() + ".tmp";
    std::error_code ec;
    const std::string text = std::to_string(instance.port) + "\n" + instance.token + "\n" + instance.gitDir + "\n";
#ifdef _WIN32
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out)
            return false;
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
#else
    // Created new (O_EXCL) and 0600: the token is never readable by others and a planted link is not followed.
    fs::remove(tmp, ec);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool wrote = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::close(fd);
    if (!wrote) {
        fs::remove(tmp, ec);
        return false;
    }
#endif
    removeDeadInstances(instance.pid);
    ec = replaceFile(tmp, file);
    return !ec;
}

void removeInstance(long long pid)
{
    if (registryDir().empty())
        return;
    std::error_code ec;
    fs::remove(instanceFile(pid), ec);
}

std::vector<Instance> instances()
{
    std::vector<Instance> out;
    std::error_code ec;
    const fs::path dir = registryDir();
    if (dir.empty())
        return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p.extension() != ".instance")
            continue;
#ifndef _WIN32
        // Only a regular file of ours: not a link and not a file another user planted.
        struct stat st{};
        if (::lstat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid())
            continue;
#endif
        std::ifstream in(p, std::ios::binary);
        Instance i;
        std::string port;
        if (!std::getline(in, port) || !std::getline(in, i.token) || !std::getline(in, i.gitDir))
            continue;
        i.port = std::atoi(port.c_str());
        i.pid = std::atoll(p.stem().string().c_str());
        if (i.port > 0 && !i.token.empty())
            out.push_back(std::move(i));
    }
    return out;
}

bool raiseOther(const std::string& key, long long ownPid)
{
    if (key.empty())
        return false;
    for (const auto& instance : instances()) {
        if (instance.pid == ownPid || instance.gitDir != key)
            continue;
        const auto s = net::connectLoopback(instance.port);
        if (s == net::kInvalid) {
            removeInstance(instance.pid); // a stale registration
            continue;
        }
        std::string reply;
        const bool raised = net::sendAll(s, instance.token + "\n" + kRaiseCommand + "\n" + key + "\n")
            && net::recvLine(s, reply, 2000) && reply == "OK";
        net::closeSocket(s);
        if (raised)
            return true;
    }
    return false;
}

fs::path resolveGitDir(const fs::path& path)
{
    try {
        const git2::Repository repo = git2::openRepository(path);
        return fs::path(git_repository_path(repo.get())).lexically_normal();
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace gg::seqlink
