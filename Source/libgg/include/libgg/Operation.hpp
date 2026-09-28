// Recording one operation in the undo journal (REBUILD_PLAN §5 U1), shared by ggui's mutation
// pipeline and git-gg: reads refs (and optionally the index tree) before and after, writes the
// journal records and exports GG_OPERATION to child git processes so managed hooks join the
// operation instead of creating duplicates (loop guard).
#pragma once

#include "libgg/Git2.hpp"
#include "libgg/Journal.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace gg {

// Current value of every ref the journal tracks: this worktree's HEAD (symbolic as
// "ref:<target>") under its journal key, and everything under refs/ (except refs/gg/ caches).
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
    // Writes the begin record and exports GG_OPERATION. Recording problems never fail the
    // operation itself (the journal is history only). While a native rebase is in progress the
    // recorder joins that rebase's operation instead (NativeRebase.hpp); an operation that starts
    // a rebase stays open until the rebase is finished.
    void begin();
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
    std::string m_previousOperation;
    std::string m_error;
};

} // namespace gg
