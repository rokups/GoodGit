// The undo journal (product spec §5 U1; format in docs/spec/undo-journal.md).
//
// One JSON-Lines file in $GIT_COMMON_DIR/gg/journal shared by all worktrees; HEAD values and
// index trees are keyed per worktree. Append-only, locked like git's ref locks, torn or corrupt
// lines are skipped. The journal is history only: deleting it disables Undo, nothing else.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gg::journal {

constexpr int kFormatVersion = 1;

struct RefChange {
    std::string ref;       // "refs/heads/x", "HEAD", "worktrees/<id>/HEAD"
    std::string oldValue;  // hex id, all-zero id (absent) or "ref:<target>"
    std::string newValue;
};

struct IndexChange {
    std::string wt;        // worktree key: "main" or the linked worktree id
    std::string before;    // tree id ("" = unknown)
    std::string after;
    bool worktree = false; // the operation updated the working tree to follow the index
};

// A linked worktree added, removed, locked or unlocked by the operation (`git worktree …`). Undo
// does the opposite: add ↔ remove, lock ↔ unlock (docs/spec/undo-journal.md §2.1, §5.4).
struct WorktreeChange {
    std::string action;    // "add", "remove", "lock", "unlock"
    std::string path;      // the worktree's directory, as `git worktree list` shows it
    std::string head;      // add/remove: the commit it had checked out
    std::string branch;    // add/remove: "refs/heads/<name>" when on a branch, "" when detached
    bool locked = false;   // add/remove: whether it was locked
    std::string reason;    // the lock's reason (add/remove when locked, lock, unlock)
};

// The change that undoes `c` (same worktree, opposite action).
WorktreeChange inverse(const WorktreeChange& c);

struct Operation {
    std::string id;
    std::string src;       // "ggui", "git-gg", "git"
    std::string label;
    std::string wt;
    std::string cmd;
    std::string undoes;    // for undo/redo operations
    bool redo = false;
    std::int64_t time = 0;
    std::int64_t pid = 0;               // the writer's process id (begin record; 0 = not recorded)
    std::uint64_t pstart = 0;           // and its start time (gg::ProcessInfo::start; 0 = unknown)
    bool ended = false;
    bool ok = true;
    bool spansRebase = false;           // a native rebase: open until its end record (NativeRebase.hpp)
    std::vector<RefChange> refs;        // merged per ref: first old value, last new value
    std::vector<IndexChange> index;     // merged per worktree
    std::vector<std::pair<std::string, std::string>> rewrites; // old → new commit
    std::vector<WorktreeChange> worktrees; // in the order they happened
    size_t order = 0;                   // position of the begin record

    bool isUndo() const { return !undoes.empty() && !redo; }
    // Something Undo can restore (refs, an index tree or worktrees).
    bool restorable() const
    {
        for (const auto& r : refs)
            if (r.oldValue != r.newValue)
                return true;
        return !index.empty() || !worktrees.empty();
    }
    bool isRedo() const { return !undoes.empty() && redo; }
};

// The append API of the journal. Every call appends one record and returns false (with a reason)
// when it cannot be written. Journal locks per call, Journal::Transaction holds the lock for its
// whole lifetime; code that only appends takes a Writer& and works with both.
class Writer {
public:
    virtual ~Writer() = default;

    bool begin(const Operation& op, std::string* error = nullptr);
    bool appendRefs(const std::string& id, const std::vector<RefChange>& changes, std::string* error = nullptr);
    bool appendIndex(const std::string& id, const IndexChange& change, std::string* error = nullptr);
    bool appendWorktree(const std::string& id, const WorktreeChange& change, std::string* error = nullptr);
    bool appendRewrites(const std::string& id, const std::vector<std::pair<std::string, std::string>>& map,
        std::string* error = nullptr);
    bool end(const std::string& id, bool ok, std::string* error = nullptr);
    // Marks `id` as the operation of a native rebase: it stays open (not undoable) until its end
    // record, even when the git process that began it is gone.
    bool markRebase(const std::string& id, std::string* error = nullptr);

    // All operations in journal order (reading never takes the lock). See Journal::read.
    virtual std::vector<Operation> read(std::string* error = nullptr, size_t* skipped = nullptr) const = 0;

protected:
    // Appends one JSON line (without the newline).
    virtual bool appendLine(const std::string& line, std::string* error) = 0;
};

class Journal : public Writer {
public:
    explicit Journal(std::filesystem::path commonDir);

    const std::filesystem::path& path() const { return m_path; }
    const std::filesystem::path& dir() const { return m_dir; }
    static std::string newOperationId();

    // The Writer calls (begin, appendRefs, ...) each take the lock for that one record.

    // A held journal lock: several records written under one lock. Reading stays possible. A plain
    // Journal call from elsewhere fails with "journal busy" while a Transaction is alive.
    class Transaction : public Writer {
    public:
        explicit Transaction(const Journal& journal);
        ~Transaction() override;
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        bool locked() const;
        std::vector<Operation> read(std::string* error = nullptr, size_t* skipped = nullptr) const override;

    protected:
        bool appendLine(const std::string& line, std::string* error) override;

    private:
        struct Held;
        const Journal& m_journal;
        std::unique_ptr<Held> m_held;
    };

    // All operations in journal order. Corrupt or torn lines are skipped; `skipped` counts them.
    // A journal written by a newer major version yields an error and no operations.
    std::vector<Operation> read(std::string* error = nullptr, size_t* skipped = nullptr) const override;

    // Tail search: does the last `bytes` of the file hold a record of operation `id` (so it was begun)?
    bool hasOpenOperation(const std::string& id, size_t bytes = 64 * 1024) const;

private:
    bool appendLine(const std::string& line, std::string* error) override;

    std::filesystem::path m_dir;
    std::filesystem::path m_path;
};

// Worktree key for a repository handle's git dir: "main" or the linked worktree id.
std::string worktreeKeyForGitDir(const std::filesystem::path& gitDir, const std::filesystem::path& commonDir);
// Journal key for HEAD of a worktree.
std::string headKey(const std::string& worktree);

// ---- Undo / redo planning --------------------------------------------------------------------

struct UndoPlan {
    bool ok = false;
    std::string error;                  // refusal reason
    const Operation* target = nullptr;
    bool redo = false;
    std::vector<RefChange> restore;     // ref: oldValue = current (expected), newValue = restored value
    std::optional<IndexChange> index;   // before = current index tree, after = tree to restore
    std::vector<std::string> movedRefs; // refs that moved outside the journal
    std::vector<WorktreeChange> worktrees; // to apply, in this order (the target's, inverted, last first)
};

// Chooses the operation to undo (or redo) for worktree `wt` and checks that every ref it
// touched still has the value the operation left (`current(ref)` returns the value now).
UndoPlan planUndo(const std::vector<Operation>& ops, const std::string& wt, bool redo,
    const std::function<std::string(const std::string& ref)>& current);

// True when the operation is visible from worktree `wt` (touched a shared ref or wt's HEAD/index).
// An operation that added, removed, locked or unlocked worktrees belongs to the worktree that ran
// it only.
bool visibleFrom(const Operation& op, const std::string& wt);

} // namespace gg::journal
