#include "panels/CommitMenu.hpp"

#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Session.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

#include <IconsMaterialSymbols.h>
#include <imgui.h>

#include <algorithm>
#include <set>
#include <unordered_set>

namespace ggui {

namespace {

bool free(Session& s) { return s.actions().busy().empty(); }

// Files of the commit as listed in Changes (when that commit is the selection).
std::vector<std::string> commitFiles(Session& s, const core::Oid& id)
{
    std::vector<std::string> out;
    if (s.selection().kind != SelKind::Commit || s.selection().id != id)
        return out;
    for (const auto& r : s.changes().rows())
        if (r.group == FileGroup::Commit)
            out.push_back(r.path);
    return out;
}

// The "other" commit for a dialog opened on `row`: another commit of the History selection, else
// HEAD. `ref` is what a dialog field holds: HEAD's branch name, or a short id; empty (and a null
// `id`) when there is none (unborn HEAD, or the other commit is `row` itself).
struct Other {
    core::Oid id;
    std::string ref;
};

Other otherCommit(Session& s, const core::HistoryRow& row)
{
    std::vector<core::Oid> picked;
    if (s.selection().kind == SelKind::Commit)
        picked.push_back(s.selection().id);
    for (const auto& e : s.history().extraSelection())
        picked.push_back(e);
    for (const auto& id : picked)
        if (id != row.id && !id.isNull())
            return {id, id.hex()};
    const auto snap = s.snapshot();
    if (snap->headUnborn || snap->head.isNull() || snap->head == row.id)
        return {};
    return {snap->head, snap->headDetached || snap->headBranch.empty() ? snap->head.hex() : snap->headBranch};
}

// What Squash acts on: the commits to fold into one, newest first. One selected commit with a
// single parent: it and its parent. Adjacent selected commits (none a merge): exactly those.
// Empty, and `why` set, when the selection cannot be squashed.
std::vector<core::Oid> squashCommits(Session& s, const SelectionShape& sel, const char** why)
{
    *why = nullptr;
    std::vector<core::Oid> out;
    if (sel.ids.empty() || !(sel.single() || sel.range())) {
        *why = "Select one commit, or adjacent commits (no gaps).";
        return out;
    }
    for (const auto& id : sel.ids) {
        const core::HistoryRow* r = s.history().row(id);
        if (!r || r->parents.size() > 1) {
            *why = "A merge commit cannot be squashed.";
            return out;
        }
    }
    out = sel.ids;
    if (sel.single()) {
        const core::HistoryRow* r = s.history().row(sel.ids.front());
        if (r->parents.empty()) {
            *why = "A root commit has no parent to squash into.";
            return {};
        }
        out.push_back(r->parents.front());
    }
    return out;
}

} // namespace

SelectionShape selectionShape(Session& s)
{
    SelectionShape shape;
    if (s.selection().kind != SelKind::Commit)
        return shape;
    shape.ids.push_back(s.selection().id);
    for (const auto& e : s.history().extraSelection())
        if (std::find(shape.ids.begin(), shape.ids.end(), e) == shape.ids.end())
            shape.ids.push_back(e);
    const auto& history = s.history();
    auto position = [&](const core::Oid& id) {
        const core::HistoryRow* r = history.row(id);
        return r ? r - history.rows().data() : std::ptrdiff_t(-1);
    };
    std::sort(shape.ids.begin(), shape.ids.end(), [&](const core::Oid& a, const core::Oid& b) { return position(a) < position(b); });
    shape.contiguous = true;
    for (size_t i = 0; i + 1 < shape.ids.size(); ++i) {
        const core::HistoryRow* r = history.row(shape.ids[i]);
        if (!r || r->parents.empty() || r->parents.front() != shape.ids[i + 1])
            shape.contiguous = false;
    }
    return shape;
}

void disabledHint(bool disabled, const char* why)
{
    if (disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        tooltip("%s", why);
}

namespace {

// What the commit menu items share: whether an action can start, the selection and the Shift state.
struct MenuContext
{
    Session& session;
    const core::HistoryRow& row;
    bool free;
    SelectionShape sel;
    // Holding Shift swaps an item for its sibling (the same variants as the Shift+key hotkeys).
    bool shift;
    bool merge;
    bool headCommit; // HEAD has a commit
    bool isHead;     // this commit is HEAD

    MenuContext(Session& s, const core::HistoryRow& r)
        : session(s)
        , row(r)
        , free(::ggui::free(s))
        , sel(selectionShape(s))
        , shift(ImGui::GetIO().KeyShift)
        , merge(r.parents.size() > 1)
    {
        const auto snap = s.snapshot();
        isHead = snap->head == r.id; // null when unborn
        headCommit = !snap->headUnborn;
    }

    // An item acting on one commit: disabled unless exactly one commit is selected (and `enabled`).
    // `why` explains a disabled `enabled`. `withOther`: a second selected commit is allowed (the
    // item's dialog takes it as the other commit).
    bool one(const char* icon, const char* label, const char* shortcut, bool enabled = true, const char* why = nullptr,
             bool withOther = false) const
    {
        const bool shapeOk = sel.single() || (withOther && sel.count() == 2);
        const bool hit = menuItem(icon, label, shortcut, false, free && shapeOk && enabled);
        if (!shapeOk)
            disabledHint(true, withOther ? "Needs one selected commit, or two (the second is the other commit)."
                                         : "Needs a single selected commit.");
        else if (why)
            disabledHint(!enabled, why);
        return hit;
    }
};

} // namespace

void drawCommitIntegrateItems(Session& session, const core::HistoryRow& row)
{
    const MenuContext c(session, row);
    const Other other = otherCommit(session, row);
    // HEAD and this commit (plan §4.3 "Merge into @"): also in Branches.
    if (c.one(ICON_MS_MERGE, "Merge into HEAD...", nullptr, c.headCommit && !c.isHead,
            c.headCommit ? "This commit is HEAD." : "HEAD has no commit yet."))
        showMergeDialog(session, row.id.hex(), true);
    if (c.one(ICON_MS_LOW_PRIORITY, "Rebase onto...", nullptr, true, nullptr, true))
        showRebaseDialog(session, row.id, other.ref);
    if (c.one(ICON_MS_LOW_PRIORITY, "Interactive rebase...", "I"))
        openInteractiveRebase(session, row.id);
    // The label names the branch; the ID stays the same. Enabled on HEAD too (Mixed unstages, Hard discards).
    const auto snap = session.snapshot();
    const bool onBranch = !snap->headDetached && !snap->headUnborn && !snap->headBranch.empty();
    const bool idle = snap->state == core::RepoState::None;
    const std::string label = (onBranch ? "Reset " + snap->headBranch + " to here..." : std::string("Reset to here..."))
        + "###reset_here";
    if (c.one(ICON_MS_RESTART_ALT, label.c_str(), nullptr, onBranch && idle,
            onBranch ? "An operation is in progress." : "HEAD is not on a branch."))
        showResetDialog(session, row.id);
}

void drawCommitPickItems(Session& session, const core::HistoryRow& row)
{
    const MenuContext c(session, row);
    // Revert / cherry-pick onto HEAD (plan §4.3), each selected commit in the order git takes them.
    // A merge commit's change is taken against its first parent (-m 1). Picking an ancestor of HEAD
    // other than HEAD is refused on the worker (for every selected commit, not only this one).
    const std::string blocked = !c.headCommit ? "HEAD has no commit yet."
        : c.isHead                            ? "This commit is HEAD: its change is already there."
                                              : "";
    const size_t count = c.sel.count();
    auto item = [&](const char* icon, const char* name, const char* id, bool enabled, const char* what, bool revert, bool commit) {
        std::string label = name;
        if (count > 1)
            label += " " + std::to_string(count) + " commits";
        if (!commit)
            label += " (no commit)";
        label += std::string("###") + id;
        if (menuItem(icon, label.c_str(), nullptr, false, c.free && count > 0 && enabled))
            session.actions().revertOrPick(c.sel.ids, revert, commit);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            std::string tip = what;
            if (c.merge)
                tip += "\nA merge commit: its change against its first parent (-m 1).";
            if (count > 1)
                tip += revert ? "\nThe selected commits, the newest first." : "\nThe selected commits, the oldest first.";
            if (count > 0 && !enabled)
                tip += "\n" + (revert ? std::string("HEAD has no commit yet.") : blocked);
            tooltip("%s", tip.c_str());
        }
    };
    item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick", "cherry_pick", blocked.empty(),
        "A copy of the commit on HEAD, with its author. Text conflicts become first-class conflicts.", false, true);
    item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick", "cherry_pick_no_commit", blocked.empty(),
        "Apply the commit's change to the index and working tree, without committing (git cherry-pick --no-commit).", false, false);
    item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert", "revert", c.headCommit,
        "A new commit on HEAD that undoes the commit. Text conflicts become first-class conflicts.", true, true);
    item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert", "revert_no_commit", c.headCommit,
        "Undo the commit's change in the index and working tree, without committing (git revert --no-commit).", true, false);
}

