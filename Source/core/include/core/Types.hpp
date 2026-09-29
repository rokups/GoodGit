// Immutable values that cross from engine workers to the UI thread (product spec §3.1).
// Nothing here refers to libgit2; the UI includes only these headers.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ggui::core {

// ---- Object IDs --------------------------------------------------------------------------------

struct Oid {
    std::array<std::uint8_t, 32> bytes{};
    std::uint8_t size = 0; // 20 (SHA-1), 32 (SHA-256), 0 = null

    bool isNull() const;
    std::string hex() const;
    std::string shortHex(size_t n = 7) const { return hex().substr(0, n); }
    static Oid fromHex(std::string_view hex); // null Oid on error
    static Oid fromBytes(const unsigned char* raw, size_t size);

    bool operator==(const Oid& o) const { return size == o.size && bytes == o.bytes; }
    bool operator!=(const Oid& o) const { return !(*this == o); }
    bool operator<(const Oid& o) const { return size != o.size ? size < o.size : bytes < o.bytes; }
};

struct OidHash {
    size_t operator()(const Oid& o) const noexcept
    {
        size_t h = 0;
        for (size_t i = 0; i < sizeof(size_t) && i < o.bytes.size(); ++i)
            h = (h << 8) | o.bytes[i];
        return h;
    }
};

// ---- Snapshot ----------------------------------------------------------------------------------

enum class RepoState {
    None,
    Merging,
    RebasingInteractive,
    Rebasing, // apply backend / git am
    CherryPicking,
    Reverting,
    Bisecting,
};
const char* repoStateBadge(RepoState s); // "MERGING", "REBASING", … ("" for None)

struct BranchInfo {
    std::string name;            // short name, e.g. "main"
    Oid target;
    std::string upstream;        // e.g. "origin/main" ("" = none)
    bool upstreamGone = false;   // configured but the ref does not exist
    int ahead = 0;
    int behind = 0;
    bool isHead = false;         // checked out in this worktree
    std::string worktree;        // other worktree that has it checked out ("" = none)
};

struct RemoteBranchInfo {
    std::string remote;          // "origin"
    std::string name;            // "origin/main"
    Oid target;
};

struct TagInfo {
    std::string name;
    Oid target;                  // peeled (usually a commit)
    Oid object;                  // the tag object for annotated tags
    bool annotated = false;
    std::string message;
};

struct RemoteInfo {
    std::string name;
    std::string url;
    std::string pushUrl;
    bool pruneOnFetch = false;
};

struct WorktreeInfo {
    std::string name;            // "main" for the main worktree, else the id (directory name)
    std::filesystem::path path;
    Oid head;
    std::string branch;          // short branch name, "" when detached
    bool isMain = false;
    bool isCurrent = false;
    bool bare = false;
    bool locked = false;
    std::string lockReason;
    bool missing = false;        // its directory is gone (moved or deleted by hand)
    bool prunable = false;       // git worktree prune would remove it (invalid and not locked)
};

struct StashInfo {
    int index = 0;               // stash@{index}
    std::string message;
    Oid commit;
    Oid base;                    // first parent
    std::int64_t time = 0;
    bool hasIndexChanges = false;
    bool hasUntracked = false;
};

// A `git rebase -i` in progress (rebase-merge/), as git's todo files have it (§4.10 progress view).
struct RebaseStep {
    std::string action;              // "pick", "edit", "exec", "break", "update-ref", …
    std::string commit;              // full id for commit rows
    std::string text;                // the subject for commit rows, the argument otherwise
};
struct RebaseProgress {
    std::vector<RebaseStep> done;    // done/: the last one is where the rebase stopped
    std::vector<RebaseStep> remaining; // git-rebase-todo
    std::string todoText;            // git-rebase-todo as read ("Edit remaining todo" starts from it)
    std::string headName;            // head-name: "refs/heads/<branch>" or "detached HEAD"
};

