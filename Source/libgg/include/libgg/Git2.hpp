// Small libgit2 helpers (REBUILD_PLAN §3): RAII handles, error checking, OID conversion.
//
// These are conveniences, not a wrapper API: callers use libgit2 directly and pass
// git_repository* around. Every helper that touches a repository asserts that it does not
// run on the UI thread.
#pragma once

#include <git2.h>
#include <git2/sys/errors.h>

#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gg::git2 {

// ---- Errors --------------------------------------------------------------------------------

class Error : public std::runtime_error {
public:
    Error(int code, int klass, const std::string& message)
        : std::runtime_error(message), m_code(code), m_klass(klass) { }
    int code() const { return m_code; }
    int klass() const { return m_klass; }

private:
    int m_code;
    int m_klass;
};

// Throws Error carrying libgit2's last error message when rc < 0. Returns rc otherwise.
int check(int rc, std::string_view what);
// libgit2's last error message (or a fallback).
std::string lastErrorMessage(std::string_view fallback = "unknown libgit2 error");

// ---- Library lifetime ----------------------------------------------------------------------

// Initializes libgit2 once per process (thread safe). Call before any other libgit2 use.
void initLibrary();
// Re-reads HOME/XDG-based config search paths (tests change them between scenarios).
void resetConfigSearchPaths();
// Restricts the system config search path (tests: isolate from /etc/gitconfig).
void setSystemConfigPath(const std::filesystem::path& dir);

// ---- RAII handles --------------------------------------------------------------------------

template <typename T, void (*Free)(T*)>
struct Deleter {
    void operator()(T* p) const noexcept
    {
        if (p)
            Free(p);
    }
};

template <typename T, void (*Free)(T*)>
using Handle = std::unique_ptr<T, Deleter<T, Free>>;

using Repository = Handle<git_repository, git_repository_free>;
using Object = Handle<git_object, git_object_free>;
using Commit = Handle<git_commit, git_commit_free>;
using Tree = Handle<git_tree, git_tree_free>;
using TreeEntry = Handle<git_tree_entry, git_tree_entry_free>;
using TreeBuilder = Handle<git_treebuilder, git_treebuilder_free>;
using Blob = Handle<git_blob, git_blob_free>;
using Tag = Handle<git_tag, git_tag_free>;
using Reference = Handle<git_reference, git_reference_free>;
using ReferenceIterator = Handle<git_reference_iterator, git_reference_iterator_free>;
using Index = Handle<git_index, git_index_free>;
using Diff = Handle<git_diff, git_diff_free>;
using Patch = Handle<git_patch, git_patch_free>;
using Config = Handle<git_config, git_config_free>;
using Revwalk = Handle<git_revwalk, git_revwalk_free>;
using Signature = Handle<git_signature, git_signature_free>;
using Blame = Handle<git_blame, git_blame_free>;
using Reflog = Handle<git_reflog, git_reflog_free>;
using Remote = Handle<git_remote, git_remote_free>;
using StatusList = Handle<git_status_list, git_status_list_free>;
using Worktree = Handle<git_worktree, git_worktree_free>;
using Odb = Handle<git_odb, git_odb_free>;
using Mailmap = Handle<git_mailmap, git_mailmap_free>;

// Owning git_buf.
struct Buf {
    git_buf buf = GIT_BUF_INIT;
    Buf() = default;
    Buf(const Buf&) = delete;
    Buf& operator=(const Buf&) = delete;
    ~Buf() { git_buf_dispose(&buf); }
    std::string str() const { return buf.ptr ? std::string(buf.ptr, buf.size) : std::string(); }
};

// Owning git_strarray.
struct StrArray {
    git_strarray arr = {nullptr, 0};
    StrArray() = default;
    StrArray(const StrArray&) = delete;
    StrArray& operator=(const StrArray&) = delete;
    ~StrArray() { git_strarray_dispose(&arr); }
};

// ---- Opening -------------------------------------------------------------------------------

// Opens the repository containing `path` (worktree, .git dir or bare). Throws Error.
Repository openRepository(const std::filesystem::path& path);
// Opens without searching upwards.
Repository openRepositoryExact(const std::filesystem::path& path);

// ---- OIDs ----------------------------------------------------------------------------------

std::string toHex(const git_oid& oid);
// Parses a full hex id (40 or 64 characters).
std::optional<git_oid> fromHex(std::string_view hex);
git_oid_t oidType(git_repository* repo);
size_t hexSize(git_oid_t type);
bool isZero(const git_oid& oid);

// ---- Small lookups -------------------------------------------------------------------------

Commit lookupCommit(git_repository* repo, const git_oid& oid);
Tree lookupTree(git_repository* repo, const git_oid& oid);
Blob lookupBlob(git_repository* repo, const git_oid& oid);
Object revparse(git_repository* repo, const std::string& spec);
std::optional<git_oid> resolve(git_repository* repo, const std::string& spec);
Tree commitTree(const git_commit* commit);
std::string commitMessage(const git_commit* commit);
std::string blobContent(const git_blob* blob);
Config repositoryConfig(git_repository* repo);
std::optional<std::string> configString(git_config* cfg, const char* name);
std::optional<bool> configBool(git_config* cfg, const char* name);

// What HEAD points at ("refs/heads/x", also when that branch has no commit yet), "" when detached.
// (Every opened repository has a HEAD.)
std::string headTarget(git_repository* repo);

// Branches checked out in the other worktrees of `repo`'s repository ("refs/heads/x" → worktree
// name, "main" for the main worktree): the linked ones, and the main one when `repo` is linked.
std::map<std::string, std::string> branchesInOtherWorktrees(git_repository* repo);

// Iterates references; `fn(git_reference*)` returns false to stop.
template <typename Fn>
void forEachReference(git_repository* repo, Fn&& fn)
{
    git_reference_iterator* raw = nullptr;
    check(git_reference_iterator_new(&raw, repo), "git_reference_iterator_new");
    ReferenceIterator it(raw);
    git_reference* ref = nullptr;
    while (git_reference_next(&ref, it.get()) == 0) {
        Reference owned(ref);
        if (!fn(owned.get()))
            break;
    }
}

} // namespace gg::git2
