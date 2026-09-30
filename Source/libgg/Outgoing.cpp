#include "libgg/Outgoing.hpp"

#include "libgg/Conflicts.hpp"
#include "libgg/Git2.hpp"
#include "libgg/Markers.hpp"

#include <optional>
#include <string_view>

namespace gg::outgoing {

namespace fs = std::filesystem;
using namespace gg::git2;

std::vector<ConflictedCommit> conflictedOutgoing(const fs::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid)
{
    std::vector<ConflictedCommit> out;
    Repository repo = openRepository(repoDir);
    auto local = fromHex(localOid);
    if (!local)
        return out;
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo.get()), "git_revwalk_new");
    Revwalk walk(raw);
    check(git_revwalk_push(walk.get(), &*local), "git_revwalk_push");
    if (auto r = fromHex(remoteOid); r && !isZero(*r))
        if (git_revwalk_hide(walk.get(), &*r) != 0)
            git_error_clear();
    if (!remote.empty())
        if (git_revwalk_hide_glob(walk.get(), ("refs/remotes/" + remote + "/*").c_str()) != 0)
            git_error_clear();
    conflicts::Cache cache{fs::path(git_repository_commondir(repo.get()))};
    git_oid oid;
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        const auto files = conflicts::commitConflicts(repo.get(), oid, cache);
        if (files.empty())
            continue;
        ConflictedCommit c;
        c.id = toHex(oid);
        Commit commit = lookupCommit(repo.get(), oid);
        const char* summary = git_commit_summary(commit.get());
        c.subject = summary ? summary : "";
        for (const auto& f : files)
            c.files.push_back(f.path);
        out.push_back(std::move(c));
    }
    git_error_clear();
    return out;
}

// Blob text at `path` in `tree`, or nullopt when absent or not a regular blob.
static std::optional<std::string> textAtPath(git_repository* repo, const git_tree* tree, const std::string& path)
{
    if (!tree)
        return std::nullopt;
    git_tree_entry* raw = nullptr;
    if (git_tree_entry_bypath(&raw, tree, path.c_str()) != 0) {
        git_error_clear();
        return std::nullopt;
    }
    TreeEntry entry(raw);
    if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB)
        return std::nullopt;
    git_blob* rawBlob = nullptr;
    if (git_blob_lookup(&rawBlob, repo, git_tree_entry_id(entry.get())) != 0) {
        git_error_clear();
        return std::nullopt;
    }
    Blob blob(rawBlob);
    return blobContent(blob.get());
}

std::vector<BrokenCommit> brokenOutgoing(const fs::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid)
{
    std::vector<BrokenCommit> out;
    Repository repo = openRepository(repoDir);
    auto local = fromHex(localOid);
    if (!local)
        return out;
    git_revwalk* raw = nullptr;
    check(git_revwalk_new(&raw, repo.get()), "git_revwalk_new");
    Revwalk walk(raw);
    check(git_revwalk_push(walk.get(), &*local), "git_revwalk_push");
    if (auto r = fromHex(remoteOid); r && !isZero(*r))
        if (git_revwalk_hide(walk.get(), &*r) != 0)
            git_error_clear();
    if (!remote.empty())
        if (git_revwalk_hide_glob(walk.get(), ("refs/remotes/" + remote + "/*").c_str()) != 0)
            git_error_clear();
    conflicts::Cache cache{fs::path(git_repository_commondir(repo.get()))};
    git_oid oid;
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        Commit commit = lookupCommit(repo.get(), oid);
        if (git_commit_parentcount(commit.get()) == 0)
            continue; // no parent to have held a conflict this commit could have broken
        const git_oid parentId = *git_commit_parent_id(commit.get(), 0);
        const auto parentFiles = conflicts::commitConflicts(repo.get(), parentId, cache);
        if (parentFiles.empty())
            continue;
        Commit parentCommit = lookupCommit(repo.get(), parentId);
        Tree parentTree = commitTree(parentCommit.get());
        Tree tree = commitTree(commit.get());
        BrokenCommit bc;
        for (const auto& f : parentFiles) {
            const auto before = textAtPath(repo.get(), parentTree.get(), f.path);
            const auto after = textAtPath(repo.get(), tree.get(), f.path);
            if (!before || !after)
                continue; // deleted or no longer a blob: not "left broken markers"
            auto broken = markers::brokenMarkers(*before, *after);
            if (broken.empty())
                continue;
            bc.files.push_back(BrokenFile{f.path, std::move(broken)});
        }
        if (bc.files.empty())
            continue;
        bc.id = toHex(oid);
        const char* summary = git_commit_summary(commit.get());
        bc.subject = summary ? summary : "";
        out.push_back(std::move(bc));
    }
    git_error_clear();
    return out;
}

