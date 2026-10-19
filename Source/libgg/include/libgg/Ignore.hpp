// Ignore patterns: build the lines for a .gitignore or .git/info/exclude file and add them.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gg::ignore {

// Escapes the characters that a gitignore pattern reads as special: * ? [ ] \, a leading # or !,
// and trailing spaces.
std::string escape(const std::string& text);
// "/path" for a path relative to the root of the repository. A path that ends in "/" stays a folder.
std::string anchored(const std::string& path);
// "*.ext" for a file name with an extension. Empty when the file has no extension.
std::string extensionPattern(const std::string& path);
// "/dir/" for the parent folder of a path. Empty when the path is in the root of the repository.
std::string folderPattern(const std::string& path);

// Appends each line that the file does not have yet. Creates the file and its folder when they are
// missing. Keeps a trailing newline. Returns the number of lines added. `ec` holds the error when
// the file cannot be read or written.
size_t addLines(const std::filesystem::path& file, const std::vector<std::string>& lines, std::error_code& ec);

} // namespace gg::ignore
