// Side panels (product spec §4.7): Branches, Tags, Worktrees, Remotes, Stashes, Reflog, Operations.
// Each panel keeps its own small view model.
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <string>

namespace ggui {

class BranchesPanel {
public:
    explicit BranchesPanel(Session& session) : m_session(session) { }
    void onSnapshot(const core::SnapshotPtr& snapshot) { m_snapshot = snapshot; }
    void draw(bool* open);

private:
    void branchMenu(const core::BranchInfo& b);
    void remoteBranchMenu(const core::RemoteBranchInfo& r);
    void remoteHeadMenu(const core::RemoteHeadInfo& h);
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
