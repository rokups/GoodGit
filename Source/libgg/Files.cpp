#include "libgg/Files.hpp"

#include <cerrno>
#include <chrono>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

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

std::error_code writeFileAtomic(const fs::path& path, std::string_view text)
{
    // A unique name: it must not hit a user file or the tmp file of a concurrent writer.
    fs::path tmp = path;
    tmp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
        + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::error_code ec(errno ? errno : EIO, std::generic_category());
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return ec;
        }
    }
    std::error_code ec;
    const auto perms = fs::status(path, ec).permissions();
    if (!ec)
        fs::permissions(tmp, perms, ec);
    ec = replaceFile(tmp, path);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
    }
    return ec;
}

} // namespace gg
