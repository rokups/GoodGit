// The reconciler: makes the undo journal cover plain git (terminal, other tools) without hooks.
//
// It compares the refs now (readRefValues) with what the journal and its baseline say they should
// be. Plain git commands are read from HEAD's reflog past a cursor (reconcile.json "cursors") and
// written as one operation each (src "git", cmd = the reflog message); what is left is written as
// one "external changes" operation. Everything runs
// under the journal lock (Journal::Transaction), so it serialises with OperationRecorder and
// every other writer. State lives in $GIT_COMMON_DIR/gg/reconcile.json (docs: reconciler design).
#pragma once

#include "libgg/Git2.hpp"

#include <string>

namespace gg::reconcile {

struct Result {
    size_t appended = 0;   // operations written
    bool deferred = false; // another process has an operation open; it will record its own changes
    bool skipped = false;  // nothing done: the journal lock is busy or unreadable
};

// One reconcile pass. Safe to call from any thread, never while this thread holds a Journal::Transaction.
Result run(git_repository* repo, std::string* error = nullptr);

// Moves this worktree's HEAD reflog cursor to the reflog's tip, so the entries of a git command
// that ggui ran itself (and journaled through OperationRecorder) are never journaled again. Does
// nothing before the first reconcile pass. Called by OperationRecorder::finish, after the
// operation's refs are recorded and before it ends.
bool advanceCursor(git_repository* repo, std::string* error = nullptr);

// The action word of a reflog message: "commit (amend): x" -> "commit", "merge feat: x" -> "merge",
// "pull --rebase origin (start): x" -> "pull". What Undo uses to tell commands that update the
// working tree from the ones that do not.
std::string reflogActionWord(const std::string& message);

} // namespace gg::reconcile
