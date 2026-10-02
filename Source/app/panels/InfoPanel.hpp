// Change information panel (product spec §4.4): message, author/committer, date, published
// state, commit ID, parents.
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <optional>
#include <string>

namespace ggui {

class InfoPanel {
public:
    explicit InfoPanel(Session& session);
    // The history selection; shown unless an override is set.
    void onSelection(const Selection& sel);
    // What the panel shows instead of the history selection (the Blame panel's selected line); nullopt
    // goes back to following the history selection.
    void setOverride(const std::optional<Selection>& sel);
    const std::optional<Selection>& override() const { return m_override; }
    // The selection the panel shows: the override, else the history selection.
    const Selection& selection() const { return m_selection; }
    void onDetails(const core::CommitDetailsEvent& event);
    void draw(bool* open);
    const core::CommitDetailsPtr& details() const { return m_details; }

private:
    void show(const Selection& sel);
    void revealParent(const core::Oid& id);
    void drawPendingCommitInfo(const core::StatusResult* status, const core::Snapshot* snap);
    Session& m_session;
    Selection m_historySelection;
    std::optional<Selection> m_override;
    Selection m_selection; // what is shown (the override, else m_historySelection)
    core::RequestId m_request = 0;
    core::CommitDetailsPtr m_details;
    std::string m_message; // editor buffer
    std::string m_commitMessage; // Working tree / Index commit field
    std::string m_mergeMessage;
    std::string m_mergeMessageSource;
};

} // namespace ggui
