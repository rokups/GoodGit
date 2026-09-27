// Worker-side readers: libgit2 → immutable values (REBUILD_PLAN §3 "Reads use libgit2").
#pragma once

#include "core/Types.hpp"

#include <libgg/Cancel.hpp>
#include <libgg/Git2.hpp>

#include <functional>

namespace ggui::core {

Oid toOid(const git_oid& oid);
git_oid toGit(const Oid& oid);

SnapshotPtr readSnapshot(git_repository* repo, std::uint64_t generation, const gg::CancelToken& cancel);

// Publishes partial results through `partial` (flagged partial) on large worktrees.
StatusPtr readStatus(git_repository* repo, std::uint64_t generation, const gg::CancelToken& cancel,
    const std::function<void(StatusPtr)>& partial);

// Adds first-class conflicts of the checked-out commit (and of edited files) to `status`
// under Conflicted with firstClass=true (REBUILD_PLAN §4.10 "Checking out a conflicted commit").
void addFirstClassConflicts(git_repository* repo, StatusResult& status, void* conflictCache);

DiffPtr readDiff(git_repository* repo, const DiffQuery& query, const gg::CancelToken& cancel);
BlamePtr readBlame(git_repository* repo, const BlameQuery& query, const gg::CancelToken& cancel);
ReflogPtr readReflog(git_repository* repo, const std::string& ref);
CommitDetailsPtr readCommitDetails(git_repository* repo, const Oid& id);
RepoSummary readSummary(const std::filesystem::path& path);

// Repository state from Git's state files in the (per-worktree) git dir.
RepoState detectState(git_repository* repo, std::string& detail);

// True when `id` is reachable from any remote-tracking ref.
bool isPublished(git_repository* repo, const git_oid& id);

} // namespace ggui::core
