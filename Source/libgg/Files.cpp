#include "libgg/Files.hpp"

#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
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
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::error_code writeFileAtomic(const fs::path& path, std::string_view text)
{
    const fs::path tmp = path.parent_path() / (path.filename().string() + ".gg-tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ec(errno ? errno : EIO, std::generic_category());
            out.close();
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
