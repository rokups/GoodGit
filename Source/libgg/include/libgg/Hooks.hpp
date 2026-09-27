// Managed hooks (REBUILD_PLAN §4.12 B, H1): install/uninstall/status and the hook entry points
// run by git through `git gg hook <name>`.
//
// Installation writes one runner script, $GIT_COMMON_DIR/gg/hooks/run, which does nothing when
// git-gg is missing (pre-push warns). It is registered either as config-defined hooks
// (hook.ggui-<name>.command/.event in the repository config) when git supports them, or as small
// wrapper scripts in the active hooks directory that run it and then chain to the previous hook.
// Uninstall restores the previous state byte for byte.
#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace gg::hooks {

const std::vector<std::string>& managedHooks();

enum class Mode { Config, Wrapper };

struct Status {
    bool installed = false;
    bool partial = false;          // some but not all hooks present
    Mode mode = Mode::Config;
    std::filesystem::path hooksDir;
    std::vector<std::string> present;
    bool gitGgFound = false;       // git-gg reachable through PATH
};

// Whether the installed git runs config-defined hooks (probed once per process).
bool configHooksSupported();
// Forces a mode for tests of the wrapper fallback (empty = probe).
void forceMode(const std::string& mode);

Status status(const std::filesystem::path& repoDir);
bool install(const std::filesystem::path& repoDir, std::string& error);
bool uninstall(const std::filesystem::path& repoDir, std::string& error);

// Entry point for `git gg hook <name> [args…]`, run by git from inside a repository.
// Returns the hook's exit status.
int runHook(const std::string& name, const std::vector<std::string>& args, std::istream& in, std::ostream& out,
    std::ostream& err);

// Commits reachable from `local` but not from `remote`'s tracking refs that hold first-class
// conflicts, with their files ("<id> <path>" lines). Used by pre-push and by ggui's push.
struct ConflictedCommit {
    std::string id;
    std::string subject;
    std::vector<std::string> files;
};
std::vector<ConflictedCommit> conflictedOutgoing(const std::filesystem::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid);

} // namespace gg::hooks
