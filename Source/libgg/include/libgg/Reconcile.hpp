// The reconciler: makes the undo journal cover plain git (terminal, other tools) without hooks.
//
// It compares the refs now (readRefValues) with what the journal and its baseline say they should
// be, and writes the difference as one "external changes" operation (src "git"). Everything runs
// under the journal lock (Journal::Transaction), so it serialises with OperationRecorder and
// every other writer. State lives in $GIT_COMMON_DIR/gg/reconcile.json (docs: reconciler design).
#pragma once

#include "libgg/Git2.hpp"

#include <string>

namespace gg::reconcile {

struct Result {
    size_t appended = 0;   // operations written (0 or 1 for the snapshot diff)
    bool deferred = false; // another process has an operation open; it will record its own changes
    bool skipped = false;  // nothing done: managed hooks journal plain git, or the journal lock is busy
};

// One reconcile pass. Safe to call from any thread, never while this thread holds a Journal::Transaction.
Result run(git_repository* repo, std::string* error = nullptr);

// Cheap check: are the managed reference-transaction hooks installed? While they are, plain git
// is journaled by them and `run` does nothing.
bool hooksInstalled(git_repository* repo);

} // namespace gg::reconcile
