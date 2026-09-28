#include "libgg/SequenceEditorLink.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace gg::seqlink {

namespace fs = std::filesystem;

fs::path registryDir()
{
    fs::path dir;
#ifdef _WIN32
    dir = fs::temp_directory_path() / "ggui"; // the user's own temp directory
#else
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime)
        dir = fs::path(runtime) / "ggui";
    else
        dir = fs::temp_directory_path() / ("ggui-" + std::to_string(static_cast<long long>(getuid())));
#endif
    std::error_code ec;
    if (fs::create_directories(dir, ec))
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
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

} // namespace

bool writeInstance(const Instance& instance)
{
    const fs::path file = instanceFile(instance.pid);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << instance.port << "\n" << instance.token << "\n" << instance.gitDir << "\n";
        if (!out)
            return false;
    }
    std::error_code ec;
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    fs::rename(tmp, file, ec);
    return !ec;
}

void removeInstance(long long pid)
{
    std::error_code ec;
    fs::remove(instanceFile(pid), ec);
}

std::vector<Instance> instances()
{
    std::vector<Instance> out;
    std::error_code ec;
    for (fs::directory_iterator it(registryDir(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p.extension() != ".instance")
            continue;
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

} // namespace gg::seqlink
