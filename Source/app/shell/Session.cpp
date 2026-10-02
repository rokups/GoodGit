#include "shell/Session.hpp"

#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/App.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>

#include <chrono>

namespace ggui {

Session::Session(App& app, std::filesystem::path path) : m_app(app), m_path(std::move(path))
{
    core::Engine::Options opts;
    opts.path = m_path;
    m_engine = std::make_unique<core::Engine>(opts);
    m_actions = std::make_unique<Actions>(*this);
    m_history = std::make_unique<HistoryPanel>(*this);
    m_changes = std::make_unique<ChangesPanel>(*this);
    m_info = std::make_unique<InfoPanel>(*this);
    m_diff = std::make_unique<DiffPanel>(*this);
    m_blame = std::make_unique<BlamePanel>(*this);
    m_branches = std::make_unique<BranchesPanel>(*this);
    m_tags = std::make_unique<TagsPanel>(*this);
    m_worktrees = std::make_unique<WorktreesPanel>(*this);
    m_remotes = std::make_unique<RemotesPanel>(*this);
    m_stashes = std::make_unique<StashesPanel>(*this);
    m_reflog = std::make_unique<ReflogPanel>(*this);
    m_operationsPanel = std::make_unique<OperationsPanel>(*this);
    m_rebase = std::make_unique<RebasePanel>(*this);
    m_openRequest = m_engine->open();
}

Session::~Session()
{
    m_engine->cancelAll();
    m_engine.reset();
}

std::string Session::displayName() const
{
    if (m_snapshot)
        return m_snapshot->name;
    return m_path.filename().string();
}

void Session::cancelOpen() { m_engine->cancel(m_openRequest); }

bool Session::idle() const { return m_engine->idle(); }

std::vector<core::Activity> Session::activities() const { return m_engine->activities(); }

void Session::pump()
{
    // Bounded per frame: at most 64 events or ~4 ms of work (§3.1).
    const auto start = std::chrono::steady_clock::now();
    for (int round = 0; round < 8; ++round) {
        auto events = m_engine->poll(8);
        if (events.empty())
            break;
        for (auto& e : events)
            handle(e);
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(4))
            break;
    }
}

void Session::handle(core::Event& event)
{
    std::visit(
        [this](auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, core::OpenedEvent>) {
                m_opened = true;
                onSnapshot(e.snapshot, true);
                m_engine->refreshStatus();
                m_engine->readOperations();
            } else if constexpr (std::is_same_v<T, core::SnapshotEvent>) {
                if (m_opened)
                    onSnapshot(e.snapshot, false);
            } else if constexpr (std::is_same_v<T, core::StatusEvent>) {
                m_status = e.status;
                m_history->onStatus(m_status);
                m_changes->onStatus(m_status);
                // The commit warning follows the index.
                if (!m_status || m_status->staged.empty())
                    m_commitWarnings.clear();
                else
                    m_engine->readCommitWarnings();
            } else if constexpr (std::is_same_v<T, core::HistoryEvent>) {
                std::vector<core::Oid> ids;
                for (const auto& row : e.batch->rows)
                    ids.push_back(row.id);
                if (e.batch->reset)
                    m_conflicts.clear();
                m_history->onHistory(e);
                if (!ids.empty())
                    m_engine->scanConflicts(std::move(ids));
            } else if constexpr (std::is_same_v<T, core::RevealEvent>) {
                m_history->onReveal(e);
            } else if constexpr (std::is_same_v<T, core::SearchEvent>) {
                m_history->onSearch(e);
            } else if constexpr (std::is_same_v<T, core::DiffEvent>) {
                if (e.slot == DiffPanel::kSlot)
                    m_diff->onDiff(e);
                else
                    m_changes->onDiff(e);
            } else if constexpr (std::is_same_v<T, core::BlameEvent>) {
                m_blame->onBlame(e);
            } else if constexpr (std::is_same_v<T, core::ReflogEvent>) {
                m_reflog->onReflog(e);
            } else if constexpr (std::is_same_v<T, core::CommitDetailsEvent>) {
                m_info->onDetails(e);
            } else if constexpr (std::is_same_v<T, core::CommitMessagesEvent>) {
                if (const auto it = m_messageWaiters.find(e.request); it != m_messageWaiters.end()) {
                    auto done = std::move(it->second);
                    m_messageWaiters.erase(it);
                    done(e.messages);
                }
            } else if constexpr (std::is_same_v<T, core::ErrorEvent>) {
                if (e.request == m_openRequest && !m_opened) {
                    m_failed = true;
                    m_app.showError("Cannot open repository", m_path.string() + ":\n" + e.message);
                } else {
                    m_app.showError(e.title, e.message);
                }
            } else if constexpr (std::is_same_v<T, core::TaskFinishedEvent>) {
                if (e.request == m_openRequest && !m_opened && (e.cancelled || e.failed))
                    m_failed = true;
                m_history->onTaskFinished(e);
                m_rebase->onTaskFinished(e);
            } else if constexpr (std::is_same_v<T, core::WatchEvent>) {
                if (e.refs)
                    m_reflog->reload();
                if (e.journal)
                    m_engine->readOperations();
            } else if constexpr (std::is_same_v<T, core::MutationFinishedEvent>) {
                m_actions->onFinished(e);
            } else if constexpr (std::is_same_v<T, core::OperationsEvent>) {
                m_operations = std::move(e.operations);
                // Once per problem: the watcher and a refresh may both read the same journal.
                if (!e.error.empty() && e.error != m_journalError)
                    m_app.showError("Undo journal", e.error);
                m_journalError = e.error;
            } else if constexpr (std::is_same_v<T, core::ConflictsEvent>) {
                for (const auto& id : e.scanned)
                    m_conflicts.erase(id);
                for (auto& c : e.commits)
                    m_conflicts[c.commit] = std::move(c.files);
                m_history->onConflicts(e);
            } else if constexpr (std::is_same_v<T, core::RemoteTagsEvent>) {
                if (e.request == m_remoteTagsRequest) {
                    auto& r = m_remoteTags[e.remote];
                    r.ok = e.ok;
                    r.tags = std::set<std::string>(e.tags.begin(), e.tags.end());
                    r.error = e.error;
                }
            } else if constexpr (std::is_same_v<T, core::ConfigEvent>) {
                m_config = std::move(e.values);
            } else if constexpr (std::is_same_v<T, core::CommitWarningsEvent>) {
                m_commitWarnings = std::move(e.warnings);
            } else if constexpr (std::is_same_v<T, core::RebasePreviewEvent>) {
                m_rebase->onPreview(e);
            }
        },
        event);
}

