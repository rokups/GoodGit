// Build identity: the version, the CI commit (empty for local builds) and the package flavour
// (for example "linux-x86_64"), which names the matching release asset.
#pragma once

#include <string>

namespace ggui {

struct BuildInfo {
    std::string version;
    std::string commit;
    std::string packageSystem;
};

BuildInfo buildInfo();

// The first 7 characters of a commit id.
std::string shortCommit(const std::string& commit);

} // namespace ggui
