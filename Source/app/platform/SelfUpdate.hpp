// Self-update release model: parse a GitHub release, pick the asset for this build, and compare
// commit ids. Pure logic, no network or file access.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ggui::update {

struct Asset {
    std::string name;
    std::string url;
    std::string sha256; // lowercase hex; empty when the release gives no sha256 digest
    std::uint64_t size = 0;
};

struct Release {
    std::string commit; // target_commitish
    std::vector<Asset> assets;
};

// Reads a GitHub "release" JSON object. Assets whose state is not "uploaded" are skipped.
// Returns false and sets `error` for malformed JSON. Never throws.
bool parseRelease(std::string_view json, Release& out, std::string& error);

// The asset for `packageSystem` (for example "linux-x86_64" or "windows-msvc"), or null when there
// is none or the asset is not trustworthy (foreign URL, bad digest, zero size).
const Asset* selectAsset(const Release& release, std::string_view packageSystem);

enum class Freshness { UpToDate, Newer, Unknown };

// Compares two 40-hex commit ids (case-insensitive). Anything else gives Unknown.
Freshness compare(std::string_view local, std::string_view release);

} // namespace ggui::update
