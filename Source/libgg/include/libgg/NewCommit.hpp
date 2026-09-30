// `new`: an empty commit on the given parents (product spec §4.3 "New commit", §6 git gg new).
// Shared by ggui and git-gg. The commit object is built with libgit2; the ref move goes through
// git (update-ref / switch) so hooks and reflogs behave as for any git command.
#pragma once

#include "libgg/Git2.hpp"

#include <string>
#include <vector>

namespace gg {

struct NewCommitOptions {
    std::vector<std::string> parents; // revisions; empty = HEAD (none for an unborn HEAD)
    std::string message;
    bool detach = false;              // leave branches alone; HEAD detaches at the new commit
    std::string branch;               // local branch at the first parent to advance and switch to
                                      // (empty = HEAD's branch, when the first parent is HEAD)
};

struct NewCommitResult {
    bool ok = false;
    std::string error;
    std::string commit;               // new commit id
    std::string movedBranch;          // branch advanced ("" when HEAD was detached)
};

// Creates the commit and moves HEAD:
//  * HEAD attached, first parent == HEAD, not --detach → the branch advances (HEAD follows);
//  * `branch` set (another branch at the first parent) → it advances and HEAD switches to it;
//  * otherwise HEAD detaches at the new commit (git switch --detach, which refuses to
//    overwrite local changes).
NewCommitResult newCommit(git_repository* repo, const NewCommitOptions& options);

} // namespace gg