void Session::commitMessages(const std::vector<core::Oid>& ids, std::function<void(const std::vector<std::string>&)> done)
{
    m_messageWaiters.clear(); // a newer request supersedes the older one (same worker slot)
    const core::RequestId id = m_engine->commitMessages(ids);
    m_messageWaiters[id] = std::move(done);
}

void Session::onSnapshot(core::SnapshotPtr snap, bool first)
{
    const bool refsChanged = !m_snapshot || m_snapshot->refsFingerprint() != snap->refsFingerprint();
    m_snapshot = std::move(snap);
    m_history->onSnapshot(m_snapshot, refsChanged || first);
    m_branches->onSnapshot(m_snapshot);
    m_tags->onSnapshot(m_snapshot);
    m_reflog->onSnapshot(m_snapshot);
    m_stashes->onSnapshot(m_snapshot);
    const auto editFile = gg::edit::sessionFile(m_snapshot->gitDir, m_snapshot->commonDir);
    m_editSession = gg::edit::read(editFile);
    if (m_editSession && (!m_snapshot->headDetached || !m_snapshot->findBranch(m_editSession->branch))) {
        gg::edit::clear(editFile);
        m_editSession.reset();
    }
    if (first) {
        if (!m_snapshot->bare)
            select(Selection{SelKind::WorkingTree, {}, -1});
        else if (!m_snapshot->head.isNull())
            select(Selection{SelKind::Commit, m_snapshot->head, -1});
    } else if (m_selection.kind == SelKind::Stash) {
        bool found = false;
        for (const auto& s : m_snapshot->stashes)
            if (s.commit == m_selection.id) {
                m_selection.stashIndex = s.index;
                found = true;
            }
        if (!found && !m_stashRewordPending)
            select(Selection{SelKind::WorkingTree, {}, -1});
    }
}

void Session::stashRewordDone(const std::string& oldCommit, const std::string& newCommit, int index)
{
    if (m_selection.kind != SelKind::Stash || m_selection.id.hex() != oldCommit)
        return;
    const core::Oid id = core::Oid::fromHex(newCommit);
    if (!id.isNull())
        select(Selection{SelKind::Stash, id, index});
    else
        select(Selection{SelKind::WorkingTree, {}, -1});
}

void Session::stopEditing()
{
    gg::edit::clear(gg::edit::sessionFile(m_snapshot->gitDir, m_snapshot->commonDir));
    m_editSession.reset();
}