struct Snapshot {
    std::uint64_t generation = 0;
    std::filesystem::path workdir;   // empty for bare
    std::filesystem::path gitDir;
    std::filesystem::path commonDir;
    std::string name;                // display name (workdir or gitdir leaf)
    std::string objectFormat = "sha1";
    bool bare = false;
    bool headUnborn = false;
    bool headDetached = false;
    std::string headBranch;          // short name, also for unborn
    Oid head;
    std::string worktreeId = "main";
    RepoState state = RepoState::None;
    std::string stateDetail;         // e.g. "2/5" while rebasing
    std::string stateOnto;           // rebase onto / merge head description
    std::string mergeMessage;        // MERGE_MSG while in progress
    std::optional<RebaseProgress> rebase; // interactive rebase in progress
    std::vector<BranchInfo> branches;
    std::vector<RemoteBranchInfo> remoteBranches;
    std::vector<TagInfo> tags;
    std::vector<RemoteInfo> remotes;
    std::vector<WorktreeInfo> worktrees;
    std::vector<StashInfo> stashes;
    std::vector<std::string> oldGgRefs; // leftover refs/gg/* (C3)

    const BranchInfo* currentBranch() const;
    const BranchInfo* findBranch(const std::string& name) const;
    // A stable fingerprint of all ref targets (history reloads when it changes).
    std::string refsFingerprint() const;
};

using SnapshotPtr = std::shared_ptr<const Snapshot>;

// ---- Status ------------------------------------------------------------------------------------

enum class ChangeKind : char {
    Added = 'A',
    Modified = 'M',
    Deleted = 'D',
    Renamed = 'R',
    Copied = 'C',
    TypeChanged = 'T',
    Untracked = '?',
    Conflicted = 'U',
};

struct StatusEntry {
    std::string path;
    std::string oldPath;           // renames/copies
    ChangeKind kind = ChangeKind::Modified;
    bool intentToAdd = false;
    bool binary = false;
    // Conflicts: which index stages exist (1 base, 2 ours, 3 theirs) and a description.
    bool stage1 = false, stage2 = false, stage3 = false;
    std::string conflictDescription; // "both modified", "deleted by them", …
    bool firstClass = false;          // conflicted through in-file markers, index clean
    int sides = 0;                    // number of sides for first-class conflicts
    // Set on an ordinary Modified entry when HEAD held a first-class conflict for this path and
    // the edit broke the region (left an opening/closing marker as plain text) instead of
    // resolving it: 1-based line numbers of the leftover markers (gg::markers::brokenMarkers).
    std::vector<size_t> brokenMarkerLines;
};

struct StatusResult {
    std::uint64_t generation = 0;
    bool partial = false;            // "scanning…"
    std::vector<StatusEntry> staged;
    std::vector<StatusEntry> unstaged;
    std::vector<StatusEntry> untracked;
    std::vector<StatusEntry> conflicted;
    bool empty() const { return staged.empty() && unstaged.empty() && untracked.empty() && conflicted.empty(); }
};
using StatusPtr = std::shared_ptr<const StatusResult>;

// ---- History -----------------------------------------------------------------------------------

enum class RefKind : std::uint8_t { LocalBranch, RemoteBranch, Tag, Head, Worktree, Stash };

struct RefBadge {
    RefKind kind;
    std::string name;
    bool current = false;            // checked-out branch
};

// A line segment drawn in one history row, in lane units.
// Positions: 0 = top edge, 1 = node centre, 2 = bottom edge.
struct GraphLine {
    std::int16_t fromLane;
    std::int16_t toLane;
    std::uint8_t fromPos;
    std::uint8_t toPos;
    std::uint8_t color;
};

struct HistoryRow {
    Oid id;
    std::string shortId;
    std::string subject;
    std::string author;
    std::string authorEmail;
    std::int64_t time = 0;
    std::vector<Oid> parents;
    std::vector<RefBadge> refs;
    bool published = false;
    bool conflicted = false;          // first-class conflict (Phase 2 detection)
    int lane = 0;
    std::uint8_t color = 0;
    std::vector<GraphLine> lines;
    int collapsedCount = 0;           // merge rows: commits hidden when collapsed
    bool collapsed = false;
    bool collapsible = false;         // merge rows: collapsing would hide commits
};

struct HistoryScope {
    bool allRefs = true;              // everything (branches, remotes, tags, HEAD)
    std::vector<std::string> refs;    // full ref names when !allRefs
    bool mergesCollapsed = true;      // default state of every merge's side history
    std::vector<Oid> toggledMerges;   // merges whose state differs from the default
    bool operator==(const HistoryScope&) const = default;
};

