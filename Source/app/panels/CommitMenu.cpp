#include "panels/CommitMenu.hpp"

#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

#include <IconsMaterialSymbols.h>
#include <imgui.h>

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

} // namespace

void drawCommitEditItems(Session& session, const core::HistoryRow& row)
{
    const bool ok = free(session);
    const bool merge = row.parents.size() > 1;
    const bool root = row.parents.empty();
    if (menuItem(ICON_MS_ADD, "New commit before", nullptr, false, ok))
        session.actions().insertCommit(row.id, true, {});
    if (menuItem(ICON_MS_ADD, "New commit after", nullptr, false, ok))
        session.actions().insertCommit(row.id, false, {});
    ImGui::Separator();
    if (menuItem(ICON_MS_CONTROL_POINT_DUPLICATE, "Duplicate", "D", false, ok))
        session.actions().duplicate(row.id, false);
    if (menuItem(ICON_MS_CONTROL_POINT_DUPLICATE, "Duplicate branch", "Shift+D", false, ok))
        session.actions().duplicate(row.id, true);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase onto...", nullptr, false, ok))
        showRebaseDialog(session, row.id);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Interactive rebase from here...", "I", false, ok))
        openInteractiveRebase(session, row.id);
    // HEAD and this commit (plan §4.3 "Merge into @", "Rebase @ onto"): also in Branches.
    const auto snap = session.snapshot();
    const bool isHead = snap->head == row.id; // null when unborn
    const bool headCommit = !snap->headUnborn;
    if (menuItem(ICON_MS_MERGE, "Merge into HEAD...", nullptr, false, ok && headCommit && !isHead))
        showMergeDialog(session, session.shortId(row.id), true);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase HEAD onto this", nullptr, false, ok && headCommit && !snap->headDetached && !isHead))
        session.actions().rebaseHeadOnto(session.shortId(row.id));
    // Revert / cherry-pick onto HEAD (plan §4.3). A merge commit's change is taken against its
    // first parent (-m 1). Picking an ancestor of HEAD other than HEAD is refused on the worker.
    {
        const std::string blocked = !headCommit ? "HEAD has no commit yet."
            : isHead                            ? "This commit is HEAD: its change is already there."
                                                : "";
        auto item = [&](const char* icon, const char* label, bool enabled, const char* what, bool revert, bool commit) {
            if (menuItem(icon, label, nullptr, false, ok && enabled))
                session.actions().revertOrPick(row.id, revert, commit);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
                std::string tip = what;
                if (merge)
                    tip += "\nA merge commit: its change against its first parent (-m 1).";
                if (!enabled)
                    tip += "\n" + (revert ? std::string("HEAD has no commit yet.") : blocked);
                ImGui::SetTooltip("%s", tip.c_str());
            }
        };
        item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert", headCommit,
            "Undo this commit's change in the index and working tree, without committing (git revert --no-commit).", true, false);
        item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert and commit", headCommit,
            "A new commit on HEAD that undoes this commit. Text conflicts become first-class conflicts.", true, true);
        item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick", blocked.empty(),
            "Apply this commit's change to the index and working tree, without committing (git cherry-pick --no-commit).", false,
            false);
        item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick and commit", blocked.empty(),
            "A copy of this commit on HEAD, with its author. Text conflicts become first-class conflicts.", false, true);
    }
    if (menuItem(ICON_MS_JOIN_INNER, "Squash...", "S", false, ok && !merge && !root))
        showSquashDialog(session, row.id);
    if (menuItem(ICON_MS_JOIN_INNER, "Squash descendants into this", "Shift+S", false, ok))
        session.actions().squashDescendants(row.id);
    if (menuItem(ICON_MS_CALL_SPLIT, "Split...", "Alt+S", false, ok && !merge))
        showSplitDialog(session, row.id);
    if (menuItem(ICON_MS_RESTORE, "Restore from...", nullptr, false, ok))
        showRestoreDialog(session, row.id);
    if (menuItem(ICON_MS_ACCOUNT_TREE, "Simplify parents", nullptr, false, ok && merge))
        session.actions().simplifyParents(row.id);
    ImGui::Separator();
    if (menuItem(ICON_MS_DELETE_FOREVER, "Abandon", "A", false, ok))
        session.actions().abandon(row.id, false);
    if (menuItem(ICON_MS_DELETE_FOREVER, "Abandon branch...", "Shift+A", false, ok))
        showAbandonBranchDialog(session, row.id);
}

