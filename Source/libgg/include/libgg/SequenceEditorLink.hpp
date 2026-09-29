// ggui's todo editor as Git's sequence.editor (product spec §4.13, §6): the link between
// `git gg sequence-editor FILE`, run by a plain `git rebase -i`, and a ggui that shows the list.
//
// Every running ggui listens on a loopback port (token-protected, like the askpass bridge) and
// registers itself in a per-user directory: one file per process with the port, the token and the
// git dir of the repository it has open. git-gg finds the instance whose git dir is the one of
// FILE (FILE is <git dir>/rebase-merge/git-rebase-todo) and hands the file over:
//
//   git-gg → ggui   <token>\n  sequence-editor\n  <git dir>\n  <FILE>\n
//   ggui → git-gg   OK\n                   the editor will show the list
//                   NO\n                   not this repository (git-gg tries another instance)
//   later           SAVE <bytes>\n<list>   the saved list: git-gg writes it to FILE
//                   CANCEL\n               cancelled or closed: git's "empty todo aborts" for a
//                                          starting rebase, FILE unchanged for --edit-todo
//                   ERROR <message>\n      the list could not be read
//
// The registry files are disposable: a stale one (the process is gone) only makes git-gg try a
// port nobody answers on. Nothing here is written into the repository.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gg::seqlink {

struct Instance {
    long long pid = 0;
    int port = 0;
    std::string token;
    std::string gitDir;   // normalized (gitDirKey); "" = no repository open
};

// $XDG_RUNTIME_DIR/ggui (else <temp>/ggui-<user>), created on demand with owner-only access.
std::filesystem::path registryDir();
// The key two git dirs are compared by: absolute, canonical where it exists, generic separators.
std::string gitDirKey(const std::filesystem::path& gitDir);
// Registers (or updates) this process; returns false when the file cannot be written.
bool writeInstance(const Instance& instance);
void removeInstance(long long pid);
// Every registered instance (unreadable files are skipped).
std::vector<Instance> instances();

// Protocol words (see the file comment).
inline constexpr const char* kCommand = "sequence-editor";

} // namespace gg::seqlink
