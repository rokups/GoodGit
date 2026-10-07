#include "libgg/EditSession.hpp"

#include "libgg/Files.hpp"
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
    // Written in a sibling directory of the session files (gg/edit.tmp) and renamed over the target. The name
    // of a session file is the id of a worktree, one path component, so it never names a file in another
    // directory: no worktree id (foo, foo.tmp) can take the name of a temporary file. A write that fails
    // leaves the earlier session.
    const fs::path dir = file.parent_path();
    const fs::path tmpDir = dir.parent_path() / (dir.filename().string() + ".tmp");
    fs::create_directories(tmpDir, ec);
    if (ec)
        return false;
    const fs::path tmp = tmpDir / file.filename();
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open())
            return false; // the name is not ours to remove
        out << "commit " << session.commit << "\nbranch " << session.branch << "\ndescendants " << session.descendants
            << "\n";
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    if (replaceFile(tmp, file)) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

void clear(const fs::path& file)
{
    std::error_code ec;
    fs::remove(file, ec);
}

} // namespace gg::edit
