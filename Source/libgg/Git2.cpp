#include "libgg/Git2.hpp"

#include "libgg/Thread.hpp"

#include <mutex>

namespace gg::git2 {

std::string lastErrorMessage(std::string_view fallback)
{
    const git_error* e = git_error_last();
    if (e && e->message && *e->message)
        return e->message;
    return std::string(fallback);
}

int check(int rc, std::string_view what)
{
    if (rc >= 0)
        return rc;
    const git_error* e = git_error_last();
    const int klass = e ? e->klass : 0;
    throw Error(rc, klass, lastErrorMessage(what));
}

void initLibrary()
{
    static std::once_flag once;
    std::call_once(once, [] {
        git_libgit2_init();
        // Workers open their own handles; keep libgit2's object cache per repository.
        git_libgit2_opts(GIT_OPT_ENABLE_STRICT_HASH_VERIFICATION, 0);
        // Repositories owned by other users are still readable (git decides on writes).
        git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
    });
}

void resetConfigSearchPaths()
{
    initLibrary();
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, nullptr);
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_XDG, nullptr);
}

void setSystemConfigPath(const std::filesystem::path& dir)
{
    initLibrary();
    const std::string s = dir.string();
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, s.c_str());
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_PROGRAMDATA, s.c_str());
}

Repository openRepository(const std::filesystem::path& path)
{
    assertNotUiThread("git2::openRepository");
    initLibrary();
    git_repository* raw = nullptr;
    const std::string p = path.string();
    check(git_repository_open_ext(&raw, p.c_str(), 0, nullptr), "git_repository_open_ext");
    return Repository(raw);
}

Repository openRepositoryExact(const std::filesystem::path& path)
{
    assertNotUiThread("git2::openRepositoryExact");
    initLibrary();
    git_repository* raw = nullptr;
    const std::string p = path.string();
    check(git_repository_open_ext(&raw, p.c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr), "git_repository_open_ext");
    return Repository(raw);
}

std::string toHex(const git_oid& oid)
{
    char buf[GIT_OID_MAX_HEXSIZE + 1] = {};
    git_oid_tostr(buf, sizeof(buf), &oid);
    return buf;
}

std::optional<git_oid> fromHex(std::string_view hex)
{
    git_oid oid;
    git_oid_t type;
    if (hex.size() == GIT_OID_SHA1_HEXSIZE)
        type = GIT_OID_SHA1;
    else if (hex.size() == GIT_OID_SHA256_HEXSIZE)
        type = GIT_OID_SHA256;
    else
        return std::nullopt;
    const std::string s(hex);
    if (git_oid_fromstr(&oid, s.c_str(), type) < 0)
        return std::nullopt;
    return oid;
}

git_oid_t oidType(git_repository* repo) { return git_repository_oid_type(repo); }

size_t hexSize(git_oid_t type) { return type == GIT_OID_SHA256 ? GIT_OID_SHA256_HEXSIZE : GIT_OID_SHA1_HEXSIZE; }

bool isZero(const git_oid& oid) { return git_oid_is_zero(&oid) != 0; }

Commit lookupCommit(git_repository* repo, const git_oid& oid)
{
    assertNotUiThread("git2::lookupCommit");
    git_commit* raw = nullptr;
    check(git_commit_lookup(&raw, repo, &oid), "git_commit_lookup");
    return Commit(raw);
}

Tree lookupTree(git_repository* repo, const git_oid& oid)
{
    assertNotUiThread("git2::lookupTree");
    git_tree* raw = nullptr;
    check(git_tree_lookup(&raw, repo, &oid), "git_tree_lookup");
    return Tree(raw);
}

Blob lookupBlob(git_repository* repo, const git_oid& oid)
{
    assertNotUiThread("git2::lookupBlob");
    git_blob* raw = nullptr;
    check(git_blob_lookup(&raw, repo, &oid), "git_blob_lookup");
    return Blob(raw);
}

