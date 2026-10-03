#include "platform/PathSetup.hpp"

#include "util/Env.hpp"

#include <libgg/Files.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ggui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kTail = ":${PATH}";

std::string trim(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// The directory in the file's `PATH=<dir>:${PATH}` line ("" when there is none).
std::string pathEntry(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("PATH=", 0) != 0)
            continue;
        std::string value = line.substr(5);
        const std::string tail = kTail;
        if (value.size() >= tail.size() && value.compare(value.size() - tail.size(), tail.size(), tail) == 0)
            value.resize(value.size() - tail.size());
        return value;
    }
    return {};
}

} // namespace

PathSetupSupport pathSetupSupport([[maybe_unused]] const std::string& root)
{
#ifdef __linux__
    std::error_code ec;
    return fs::is_directory(fs::path(root + "/run/systemd/system"), ec) ? PathSetupSupport::Available
                                                                       : PathSetupSupport::NoSystemd;
#else
    return PathSetupSupport::Unsupported;
#endif
}

std::string pathSetupUnavailableReason(PathSetupSupport support)
{
    switch (support) {
    case PathSetupSupport::Available:
        return {};
    case PathSetupSupport::NoSystemd:
        return "Adding GoodGit to PATH needs systemd (it writes a systemd user environment.d file), "
               "which this system does not run.";
    case PathSetupSupport::Unsupported:
        break;
    }
    return "Adding GoodGit to PATH is not supported on this platform.";
}

std::string pathSetupConfigHome()
{
    const std::string xdg = getEnv("XDG_CONFIG_HOME");
    if (!xdg.empty() && xdg.front() == '/')
        return xdg;
    const std::string home = getEnv("HOME");
    return home.empty() ? std::string() : home + "/.config";
}

std::string pathSetupFile(const std::string& configHome)
{
    return (fs::path(configHome) / "environment.d" / "60-goodgit.conf").string();
}

PathSetupState readPathSetup(const std::string& configHome, const std::string& dir)
{
    PathSetupState st;
    if (configHome.empty())
        return st;
    std::ifstream in(pathSetupFile(configHome), std::ios::binary);
    if (!in)
        return st;
    std::ostringstream buf;
    buf << in.rdbuf();
    st.present = true;
    const std::string entry = pathEntry(buf.str());
    st.enabled = !entry.empty() && entry == dir;
    if (!st.enabled)
        st.otherDir = entry;
    return st;
}

std::string writePathSetup(
    PathSetupSupport support, const std::string& configHome, const std::string& dir, bool enable)
{
    if (support != PathSetupSupport::Available)
        return pathSetupUnavailableReason(support);
    if (configHome.empty())
        return "Cannot find the user configuration directory (neither XDG_CONFIG_HOME nor HOME is set).";
    const fs::path file = pathSetupFile(configHome);
    std::error_code ec;
    if (!enable) {
        fs::remove(file, ec);
        if (ec)
            return "Cannot remove " + file.string() + ": " + ec.message();
        return {};
    }
    if (dir.empty())
        return "Cannot tell where the GoodGit executable is.";
    if (dir.find_first_of("$\\\"':\n\r") != std::string::npos)
        return "The GoodGit directory (" + dir
            + ") contains a character (one of $ \\ \" ' : or a line break) that environment.d would expand or "
              "misread, so it was not added to PATH. Move GoodGit to a plainer path.";
    fs::create_directories(file.parent_path(), ec);
    if (ec)
        return "Cannot create " + file.parent_path().string() + ": " + ec.message();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return "Cannot write " + tmp.string() + ": " + std::strerror(errno);
        out << "# Managed by GoodGit (ggui): Settings > General > Add GoodGit to PATH.\n"
               "# Puts the directory holding git-gg on the login PATH. Delete this file to undo.\n"
            << "PATH=" << dir << kTail << "\n";
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return "Cannot write " + tmp.string() + ".";
        }
    }
    ec = gg::replaceFile(tmp, file);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return "Cannot write " + file.string() + ": " + ec.message();
    }
    return {};
}

} // namespace ggui