struct HistoryBatch {
    std::uint64_t query = 0;          // request that produced it
    bool reset = false;               // first batch of a new query: replace rows
    std::vector<HistoryRow> rows;     // appended rows
    bool complete = false;            // walk finished
    bool truncated = false;           // stopped at the limit: "Load more"
    std::vector<std::pair<Oid, int>> collapsedCounts; // updated hidden-commit counts of collapsed merges
    int maxLanes = 0;
};

// ---- Diff --------------------------------------------------------------------------------------

enum class Whitespace { Normal, IgnoreChanges, IgnoreAll };

enum class DiffKind {
    Commit,          // commit vs its first parent (or empty tree)
    Commits,         // a vs b
    Staged,          // HEAD vs index
    Unstaged,        // index vs working tree (untracked included)
    StashWorktree,   // stash: base vs stash commit (working tree part)
    StashIndex,      // stash: base vs index commit
    StashUntracked,  // stash: untracked commit
    Stages,          // native conflict: index stage `stageA` → stage `stageB` of `path`
    Term,            // first-class conflict: base → side `stageB` (0-based) of `path` in commit `a`
                     // (null: the working tree file)
    WorktreeCommit,  // the working tree (old side) vs commit `b` ("Compare with: Work Tree")
};

struct DiffQuery {
    DiffKind kind = DiffKind::Commit;
    Oid a;                          // commit / old side
    Oid b;                          // new side for Commits
    std::string against;            // Commits: the old side as a revision (HEAD, an ID, a ref),
                                    // resolved by the worker when set ("Compare with")
    std::string path;               // restrict to one file ("" = all files)
    std::vector<std::string> paths; // or to several files (patch of a selection)
    bool withHunks = true;          // false: file list only
    int context = 3;
    Whitespace whitespace = Whitespace::Normal;
    bool full = false;              // ignore the size cap
    int stageA = 0, stageB = 0;     // for DiffKind::Stages (1 base, 2 ours, 3 theirs)
    bool operator==(const DiffQuery&) const = default;
};

struct DiffLine {
    char origin = ' ';              // ' ', '+', '-'
    int oldNo = -1;
    int newNo = -1;
    std::string text;               // without the line ending
    bool crlf = false;              // the line ended in "\r\n"
    bool noNewline = false;         // "\ No newline at end of file" follows
};

struct DiffHunk {
    std::string header;
    int oldStart = 0, oldLines = 0, newStart = 0, newLines = 0;
    std::vector<DiffLine> lines;
};

struct DiffFile {
    std::string oldPath;
    std::string newPath;
    ChangeKind kind = ChangeKind::Modified;
    std::uint32_t oldMode = 0;
    std::uint32_t newMode = 0;
    Oid oldId;
    Oid newId;
    bool binary = false;
    bool image = false;
    bool submodule = false;
    bool truncated = false;          // capped: "Load full diff"
    int additions = 0;
    int deletions = 0;
    std::vector<DiffHunk> hunks;
    // Full texts for context expansion (only when small enough and not binary).
    std::shared_ptr<const std::vector<std::string>> oldText;
    std::shared_ptr<const std::vector<std::string>> newText;
    std::uint64_t oldSize = 0, newSize = 0;
    std::string oldImage, newImage;  // "WxH" for images when known
    const std::string& path() const { return newPath.empty() ? oldPath : newPath; }
};

struct DiffResult {
    DiffQuery query;
    std::vector<DiffFile> files;
    std::string patch;               // unified patch text (for Copy/Save patch)
    std::string error;               // why nothing was compared (an unknown `against` revision)
};
using DiffPtr = std::shared_ptr<const DiffResult>;

// ---- Blame -------------------------------------------------------------------------------------

struct BlameLine {
    int lineNo = 0;
    Oid commit;                      // null = not committed
    std::string origPath;
    int origLine = 0;
    std::string author;
    std::int64_t time = 0;
    std::string summary;
    std::string text;
};

struct BlameQuery {
    std::string path;
    Oid commit;                      // null = working tree
    bool beforeCommit = false;       // blame at the first parent of `commit`
    int scrollToLine = 0;            // UI hint: line to reveal (originating source)
    bool operator==(const BlameQuery&) const = default;
};

