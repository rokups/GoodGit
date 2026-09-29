// Undo / redo through the journal (product spec §5 U1, §4.12). Shared by ggui and git-gg.
#pragma once

#include "libgg/Git2.hpp"

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

// Undoes (or redoes) the newest applicable operation for this worktree. `targetId` restores a
// specific operation instead (Operations panel "Restore").
UndoResult undo(git_repository* repo, bool redo, const std::string& src, const std::string& targetId = {});

} // namespace gg