Object revparse(git_repository* repo, const std::string& spec)
{
    assertNotUiThread("git2::revparse");
    git_object* raw = nullptr;
    check(git_revparse_single(&raw, repo, spec.c_str()), "git_revparse_single");
    return Object(raw);
}

std::optional<git_oid> resolve(git_repository* repo, const std::string& spec)
{
    assertNotUiThread("git2::resolve");
    git_object* raw = nullptr;
    if (git_revparse_single(&raw, repo, spec.c_str()) < 0)
        return std::nullopt;
    Object obj(raw);
    git_object* peeled = nullptr;
    if (git_object_peel(&peeled, obj.get(), GIT_OBJECT_COMMIT) < 0)
        return *git_object_id(obj.get());
    Object p(peeled);
    return *git_object_id(p.get());
}

Tree commitTree(const git_commit* commit)
{
    assertNotUiThread("git2::commitTree");
    git_tree* raw = nullptr;
    check(git_commit_tree(&raw, commit), "git_commit_tree");
    return Tree(raw);
}

std::string commitMessage(const git_commit* commit)
{
    const char* m = git_commit_message(commit);
    return m ? m : "";
}

std::string blobContent(const git_blob* blob)
{
    return std::string(static_cast<const char*>(git_blob_rawcontent(blob)), static_cast<size_t>(git_blob_rawsize(blob)));
}

Config repositoryConfig(git_repository* repo)
{
    assertNotUiThread("git2::repositoryConfig");
    git_config* raw = nullptr;
    check(git_repository_config_snapshot(&raw, repo), "git_repository_config_snapshot");
    return Config(raw);
}

std::map<std::string, std::string> branchesInOtherWorktrees(git_repository* repo)
{
    assertNotUiThread("git2::branchesInOtherWorktrees");
    std::map<std::string, std::string> out;
    auto add = [&](git_repository* r, const std::string& name) {
        git_reference* raw = nullptr;
        if (git_reference_lookup(&raw, r, "HEAD") != 0)
            return;
        Reference head(raw);
        if (git_reference_type(head.get()) == GIT_REFERENCE_SYMBOLIC)
            out[git_reference_symbolic_target(head.get())] = name;
    };
    // A linked worktree's git dir is <common>/worktrees/<name>/.
    std::string self;
    if (git_repository_is_worktree(repo) == 1) {
        self = std::filesystem::path(git_repository_path(repo)).parent_path().filename().string();
        git_repository* rawMain = nullptr;
        if (git_repository_open(&rawMain, git_repository_commondir(repo)) == 0) {
            Repository mainRepo(rawMain);
            if (git_repository_is_bare(mainRepo.get()) == 0)
                add(mainRepo.get(), "main");
        }
    }
    StrArray names;
    git_worktree_list(&names.arr, repo); // on failure the list stays empty
    for (size_t i = 0; i < names.arr.count; ++i) {
        const std::string name = names.arr.strings[i];
        git_worktree* rawWt = nullptr;
        git_repository* rawRepo = nullptr;
        if (name == self || git_worktree_lookup(&rawWt, repo, name.c_str()) != 0)
            continue;
        Worktree wt(rawWt);
        if (git_repository_open_from_worktree(&rawRepo, wt.get()) != 0)
            continue; // its directory is gone (prunable)
        Repository wtRepo(rawRepo);
        add(wtRepo.get(), name);
    }
    git_error_clear();
    return out;
}

std::optional<std::string> configString(git_config* cfg, const char* name)
{
    const char* value = nullptr;
    if (git_config_get_string(&value, cfg, name) < 0 || !value)
        return std::nullopt;
    return std::string(value);
}

std::optional<bool> configBool(git_config* cfg, const char* name)
{
    int value = 0;
    if (git_config_get_bool(&value, cfg, name) < 0)
        return std::nullopt;
    return value != 0;
}

} // namespace gg::git2
