// In-memory history rewrites (REBUILD_PLAN §3, §4.3, §4.10, §5 R1/R2).
//
// A rewrite is a list of steps (pick a commit onto new parents, squash one into the previous
// step, create an empty commit, …) computed entirely in memory: every object goes to an
// in-memory object store, so a rewrite that is cancelled (or needs pre-flight decisions)
// leaves the repository byte-identical. Text conflicts become first-class conflicts in the
// file content (marker algebra, docs/spec/conflict-markers.md §7) and never stop the rewrite;
// non-text conflicts are collected for the pre-flight dialog and need a Resolution.
//
// Applying writes the objects as one pack, moves every ref in one `git update-ref --stdin`
// (reference-transaction hooks fire natively), updates the working tree with
// `git read-tree -m -u`, runs post-rewrite / post-checkout through `git hook run` and adds the
// old→new mapping to the current journal operation.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct git_repository;

namespace gg::rewrite {

struct Person {
    std::string name;
    std::string email;
    std::int64_t time = 0;   // seconds since the epoch
    int offset = 0;          // minutes east of UTC
};

struct Step {
    enum class Kind {
        Pick,    // the source commit's change (against its first parent) on new parents
        Squash,  // the source commit's change folded into the previous step's commit
        Empty,   // a new empty commit (tree of its first parent, or `tree`)
        Merge,   // a new merge commit of its parents (tree merged in memory, conflicts first-class)
    };
    Kind kind = Kind::Pick;
    std::string source;      // original commit id (Pick, Squash)
    std::string key;         // how later steps refer to this one (default: source)
    // New parents, each a step key, an original commit id (→ its rewritten version, or itself
    // when it is not rewritten) or "=<id>" (exactly that commit). Used when !sourceParents.
    std::vector<std::string> parents;
    bool sourceParents = true;       // the source's own parents, mapped
    std::optional<std::string> message;
    std::optional<Person> author;
    std::optional<std::string> tree; // fixed resulting tree
    // Apply only these paths of the source's change (empty = all), or all but these.
    std::vector<std::string> onlyPaths;
    std::vector<std::string> exceptPaths;
    // Files set in the result (bytes; nullopt = removed), e.g. working tree files absorbed
    // into the commit. Blobs go to the in-memory store like everything else.
    std::vector<std::pair<std::string, std::optional<std::string>>> setFiles;
    bool forceNew = false;   // a new commit even when nothing changed (duplicates)
    bool mapSource = true;   // the result replaces the source (children and branches follow it)
};

enum class Choice { Ours, Theirs, Base, File, Delete };

// A decision for one non-text conflict (§4.10 pre-flight). Ours = the side the commit is
// replayed onto (side A), Theirs = the replayed commit (side B).
struct Resolution {
    Choice choice = Choice::Theirs;
    std::string file;                       // Choice::File: content taken from this path on disk
    std::optional<std::uint32_t> mode;      // mode conflicts: the mode to keep
};

struct NonTextConflict {
    std::string step;       // step key
    std::string commit;     // replayed commit
    std::string subject;
    std::string path;
    std::string kind;       // binary, modify/delete, add/add, mode, symlink, submodule, rename, filtered, opt-out
    bool hasBase = false;
    bool hasOurs = false;
    bool hasTheirs = false;
    std::uint32_t oursMode = 0;
    std::uint32_t theirsMode = 0;
    std::string oursPath;   // renames: where each side moved the file
    std::string theirsPath;
    static std::string key(const std::string& step, const std::string& path) { return step + "\n" + path; }
};

struct Plan {
    std::vector<Step> steps;
    // Original commits left out: whatever pointed at them (children, branches) goes to their
    // first parent's replacement.
    std::vector<std::string> dropped;
    std::map<std::string, Resolution> resolutions; // NonTextConflict::key(step, path) → decision
    std::string reflogMessage = "ggui: rewrite";
    std::string rewriteKind = "rebase";   // post-rewrite argument: "rebase" or "amend"
    bool rebaseLike = false;              // run pre-rebase with `upstream`
    std::string upstream;
    // Extra refs to create or move to a step's result ("refs/heads/x" → step key).
    std::map<std::string, std::string> refsToSteps;
    // Commits already replaced outside the plan (original → existing commit), e.g. HEAD after
    // `git commit --amend`: children and branches of the original follow the replacement.
    std::map<std::string, std::string> replaced;
    // Parents that other steps get instead of an original commit (original → step key), for
    // inserting a commit between a commit and its children. Branches are not affected.
    std::map<std::string, std::string> parentRedirect;
    // Detach HEAD at a step's result ("" = no): duplicates are checked out as detached copies.
    std::string detachHeadAt;
    // Leave HEAD where it is even if its commit is rewritten (copies, duplicates).
    bool keepHead = false;
    // Do not move local branches either (copies).
    bool keepBranches = false;
    // The working tree keeps its files when HEAD moves (only the index follows HEAD): what the
    // rewrite takes out of HEAD's history stays as uncommitted changes ("uncommit").
    bool keepWorktree = false;
};

struct RefMove {
    std::string ref;
    std::string oldId;       // "" = created
    std::string newId;
    bool otherWorktree = false; // a branch checked out in another worktree
};

struct Result {
    bool ok = false;
    std::string error;
    std::vector<NonTextConflict> unresolved;         // pre-flight needed (nothing may be applied)
    std::map<std::string, std::string> mapping;      // original → new, rewritten commits only
    std::map<std::string, std::string> steps;        // step key → new commit
    std::vector<RefMove> moves;
    std::vector<std::string> conflicted;             // new commits that gained first-class conflicts
    std::vector<std::string> published;              // rewritten originals already on a remote
    std::string headBefore;                          // this worktree's HEAD commit
    std::string headAfter;
    bool changed() const { return !mapping.empty() || !moves.empty(); }
};

class Rewriter {
public:
    // Opens its own repository instance with an in-memory object store on top.
    explicit Rewriter(const std::filesystem::path& repoDir);
    ~Rewriter();
    Rewriter(const Rewriter&) = delete;
    Rewriter& operator=(const Rewriter&) = delete;

    // Builds every commit in memory. With unresolved non-text conflicts the result lists them
    // (provisional choices let later steps be computed) and is not applicable.
    Result compute(const Plan& plan);
    // Writes the computed rewrite (see the file comment). False with `error` on failure, in
    // which case refs, HEAD, index and working tree are unchanged.
    bool apply(const Plan& plan, Result& result, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// Commits to replay when `changed` are rewritten: every commit reachable from a local branch
// (or the detached HEAD) that has one of them as an ancestor, `changed` included, parents
// first.
std::vector<std::string> descendants(git_repository* repo, const std::vector<std::string>& changed);

// A Plan that replays `changed` and their descendants with the given per-commit edits.
Plan replayPlan(git_repository* repo, const std::vector<std::string>& changed);

// A tree with a unified patch (old → new, as `git diff` writes it) applied to it.
std::string applyPatchToTree(git_repository* repo, const std::string& tree, const std::string& patch);
// The same patch in the other direction (new → old).
std::string reversePatch(const std::string& patch);

// A new empty commit inserted before or after `at` (its descendants rebased onto it). When
// nothing comes after `at`, the branches (and a detached HEAD) at it advance to the new commit.
// The step key of the new commit is "inserted".
Plan insertPlan(git_repository* repo, const std::string& at, bool before, const std::string& message);

} // namespace gg::rewrite
