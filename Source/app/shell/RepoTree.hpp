// The tree of the repo browser, built from the permanent repository list (GG-12).
#pragma once

#include "shell/Settings.hpp"

#include <string>
#include <vector>

namespace ggui {

// A node of the repo browser tree: a group (a folder made from alias segments) or a repository.
struct RepoNode {
    std::string label;              // one alias segment, or the base name of the folder
    std::string path;               // repository path; empty for a group
    std::string group;              // for a group: its full group path ("work/web"); empty for a repository
    std::vector<RepoNode> children; // groups only
    bool isGroup() const { return path.empty(); }
};

// The tree of the repo browser. An alias "g/s/name" puts the repository in the group "g/s" under
// the label "name". A repository without an alias goes by its deduplicated name
// (uniqueRecentNames over all the paths): the folder prefix "work/" gives the group "work" and the
// base name is the label; with no prefix it is a top-level leaf. An unaliased "work/foo" and an
// alias "work/bar" share the group "work". The group of a repository without an alias can change
// when a repository with the same folder name is added or removed. A group with one member stays
// a group; equal aliases give separate leaves; group names are case-sensitive. Each level lists
// the groups first, then the leaves, each part by label (case-insensitive, stable).
std::vector<RepoNode> buildRepoTree(const std::vector<RepoEntry>& entries);

// The group path of a deduplication prefix ("p/q/" gives "p/q", "" gives "").
std::string deduplicationGroup(const std::string& prefix);

// The alias of a repository after a drop onto `group` ("" is the top level): the last segment of
// the normalised `alias` under `group`. An empty alias takes the last "/" segment of `defaultName`
// (the leaf label) instead; an empty alias dropped on the top level, or with no name in
// `defaultName`, stays empty.
std::string aliasWithGroup(const std::string& alias, const std::string& defaultName, const std::string& group);

} // namespace ggui
