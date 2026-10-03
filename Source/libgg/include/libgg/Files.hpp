// Replacing a file by renaming a temp file over it.
#pragma once

#include <filesystem>
#include <system_error>

namespace gg {

// Renames tmp over target. On Windows the rename fails while another handle has the target open
// (a reader, a scanner), so it is retried for a short time, as Git for Windows does. Does not
// remove tmp; returns the last error when every attempt failed.
std::error_code replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& target);

} // namespace gg
