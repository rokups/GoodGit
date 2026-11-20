#include "libgg/Files.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gg {

namespace fs = std::filesystem;

std::error_code replaceFile(const fs::path& tmp, const fs::path& target)
{
    constexpr int kAttempts = 20;
    std::error_code ec;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ec.clear();
        fs::rename(tmp, target, ec);
        if (!ec)
            return ec;
    }
    return ec;
}

bool readFile(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string data;
    char buf[65536];
    while (in.read(buf, sizeof buf) || in.gcount() > 0)
        data.append(buf, static_cast<size_t>(in.gcount()));
    if (in.bad() || !in.eof()) // a directory opens, then fails at the first read
        return false;
    out = std::move(data);
    return true;
}

namespace {

// Creates a new file (it fails when the name exists), writes all data, flushes it to disk.
std::error_code writeTempFile(const fs::path& tmp, std::string_view text)
{
#ifdef _WIN32
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
    std::error_code ec;
    size_t done = 0;
    while (!ec && done < text.size()) {
        DWORD n = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(text.size() - done, 1u << 30));
        if (!WriteFile(h, text.data() + done, want, &n, nullptr) || n == 0)
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        done += n;
    }
    if (!ec && !FlushFileBuffers(h))
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    CloseHandle(h);
    return ec;
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0)
        return std::error_code(errno, std::generic_category());
    std::error_code ec;
    size_t done = 0;
    while (!ec && done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ec = std::error_code(n < 0 ? errno : EIO, std::generic_category());
        else
            done += static_cast<size_t>(n);
    }
    if (!ec && ::fsync(fd) != 0)
        ec = std::error_code(errno, std::generic_category());
    if (::close(fd) != 0 && !ec)
        ec = std::error_code(errno, std::generic_category());
    return ec;
#endif
}

} // namespace

std::error_code writeFileAtomic(const fs::path& path, std::string_view text, std::optional<fs::perms> mode)
{
    // A unique name: the pid tells two processes apart, the counter two writes of one process.
    static std::atomic<unsigned> counter{0};
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    fs::path tmp = path;
    tmp += "." + std::to_string(pid) + "-" + std::to_string(counter.fetch_add(1)) + ".tmp";
    std::error_code ec = writeTempFile(tmp, text);
    if (!ec) {
        std::error_code ignore;
        const fs::perms perms = mode ? *mode : fs::status(path, ignore).permissions();
        if (mode || !ignore)
            fs::permissions(tmp, perms, ec);
    }
    if (!ec)
        ec = replaceFile(tmp, path);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return ec;
    }
#ifndef _WIN32
    // Makes the rename durable. A failure here is ignored: the file is already in place.
    const fs::path dir = path.has_parent_path() ? path.parent_path() : fs::path(".");
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        (void)::fsync(dfd);
        ::close(dfd);
    }
#endif
    return ec;
}

} // namespace gg
