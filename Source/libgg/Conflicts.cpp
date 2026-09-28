#include "libgg/Conflicts.hpp"

#include "libgg/Markers.hpp"
#include "libgg/Thread.hpp"

#include <fstream>
#include <sstream>

namespace gg::conflicts {

namespace fs = std::filesystem;
using namespace gg::git2;

namespace {

constexpr const char* kHeader = "gg-conflict-cache 1";

std::string key(const git_oid& oid)
{
    const size_t size = oid.type == GIT_OID_SHA256 ? 32 : 20;
    return std::string(reinterpret_cast<const char*>(oid.id), size);
}

std::string hexKey(const std::string& k)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : k) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}

std::string unhexKey(const std::string& h)
{
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}

int blobSides(git_repository* repo, const git_oid& id, Cache& cache)
{
    const std::string k = key(id);
    if (auto it = cache.blobs.find(k); it != cache.blobs.end())
        return it->second;
    int sides = 0;
    git_blob* raw = nullptr;
    if (git_blob_lookup(&raw, repo, &id) == 0) {
        Blob blob(raw);
        const auto* data = static_cast<const char*>(git_blob_rawcontent(blob.get()));
        sides = contentSides(std::string_view(data, static_cast<size_t>(git_blob_rawsize(blob.get()))));
    } else {
        git_error_clear();
    }
    cache.blobs[k] = sides;
    return sides;
}

const std::vector<ConflictedFile>& scan(git_repository* repo, const git_oid& treeId, Cache& cache, const CancelToken& cancel)
{
    const std::string k = key(treeId);
    if (auto it = cache.trees.find(k); it != cache.trees.end())
        return it->second;
    throwIfCancelled(cancel);
    std::vector<ConflictedFile> result;
    git_tree* raw = nullptr;
    check(git_tree_lookup(&raw, repo, &treeId), "git_tree_lookup");
    Tree tree(raw);
    const size_t n = git_tree_entrycount(tree.get());
    for (size_t i = 0; i < n; ++i) {
        const git_tree_entry* e = git_tree_entry_byindex(tree.get(), i);
        const git_filemode_t mode = git_tree_entry_filemode(e);
        const std::string name = git_tree_entry_name(e);
        if (mode == GIT_FILEMODE_TREE) {
            for (const auto& f : scan(repo, *git_tree_entry_id(e), cache, cancel))
                result.push_back(ConflictedFile{name + "/" + f.path, f.sides});
        } else if (mode == GIT_FILEMODE_BLOB || mode == GIT_FILEMODE_BLOB_EXECUTABLE) {
            if (const int sides = blobSides(repo, *git_tree_entry_id(e), cache))
                result.push_back(ConflictedFile{name, sides});
        }
        // Symlinks and submodules never hold first-class conflicts.
    }
    cache.dirty = true;
    return cache.trees[k] = std::move(result);
}

// An attribute's kind (unspecified when it cannot be read) and, for a string value, the value.
git_attr_value_t attr(git_repository* repo, git_attr_options* opts, const std::string& path, const char* name,
    std::string* value = nullptr)
{
    const char* v = nullptr;
    if (git_attr_get_ext(&v, repo, opts, path.c_str(), name) != 0)
        git_error_clear();
    const git_attr_value_t kind = git_attr_value(v);
    if (value && kind == GIT_ATTR_VALUE_STRING)
        *value = v;
    return kind;
}

} // namespace

int contentSides(std::string_view content)
{
    if (markers::looksBinary(content))
        return 0;
    return markers::parse(content).maxSides();
}

Cache::Cache(fs::path commonDir)
{
    if (!commonDir.empty()) {
        m_file = commonDir / "gg" / "cache" / "conflicts-v1";
        load();
    }
}

Cache::~Cache()
{
    if (dirty)
        save();
}

void Cache::load()
{
    std::ifstream in(m_file, std::ios::binary);
    if (!in)
        return;
    std::string line;
    if (!std::getline(in, line) || line != kHeader)
        return; // unknown or corrupt cache: ignore, it is rebuilt
    std::vector<ConflictedFile>* current = nullptr;
    while (std::getline(in, line)) {
        if (line.rfind("T ", 0) == 0) {
            current = &trees[unhexKey(line.substr(2))];
        } else if (current && !line.empty()) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            current->push_back(ConflictedFile{line.substr(tab + 1), std::atoi(line.substr(0, tab).c_str())});
        }
    }
}

void Cache::save()
{
    if (m_file.empty())
        return;
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    const fs::path tmp = m_file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << kHeader << '\n';
        for (const auto& [k, files] : trees) {
            out << "T " << hexKey(k) << '\n';
            for (const auto& f : files)
                out << f.sides << '\t' << f.path << '\n';
        }
    }
    fs::rename(tmp, m_file, ec);
    dirty = false;
}

std::vector<ConflictedFile> scanTree(git_repository* repo, const git_oid& tree, Cache& cache, const CancelToken& cancel)
{
    assertNotUiThread("conflicts::scanTree");
    return scan(repo, tree, cache, cancel);
}

bool eligible(git_repository* repo, const git_oid* commit, const std::string& path)
{
    return ineligibleReason(repo, commit, path).empty();
}

std::string ineligibleReason(git_repository* repo, const git_oid* commit, const std::string& path)
{
    git_attr_options opts = GIT_ATTR_OPTIONS_INIT;
    if (commit) {
        opts.flags = GIT_ATTR_CHECK_INCLUDE_COMMIT | GIT_ATTR_CHECK_NO_SYSTEM | GIT_ATTR_CHECK_INDEX_ONLY;
        opts.attr_commit_id = *commit;
    } else {
        opts.flags = GIT_ATTR_CHECK_FILE_THEN_INDEX | GIT_ATTR_CHECK_NO_SYSTEM;
    }
    std::string optOut, filter;
    // "gg-conflicts=false" (the documented form, a string value) or "-gg-conflicts".
    const git_attr_value_t gg = attr(repo, &opts, path, "gg-conflicts", &optOut);
    if (gg == GIT_ATTR_VALUE_FALSE || optOut == "false")
        return "opt-out";
    if (attr(repo, &opts, path, "filter", &filter) == GIT_ATTR_VALUE_STRING && !filter.empty())
        return "filtered";
    if (attr(repo, &opts, path, "binary") == GIT_ATTR_VALUE_TRUE || attr(repo, &opts, path, "text") == GIT_ATTR_VALUE_FALSE
        || attr(repo, &opts, path, "diff") == GIT_ATTR_VALUE_FALSE)
        return "filtered";
    return {};
}

std::vector<ConflictedFile> commitConflicts(git_repository* repo, const git_oid& commit, Cache& cache,
    const CancelToken& cancel)
{
    assertNotUiThread("conflicts::commitConflicts");
    Commit c = lookupCommit(repo, commit);
    std::vector<ConflictedFile> files = scan(repo, *git_commit_tree_id(c.get()), cache, cancel);
    std::erase_if(files, [&](const ConflictedFile& f) { return !eligible(repo, &commit, f.path); });
    return files;
}

} // namespace gg::conflicts
