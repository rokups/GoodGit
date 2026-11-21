// Undo / redo through the journal (product spec §5 U1, §4.12). Shared by ggui and git-gg.
#pragma once

#include "libgg/Git2.hpp"
#include "libgg/Journal.hpp"

#include <string>
#include <vector>

namespace gg {

struct UndoResult {
    bool ok = false;
    bool nothing = false;      // nothing to undo / redo
    bool wouldLoseData = false; // the working tree has changes the restore would overwrite
    std::string error;
    std::string label;         // label of the recorded undo/redo operation
    std::string target;        // id of the undone operation
};

// What a restore would do, from the same plan undo() runs (see `preview` below).
struct UndoPreview {
    struct Move { std::string ref, from, to; }; // from = the value now, to = the value after (the null id: none)
    std::vector<Move> refs;                     // keep refs left out: they are derived state
    bool tree = false;                          // the index or the working tree changes too
    std::vector<journal::WorktreeChange> worktrees; // linked worktrees the restore adds or removes
};

// Undoes (or redoes) the newest applicable operation for this worktree. `targetId` restores a
// specific operation instead (Operations panel "Restore"). With `preview`, the restore does not
// run, but the reconcile pass before the plan can still append journal records for plain git
// commands, like a refresh. `preview` gets the refs and the tree change of the plan, or `error` says why the plan fails.
UndoResult undo(git_repository* repo, bool redo, const std::string& src, const std::string& targetId = {},
    UndoPreview* preview = nullptr);

} // namespace gg
