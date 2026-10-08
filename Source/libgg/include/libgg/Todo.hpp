// Interactive rebase todo model (product spec §4.13, R3). Shared by ggui (todo editor, preview,
// in-memory engine) and git-gg (sequence editor for the native engine).
//
// The todo is Git's list, oldest first: parse() reads what `git rebase -i` writes (full or
// abbreviated ids, short or long command names, comments) and format() writes lines Git reads
// back. read() builds the starting todo for a range the way `git rebase -i` does (merges
// dropped, commits already upstream left out, update-ref lines; autosquash() and the editor's
// options apply to it later), together with everything validation and message assembly need, so
// none of those touch the repository. It also builds the list `git rebase -i --rebase-merges`
// starts with (label, reset and merge rows keeping the branches' shape).
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct git_repository;

namespace gg::todo {

enum class Action {
    Pick, Reword, Edit, Squash, Fixup, Drop, // commit actions
    Exec, Break, UpdateRef,
    Label, Reset, Merge,                      // --rebase-merges
};

// `fixup -C` uses this commit's message instead of the group's, `fixup -c` also edits it. A merge
// row uses the same choice: `merge -C <commit>` recreates that merge with its message and author,
// `merge -c <commit>` also edits the message, a plain `merge` gets Git's "Merge branch '…'".
enum class FixupMessage { None, Use, Edit };

// The reset target of a root commit's branch (`git rebase --root` without --onto).
inline constexpr const char* kNewRoot = "[new root]";

struct Item {
    Action action = Action::Pick;
    // Commit actions, and the original merge of a `merge -C/-c` row (full id after read()/expand();
    // may be abbreviated after parse()). A plain `merge` row may keep the merge it was made from (the
    // editor switched it from -C): format() leaves it out, Git gets `merge <labels>`.
    std::string commit;
    // exec: command; update-ref: full ref; label: its name; reset: the label or revision it goes
    // to (or kNewRoot); merge: the labels (or revisions) merged, separated by spaces.
    std::string arg;
    FixupMessage fixup = FixupMessage::None;
    // Text after the id, or after "#" on reset/merge rows (display only; Git ignores it, except on
    // a plain `merge` row, where it is the merge's message).
    std::string subject;
    // The message typed in ggui for a reword row or a squash group (kept on the group's first
    // row), or a `merge -c` row. Cleaned with cleanup=strip like a message from Git's editor.
    // nullopt = Git's default.
    std::optional<std::string> message;

    bool isCommit() const;    // Pick … Drop
    // A row that makes a commit of the rebased history: a commit action but drop, or a merge.
    bool makesCommit() const;
    // A merge row: the labels (or revisions) it merges.
    std::vector<std::string> mergeHeads() const;
    bool operator==(const Item&) const = default;
};

struct Todo {
    std::vector<Item> items;
    bool operator==(const Todo&) const = default;
};

// ---- Names ----------------------------------------------------------------------------------

const char* actionName(Action action);   // "pick", "fixup", "update-ref", …
// Long or short command name (Git's p r e s f d x b u l t m) → action.
std::optional<Action> parseAction(std::string_view word);

// ---- Git's todo text ------------------------------------------------------------------------

struct ParseError {
    int line = 0;             // 1-based
    std::string message;      // Git wording where Git has one
};

// Parses a todo file. Blank lines, comment lines ("#") and `noop` are skipped. Unknown commands and
// malformed lines are reported and left out.
Todo parse(std::string_view text, std::vector<ParseError>* errors = nullptr);

// The commits the rows name: the commit rows (pick … drop) and the `merge -C/-c` rows, in order
// (ids as written: may be abbreviated after parse()).
std::vector<std::string> replayedCommits(const Todo& list);

// Writes the todo as Git does: "pick <full id> # <subject>", "fixup -C <id> # …", "exec <cmd>", a
// blank line after each update-ref, "label <name>", "reset <name> # <subject>", "merge -C <id>
// <labels> # <subject>", "noop" when empty.
std::string format(const Todo& todo);

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
    // Short names (in name order) of the local branches that stay on the old commits: not the tip
    // ref, tip outside the range, but built on a commit of the range. read() fills it once; it
    // does not follow later edits of the todo (a dropped row does not change it). A branch at a
    // merge commit of the range is not here: the editor adds it while Rebase merges is off.
    std::vector<std::string> leftBehind;
    Todo initial;                                 // the todo Git would start with
    // --rebase-merges: the merge commits of the range (oldest first; not in `range`, but in
    // `commits`), and the todo `git rebase -i --rebase-merges` starts with (label/reset/merge rows,
    // update-ref rows as in `initial`).
    std::vector<std::string> merges;
    Todo initialMerges;
    // Names on reset/merge rows that are not (only) labels of the todo: what they resolve to, as
    // Git resolves them when no earlier label row defines them (refs/rewritten/<name>, then any
    // revision). Filled by read() (abbreviated ids of commits outside the range) and expand().
    std::map<std::string, std::string> revisions;
    // Labels a stopped rebase already defined (refs/rewritten/<name>; readRemaining()).
    std::set<std::string> definedLabels;
    // The remaining part of a stopped `git rebase -i` (readRemaining): onto is the current HEAD,
    // and squash/fixup rows before any commit row fold into it.
    bool continuesHead = false;
};

