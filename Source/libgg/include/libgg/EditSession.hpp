// "Edit commit" sessions (product spec §4.3): a commit checked out detached to be amended in
// place, and the branch to return to once its descendants are restacked. One per worktree, in
// <common dir>/gg/edit/<worktree key>, so it survives restarts.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gg::edit {

struct Session {
    std::string commit;      // the commit being edited (follows amends)
    std::string branch;      // local branch to return to (short name)
    int descendants = 0;     // commits an amend restacks
};

std::filesystem::path sessionFile(const std::filesystem::path& gitDir, const std::filesystem::path& commonDir);
std::optional<Session> read(const std::filesystem::path& file);
void write(const std::filesystem::path& file, const Session& session);
void clear(const std::filesystem::path& file);

} // namespace gg::edit