void Session::select(const Selection& sel)
{
    if (sel == m_selection)
        return;
    m_selection = sel;
    m_changes->onSelection(m_selection);
    m_info->onSelection(m_selection);
    m_diff->onSelection(m_selection);
}

void Session::selectCommit(const core::Oid& id) { select(Selection{SelKind::Commit, id, -1}); }

void Session::revealCommit(const core::Oid& id)
{
    focusPanel(panel::History);
    m_history->reveal(id);
}

void Session::refresh()
{
    // Everything, the undo journal too: a refresh must not depend on the file watcher noticing.
    m_engine->refresh(true);
    m_engine->readOperations();
}

void Session::nextChangedFile(int direction) { m_changes->moveCurrent(direction); }

void Session::blameFile(const std::string& path, const core::Oid& commit)
{
    focusPanel(panel::Blame);
    m_blame->open(path, commit);
}

void Session::focusPanel(const char* name)
{
    m_app.settings().data().panels[name] = true;
    m_pendingFocus = name;
}

const ConflictList* Session::conflictsOf(const core::Oid& id) const
{
    auto it = m_conflicts.find(id);
    return it == m_conflicts.end() ? nullptr : &it->second;
}

void Session::requestRemoteTagsIfStale()
{
    std::vector<std::string> remotes;
    for (const auto& r : m_snapshot->remotes)
        remotes.push_back(r.name);
    if (!m_remoteTagsStale && remotes == m_remoteTagsFor)
        return;
    m_remoteTagsStale = false;
    m_remoteTagsFor = remotes;
    // Remotes that are gone lose their list; the others keep theirs until the new one arrives.
    std::erase_if(m_remoteTags, [&](const auto& kv) {
        return std::find(remotes.begin(), remotes.end(), kv.first) == remotes.end();
    });
    m_remoteTagsRequest = remotes.empty() ? 0 : m_engine->readRemoteTags(remotes);
}

void Session::requestConfig()
{
    m_engine->readConfig({"user.name", "user.email", "core.editor", "merge.tool", "diff.tool", "pull.rebase", "pull.ff",
        "extensions.worktreeConfig", "sequence.editor", "gg.previousSequenceEditor", "gg.sameChange"});
}

std::string Session::shortId(const core::Oid& id) const
{
    return id.shortHex(shortIdLength());
}

size_t Session::shortIdLength() const
{
    return kShortIdLength;
}

bool Session::pullAvailable(std::string* reason) const
{
    auto fail = [&](const char* why) {
        *reason = why;
        return false;
    };
    if (m_snapshot->headDetached)
        return fail("HEAD is detached: check out a branch to pull");
    const auto* b = m_snapshot->currentBranch();
    if (!b || b->upstream.empty())
        return fail("The current branch has no upstream (set one in the Branches panel)");
    return true;
}

int Session::incoming() const
{
    const auto* b = m_snapshot->currentBranch();
    return b ? b->behind : 0;
}

int Session::outgoing() const
{
    const auto* b = m_snapshot->currentBranch();
    return b ? b->ahead : 0;
}

std::vector<std::string> Session::selectedPaths() const
{
    std::vector<std::string> out;
    for (const auto& r : m_changes->rows())
        if (m_changes->selectedKeys().count(r.key()))
            out.push_back(r.path);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Session::draw()
{
    auto& panels = m_app.settings().data().panels;
    auto visible = [&](const char* name) -> bool* {
        auto it = panels.find(name);
        if (it == panels.end())
            it = panels.emplace(name, panel::defaultVisible(name)).first;
        return &it->second;
    };
    auto drawPanel = [&](const char* name, auto& p) {
        bool* open = visible(name);
        if (*open) {
            const bool before = *open;
            p.draw(open);
            if (before != *open)
                m_app.settings().save();
        }
    };
    drawPanel(panel::Branches, *m_branches);
    drawPanel(panel::Tags, *m_tags);
    // Tabs of a shared dock node follow this order.
    drawPanel(panel::Remotes, *m_remotes);
    drawPanel(panel::Stashes, *m_stashes);
    drawPanel(panel::Worktrees, *m_worktrees);
    drawPanel(panel::History, *m_history);
    drawPanel(panel::Changes, *m_changes);
    drawPanel(panel::Info, *m_info);
    drawPanel(panel::Diff, *m_diff);
    drawPanel(panel::Blame, *m_blame);
    drawPanel(panel::Reflog, *m_reflog);
    drawPanel(panel::Operations, *m_operationsPanel);
    m_rebase->draw();
    if (!m_pendingFocus.empty()) {
        ImGui::SetWindowFocus(m_pendingFocus.c_str());
        m_pendingFocus.clear();
    }
}

} // namespace ggui
