// Interactive rebase todo model (REBUILD_PLAN §4.13, R3). Shared by ggui (todo editor, preview,
// in-memory engine) and git-gg (sequence editor for the native engine).
//
// The todo is Git's list, oldest first: parse() reads what `git rebase -i` writes (full or
// abbreviated ids, short or long command names, comments) and format() writes lines Git reads
// back. read() builds the starting todo for a range the way `git rebase -i` does (merges
// dropped, commits already upstream left out, update-ref lines, optional autosquash), together
// with everything validation and message assembly need, so none of those touch the repository.
//
// Messages follow Git: a squash group offers Git's commented template ("This is a combination
// of N commits…", fixup messages commented out, `squash!`/`fixup!`/`amend!` subjects commented
// out as in Git ≥ 2.32) and the message the user keeps is cleaned with cleanup=strip, as Git
// does after its editor. Groups made of fixups only keep a message without an editor (the first
// commit's, or the last `fixup -C` commit's without its `amend!` subject).
#pragma once

#include "libgg/Rewrite.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

struct git_repository;

namespace gg::todo {

enum class Action {
    Pick, Reword, Edit, Squash, Fixup, Drop, // commit actions
    Exec, Break, UpdateRef,
    Label, Reset, Merge,                      // --rebase-merges (Phase 4): parsed and written back only
};

// `fixup -C` uses this commit's message instead of the group's, `fixup -c` also edits it.
enum class FixupMessage { None, Use, Edit };

struct Item {
    Action action = Action::Pick;
    std::string commit;       // commit actions (full id after read()/expand(); may be abbreviated after parse())
    std::string arg;          // exec: command; update-ref: full ref; label/reset: label; merge: the rest of the line
    FixupMessage fixup = FixupMessage::None;
    std::string subject;      // text after the id (display only; Git ignores it)
    // The message typed in ggui for a reword row or a squash group (kept on the group's first
    // row). Cleaned with cleanup=strip like a message from Git's editor. nullopt = Git's default.
    std::optional<std::string> message;

