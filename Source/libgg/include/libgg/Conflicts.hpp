// Which files of a tree hold first-class conflicts (product spec §4.10).
//
// A tree's conflicted files are a pure function of its objects: results are memoised per tree
// id (so a commit that changes one file only rescans the trees on that file's path) and kept in
// a disposable cache, $GIT_COMMON_DIR/gg/cache/conflicts-v1. Deleting the cache changes
// nothing but speed. Eligibility (K1) is applied per commit from .gitattributes.
#pragma once

#include "libgg/Cancel.hpp"
#include "libgg/Git2.hpp"
#include "libgg/Markers.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace gg::conflicts {

struct ConflictedFile {
    std::string path;
    int sides = 2;
    bool operator==(const ConflictedFile&) const = default;
};

class Cache {
public:
    // commonDir empty = memory only.
    explicit Cache(std::filesystem::path commonDir = {});
    ~Cache();
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    void save();
    const std::filesystem::path& file() const { return m_file; }

    // Raw (attribute-free) results per tree id (binary id bytes as key).
    std::unordered_map<std::string, std::vector<ConflictedFile>> trees;
    std::unordered_map<std::string, int> blobs; // blob id → sides (0 = not conflicted)
    bool dirty = false;

private:
    void load();
    std::filesystem::path m_file;
};

// Conflicted files of `tree` by content only (text blobs with well-formed regions).
std::vector<ConflictedFile> scanTree(git_repository* repo, const git_oid& tree, Cache& cache,
    const CancelToken& cancel = CancelToken::none());

// Conflicted files of a commit's tree, keeping only paths that may hold first-class conflicts
// according to the commit's attributes (gg-conflicts, filter, binary / -text / -diff).
std::vector<ConflictedFile> commitConflicts(git_repository* repo, const git_oid& commit, Cache& cache,
    const CancelToken& cancel = CancelToken::none());

// Eligibility of `path` (K1). With `commit` null the working tree / index attributes are used.
bool eligible(git_repository* repo, const git_oid* commit, const std::string& path);
// Why a path may not hold first-class conflicts: "" (it may), "opt-out" (gg-conflicts=false)
// or "filtered" (filter=, binary, -text, -diff).
std::string ineligibleReason(git_repository* repo, const git_oid* commit, const std::string& path);

// Marker write options for `path`, from repo config and gitattributes: gg.sameChange
// ("accept", the default, or "keep"; unknown values fall back to "accept") and the
// conflict-marker-size attribute (invalid or absent: the default minimum, 7).
gg::markers::WriteOptions writeOptions(git_repository* repo, const std::string& path);

// Sides of a conflicted blob content (0 = not conflicted).
int contentSides(std::string_view content);

} // namespace gg::conflicts