// Reads the range and builds the starting todo (see the file comment). Throws on unknown
// revisions or when `upstream` is not an ancestor of the tip's history.
Context read(git_repository* repo, const ReadOptions& options);

// Resolves abbreviated ids in `todo` to full ids and adds the commits to `context`, and resolves
// the names on reset/merge rows into `context.revisions`. Returns the problems (unknown or
// ambiguous ids), with 1-based item numbers as lines.
std::vector<ParseError> expand(git_repository* repo, Todo& todo, Context& context);

// The remaining todo of a stopped `git rebase -i` ("Edit remaining todo"): `todoText` is
// rebase-merge/git-rebase-todo, `headName` rebase-merge/head-name (the branch that moves at the
// end, or "detached HEAD"). The Context describes only that part: upstream = onto = the current
// HEAD, the branch as tipRef, HEAD following it; `initial` is the parsed list with full ids.
// Throws when the list has lines or ids it cannot read.
Context readRemaining(git_repository* repo, std::string_view todoText, const std::string& headName);

// The list a starting `git rebase -i` hands to its sequence editor (plain `git rebase -i` with
// `git gg sequence-editor` as sequence.editor): `todoText` is rebase-merge/git-rebase-todo,
// `onto`, `origHead` and `headName` the files of that name. HEAD is still on the branch then. The
// Context: onto = upstream = onto, tip = orig-head, the branch as tipRef, HEAD following it;
// `initial` (and `initialMerges` for a --rebase-merges list) is the parsed list with full ids.
// Throws when the list has lines or ids it cannot read.
Context readStarting(git_repository* repo, std::string_view todoText, const std::string& onto, const std::string& origHead,
    const std::string& headName);

// ---- Autosquash -----------------------------------------------------------------------------

// Moves `fixup!`/`squash!`/`amend!` commits after their target and marks them (fixup, squash,
// fixup -C) the way `git rebase --autosquash` does: prefixes are skipped repeatedly; the target
// is the earliest earlier commit with that subject, else an earlier commit whose id starts with
// it, else the earliest earlier commit whose subject starts with it. Chains keep their order.
void autosquash(Todo& todo, const Context& context);

// ---- Messages -------------------------------------------------------------------------------

// Git's commit message cleanup=strip ("#" comment lines, trailing whitespace, blank lines at the
// ends and repeated blank lines removed).
std::string cleanup(std::string_view message);

// A squash group: a pick/reword/edit row and the squash/fixup rows after it (drop rows in between
// do not matter). As in Git, an exec, break, update-ref or label row finishes the group's commit: squash/
// fixup rows after it form a new group that amends that commit (`amends`; its `first` is then the
// first squash/fixup row and the previous commit's message is the template's first message).
struct Group {
    size_t first = 0;
    std::vector<size_t> followers;
    bool needsEditor = false;  // a squash or fixup -c: Git would open its editor
    std::optional<size_t> amends; // first row of the group whose finished commit this one amends
};
// Every commit row that is not dropped, with its followers. A squash/fixup without a commit
// before it (or right after a reset or merge row, which moves HEAD) gets a group of its own with
// it as `first` (validate() reports the first case).
std::vector<Group> groups(const Todo& todo);
// The group whose first row or follower is `row`, if any.
std::optional<Group> groupAt(const Todo& todo, size_t row);

