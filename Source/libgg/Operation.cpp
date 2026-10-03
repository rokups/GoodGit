#include "libgg/Operation.hpp"

#include "libgg/GitRunner.hpp"
#include "libgg/NativeRebase.hpp"
#include "libgg/Reconcile.hpp"
#include "libgg/Thread.hpp"

namespace gg {

namespace fs = std::filesystem;
using namespace gg::git2;

std::string zeroId(git_repository* repo)
{
    return std::string(hexSize(oidType(repo)), '0');
}

std::string worktreeKey(git_repository* repo)
{
    return journal::worktreeKeyForGitDir(git_repository_path(repo), git_repository_commondir(repo));
}

std::map<std::string, std::string> readRefValues(git_repository* repo)
{
    assertNotUiThread("readRefValues");
    std::map<std::string, std::string> values;
    const std::string wt = worktreeKey(repo);
    git_reference* rawHead = nullptr;
    if (git_reference_lookup(&rawHead, repo, "HEAD") == 0) {
        Reference head(rawHead);
        if (git_reference_type(head.get()) == GIT_REFERENCE_SYMBOLIC)
            values[journal::headKey(wt)] = std::string("ref:") + git_reference_symbolic_target(head.get());
        else
            values[journal::headKey(wt)] = toHex(*git_reference_target(head.get()));
    } else {
        git_error_clear();
    }
    forEachReference(repo, [&](git_reference* ref) {
        const std::string name = git_reference_name(ref);
        // Leftovers of the old gg (refs/gg/*) are deleted on open and never journaled; the keep
        // refs (refs/gg/keep/*) are.
        if (!journal::tracked(name))
            return true;
        if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC)
            values[name] = std::string("ref:") + git_reference_symbolic_target(ref);
        else
            values[name] = toHex(*git_reference_target(ref));
        return true;
    });
    return values;
}

std::string indexTree(const fs::path& workdir)
{
    if (workdir.empty())
        return {};
    RunRequest r;
    r.args = {"git", "write-tree"};
    r.cwd = workdir;
    r.gitEnvironment = false;
    const RunResult res = run(r);
    return res.ok() ? trim(res.out) : std::string();
}

OperationRecorder::OperationRecorder(git_repository* repo, std::string src, std::string label, bool captureIndex)
    : m_repo(repo), m_journal(fs::path(git_repository_commondir(repo))), m_captureIndex(captureIndex)
{
    if (const char* wd = git_repository_workdir(repo))
        m_workdir = wd;
    m_op.id = journal::Journal::newOperationId();
    m_op.src = std::move(src);
    m_op.label = std::move(label);
    m_op.wt = worktreeKey(repo);
}

OperationRecorder::~OperationRecorder()
{
    if (m_begun && !m_finished)
        finish(false);
}

void OperationRecorder::setUndoes(std::string id, bool redo)
{
    m_op.undoes = std::move(id);
    m_op.redo = redo;
}

void OperationRecorder::begin()
{
    // Changes made outside ggui since the last pass are journaled first, as their own operation,
    // so they are not attributed to this one. Never run inside a Journal::Transaction.
    {
        std::string ignored;
        reconcile::run(m_repo, &ignored);
    }
    // A native rebase is one operation from start to finish: while one is in progress, operations
    // join the one that started it (undo operations never do).
    native::closeFinishedGroup(m_repo, m_journal);
    m_rebaseAtBegin = !native::rebaseIdentity(m_repo).empty();
    if (m_op.undoes.empty())
        if (const auto group = native::openGroup(m_repo); group && group->active) {
            m_op.id = group->op;
            m_resumed = true;
            // A plain git rebase's operation (opened by the reconciler, or by the managed hooks of older versions) has no
            // index: one taken in the middle says nothing about its start. ggui's has the start's;
            // this adds the latest.
            if (group->src == "git")
                m_captureIndex = false;
        }
    m_before = readRefValues(m_repo);
    if (m_captureIndex && !git_repository_is_bare(m_repo))
        m_indexBefore = indexTree(m_workdir);
    if (!m_resumed)
        m_journal.begin(m_op, &m_error);
    m_previousOperation = currentOperation();
    setCurrentOperation(m_op.id);
    m_begun = true;
}

void OperationRecorder::finish(bool ok, bool worktreeFollowsIndex)
{
    if (!m_begun || m_finished)
        return;
    m_finished = true;
    setCurrentOperation(m_previousOperation);
    const auto after = readRefValues(m_repo);
    const std::string zero = zeroId(m_repo);
    std::vector<journal::RefChange> changes;
    for (const auto& [ref, value] : after) {
        auto it = m_before.find(ref);
        const std::string old = it == m_before.end() ? zero : it->second;
        if (old != value)
            changes.push_back(journal::RefChange{ref, old, value});
    }
    for (const auto& [ref, value] : m_before)
        if (!after.count(ref))
            changes.push_back(journal::RefChange{ref, value, zero});
    std::string error;
    for (const auto& w : m_worktrees)
        m_journal.appendWorktree(m_op.id, w, &error);
    m_journal.appendRefs(m_op.id, changes, &error);
    // The git commands this operation ran left reflog entries: they are accounted for now (the
    // op is still open, so no reconcile pass can journal them meanwhile).
    reconcile::advanceCursor(m_repo, &error);
    // The rebase this operation started or joined: open while it is stopped, ended with it.
    const bool rebasing = !native::rebaseIdentity(m_repo).empty();
    const bool startsGroup = !m_resumed && rebasing && !m_rebaseAtBegin && m_op.undoes.empty();
    if (m_captureIndex) {
        // A rebase group's steps record the index even when a step leaves it as it was: the group
        // then keeps its first index and its latest one, although the user may stage changes
        // between the steps (for an edit stop) outside any operation. An index with conflicts has
        // no tree (""): a step that stops at conflicts records its start, and the step that goes
        // on from there its end, so the group still has both.
        const std::string indexAfter = indexTree(m_workdir);
        const bool groupStep = m_resumed || startsGroup;
        if (groupStep ? !m_indexBefore.empty() || !indexAfter.empty()
                      : !m_indexBefore.empty() && !indexAfter.empty() && indexAfter != m_indexBefore)
            m_journal.appendIndex(m_op.id, journal::IndexChange{m_op.wt, m_indexBefore, indexAfter, worktreeFollowsIndex},
                &error);
    }
    if (m_resumed) {
        if (!rebasing)
            native::finishGroup(m_repo, m_journal, ok);
    } else if (startsGroup) {
        native::rememberGroup(m_repo, m_journal, m_op.id, m_op.src);
    } else {
        m_journal.end(m_op.id, ok, &error);
    }
    if (m_error.empty())
        m_error = error;
}

} // namespace gg
