// History editing actions on one commit (product spec §4.3), shared by the History row menu, the
// Commit menu and the History keys (D, Shift+D, S, Shift+S, Alt+S, A, Shift+A).
#pragma once

#include <core/Types.hpp>

#include <string>
#include <vector>

namespace ggui {

class Session;

// Menu items for `row` (inside an open menu or popup).
void drawCommitEditItems(Session& session, const core::HistoryRow& row);
// Keyboard shortcuts for the selected commit (History panel focused).
void handleCommitEditKeys(Session& session, const core::HistoryRow& row);

// `prefill` fills the dialog's commit field (see otherCommit in CommitMenu.cpp).
void showRebaseDialog(Session& session, const core::Oid& commit, const std::string& prefill = {});
void showSquashDialog(Session& session, const core::Oid& commit, const std::string& prefill = {});
void showSplitDialog(Session& session, const core::Oid& commit);
void showAbandonBranchDialog(Session& session, const core::Oid& commit);
void showRestoreDialog(Session& session, const core::Oid& commit, const std::string& prefill = {});
// Merge `rev` into HEAD: a branch name, or a commit (`commit` = true: git's "Merge commit '<rev>'"
// message).
void showMergeDialog(Session& session, const std::string& rev, bool commit = false);

// Interactive rebase (§4.13): the commit and its descendants up to HEAD or the branch containing
// them ("from here", key I); the commits between the oldest and newest selected ones (the rest of
// the branch follows as picks); or `tip` ("HEAD" or a branch) onto a base the dialog asks for.
void openInteractiveRebase(Session& session, const core::Oid& commit);
void openInteractiveRebaseSelection(Session& session, const std::vector<core::Oid>& commits);
void showInteractiveRebaseDialog(Session& session, const std::string& tip);

} // namespace ggui
