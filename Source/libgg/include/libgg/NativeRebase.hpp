// Native interactive rebase support (product spec §4.13 execution 2, §6), shared by ggui and git-gg.
//
// ggui runs `git rebase -i` with GIT_SEQUENCE_EDITOR / GIT_EDITOR set to `git gg sequence-editor`
// and GG_SEQUENCE_DIR pointing at a prepared state directory: the todo to hand to git, and the
// messages typed in the todo editor, keyed by the commit git is working on when it opens its
// editor (the last line of rebase-merge/done). Nothing here writes git's sequencer files.
//
// Journal grouping (U1 §4): a whole native rebase, from `rebase (start)` to `rebase (finish)`, is
// one journal operation. The operation that saw the rebase start is remembered per worktree with
// the rebase's identity; later ggui mutations and the managed hooks (for a continuation in a
// terminal) join it while that rebase is in progress, and it is ended when the rebase is gone.
//
// Everything lives in $GIT_COMMON_DIR/gg/rebase/<worktree>/ and is disposable: deleting it only
// splits the rebase into several journal operations and makes Continue use git's own messages.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

struct git_repository;

namespace gg::journal {
class Writer;
}

namespace gg::native {

// "<orig-head> <onto> <head-name>" of the `git rebase` (merge backend: rebase-merge/) in progress
// in the worktree whose git dir is `gitDir`, "" when there is none.
std::string rebaseIdentity(const std::filesystem::path& gitDir);
std::string rebaseIdentity(git_repository* repo);

// $GIT_COMMON_DIR/gg/rebase/<worktree key>
std::filesystem::path stateDir(git_repository* repo);

// ---- Journal grouping ---------------------------------------------------------------------------

// The operation of the rebase in progress in this worktree ("" when none is remembered for it).
std::string groupOperation(git_repository* repo);
// Remembers `op` as the operation of the rebase in progress and marks it in the journal (it stays
// open while the rebase is stopped).
void rememberGroup(git_repository* repo, journal::Writer& journal, const std::string& op);
// A remembered operation whose rebase is no longer in progress (finished or aborted, maybe in a
// terminal) gets its end record; the state directory is removed.
void closeFinishedGroup(git_repository* repo, journal::Writer& journal);
// Ends and forgets the remembered operation (the rebase just finished).
void finishGroup(git_repository* repo, journal::Writer& journal, bool ok = true);

// ---- Prepared todo and messages ---------------------------------------------------------------

struct Prepared {
    std::string todo;                             // for git-rebase-todo ("" = leave git's list)
    std::map<std::string, std::string> messages;  // commit id → message for git's editor
    std::string identity;                         // the rebase it belongs to ("" = not started yet)
};

bool writePrepared(const std::filesystem::path& dir, const Prepared& prepared, std::string& error);
std::optional<Prepared> readPrepared(const std::filesystem::path& dir);
// Removes the prepared state (the rebase finished); the directory goes when nothing else is in it.
void discardPrepared(git_repository* repo);
// The prepared state for the rebase in progress in this worktree, if ggui prepared it.
std::optional<Prepared> preparedFor(git_repository* repo);

// `git gg sequence-editor FILE`: FILE named git-rebase-todo gets the prepared todo; any other
// file (COMMIT_EDITMSG) gets the message prepared for the commit on the last line of
// rebase-merge/done, or stays as git wrote it. Returns the exit status; errors go to `error`.
int sequenceEditor(const std::filesystem::path& file, std::string& error);

} // namespace gg::native
