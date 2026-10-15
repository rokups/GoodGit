// SHA-256, written in-tree. Used to check a downloaded update against its release digest.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ggui {

// The lowercase hex SHA-256 (64 characters) of `data`.
std::string sha256Hex(std::string_view data);

// The lowercase hex SHA-256 of a file, read in 64 KiB blocks. On an open or read failure the
// result is empty and `error` holds the reason.
std::string sha256File(const std::filesystem::path& path, std::string& error);

} // namespace ggui
