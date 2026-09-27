#include "panels/CommitMenu.hpp"

#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

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
    if (ImGui::MenuItem("New commit before", nullptr, false, ok))
        session.actions().insertCommit(row.id, true, {});
    if (ImGui::MenuItem("New commit after", nullptr, false, ok))
        session.actions().insertCommit(row.id, false, {});
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "D", false, ok))
        session.actions().duplicate(row.id, false);
    if (ImGui::MenuItem("Duplicate branch", "Shift+D", false, ok))
        session.actions().duplicate(row.id, true);
    if (ImGui::MenuItem("Rebase onto...", nullptr, false, ok))
        showRebaseDialog(session, row.id);
    if (ImGui::MenuItem("Squash...", "S", false, ok && !merge && !root))
        showSquashDialog(session, row.id);
    if (ImGui::MenuItem("Squash descendants into this", "Shift+S", false, ok))
        session.actions().squashDescendants(row.id);
    if (ImGui::MenuItem("Split...", "Alt+S", false, ok && !merge))
        showSplitDialog(session, row.id);
    if (ImGui::MenuItem("Restore from...", nullptr, false, ok))
        showRestoreDialog(session, row.id);
    if (ImGui::MenuItem("Simplify parents", nullptr, false, ok && merge))
        session.actions().simplifyParents(row.id);
    ImGui::Separator();
    if (ImGui::MenuItem("Abandon", "A", false, ok))
        session.actions().abandon(row.id, false);
    if (ImGui::MenuItem("Abandon branch...", "Shift+A", false, ok))
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
                             if (const auto snap = s->snapshot())
                                 for (const auto& b : snap->branches) {
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

void showMergeDialog(Session& session, const std::string& branch)
{
    Form f;
    f.title = "Merge into HEAD";
    f.message = "Merge " + branch + " into HEAD.";
    f.add(Field{Field::Text, "message", "Message", "Merge branch '" + branch + "'"});
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

} // namespace ggui
