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
            return {id, s.shortId(id)};
    const auto snap = s.snapshot();
    if (snap->headUnborn || snap->head.isNull() || snap->head == row.id)
        return {};
    return {snap->head, snap->headDetached || snap->headBranch.empty() ? s.shortId(snap->head) : snap->headBranch};
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
        ImGui::SetTooltip("%s", why);
}

void drawCommitEditItems(Session& session, const core::HistoryRow& row)
{
    const bool free = ::ggui::free(session);
    const SelectionShape sel = selectionShape(session);
    const Other other = otherCommit(session, row);
    const bool merge = row.parents.size() > 1;
    // Holding Shift swaps an item for its sibling (the same variants as the Shift+key hotkeys).
    const bool shift = ImGui::GetIO().KeyShift;
    // An item acting on one commit: disabled unless exactly one commit is selected (and `enabled`).
    // `why` explains a disabled `enabled`. `withOther`: a second selected commit is allowed (the
    // item's dialog takes it as the other commit).
    auto one = [&](const char* icon, const char* label, const char* shortcut, bool enabled = true, const char* why = nullptr,
                   bool withOther = false) {
        const bool shapeOk = sel.single() || (withOther && sel.count() == 2);
        const bool hit = menuItem(icon, label, shortcut, false, free && shapeOk && enabled);
        if (!shapeOk)
            disabledHint(true, withOther ? "Needs one selected commit, or two (the second is the other commit)."
                                         : "Needs a single selected commit.");
        else if (why)
            disabledHint(!enabled, why);
        return hit;
    };
    if (one(ICON_MS_EDIT, "Edit commit", "E"))
        session.actions().editCommit(row.id);
    ImGui::Separator();
    if (one(ICON_MS_CONTROL_POINT_DUPLICATE, shift ? "Duplicate branch" : "Duplicate", shift ? "Shift+D" : "D"))
        session.actions().duplicate(row.id, shift);
    if (one(ICON_MS_LOW_PRIORITY, "Rebase onto...", nullptr, true, nullptr, true))
        showRebaseDialog(session, row.id, other.ref);
    if (one(ICON_MS_LOW_PRIORITY, "Interactive rebase...", "I"))
        openInteractiveRebase(session, row.id);
    // HEAD and this commit (plan §4.3 "Merge into @"): also in Branches.
    const auto snap = session.snapshot();
    const bool isHead = snap->head == row.id; // null when unborn
    const bool headCommit = !snap->headUnborn;
    if (one(ICON_MS_MERGE, "Merge into HEAD...", nullptr, headCommit && !isHead,
            headCommit ? "This commit is HEAD." : "HEAD has no commit yet."))
        showMergeDialog(session, session.shortId(row.id), true);
    // Revert / cherry-pick onto HEAD (plan §4.3). A merge commit's change is taken against its
    // first parent (-m 1). Picking an ancestor of HEAD other than HEAD is refused on the worker.
    {
        const std::string blocked = !headCommit ? "HEAD has no commit yet."
            : isHead                            ? "This commit is HEAD: its change is already there."
                                                : "";
        auto item = [&](const char* icon, const char* label, bool enabled, const char* what, bool revert, bool commit) {
            if (menuItem(icon, label, nullptr, false, free && sel.single() && enabled))
                session.actions().revertOrPick(row.id, revert, commit);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
                std::string tip = what;
                if (merge)
                    tip += "\nA merge commit: its change against its first parent (-m 1).";
                if (!sel.single())
                    tip += "\nNeeds a single selected commit.";
                else if (!enabled)
                    tip += "\n" + (revert ? std::string("HEAD has no commit yet.") : blocked);
                ImGui::SetTooltip("%s", tip.c_str());
            }
        };
        if (shift)
            item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert and commit", headCommit,
                "A new commit on HEAD that undoes this commit. Text conflicts become first-class conflicts.", true, true);
        else
            item(ICON_MS_SETTINGS_BACKUP_RESTORE, "Revert", headCommit,
                "Undo this commit's change in the index and working tree, without committing (git revert --no-commit).", true, false);
        if (shift)
            item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick and commit", blocked.empty(),
                "A copy of this commit on HEAD, with its author. Text conflicts become first-class conflicts.", false, true);
        else
            item(ICON_MS_CONTENT_PASTE_GO, "Cherry-pick", blocked.empty(),
                "Apply this commit's change to the index and working tree, without committing (git cherry-pick --no-commit).", false,
                false);
    }
    if (shift) {
        if (one(ICON_MS_JOIN_INNER, "Squash descendants into this", "Shift+S"))
            session.actions().squashDescendants(row.id);
    } else {
        const char* why = nullptr;
        const auto commits = squashCommits(session, sel, &why);
        const bool hit = menuItem(ICON_MS_JOIN_INNER, "Squash...", "S", false, free && !commits.empty());
        disabledHint(commits.empty() && why, why);
        if (hit)
            showSquashDialog(session, commits);
    }
    if (one(ICON_MS_CALL_SPLIT, "Split...", "Alt+S", !merge, "A merge commit cannot be split."))
        showSplitDialog(session, row.id);
    if (one(ICON_MS_RESTORE, "Restore from...", nullptr, true, nullptr, true))
        showRestoreDialog(session, row.id, other.ref);
    if (one(ICON_MS_ACCOUNT_TREE, "Simplify parents", nullptr, merge, "Only a merge commit has parents to simplify."))
        session.actions().simplifyParents(row.id);
    ImGui::Separator();
    if (shift) {
        if (one(ICON_MS_DELETE_FOREVER, "Abandon branch...", "Shift+A"))
            showAbandonBranchDialog(session, row.id);
    } else if (one(ICON_MS_DELETE_FOREVER, "Abandon", "A")) {
        session.actions().abandon(row.id, false);
    }
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
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        squashSelection(session);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false) && io.KeyShift)
        showAbandonBranchDialog(session, row.id);
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        session.actions().abandon(row.id, false);
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

void showAbandonBranchDialog(Session& session, const core::Oid& commit)
{
    Form f;
    f.title = "Abandon branch";
    f.message = "Drop this commit and everything after it.";
    f.add(commitInfo(session, "Abandon from", commit));
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

void showRestoreDialog(Session& session, const core::Oid& commit, const std::string& prefill)
{
    std::vector<std::string> paths = session.selectedPaths();
    Form f;
    f.title = "Restore";
    f.message = paths.empty() ? "Select files in Changes first."
                              : "Restore the " + std::to_string(paths.size()) + " selected file(s) from another commit.";
    f.add(commitInfo(session, "Commit", commit));
    f.add(commitField(session, "from", "From (branch, tag or commit)", prefill));
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
