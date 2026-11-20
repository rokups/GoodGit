// Replacing a file by renaming a temp file over it.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace gg {

// Renames tmp over target. On Windows the rename fails while another handle has the target open
// (a reader, a scanner), so it is retried for a short time, as Git for Windows does. Does not
// remove tmp; returns the last error when every attempt failed.
std::error_code replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& target);

// Reads the whole file into out. Returns false when it cannot be opened or a read fails.
bool readFile(const std::filesystem::path& path, std::string& out);

// Writes text to a temp file next to path, then replaceFile. A failure leaves path as it was.
// The permissions of an existing path carry over. Returns the first error.
std::error_code writeFileAtomic(const std::filesystem::path& path, std::string_view text);

} // namespace gg
