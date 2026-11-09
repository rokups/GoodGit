// Side panels (product spec §4.7): Branches, Tags, Repositories, Worktrees, Remotes, Stashes, Reflog, Operations.
// Each panel keeps its own small view model.
#pragma once

#include "shell/RepoTree.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <core/Engine.hpp>

#include <map>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ggui {

// The items of a ref's context menu, drawn into the open popup. Each side panel puts them in the menu of a
// row; the badges of a History row put them in theirs.
void branchMenuItems(Session& session, const core::Snapshot& snap, const core::BranchInfo& b);
void remoteBranchMenuItems(Session& session, const core::Snapshot& snap, const core::RemoteBranchInfo& r);
void tagMenuItems(Session& session, const core::Snapshot& snap, const core::TagInfo& t);
void keptMenuItems(Session& session, const core::KeptInfo& k, bool current, const IdSlot& idSlot);
void stashMenuItems(Session& session, const core::StashInfo& s, const IdSlot& baseSlot);
void worktreeMenuItems(Session& session, const core::Snapshot& snap, const core::WorktreeInfo& w);

// The kind of a ref row that the Branches panel or the Tags panel can select.
enum class RefKind { LocalBranch, RemoteBranch, Tag };

// The selected ref rows of the Branches panel or the Tags panel. All selected rows have the same kind.
struct RefSelection {
    RefKind kind = RefKind::LocalBranch;
    std::set<std::string> names;  // the branch name, "origin/x" for a remote-tracking branch, or the tag name
    std::string anchor;           // the row where a Shift-click range starts ("" when none)
    size_t anchorRow = 0;         // the position of the anchor row in `drawn` when it was clicked
    // The rows drawn in the last frame, in order. Shift-click and prune() read them.
    std::vector<std::pair<RefKind, std::string>> drawn;

    bool contains(RefKind rowKind, const std::string& name) const;
    // A click on a row. A row of another kind starts a new selection.
    // Plain: only this row. Ctrl: toggle this row. Shift: the rows from the anchor to this row.
    // `row`: the position of the row in `drawn`. A name that is drawn twice (a remote ref) needs it.
    void click(RefKind rowKind, const std::string& name, bool ctrl, bool shift, size_t row = 0);
    // Drops the names that `drawn` does not have.
    void prune();
    void clear();
};

class BranchesPanel {
public:
    explicit BranchesPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);
    const RefSelection& selection() const { return m_selection; }

private:
    // `refs`: the full refs the row shows or hides in History (the menu's Show/Hide in History item).
    // `allRefs`: the same for each branch of the snapshot, by index (the menu of a selection needs them).
    void branchMenu(const core::BranchInfo& b, const std::vector<std::string>& refs,
        const std::vector<std::vector<std::string>>& allRefs);
    void remoteBranchMenu(const core::RemoteBranchInfo& r, const std::vector<std::string>& refs);
    void keptMenu(const core::KeptInfo& k, bool current, const IdSlot& idSlot, const std::vector<std::string>& refs);
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::string m_filter;
    // Remote nodes opened by the filter: the open state each had before it, put back when the filter is cleared.
    std::map<std::string, bool> m_remoteOpenBeforeFilter;
    RefSelection m_selection;
};

class TagsPanel {
public:
    explicit TagsPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);
    const RefSelection& selection() const { return m_selection; }

private:
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::string m_filter;
    RefSelection m_selection;
};

// The permanent repository list as a tree of groups and repositories (GG-12).
class RepositoriesPanel {
public:
    explicit RepositoriesPanel(Session& session) : m_session(session) { }
    void draw(bool* open);
    // The path of the selected row, repository or worktree ("" when none).
    const std::string& selectedPath() const { return m_selected; }
    // The normalised list path of the repository the window shows, as of the last frame drawn: the main
    // repository when the window shows a linked worktree (Session::repositoryListPath, GG-15).
    const std::string& openPath() const { return m_openPath; }
    // The text of the row of a repository as of the last frame drawn: the label, two spaces, the detail ("" for no row).
    std::string rowText(const std::string& path) const
    {
        const auto it = m_rowTexts.find(path);
        return it == m_rowTexts.end() ? std::string() : it->second;
    }

private:
    void drawNodes(const std::vector<RepoNode>& nodes, std::string& toOpen);
    void drawWorktrees(std::string& toOpen);
    // The submodules of a repository (`path` is a list key): the snapshot list of the open one, else the summary list of a listed one, else null.
    const std::vector<core::SubmoduleInfo>* submodulesOf(const std::string& path) const;
    // The submodules as rows (nested ones as nodes). `toOpen` gets the path of the one that is double-clicked.
    void drawSubmodules(const std::vector<core::SubmoduleInfo>& list, std::string& toOpen);
    void submoduleMenuItems(const core::SubmoduleInfo& sm);
    void repoDropTarget(const std::string& group);
    Session& m_session;
    // The label of the repository row that is dragged now: the default name of its alias after a drop.
    std::string m_dragLabel;
    // The selected row: a repository (its path), or a worktree or a submodule row (m_selectedWorktree, its path).
    std::string m_selected;
    bool m_selectedWorktree = false;
    std::string m_openPath;
    std::string m_sessionPath;                  // the normalised path of the session, as of the last frame drawn
    std::set<std::string> m_openableSubmodules; // the paths of the submodule rows that open, as of the last frame drawn
    std::map<std::string, std::string> m_rowTexts;
};

class WorktreesPanel {
public:
    explicit WorktreesPanel(Session& session) : m_session(session) { }
    void draw(bool* open);

private:
    Session& m_session;
};

class RemotesPanel {
public:
    explicit RemotesPanel(Session& session) : m_session(session) { }
    void draw(bool* open);

private:
    Session& m_session;
};

class StashesPanel {
public:
    explicit StashesPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);

private:
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    // Where each row's base ID was drawn (by position in the list; a row's menu is drawn before its ID, so it uses
    // the previous frame's).
    std::vector<IdSlot> m_baseSlots;
};

class ReflogPanel {
public:
    explicit ReflogPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot);
    void onReflog(const core::ReflogEvent& event);
    void reload();
    void draw(bool* open);
    void choose(const std::string& ref);
    const core::ReflogPtr& reflog() const { return m_reflog; }
    const std::string& ref() const { return m_ref; }

private:
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::string m_ref = "HEAD";
    std::string m_filter;
    core::RequestId m_request = 0;
    core::ReflogPtr m_reflog;
    bool m_requested = false;
};

class OperationsPanel {
public:
    explicit OperationsPanel(Session& session) : m_session(session) { }
    void draw(bool* open);

private:
    Session& m_session;
};

} // namespace ggui
