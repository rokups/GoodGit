#pragma once
// Leftovers of the old gg tool.

#include <git2.h>

#include <string>

namespace gg {

// Deletes every ref under refs/gg/ (direct or symbolic; a symbolic ref itself, never its target)
// with one `git update-ref --no-deref --stdin`. Silent and not journaled: it runs outside any
// operation, with GG_OPERATION removed and GG_NO_JOURNAL set in git's environment (so a managed
// reference-transaction hook does not journal it either). Cheap when there is nothing (libgit2 only, no
// process). Returns whether anything was deleted; a failure is reported in `error` (when given)
// and returns false.
bool removeLegacyGgRefs(git_repository* repo, std::string* error = nullptr, int* deleted = nullptr);

} // namespace gg
