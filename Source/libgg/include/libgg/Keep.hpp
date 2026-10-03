// Keep refs: the commits made on a detached HEAD stay alive (product spec §5, decision K2).
//
// refs/gg/keep/<full hex commit id> is a direct ref to that same commit; it is the only private ref
// ggui writes. "Well-formed" is a direct ref named for its target, which is a commit.
//
// Invariant K (maintain): with A the commits the refs/heads/*, refs/remotes/* and refs/tags/* point
// at (tags peeled; what is not a commit is ignored), H the HEAD of every worktree that is detached,
// not bare and not in the middle of a native operation (merge, rebase, cherry-pick, ...), and the
// candidates the targets of the existing keep refs, H and the caller's `extra` ids, the keep refs
// are those of the candidates that are not reachable from A and not an ancestor of another such
// candidate (tips only). A branch or tag that reaches a commit is its graduation: its keep ref goes.
#pragma once

#include "libgg/Git2.hpp"
#include "libgg/Journal.hpp"

#include <string>
#include <vector>

namespace gg::keep {

inline constexpr const char* kPrefix = "refs/gg/keep/";

// Whether `name` is under refs/gg/keep/.
bool isKeepRef(const std::string& name);
// "refs/gg/keep/<id>".
std::string refName(const std::string& id);

// The commits kept: the targets of the well-formed keep refs, sorted by ref name. Empty on failure.
std::vector<std::string> read(git_repository* repo);

// Brings the keep refs to invariant K with one `git update-ref --no-deref --stdin` (not run when
// nothing changes): deletes every keep ref that is not well-formed or not wanted (symbolic ones
// included), creates the missing ones. `extra` are commit ids to keep when nothing else anchors
// them. `changes` (when given) lists what was done (deletions, then creations), for the journal: the old and
// new value of each ref, the null id for a created or deleted one. Returns false on a failure,
// reported in `error` (when given); nothing is changed then, with one exception: when a ref to
// delete lies below a name to create (refs/gg/keep/<id>/x), the deletions run first, as a
// transaction of their own, and a failure of the creations leaves them done. `changes` lists them
// then (it is empty after every other failure), and the caller still has to journal them.
bool maintain(git_repository* repo, const std::vector<std::string>& extra = {},
    std::vector<journal::RefChange>* changes = nullptr, std::string* error = nullptr);

} // namespace gg::keep
