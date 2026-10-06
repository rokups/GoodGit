// Side panels (product spec §4.7): Branches, Tags, Repositories, Worktrees, Remotes, Stashes, Reflog, Operations.
// Each panel keeps its own small view model.
#pragma once

#include "shell/RepoTree.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <core/Engine.hpp>

#include <string>
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

class BranchesPanel {
public:
    explicit BranchesPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);

private:
    void branchMenu(const core::BranchInfo& b);
    void remoteBranchMenu(const core::RemoteBranchInfo& r);
    void keptMenu(const core::KeptInfo& k, bool current, const IdSlot& idSlot);
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::string m_filter;
};

class TagsPanel {
public:
    explicit TagsPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);

private:
    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::string m_filter;
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

private:
    void drawNodes(const std::vector<RepoNode>& nodes, std::string& toOpen);
    void drawWorktrees(std::string& toOpen);
    void repoDropTarget(const std::string& group);
    Session& m_session;
    // The label of the repository row that is dragged now: the default name of its alias after a drop.
    std::string m_dragLabel;
    // The selected row: a repository (its path) or a worktree of the open one (m_selectedWorktree, its path).
    std::string m_selected;
    bool m_selectedWorktree = false;
    std::string m_openPath;
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
