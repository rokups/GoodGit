// The managed git hooks that older versions installed (config-defined hook.ggui-<name> entries,
// or wrapper scripts that chain to <name>.gg-previous, both running $GIT_COMMON_DIR/gg/hooks/run).
// They are gone: Undo reads plain git from the reflogs. This module only finds and removes what an
// old install left behind, restoring the user's previous hooks byte for byte. `git gg hook <name>`
// stays as a silent no-op for the stale copies that still call it.
#pragma once

#include <git2.h>

#include <string>
#include <vector>

namespace gg::hooks {

// The hooks the old installer managed.
const std::vector<std::string>& managedHooks();

// Whether any trace of a managed install is left in the repository (local config entries, wrapper
// scripts, the runner). Cheap: libgit2 and file checks only, no process.
bool installed(git_repository* repo);

// Removes every trace: the hook.ggui-* config sections (spawns git, only when present), the wrapper
// scripts (putting <name>.gg-previous back), and the runner. Returns false, with `error` set, when
// the repository's paths cannot be resolved.
bool uninstall(git_repository* repo, std::string& error);

} // namespace gg::hooks
