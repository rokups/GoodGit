#include "shell/RepoTree.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

namespace ggui {

namespace {

// The "/" segments of a normalised alias.
std::vector<std::string> splitSegments(const std::string& alias)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= alias.size()) {
        size_t slash = alias.find('/', start);
        if (slash == std::string::npos)
            slash = alias.size();
        out.push_back(alias.substr(start, slash - start));
        start = slash + 1;
    }
    return out;
}

std::string lowerCase(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void sortLevel(std::vector<RepoNode>& level)
{
    for (auto& node : level)
        sortLevel(node.children);
    std::stable_sort(level.begin(), level.end(), [](const RepoNode& a, const RepoNode& b) {
        if (a.isGroup() != b.isGroup())
            return a.isGroup();
        return lowerCase(a.label) < lowerCase(b.label);
    });
}

} // namespace

std::vector<RepoNode> buildRepoTree(const std::vector<RepoEntry>& entries)
{
    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (const auto& e : entries)
        paths.push_back(e.path);
    const auto names = uniqueRecentNames(paths);

    std::vector<RepoNode> root;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string alias = normalizeAlias(entries[i].alias);
        std::vector<RepoNode>* level = &root;
        RepoNode leaf;
        leaf.path = entries[i].path;
        if (alias.empty()) {
            leaf.label = names[i].text();
        } else {
            auto segments = splitSegments(alias);
            leaf.label = segments.back();
            segments.pop_back();
            std::string groupPath;
            for (const auto& segment : segments) {
                groupPath += (groupPath.empty() ? "" : "/") + segment;
                auto it = std::find_if(level->begin(), level->end(),
                    [&](const RepoNode& n) { return n.isGroup() && n.label == segment; });
                if (it == level->end()) {
                    RepoNode group;
                    group.label = segment;
                    group.group = groupPath;
                    level->push_back(std::move(group));
                    it = level->end() - 1;
                }
                level = &it->children;
            }
        }
        level->push_back(std::move(leaf));
    }
    sortLevel(root);
    return root;
}

std::string aliasWithGroup(const std::string& alias, const std::string& defaultName, const std::string& group)
{
    const std::string target = normalizeAlias(group);
    std::string name = splitSegments(normalizeAlias(alias)).back();
    if (name.empty()) {
        if (target.empty())
            return {};
        name = splitSegments(normalizeAlias(defaultName)).back();
        if (name.empty())
            return {};
    }
    return normalizeAlias(target.empty() ? name : target + "/" + name);
}

} // namespace ggui
