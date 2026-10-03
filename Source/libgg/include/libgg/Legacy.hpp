#pragma once
// Leftovers of the old gg tool and of the managed git hooks.

#include <git2.h>

#include <string>

namespace gg {

// Deletes every ref under refs/gg/ except the keep refs under refs/gg/keep/ (Keep.hpp); direct or
// symbolic, a symbolic ref itself, never its target; with one `git update-ref --no-deref --stdin`.
// Silent and not journaled: it runs outside any operation, and the reconciler does not read what it
// deletes. Cheap when there is nothing (libgit2 only, no process). Returns whether anything was
// deleted; a failure is reported in `error` (when given) and returns false.
bool removeLegacyGgRefs(git_repository* repo, std::string* error = nullptr, int* deleted = nullptr);

struct LegacyMigration {
    bool hooksRemoved = false; // the managed hooks of older versions were uninstalled
    int refsDeleted = 0;       // refs/gg/* refs deleted (not the keep refs)
    std::string error;         // the first failure, empty when none
};

// The automatic, silent migration from older versions, run when a repository is opened: uninstalls
// the managed git hooks (restoring the user's own hooks byte for byte), then deletes the
// refs/gg/* refs other than the keep refs (refs/gg/keep/*). Cheap when there is nothing to do (libgit2 and file checks only, no process).
LegacyMigration migrateLegacy(git_repository* repo);

} // namespace gg
