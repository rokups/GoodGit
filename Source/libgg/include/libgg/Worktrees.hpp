// Linked worktrees through `git worktree` (REBUILD_PLAN §4.7): the porcelain list, and the changes
// Undo makes to worktrees (add ↔ remove, lock ↔ unlock; docs/spec/undo-journal.md §5.4). Shared
// by ggui and git-gg. Every change runs `git worktree …`; nothing is written by hand.
#pragma once

#include "libgg/Journal.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gg::worktrees {

// One entry of `git worktree list --porcelain -z`.
struct Entry {
    std::filesystem::path path;
    std::string head;             // commit id ("" when unborn or bare)
    std::string branch;           // "refs/heads/<name>", "" when detached
    bool main = false;            // the first entry
    bool bare = false;
    bool detached = false;
    bool locked = false;
    std::string lockReason;
    bool prunable = false;
    std::string prunableReason;
};

std::vector<Entry> parse(const std::string& porcelainZ);
// `git worktree list --porcelain -z` run in `cwd` (any worktree of the repository).
std::vector<Entry> list(const std::filesystem::path& cwd, std::string* error = nullptr);
// Whether two paths name the same directory (the directories need not exist).
bool samePath(const std::filesystem::path& a, const std::filesystem::path& b);
const Entry* find(const std::vector<Entry>& entries, const std::filesystem::path& path);
// The journal record for `e` with `action`.
journal::WorktreeChange describe(const Entry& e, const std::string& action);

// Why `change` cannot be made now ("" when it can): Undo checks every change before it changes
// anything. `cwd` is the worktree Undo runs in; `restoredBranches` are branches Undo brings back
// before it adds worktrees.
std::string check(const std::filesystem::path& cwd, const journal::WorktreeChange& change,
    const std::vector<std::string>& restoredBranches = {});

struct Applied {
    bool ok = false;
    std::string error;
    journal::WorktreeChange done; // what was done, for the journal
};
// Makes `change` with `git worktree add|remove|lock|unlock` (a locked worktree is unlocked
// before it is removed, and locked again when the removal fails).
Applied apply(const std::filesystem::path& cwd, const journal::WorktreeChange& change);

} // namespace gg::worktrees