struct BlameResult {
    BlameQuery query;
    std::vector<BlameLine> lines;
    bool truncated = false;
};
using BlamePtr = std::shared_ptr<const BlameResult>;

// ---- Reflog ------------------------------------------------------------------------------------

struct ReflogEntry {
    Oid oldId;
    Oid newId;
    std::string message;
    std::string committer;
    std::int64_t time = 0;
};

struct ReflogResult {
    std::string ref;
    std::vector<ReflogEntry> entries;
};
using ReflogPtr = std::shared_ptr<const ReflogResult>;

// ---- Commit details (Change information) -------------------------------------------------------

struct CommitDetails {
    Oid id;
    std::string message;
    std::string authorName, authorEmail;
    std::int64_t authorTime = 0;
    int authorOffset = 0;            // minutes
    std::string committerName, committerEmail;
    std::int64_t committerTime = 0;
    std::vector<Oid> parents;
    bool published = false;
    std::vector<std::pair<std::string, int>> conflictedFiles; // path, sides
};
using CommitDetailsPtr = std::shared_ptr<const CommitDetails>;

// ---- Interactive rebase live preview (§4.13) ----------------------------------------------------

// The result of an interactive rebase todo, computed in memory on a worker (nothing is written
// to the repository). Rows are the resulting commits, oldest first.
struct RebasePreview {
    struct NonText {
        std::string path;
        std::string kind;                 // binary, modify/delete, rename, … (gg::rewrite::NonTextConflict)
    };
    struct Row {
        std::string id;                   // resulting commit (an unchanged commit keeps its id)
        std::string tree;                 // its tree (the same as Start's result: trees hold no dates)
        std::vector<std::string> sources; // original commits: the row's own, then squashed ones
        size_t todoRow = 0;               // the todo row that starts it
        std::string subject;              // first line of the resulting message
        bool unchanged = false;           // same commit as before (keeps its id)
        bool empty = false;               // same tree as its parent
        bool wasEmpty = false;            // its commit(s) were already empty before the rebase
        std::vector<std::pair<std::string, int>> conflicts; // first-class conflicted files: path, sides
        bool newConflicts = false;        // conflicts this rebase creates (not only carried along)
        std::vector<std::string> resolved; // files whose first-class conflicts in the original commits are gone
        std::vector<NonText> decisions;   // non-text conflicts that need a decision (pre-flight)
        std::vector<std::string> branches; // short names of branches ending here ("HEAD" = detached HEAD)
        // Its parents (result commits, the base or commits outside the range), first parent first.
        std::vector<std::string> parents;
        bool merge = false;               // made by a merge row (--rebase-merges)
    };
    struct Move {
        std::string ref;                  // short branch name, or "HEAD"
        std::string from;                 // old commit ("" = created)
        std::string to;
    };
    bool ok = false;
    std::string error;                    // why there is no preview
    bool unsupported = false;             // `error` is a list the preview cannot model (gg::todo::NoPreview), not a failure
    std::string onto;                     // the base ("" = the root)
    std::string ontoSubject;
    std::vector<Row> rows;
    std::vector<Move> moves;              // branches (and a detached HEAD) that move
    std::vector<std::string> ontoBranches; // branches ending at the base (every commit dropped)
    std::vector<std::string> staying;     // branches in the range that stay on the old commits
    // A branch whose update-ref row comes before a squash/fixup row: Git finishes the commit for
    // it and the squash/fixup amends a copy, so the branch ends beside the result.
    struct Aside {
        std::string branch;               // short name
        std::string id;
        std::string tree;
        std::string subject;
        size_t row = 0;                   // the result row that amends it (same parent)
    };
    std::vector<Aside> aside;
    std::vector<std::string> droppedEmpty; // subjects of commits left out because they became empty
};
using RebasePreviewPtr = std::shared_ptr<const RebasePreview>;

// ---- Recent repository summary -----------------------------------------------------------------

struct RepoSummary {
    std::filesystem::path path;
    bool exists = false;
    std::string branch;               // "" when detached
    bool detached = false;
    std::string upstream;
    int ahead = 0, behind = 0;
};

// ---- Formatting helpers ------------------------------------------------------------------------

std::string formatTime(std::int64_t unixSeconds, bool withSeconds = false);

} // namespace ggui::core
