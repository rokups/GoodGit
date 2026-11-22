// Replacing a file by renaming a temp file over it.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace gg {

// Renames tmp over target. On Windows the rename fails while another handle has the target open
// (a reader, a scanner), so it is retried for a short time, as Git for Windows does. Does not
// remove tmp; returns the last error when every attempt failed. Each attempt after the first waits 10 ms.
std::error_code replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& target,
                            int attempts = 20);

// Reads the whole file into out. Returns false when it cannot be opened or a read fails.
bool readFile(const std::filesystem::path& path, std::string& out);

// Same, but returns nullopt on failure.
std::optional<std::string> readFile(const std::filesystem::path& path);

// Writes text to a unique temp file next to path (created new, flushed to disk), then replaceFile.
// A failure leaves path as it was and no temp file. mode, if set, is the mode of the new file;
// else the permissions of an existing path carry over. Returns the first error.
std::error_code writeFileAtomic(const std::filesystem::path& path, std::string_view text,
                                std::optional<std::filesystem::perms> mode = std::nullopt, int attempts = 20);

// Creates dir and its missing parents, but only when gitDir is an existing directory. A late writer
// must not recreate a git directory that the user moved away. Else sets no_such_file_or_directory.
bool makeDirsUnder(const std::filesystem::path& gitDir, const std::filesystem::path& dir, std::error_code& ec);

} // namespace gg