void handleCommitEditKeys(Session& session, const core::HistoryRow& row)
{
    if (!free(session))
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_D, false))
        session.actions().duplicate(row.id, io.KeyShift);
    else if (ImGui::IsKeyPressed(ImGuiKey_I, false) && !io.KeyShift && !io.KeyAlt)
        openInteractiveRebase(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && io.KeyAlt)
        showSplitDialog(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && io.KeyShift)
        session.actions().squashDescendants(row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && row.parents.size() == 1)
        showSquashDialog(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false) && io.KeyShift)
        showAbandonBranchDialog(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        session.actions().abandon(row.id, false);
}

void showRebaseDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Rebase onto";
    f.add(Field{Field::Text, "destination", "Destination (branch, tag or commit)"});
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

void showSquashDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Squash";
    f.add(Field{Field::Text, "target", "Into (empty = the parent; or an ancestor)"});
    Field combine{Field::Check, "combine", "Combine the messages (squash; otherwise keep the target's: fixup)"};
    combine.checked = true;
    f.add(combine);
    Session* s = &session;
    f.buttons.push_back({"Squash", [s, commit](Form& form) {
                             s->actions().squash(commit, gg::trim(form.text("target")), form.checked("combine"));
                         }});
    // From the target on, with the commit moved after it as squash (or fixup).
    f.buttons.push_back({"Open as interactive rebase...", [s, commit](Form& form) {
                             const std::string target = gg::trim(form.text("target"));
                             const bool squash = form.checked("combine");
                             RebasePanel::Request r;
                             r.from = target.empty() ? commit.hex() + "^" : target;
                             r.tipContaining = commit.hex();
                             r.selected = {commit.hex()};
                             const std::string id = commit.hex();
                             r.adjust = [id, squash](gg::todo::Todo& t, const gg::todo::Context& c) {
                                 auto& items = t.items;
                                 auto find = [&](const std::string& commitId) {
                                     return std::find_if(items.begin(), items.end(),
                                         [&](const gg::todo::Item& i) { return i.isCommit() && i.commit == commitId; });
                                 };
                                 auto src = find(id);
                                 if (c.range.empty() || src == items.end() || src->commit == c.range.front())
                                     return;
                                 gg::todo::Item moved = *src;
                                 moved.action = squash ? gg::todo::Action::Squash : gg::todo::Action::Fixup;
                                 items.erase(src);
                                 auto at = find(c.range.front()) + 1;
                                 while (at != items.end()
                                     && (at->action == gg::todo::Action::Squash || at->action == gg::todo::Action::Fixup))
                                     ++at;
                                 items.insert(at, moved);
                             };
                             s->rebase().open(std::move(r));
                         }});
    f.buttons.push_back({"Cancel", {}});
    session.app().dialogs().open(std::move(f));
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

void showAbandonBranchDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Abandon branch";
    f.message = "Drop this commit and everything after it.";
    Field del{Field::Check, "delete_branches", "Delete the branches that only point into it"};
    del.checked = true;
    f.add(del);
    f.add(Field{Field::Check, "delete_remote", "Also delete them on their remote"});
    Session* s = &session;
    f.buttons.push_back({"Abandon", [s, commit](Form& form) {
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

void showRestoreDialog(Session& session, const core::Oid& commit)
{
    std::vector<std::string> paths = session.selectedPaths();
    Form f;
    f.title = "Restore";
    f.message = paths.empty() ? "Select files in Changes first." : std::to_string(paths.size()) + " selected file(s).";
    f.add(Field{Field::Text, "from", "From (branch, tag or commit)"});
    Field where{Field::Combo, "where", "Restore into"};
    where.options = {"This commit (rewrite it)", "The working tree (git restore)"};
    f.add(where);
    Session* s = &session;
    f.buttons.push_back({"Restore",
        [s, commit, paths](Form& form) {
            const std::string from = gg::trim(form.text("from"));
            if (form.choice("where") == 0)
                s->actions().restorePaths(commit, from, paths);
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
    f.message = "Merge " + std::string(commit ? "commit " : "") + branch + " into HEAD.";
    f.add(Field{Field::Text, "message", "Message", (commit ? "Merge commit '" : "Merge branch '") + branch + "'"});
    f.add(Field{Field::Check, "native", "Use native git merge (stops with index conflicts)"});
    Session* s = &session;
    f.buttons.push_back({"Merge", [s, branch](Form& form) {
                             if (form.checked("native"))
                                 s->actions().mergeNative(branch);
                             else
                                 s->actions().mergeIntoHead(branch, form.text("message"));
                         }});
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
    f.message = "Rebase " + tip + " interactively: the commits not on the base are listed in the todo editor.";
    f.add(Field{Field::Text, "base", "Base (branch, tag or commit)"});
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
