#include "libgg/Files.hpp"

#include <chrono>
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

} // namespace gg