void drawCommitEditItems(Session& session, const core::HistoryRow& row)
{
    const MenuContext c(session, row);
    if (menuItem(ICON_MS_EDIT, "Edit commit (checkout detached)", "E", false, c.free && c.sel.single()))
        session.actions().editCommit(row.id);
    if (!c.sel.single())
        disabledHint(true, "Needs a single selected commit.");
    if (c.one(ICON_MS_CONTROL_POINT_DUPLICATE, c.shift ? "Duplicate branch" : "Duplicate", c.shift ? "Shift+D" : "D"))
        session.actions().duplicate(row.id, c.shift);
    if (c.shift) {
        if (c.one(ICON_MS_JOIN_INNER, "Squash descendants into this", "Shift+S"))
            session.actions().squashDescendants(row.id);
    } else {
        const char* why = nullptr;
        const auto commits = squashCommits(session, c.sel, &why);
        const bool hit = menuItem(ICON_MS_JOIN_INNER, "Squash...", "S", false, c.free && !commits.empty());
        disabledHint(commits.empty() && why, why);
        if (hit)
            showSquashDialog(session, commits);
    }
    if (c.one(ICON_MS_CALL_SPLIT, "Split...", "Alt+S", !c.merge, "A merge commit cannot be split."))
        showSplitDialog(session, row.id);
    if (c.one(ICON_MS_ACCOUNT_TREE, "Simplify parents", nullptr, c.merge, "Only a merge commit has parents to simplify."))
        session.actions().simplifyParents(row.id);
    if (c.shift) {
        if (c.one(ICON_MS_DELETE_FOREVER, "Drop commit and descendants...", "Shift+A"))
            showAbandonBranchDialog(session, row.id);
    } else if (c.one(ICON_MS_DELETE_FOREVER, "Drop commit...", "A")) {
        showAbandonDialog(session, row.id);
    }
}