std::vector<StagedWarning> stagedConflictWarnings(git_repository* repo)
{
    std::vector<StagedWarning> out;
    conflicts::Cache cache{fs::path(git_repository_commondir(repo))};
    git_oid head;
    const bool hasHead = git_reference_name_to_id(&head, repo, "HEAD") == 0;
    if (!hasHead)
        git_error_clear();
    git_index* rawIndex = nullptr;
    if (git_repository_index(&rawIndex, repo) != 0) {
        git_error_clear();
        return out;
    }
    Index index(rawIndex);
    git_index_read(index.get(), 0); // the repository handle may be long-lived: pick up changes made by other processes

    Commit headCommit;
    Tree headTree;
    if (hasHead) {
        headCommit = lookupCommit(repo, head);
        headTree = commitTree(headCommit.get());
    }

    // New/modified staged files that are themselves first-class conflicts.
    git_diff* rawDiff = nullptr;
    git_diff_options diffOpts = GIT_DIFF_OPTIONS_INIT;
    if (git_diff_tree_to_index(&rawDiff, repo, headTree.get(), index.get(), &diffOpts) == 0) {
        Diff diff(rawDiff);
        const size_t n = git_diff_num_deltas(diff.get());
        for (size_t i = 0; i < n; ++i) {
            const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
            if (d->status == GIT_DELTA_DELETED || d->status == GIT_DELTA_TYPECHANGE)
                continue;
            const std::string path = d->new_file.path;
            if (!conflicts::eligible(repo, nullptr, path))
                continue;
            git_blob* rawBlob = nullptr;
            if (git_blob_lookup(&rawBlob, repo, &d->new_file.id) != 0) {
                git_error_clear();
                continue;
            }
            Blob blob(rawBlob);
            const std::string_view text(static_cast<const char*>(git_blob_rawcontent(blob.get())),
                static_cast<size_t>(git_blob_rawsize(blob.get())));
            const int sides = conflicts::contentSides(text);
            if (!sides)
                continue;
            StagedWarning w;
            w.path = path;
            w.kind = StagedWarning::Kind::Conflict;
            w.sides = sides;
            out.push_back(std::move(w));
        }
    } else {
        git_error_clear();
    }

    if (!hasHead)
        return out;
    // A staged file whose HEAD version held a first-class conflict and whose staged edit broke
    // the region instead of resolving it.
    const auto headFiles = conflicts::commitConflicts(repo, head, cache);
    for (const auto& f : headFiles) {
        const git_index_entry* entry = git_index_get_bypath(index.get(), f.path.c_str(), 0);
        if (!entry)
            continue; // no longer in the index (deleted, renamed away, ...)
        git_tree_entry* rawHeadEntry = nullptr;
        if (git_tree_entry_bypath(&rawHeadEntry, headTree.get(), f.path.c_str()) != 0) {
            git_error_clear();
            continue;
        }
        TreeEntry headEntry(rawHeadEntry);
        git_blob* rawHeadBlob = nullptr;
        git_blob* rawStagedBlob = nullptr;
        if (git_blob_lookup(&rawHeadBlob, repo, git_tree_entry_id(headEntry.get())) != 0
            || git_blob_lookup(&rawStagedBlob, repo, &entry->id) != 0) {
            git_error_clear();
            if (rawHeadBlob)
                git_blob_free(rawHeadBlob);
            continue;
        }
        Blob headBlob(rawHeadBlob), stagedBlob(rawStagedBlob);
        const std::string_view headText(static_cast<const char*>(git_blob_rawcontent(headBlob.get())),
            static_cast<size_t>(git_blob_rawsize(headBlob.get())));
        const std::string_view stagedText(static_cast<const char*>(git_blob_rawcontent(stagedBlob.get())),
            static_cast<size_t>(git_blob_rawsize(stagedBlob.get())));
        auto broken = markers::brokenMarkers(headText, stagedText);
        if (broken.empty())
            continue;
        StagedWarning w;
        w.path = f.path;
        w.kind = StagedWarning::Kind::BrokenMarkers;
        w.lines = std::move(broken);
        out.push_back(std::move(w));
    }
    return out;
}

} // namespace gg::outgoing
