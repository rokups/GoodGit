// What committing or pushing would carry along that touches first-class conflicts (product spec
// §4.10, §8): the outgoing commits that hold conflicts or left broken markers (push refuses
// them), and the staged files a commit would make such (the commit warns, never blocks).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct git_repository;

namespace gg::outgoing {

// Commits reachable from `local` but not from `remote`'s tracking refs that hold first-class
// conflicts, with their files ("<id> <path>" lines). Used by pre-push and by ggui's push.
struct ConflictedCommit {
    std::string id;
    std::string subject;
    std::vector<std::string> files;
};
std::vector<ConflictedCommit> conflictedOutgoing(const std::filesystem::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid);

// Commits reachable from `local` but not from `remote`'s tracking refs that left broken
// conflict markers (product spec §4.10, §8): for a file the commit changes, its first parent's
// version held a first-class conflict and the commit's version has gg::markers::brokenMarkers
// non-empty. Used by pre-push and by ggui's push, alongside conflictedOutgoing.
struct BrokenFile {
    std::string path;
    std::vector<size_t> lines; // 1-based, from markers::brokenMarkers
};
struct BrokenCommit {
    std::string id;
    std::string subject;
    std::vector<BrokenFile> files;
};
std::vector<BrokenCommit> brokenOutgoing(const std::filesystem::path& repoDir, const std::string& localOid,
    const std::string& remote, const std::string& remoteOid);

// A staged file the next commit would turn into, or leave as, a conflict problem.
struct StagedWarning {
    enum class Kind {
        Conflict,      // the staged file is itself a first-class conflict
        BrokenMarkers, // HEAD's version held a first-class conflict and the staged edit broke it
    };
    std::string path;
    Kind kind = Kind::Conflict;
    int sides = 0;             // Conflict: the number of sides
    std::vector<size_t> lines; // BrokenMarkers: 1-based, from markers::brokenMarkers
};

// The warnings for what is staged right now, in index order (first the files that are
// conflicts, then the broken ones). Never blocks anything: committing stays the user's call.
// Conflict: limited to paths the commit touches (new or changed against HEAD), so a conflict
// already sitting unchanged in HEAD is not re-reported by every unrelated commit.
std::vector<StagedWarning> stagedConflictWarnings(git_repository* repo);

} // namespace gg::outgoing