    bool isCommit() const;    // Pick … Drop
    bool operator==(const Item&) const = default;
};

struct Todo {
    std::vector<Item> items;
    bool operator==(const Todo&) const = default;
};

// ---- Names ----------------------------------------------------------------------------------

const char* actionName(Action action);   // "pick", "fixup", "update-ref", …
char actionKey(Action action);           // Git's short form: p r e s f d x b u l t m
// Long or short command name → action.
std::optional<Action> parseAction(std::string_view word);

// ---- Git's todo text ------------------------------------------------------------------------

struct ParseError {
    int line = 0;             // 1-based
    std::string message;      // Git wording where Git has one
};

// Parses a todo file. Blank lines, comment lines and `noop` are skipped. Unknown commands and
// malformed lines are reported and left out.
Todo parse(std::string_view text, std::vector<ParseError>* errors = nullptr, std::string_view comment = "#");

struct FormatOptions {
    int abbrev = 0;              // id length (0 = full)
    bool shortCommands = false;  // rebase.abbreviateCommands
    std::string comment = "#";
};

// Writes the todo as Git does: "pick <id> # <subject>", "fixup -C <id> # …", "exec <cmd>", a
// blank line after each update-ref, "noop" when empty.
std::string format(const Todo& todo, const FormatOptions& options = {});

// ---- The range and what the todo refers to --------------------------------------------------

struct CommitInfo {
    std::string id;
    std::vector<std::string> parents;
    std::string message;
    std::string subject;       // first line
    std::string authorName;
    std::string authorEmail;
    std::int64_t authorTime = 0;
    int authorOffset = 0;
    bool empty = false;        // same tree as its first parent
    bool published = false;    // reachable from a remote-tracking branch
};

struct ReadOptions {
    std::string upstream;      // commits after it are listed ("" = from the root commit)
    std::string tip = "HEAD";  // local branch name, "HEAD" or a revision (then nothing moves but HEAD if detached there)
    std::string onto;          // new base ("" = upstream)
    bool updateRefs = true;    // --update-refs
    bool autosquash = false;   // --autosquash
};

struct Context {
    std::string upstream;      // full ids ("" = root)
    std::string onto;
    std::string tip;
    std::string tipRef;        // "refs/heads/x" that moves with the rebase ("" = none)
    bool tipIsHead = false;    // HEAD is (or points at) the tip: it follows the rebase
    std::vector<std::string> range;               // listed commits, oldest first, before autosquash
    std::map<std::string, CommitInfo> commits;    // every commit the todo refers to
    std::map<std::string, std::vector<std::string>> branchesAt; // commit → local branch refs
    std::set<std::string> checkedOutElsewhere;    // branch refs checked out in other worktrees
    Todo initial;                                 // the todo Git would start with
};

// Reads the range and builds the starting todo (see the file comment). Throws on unknown
// revisions or when `upstream` is not an ancestor of the tip's history.
Context read(git_repository* repo, const ReadOptions& options);

// Resolves abbreviated ids in `todo` to full ids and adds the commits to `context`. Returns
// the problems (unknown or ambiguous ids), with 1-based item numbers as lines.
std::vector<ParseError> expand(git_repository* repo, Todo& todo, Context& context);

// ---- Autosquash -----------------------------------------------------------------------------

// Moves `fixup!`/`squash!`/`amend!` commits after their target and marks them (fixup, squash,
// fixup -C) the way `git rebase --autosquash` does: prefixes are skipped repeatedly; the target
// is the earliest earlier commit with that subject, else an earlier commit whose id starts with
// it, else the earliest earlier commit whose subject starts with it. Chains keep their order.
void autosquash(Todo& todo, const Context& context);

// ---- Messages -------------------------------------------------------------------------------

enum class Cleanup { Strip, Whitespace, Verbatim, Scissors };
// Git's commit message cleanup (`git commit --cleanup`).
std::string cleanup(std::string_view message, Cleanup mode = Cleanup::Strip, std::string_view comment = "#");

// A squash group: a pick/reword/edit row and the squash/fixup rows right after it (exec, break
// and update-ref rows in between end the group, as in Git).
struct Group {
    size_t first = 0;
    std::vector<size_t> followers;
    bool needsEditor = false;  // a squash or fixup -c: Git would open its editor
};
// Every commit row that is not dropped, with its followers. A squash/fixup without a commit
// before it gets a group of its own with it as `first` (validate() reports it).
std::vector<Group> groups(const Todo& todo);
// The group whose first row or follower is `row`, if any.
std::optional<Group> groupAt(const Todo& todo, size_t row);

// Git's commented template for a group with followers (what Git's editor would show).
std::string squashTemplate(const Todo& todo, const Group& group, const Context& context, std::string_view comment = "#");
// The message the group's commit gets: the typed message (cleaned), or Git's default (the
// cleaned template when Git would open its editor, the kept message otherwise).
std::string groupMessage(const Todo& todo, const Group& group, const Context& context, std::string_view comment = "#");
// Text for the inline message editor of the group's first row: the typed message, or the
// template, or the kept message.
std::string editorText(const Todo& todo, const Group& group, const Context& context, std::string_view comment = "#");

// ---- Validation -----------------------------------------------------------------------------

struct Issue {
    enum class Severity { Error, Warning };
    enum class Code {
        SquashWithoutCommit,   // a squash/fixup with no commit before it
        UnknownCommit,         // a commit the context does not know
        EmptyExec,             // exec without a command
        BadRef,                // update-ref without a full refs/ name, or twice
        DuplicateCommit,       // the same commit on two rows
        BranchLosesCommits,    // every commit of a moving branch is dropped
        Published,             // rewrites or drops commits already on a remote
    };
    Severity severity = Severity::Error;
    Code code = Code::UnknownCommit;
    int row = -1;              // item index, -1 = the whole todo
    std::string message;
    bool error() const { return severity == Severity::Error; }
};

std::vector<Issue> validate(const Todo& todo, const Context& context);
bool hasErrors(const std::vector<Issue>& issues);

// Leading rows Git leaves untouched (picks of the original commits in their original order on
// the original parent): their commits keep their ids.
size_t unchangedPrefix(const Todo& todo, const Context& context);

// ---- Engine (R3) ----------------------------------------------------------------------------

struct Options {
    bool updateRefs = true;
    bool autosquash = false;
    bool autostash = false;
    std::string execEach;          // "exec after every commit" ("" = none)
    bool keepCommitterDate = false;
    bool runAsGitRebase = false;   // the user's choice
};

enum class Engine { InMemory, Native };
struct EngineChoice {
    Engine engine = Engine::InMemory;
    std::string reason;
};
EngineChoice chooseEngine(const Todo& todo, const Options& options);

// Inserts "exec <command>" after every commit row (and its squash/fixup followers), as
// `git rebase --exec` does.
void addExecEach(Todo& todo, const std::string& command);

// The in-memory engine's plan: commit rows replayed in todo order on `onto`, squash/fixup as
// Squash steps with the group message, update-ref lines and the tip ref moved to the step before
// them, other branches left alone (as Git does). Step keys are "row:<index>". Throws when the
// todo has errors or needs the native engine. With `replayStops` (the live preview) edit rows are
// replayed as picks and exec/break rows are skipped: the history a native run produces when
// every stop just continues.
gg::rewrite::Plan toPlan(const Todo& todo, const Context& context, std::string_view comment = "#",
    bool replayStops = false);

} // namespace gg::todo
