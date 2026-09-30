// Recording one operation in the undo journal (product spec §5 U1), shared by ggui's mutation
// pipeline and git-gg: reads refs (and optionally the index tree) before and after, writes the
// journal records, and moves the reconciler's reflog cursor past them so the reconciler does not
// record the same changes again.
#pragma once

#include "libgg/Git2.hpp"
#include "libgg/Journal.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gg {

// Current value of every ref the journal tracks: this worktree's HEAD (symbolic as
// "ref:<target>") under its journal key, and everything under refs/ except refs/gg/ (ggui keeps no
// private refs; leftovers of the old gg there are deleted on open, never journaled).
std::map<std::string, std::string> readRefValues(git_repository* repo);
std::string zeroId(git_repository* repo);
std::string worktreeKey(git_repository* repo);
// Tree id of the index via `git write-tree` ("" when unmerged or bare).
std::string indexTree(const std::filesystem::path& workdir);

class OperationRecorder {
public:
    OperationRecorder(git_repository* repo, std::string src, std::string label, bool captureIndex);
    ~OperationRecorder();
    OperationRecorder(const OperationRecorder&) = delete;
    OperationRecorder& operator=(const OperationRecorder&) = delete;

    // For undo/redo operations.
    void setUndoes(std::string id, bool redo);
    // Writes the begin record. Recording problems never fail the
    // operation itself (the journal is history only). While a native rebase is in progress the
    // recorder joins that rebase's operation instead (NativeRebase.hpp); an operation that starts
    // a rebase stays open until the rebase is finished.
    void begin();
    // A worktree the operation added, removed, locked or unlocked (written by finish()).
    void addWorktree(journal::WorktreeChange change) { m_worktrees.push_back(std::move(change)); }
    // Records the resulting ref and index changes and the end record.
    void finish(bool ok, bool worktreeFollowsIndex = false);
    const std::string& id() const { return m_op.id; }
    const std::string& journalError() const { return m_error; }

private:
    git_repository* m_repo;
    std::filesystem::path m_workdir;
    journal::Journal m_journal;
    journal::Operation m_op;
    bool m_captureIndex;
    bool m_begun = false;
    bool m_resumed = false;       // joined the operation of the native rebase in progress
    bool m_rebaseAtBegin = false; // a native rebase was in progress when it began
    bool m_finished = false;
    std::map<std::string, std::string> m_before;
    std::string m_indexBefore;
    std::vector<journal::WorktreeChange> m_worktrees;
    std::string m_previousOperation;
    std::string m_error;
};

} // namespace gg