void handleCommitEditKeys(Session& session, const core::HistoryRow& row)
{
    if (!free(session))
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl)
        return;
    // Alt+S is a routed shortcut: releasing Alt does not toggle the menu layer.
    if (hotkey(ImGuiMod_Alt | ImGuiKey_S, ImGuiInputFlags_RouteFocused)) {
        showSplitDialog(session, row.id);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D, false))
        session.actions().duplicate(row.id, io.KeyShift);
    else if (ImGui::IsKeyPressed(ImGuiKey_I, false) && !io.KeyShift && !io.KeyAlt)
        openInteractiveRebase(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && io.KeyAlt)
        return; // Alt+S never squashes
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && io.KeyShift)
        session.actions().squashDescendants(row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        squashSelection(session);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false) && io.KeyShift)
        showAbandonBranchDialog(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        showAbandonDialog(session, row.id);
}

void showRebaseDialog(Session& session, const core::Oid& commit, const std::string& prefill)
{
    Form f;
    f.title = "Rebase onto";
    f.message = "Move this commit onto another commit.";
    f.add(commitInfo(session, "Rebase", commit));
    f.add(commitField(session, "destination", "Onto (branch, tag or commit)", prefill));
    Field with{Field::Check, "with_descendants", "With its descendants"};
    with.checked = true;
    f.add(with);
    Session* s = &session;
    f.buttons.push_back({"Rebase",
        [s, commit](Form& form) { s->actions().rebaseOnto(commit, gg::trim(form.text("destination")), form.checked("with_descendants")); },
        [](const Form& form) { return !gg::trim(form.text("destination")).empty(); }});
    // The commit and its descendants onto the destination, as a starting todo.
    f.buttons.push_back({"Open as interactive rebase...",
        [s, commit](Form& form) {
            RebasePanel::Request r;
            r.from = commit.hex();
            r.onto = gg::trim(form.text("destination"));
            r.selected = {commit.hex()};
            s->rebase().open(std::move(r));
        },
        [](const Form& form) { return !gg::trim(form.text("destination")).empty() && form.checked("with_descendants"); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void squashSelection(Session& session)
{
    const char* why = nullptr;
    if (const auto commits = squashCommits(session, selectionShape(session), &why); !commits.empty())
        showSquashDialog(session, commits);
}

void showSquashDialog(Session& session, const std::vector<core::Oid>& commits)
{
    if (commits.size() < 2)
        return;
    // The prefilled message is the inputs' full messages, which the worker reads first.
    Session* s = &session;
    session.commitMessages(commits, [s, commits](const std::vector<std::string>& messages) {
        Form f;
        f.title = "Squash";
        f.message = "The commits become one; the new commit takes the message below.";
        for (size_t i = 0; i < commits.size(); ++i) {
            const bool into = i + 1 == commits.size();
            Field info = commitInfo(*s, into ? "Into" : "Squash", commits[i]);
            info.id = "info_squash_" + std::to_string(i);
            f.add(std::move(info));
        }
        std::string text; // oldest first, a blank line between
        for (size_t i = messages.size(); i-- > 0;) {
            std::string m = messages[i];
            while (!m.empty() && (m.back() == '\n' || m.back() == '\r'))
                m.pop_back();
            if (!text.empty())
                text += "\n\n";
            text += m;
        }
        Field message{Field::Multiline, "message", "Message"};
        message.text = text;
        f.add(std::move(message));
        f.buttons.push_back({"Squash", [s, commits](Form& form) { s->actions().squashRange(commits, form.text("message")); },
            [](const Form& form) { return !gg::trim(form.text("message")).empty(); }});
        // From the base on, the other commits changed to squash (messages are then edited there).
        const core::HistoryRow* base = s->history().row(commits.back());
        const bool baseHasParent = base && !base->parents.empty();
        f.buttons.push_back({"Open as interactive rebase...",
            [s, commits](Form&) {
                RebasePanel::Request r;
                r.from = commits.back().hex();
                r.tipContaining = commits.front().hex();
                std::set<std::string> folded;
                for (size_t i = 0; i + 1 < commits.size(); ++i) {
                    r.selected.push_back(commits[i].hex());
                    folded.insert(commits[i].hex());
                }
                r.adjust = [folded](gg::todo::Todo& t, const gg::todo::Context&) {
                    for (auto& item : t.items)
                        if (item.isCommit() && folded.count(item.commit))
                            item.action = gg::todo::Action::Squash;
                };
                s->rebase().open(std::move(r));
            },
            [baseHasParent](const Form&) { return baseHasParent; }});
        f.buttons.push_back({"Cancel", {}});
        s->app().dialogs().open(std::move(f));
    });
}

void showSplitDialog(Session& session, const core::Oid& commit)
{
    const auto files = commitFiles(session, commit);
    if (files.size() < 2) {
        session.app().notify(App::Notice::Warning, "Split",
            "Select the commit first; it needs at least two changed files to split by files.");
        return;
    }
    Form f;
    f.title = "Split";
    f.message = "The checked files go into a new first commit; the rest stays in this one.";
    f.add(commitInfo(session, "Split", commit));
    for (size_t i = 0; i < files.size(); ++i)
        f.add(Field{Field::Check, "file_" + std::to_string(i), files[i]});
    f.add(Field{Field::Text, "message", "Message of the first commit", {}, false, 0, {}, "empty = the same message"});
    Session* s = &session;
    f.buttons.push_back({"Split",
        [s, commit, files](Form& form) {
            std::vector<std::string> picked;
            for (size_t i = 0; i < files.size(); ++i)
                if (form.checked("file_" + std::to_string(i)))
                    picked.push_back(files[i]);
            s->actions().split(commit, picked, form.text("message"));
        },
        [n = files.size()](const Form& form) {
            size_t picked = 0;
            for (size_t i = 0; i < n; ++i)
                picked += form.checked("file_" + std::to_string(i)) ? 1 : 0;
            return picked > 0 && picked < n;
        }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void showAbandonDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Drop commit";
    // Whatever pointed at the commit moves to its first parent's replacement, or to nothing when it has none.
    const core::HistoryRow* row = session.history().row(commit);
    f.message = "Drop this commit. Its descendants are rebased onto its parent.";
    if (row && row->parents.empty())
        f.message = "Drop this commit. Its children become root commits.";
    else if (row && row->parents.size() > 1)
        f.message = "Drop this merge. Its descendants are rebased onto its first parent; the merged-in commits are no longer part of them.";
    f.add(commitInfo(session, "Drop", commit));
    Session* s = &session;
    f.buttons.push_back({"Drop", [s, commit](Form&) { s->actions().abandon(commit, false); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void showResetDialog(Session& session, const core::Oid& commit)
{
    const std::string branch = session.snapshot()->headBranch;
    Form f;
    f.title = "Reset branch";
    f.message = "Move " + branch + " to this commit.";
    f.add(commitInfo(session, "Reset to", commit));
    Field mode{Field::Combo, "mode", "Mode"};
    mode.options = {"Soft: keep the index and the working tree", "Mixed: keep the working tree, reset the index",
        "Hard: reset the index and the working tree"};
    mode.choice = 1;
    f.add(mode);
    Session* s = &session;
    // A hard reset that would discard something is refused by the action, which then asks (Discard changes).
    f.buttons.push_back({"Reset", [s, commit, branch](Form& form) {
                             const int choice = form.choice("mode");
                             s->actions().resetBranch(branch, commit,
                                 choice == 0 ? Actions::ResetMode::Soft
                                     : choice == 2 ? Actions::ResetMode::Hard
                                                   : Actions::ResetMode::Mixed);
                         }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void showAbandonBranchDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Drop commit and descendants";
    f.message = "Drop this commit and all its descendants.";
    f.add(commitInfo(session, "Drop from", commit));
    Field del{Field::Check, "delete_branches", "Delete the branches that only point into it"};
    del.checked = true;
    f.add(del);
    f.add(Field{Field::Check, "delete_remote", "Also delete them on their remote"});
    Session* s = &session;
    f.buttons.push_back({"Drop", [s, commit](Form& form) {
                             const bool deleteBranches = form.checked("delete_branches");
                             const bool deleteRemote = form.checked("delete_remote");
                             // Branches at or after the commit (read now, before the rewrite moves them).
                             std::vector<std::pair<std::string, std::vector<std::string>>> doomed;
                             for (const auto& b : s->snapshot()->branches) {
                                 if (b.target == commit || s->history().descendsFrom(b.target, commit)) {
                                     std::vector<std::string> remotes;
                                     if (deleteRemote && !b.upstream.empty())
                                         remotes.push_back(b.upstream.substr(0, b.upstream.find('/')));
                                     doomed.emplace_back(b.name, remotes);
                                 }
                             }
                             s->actions().abandon(commit, true, [s, doomed, deleteBranches]() {
                                 if (!deleteBranches)
                                     return;
                                 for (const auto& [name, remotes] : doomed)
                                     s->actions().deleteBranch(name, true, remotes, true);
                             });
                         }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

// "Files: a, b" for up to four paths, else "Files: N selected".
static std::string filesSummary(const std::vector<std::string>& paths)
{
    std::string files = "Files: ";
    if (paths.size() <= 4) {
        for (size_t i = 0; i < paths.size(); ++i)
            files += (i ? ", " : "") + paths[i];
    } else {
        files += std::to_string(paths.size()) + " selected";
    }
    return files;
}

void showStashFilesDialog(Session& session, std::vector<std::string> paths, bool untracked, bool stagedOnly)
{
    Form f;
    f.title = "Stash selected files";
    f.message = stagedOnly ? "Stash the staged changes of the selected files." : "Stash the selected files.";
    f.add(Field{Field::Info, "info_files", "", filesSummary(paths)});
    f.add(Field{Field::Text, "message", "Message", "", false, 0, {}, "optional"});
    Session* s = &session;
    f.buttons.push_back({"Stash",
        [s, paths, untracked, stagedOnly](Form& form) {
            s->actions().stashPush(form.text("message"), false, untracked, stagedOnly, paths);
        },
        [paths](const Form&) { return !paths.empty(); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void showRestoreDialog(Session& session, const Selection& in, std::vector<std::string> paths, const std::string& prefill)
{
    const bool commit = in.kind == SelKind::Commit;
    Form f;
    f.title = "Restore";
    f.message = "Restore the selected files from another commit.";
    f.add(Field{Field::Info, "info_files", "", filesSummary(paths)});
    if (commit)
        f.add(commitInfo(session, "In", in.id));
    else
        f.add(Field{Field::Info, "info_in", "", in.kind == SelKind::Index ? "In: Index" : "In: Working tree"});
    f.add(commitField(session, "from", "From (branch, tag or commit)", prefill));
    if (commit) {
        Field where{Field::Combo, "where", "Restore into"};
        where.options = {"This commit (rewrite it)", "The working tree (git restore)"};
        f.add(where);
    }
    Session* s = &session;
    const core::Oid id = in.id;
    f.buttons.push_back({"Restore",
        [s, commit, id, paths](Form& form) {
            const std::string from = gg::trim(form.text("from"));
            if (commit && form.choice("where") == 0)
                s->actions().restorePaths(id, from, paths);
            else
                s->actions().restoreWorktree(from, paths);
        },
        [paths](const Form& form) { return !paths.empty() && !gg::trim(form.text("from")).empty(); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void showMergeDialog(Session& session, const std::string& branch, bool commit)
{
    Form f;
    f.title = "Merge into HEAD";
    f.message = "Merge another commit into the current one.";
    f.add(commitInfo(session, "Into HEAD", std::string("HEAD")));
    f.add(commitField(session, "rev", "Merge (branch, tag or commit)", branch));
    const std::string quote = commit ? "Merge commit '" : "Merge branch '";
    f.add(Field{Field::Text, "message", "Message", quote + branch + "'"});
    f.add(Field{Field::Check, "native", "Use native git merge (stops with index conflicts)"});
    Session* s = &session;
    f.buttons.push_back({"Merge",
        [s, branch, quote](Form& form) {
            const std::string rev = gg::trim(form.text("rev"));
            if (form.checked("native"))
                s->actions().mergeNative(rev);
            else
                // The default message names the commit it was made for.
                s->actions().mergeIntoHead(rev, form.text("message") == quote + branch + "'" ? quote + rev + "'" : form.text("message"));
        },
        [](const Form& form) { return !gg::trim(form.text("rev")).empty(); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

void openInteractiveRebase(Session& session, const core::Oid& commit)
{
    RebasePanel::Request r;
    r.from = commit.hex();
    r.selected = {commit.hex()};
    session.rebase().open(std::move(r));
}

void openInteractiveRebaseSelection(Session& session, const std::vector<core::Oid>& commits)
{
    // History lists newer commits first: the oldest selected commit is the last one listed.
    const auto& history = session.history();
    auto position = [&](const core::Oid& id) {
        const core::HistoryRow* row = history.row(id);
        return row ? row - history.rows().data() : -1;
    };
    std::vector<core::Oid> sorted = commits;
    std::sort(sorted.begin(), sorted.end(), [&](const core::Oid& a, const core::Oid& b) { return position(a) < position(b); });
    RebasePanel::Request r;
    r.from = sorted.back().hex();
    r.tipContaining = sorted.front().hex();
    for (const auto& id : sorted)
        r.selected.push_back(id.hex());
    session.rebase().open(std::move(r));
}

void showInteractiveRebaseDialog(Session& session, const std::string& tip)
{
    Form f;
    f.title = "Interactive rebase onto";
    f.message = "The commits not on the base are listed in the todo editor.";
    f.add(commitInfo(session, "Rebase", tip));
    // The branch's upstream, else the commit before the tip.
    std::string prefill = tip + "~1";
    const auto snap = session.snapshot();
    if (snap) {
        const core::BranchInfo* b = tip == "HEAD" ? snap->currentBranch() : snap->findBranch(tip);
        if (b && !b->upstream.empty() && !b->upstreamGone)
            prefill = b->upstream;
    }
    f.add(commitField(session, "base", "Onto base (branch, tag or commit)", prefill));
    Session* s = &session;
    f.buttons.push_back({"Open",
        [s, tip](Form& form) {
            RebasePanel::Request r;
            r.upstream = gg::trim(form.text("base"));
            r.tip = tip;
            s->rebase().open(std::move(r));
        },
        [](const Form& form) { return !gg::trim(form.text("base")).empty(); }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
}

} // namespace ggui
