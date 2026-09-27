#include "libgg/Operation.hpp"

#include "libgg/GitRunner.hpp"
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
        if (name.rfind("refs/", 0) != 0 || name.rfind("refs/gg/cache", 0) == 0)
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
    m_before = readRefValues(m_repo);
    if (m_captureIndex && !git_repository_is_bare(m_repo))
        m_indexBefore = indexTree(m_workdir);
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
    m_journal.appendRefs(m_op.id, changes, &error);
    if (m_captureIndex && !m_indexBefore.empty()) {
        const std::string indexAfter = indexTree(m_workdir);
        if (!indexAfter.empty() && indexAfter != m_indexBefore)
            m_journal.appendIndex(m_op.id, journal::IndexChange{m_op.wt, m_indexBefore, indexAfter, worktreeFollowsIndex},
                &error);
    }
    m_journal.end(m_op.id, ok, &error);
    if (m_error.empty())
        m_error = error;
}

} // namespace gg
