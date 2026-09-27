// History editing actions on one commit (REBUILD_PLAN §4.3), shared by the History row menu, the
// Commit menu and the History keys (D, Shift+D, S, Shift+S, Alt+S, A, Shift+A).
#pragma once

#include <core/Types.hpp>

namespace ggui {

class Session;

// Menu items for `row` (inside an open menu or popup).
void drawCommitEditItems(Session& session, const core::HistoryRow& row);
// Keyboard shortcuts for the selected commit (History panel focused).
void handleCommitEditKeys(Session& session, const core::HistoryRow& row);

void showRebaseDialog(Session& session, const core::Oid& commit);
void showSquashDialog(Session& session, const core::Oid& commit);
void showSplitDialog(Session& session, const core::Oid& commit);
void showAbandonBranchDialog(Session& session, const core::Oid& commit);
void showRestoreDialog(Session& session, const core::Oid& commit);
void showMergeDialog(Session& session, const std::string& branch);

} // namespace ggui
