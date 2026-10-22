#pragma once

#include "core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ggui::core {

// Stages present: base (1), ours (2), theirs (3).
std::string conflictDescription(bool s1, bool s2, bool s3);

struct ParsedStatus {
    StatusResult status; // each vector is sorted by path
    struct ConflictOids {
        std::string h1, h2, h3; // hex object ids, all zero when the stage is absent
    };
    std::vector<ConflictOids> conflictOids; // parallel to status.conflicted
};

// Parses the output of `git status --porcelain=v2 -z`. Does not access a repository.
// Throws std::runtime_error on a malformed record.
ParsedStatus parsePorcelainV2(std::string_view out);

} // namespace ggui::core
