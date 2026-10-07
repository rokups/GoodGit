#include "libgg/EditSession.hpp"

#include "libgg/Journal.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace gg::edit {

namespace fs = std::filesystem;

fs::path sessionFile(const fs::path& gitDir, const fs::path& commonDir)
{
    return commonDir / "gg" / "edit" / journal::worktreeKeyForGitDir(gitDir, commonDir);
}

std::optional<Session> read(const fs::path& file)
{
    std::ifstream in(file);
    if (!in)
        return std::nullopt;
    Session s;
    std::string line;
    while (std::getline(in, line)) {
        const auto sp = line.find(' ');
        if (sp == std::string::npos)
            continue;
        const std::string key = line.substr(0, sp);
        const std::string value = line.substr(sp + 1);
        if (key == "commit")
            s.commit = value;
        else if (key == "branch")
            s.branch = value;
        else if (key == "descendants")
            s.descendants = std::atoi(value.c_str());
    }
    if (s.commit.empty() || s.branch.empty())
        return std::nullopt;
    return s;
}

bool write(const fs::path& file, const Session& session)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec)
        return false;
    std::ofstream out(file, std::ios::trunc);
    out << "commit " << session.commit << "\nbranch " << session.branch << "\ndescendants " << session.descendants
        << "\n";
    out.flush();
    return static_cast<bool>(out);
}

void clear(const fs::path& file)
{
    std::error_code ec;
    fs::remove(file, ec);
}

} // namespace gg::edit