// Git's commented template for a group with followers (what Git's editor would show).
std::string squashTemplate(const Todo& todo, const Group& group, const Context& context);
// The message the group's commit gets: the typed message (cleaned), or Git's default (the
// cleaned template when Git would open its editor, the kept message otherwise).
std::string groupMessage(const Todo& todo, const Group& group, const Context& context);
// Text for the inline message editor of the group's first row: the typed message, or the
// template, or the kept message.
std::string editorText(const Todo& todo, const Group& group, const Context& context);

// What git's editor gets during a native run (`git gg sequence-editor`): for every group with a
// typed message, the commit git is working on when it opens its editor for that message (the
// reword row, or the group's last squash/fixup row when Git asks for the combined message, or
// the merge of a `merge -c` row) → the typed text. Other editor invocations keep Git's text, which is Git's default message.
std::map<std::string, std::string> editorMessages(const Todo& todo);

// ---- Validation -----------------------------------------------------------------------------

struct Issue {
    enum class Severity { Error, Warning };
    enum class Code {
        SquashWithoutCommit,   // a squash/fixup with no commit before it
        EmptyExec,             // exec without a command
        BadRef,                // update-ref without a full refs/ name, or twice
        DuplicateCommit,       // the same commit on two rows
        BranchLosesCommits,    // every commit of a moving branch is dropped
        Published,             // rewrites or drops commits already on a remote
        BadLabel,              // label/reset/merge without a name, or a name Git cannot use
        UnknownLabel,          // reset/merge names a label no earlier row defines (and no revision)
        LabelDefinedLater,     // reset/merge names a label defined only further down (Git takes the revision)
    };
    Severity severity = Severity::Error;
    Code code = Code::SquashWithoutCommit;
    int row = -1;              // item index, -1 = the whole todo
    std::string message;
    bool error() const { return severity == Severity::Error; }
};

std::vector<Issue> validate(const Todo& todo, const Context& context);
bool hasErrors(const std::vector<Issue>& issues);

// Leading rows Git leaves untouched (picks of the original commits in their original order on
// the original parent): their commits keep their ids.
size_t unchangedPrefix(const Todo& todo, const Context& context);
// Per row: the row replays its commit as it is (Git fast-forwards it: a pick of a commit onto its
// own parent, a `merge -C` of a merge onto its own parents), following label/reset rows.
std::vector<bool> unchangedRows(const Todo& todo, const Context& context);

// ---- Engine (R3) ----------------------------------------------------------------------------

struct Options {
    bool updateRefs = true;
    bool autosquash = false;
    bool autostash = false;
    std::string execEach;          // "exec after every commit" ("" = none)
    bool keepCommitterDate = false;
    bool runAsGitRebase = false;   // the user's choice
    bool rebaseMerges = false;     // --rebase-merges: the list keeps the merges (label/reset/merge rows)
    // Commits that become empty (git rebase --empty): Ask = Start asks (Git's interactive
    // default, --empty=stop), Keep or Drop.
    gg::rewrite::Emptied emptied = gg::rewrite::Emptied::Ask;
};

enum class Engine { InMemory, Native };
struct EngineChoice {
    Engine engine = Engine::InMemory;
    std::string reason;
};
EngineChoice chooseEngine(const Todo& todo, const Options& options);

// The todo has label, reset or merge rows (it needs `git rebase -i --rebase-merges`).
bool hasMergeRows(const Todo& todo);

// Inserts "exec <command>" after every commit row (and its squash/fixup followers), as
// `git rebase --exec` does.
void addExecEach(Todo& todo, const std::string& command);

// The in-memory engine's plan: commit rows replayed in todo order on `onto`, squash/fixup as
// Squash steps with the group message (amending steps after an exec/break/update-ref row),
// update-ref lines and the tip ref moved to the commit finished before them, other branches left
// alone (as Git does). Post-rewrite gets Git's mapping: each group's commits map to the group's
// commit. Step keys are "row:<index>". Throws when the
// todo has errors or needs the native engine. With `replayStops` (the live preview) edit rows are
// replayed as picks and exec/break rows are skipped: the history a native run produces when
// every stop just continues, and label/reset/merge rows are replayed as `git rebase -i
// --rebase-merges` runs them (labels name the commit made so far, reset moves to one, merge makes a
// merge commit: `-C` keeps the merge's message and author and reuses it when its parents stay,
// heads already merged are left out). A todo the preview cannot model throws NoPreview.
gg::rewrite::Plan toPlan(const Todo& todo, const Context& context, bool replayStops = false);

// toPlan() for a todo whose result it cannot compute in memory (the reason is the message).
struct NoPreview : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace gg::todo
