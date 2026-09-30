// Change information panel (product spec §4.4): message, author/committer, date, published
// state, commit ID, parents.
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <string>

namespace ggui {

class InfoPanel {
public:
    explicit InfoPanel(Session& session);
    void onSelection(const Selection& sel);
    void onDetails(const core::CommitDetailsEvent& event);
    void draw(bool* open);
    const core::CommitDetailsPtr& details() const { return m_details; }

private:
    void drawPendingCommitInfo(const core::StatusResult* status, const core::Snapshot* snap);
    Session& m_session;
    Selection m_selection;
    core::RequestId m_request = 0;
    core::CommitDetailsPtr m_details;
    std::string m_message; // editor buffer
    std::string m_commitMessage; // Working tree / Index commit field
    std::string m_mergeMessage;
    std::string m_mergeMessageSource;
};

} // namespace ggui
