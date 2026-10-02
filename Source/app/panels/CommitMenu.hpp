// History editing actions on one commit (product spec §4.3), shared by the History row menu, the
// Commit menu and the History keys (D, Shift+D, S, Shift+S, Alt+S, A, Shift+A).
#pragma once

#include <core/Types.hpp>

#include <string>
#include <vector>

namespace ggui {

class Session;
struct Selection;

// The commits selected in History (the primary one plus Ctrl-clicked ones), newest first.
// `contiguous`: each commit is the first parent of the one listed before it (no gaps).
struct SelectionShape {
    std::vector<core::Oid> ids;
    bool contiguous = false;
    size_t count() const { return ids.size(); }
    bool single() const { return ids.size() == 1; }
    bool range() const { return ids.size() > 1 && contiguous; } // two or more adjacent commits
};
SelectionShape selectionShape(Session& session);
// Tooltip for a menu item that just drew, shown while it is disabled (`disabled`) and hovered.
void disabledHint(bool disabled, const char* why);

// The "Edit commit" item for `row` (inside an open menu or popup).
void drawEditCommitItem(Session& session, const core::HistoryRow& row);
// Menu items for `row` (inside an open menu or popup), after the "Edit commit" item.
void drawCommitEditItems(Session& session, const core::HistoryRow& row);
// Keyboard shortcuts for the selected commit (History panel focused).
void handleCommitEditKeys(Session& session, const core::HistoryRow& row);

// `prefill` fills the dialog's commit field (see otherCommit in CommitMenu.cpp).
void showRebaseDialog(Session& session, const core::Oid& commit, const std::string& prefill = {});
// Squash the commits (newest first, each the parent of the one before) into one, asking for its
// message (prefilled from theirs). The oldest is the base the others fold into.
void showSquashDialog(Session& session, const std::vector<core::Oid>& commits);
// Squash (S / the menu item) for the History selection: its dialog when it can be squashed.
void squashSelection(Session& session);
void showSplitDialog(Session& session, const core::Oid& commit);
void showAbandonBranchDialog(Session& session, const core::Oid& commit);
// Restore `paths` from the commit named in the dialog's field, in what `in` (the Changes panel's
// selection) shows: a commit (rewrite it, or the working tree), the working tree or the index
// (git restore --source, both index and files). Opened from the Changes file menu.
void showRestoreDialog(Session& session, const Selection& in, std::vector<std::string> paths, const std::string& prefill = {});
// Stash `paths` (git stash push -m <message> [--include-untracked] [--staged] -- <paths>), with an
// optional message. `stagedOnly` (all selected rows are staged) stashes just their index part.
void showStashFilesDialog(Session& session, std::vector<std::string> paths, bool untracked, bool stagedOnly);
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
