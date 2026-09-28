#include "panels/SidePanels.hpp"
#include "panels/CommitMenu.hpp"

#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <map>

namespace ggui {

namespace {

// A row whose click toggles visibility in History (Ctrl-click: only this ref).
// Row IDs replace '/' with ':' so test references can address them (ref names contain '/').
std::string rowId(std::string id)
{
    std::replace(id.begin(), id.end(), '/', ':');
    return id;
}

bool visibilityRow(const std::string& rawId, const std::string& label, bool visible, bool outlined, ImU32 color)
{
    const std::string id = rowId(rawId);
    ImGui::PushID(id.c_str());
    ImGui::TextUnformatted(visible ? ICON_MS_VISIBILITY : ICON_MS_VISIBILITY_OFF);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, visible ? color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::Selectable((label + "###" + id).c_str(), false);
    ImGui::PopStyleColor();
    if (outlined) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(ImGuiCol_Text), 2.0f, 0, 1.5f);
    }
    ImGui::PopID();
    return clicked;
}

// The Remotes panel's menu for a remote; also on the remote and its remote-tracking branches in
// Branches.
void remoteMenuItems(Session& session, const core::RemoteInfo& r)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const auto snap = session.snapshot();
    const auto* current = snap->currentBranch();
    if (ImGui::MenuItem("Copy name"))
        ImGui::SetClipboardText(r.name.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Fetch", nullptr, false, free))
        actions.fetch(r.name, false, false);
    const bool pullable = free && current && current->upstream.rfind(r.name + "/", 0) == 0;
    if (ImGui::MenuItem("Pull", nullptr, false, pullable))
        actions.pull(PullMode::Config);
    bool prune = r.pruneOnFetch;
    if (ImGui::MenuItem("Prune on fetch", nullptr, &prune, free))
        actions.setPruneOnFetch(r.name, prune);
    if (ImGui::MenuItem("Edit URL...", nullptr, false, free))
        session.showEditRemoteDialog(r.name);
    if (ImGui::MenuItem("Delete", nullptr, false, free)) {
        Form f;
        f.title = "Delete remote";
        f.message = "Delete the remote '" + r.name + "' and its remote-tracking branches?";
        const std::string name = r.name;
        f.buttons.push_back({"Delete", [&actions, name](Form&) { actions.removeRemote(name); }});
        f.buttons.push_back({"Cancel", {}});
        session.app().dialogs().open(std::move(f));
    }
}

} // namespace

// ---- Branches -----------------------------------------------------------------------------------

void BranchesPanel::branchMenu(const core::BranchInfo& b)
{
    if (!ImGui::BeginPopupContextItem(("##branch_menu_" + rowId(b.name)).c_str()))
        return;
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const bool hasRemotes = !m_snapshot->remotes.empty();
    if (ImGui::MenuItem("Reveal"))
        m_session.revealCommit(b.target);
    if (ImGui::MenuItem("Copy name"))
        ImGui::SetClipboardText(b.name.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Check out", nullptr, false, free && !b.isHead))
        actions.checkout(b.name, false);
    const bool headAttached = !m_snapshot->headDetached && !m_snapshot->headUnborn;
    if (ImGui::MenuItem("Merge into HEAD...", nullptr, false, free && !b.isHead && !m_snapshot->headUnborn))
        showMergeDialog(m_session, b.name);
    if (ImGui::MenuItem("Rebase HEAD onto branch", nullptr, false, free && !b.isHead && headAttached))
        actions.rebaseHeadOnto(b.name);
    if (ImGui::MenuItem("Interactive rebase onto...", nullptr, false, free))
        showInteractiveRebaseDialog(m_session, b.name);
    if (b.isHead)
        disabledMenuItem("Check out in new worktree...", "Checked out in this worktree");
    else if (!b.worktree.empty())
        disabledMenuItem("Check out in new worktree...", ("Checked out in " + b.worktree).c_str());
    else if (ImGui::MenuItem("Check out in new worktree...", nullptr, false, free))
        m_session.showAddWorktreeDialog(1, b.name);
    if (ImGui::MenuItem("Push", nullptr, false, free && hasRemotes)) {
        if (!b.upstream.empty()) {
            const auto slash = b.upstream.find('/');
            actions.push(b.upstream.substr(0, slash), b.name, b.upstream.substr(slash + 1), false, false);
        } else {
            m_session.showPushToDialog(b.name);
        }
    }
    if (ImGui::MenuItem("Push to...", nullptr, false, free && hasRemotes))
        m_session.showPushToDialog(b.name);
    if (ImGui::MenuItem("Pull", nullptr, false, free && b.isHead && !b.upstream.empty()))
        actions.pull(PullMode::Config);
    if (ImGui::MenuItem("Reconcile with remote or branch...", nullptr, false, free && b.isHead)) {
        Form f;
        f.title = "Reconcile";
        f.message = b.upstream.empty() ? b.name + " has no upstream: name the branch to reconcile with."
                                       : b.name + " and " + b.upstream + " have diverged.";
        f.add(Field{Field::Text, "with", "With", b.upstream});
        Field how{Field::Combo, "how", "How"};
        how.options = {"Rebase my commits onto it", "Merge it in"};
        f.add(how);
        Session* s = &m_session;
        f.buttons.push_back({"Reconcile",
            [s](Form& form) {
                const std::string with = gg::trim(form.text("with"));
                if (form.choice("how") == 0)
                    s->actions().rebaseHeadOnto(with);
                else
                    s->actions().mergeIntoHead(with, {});
            },
            [](const Form& form) { return !gg::trim(form.text("with")).empty(); }});
        f.buttons.push_back({"Cancel", {}});
        m_session.app().dialogs().open(std::move(f));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename...", nullptr, false, free))
        m_session.showRenameBranchDialog(b.name);
    if (ImGui::BeginMenu("Delete", free)) {
        if (ImGui::MenuItem("Local", nullptr, false, !b.isHead))
            m_session.showDeleteBranchDialog(b.name, 0);
        if (ImGui::MenuItem("On its remote", nullptr, false, !b.upstream.empty()))
            m_session.showDeleteBranchDialog(b.name, 1);
        if (ImGui::MenuItem("Local and all remotes", nullptr, false, !b.isHead))
            m_session.showDeleteBranchDialog(b.name, 2);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Set upstream...", nullptr, false, free))
        m_session.showSetUpstreamDialog(b.name);
    if (ImGui::MenuItem("Unset upstream", nullptr, false, free && !b.upstream.empty()))
        actions.unsetUpstream(b.name);
    if (ImGui::MenuItem("Fast-forward to upstream", nullptr, false, free && !b.upstream.empty() && b.behind > 0 && b.ahead == 0))
        actions.fastForward(b.name);
    ImGui::EndPopup();
}

void BranchesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Branches, open)) {
        ImGui::End();
        return;
    }
    const bool free = m_session.actions().busy().empty();
    ImGui::BeginDisabled(!free || m_snapshot->head.isNull());
    if (ImGui::Button(ICON_MS_ADD "###create_branch"))
        m_session.showCreateBranchDialog(m_snapshot->head.hex());
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Create branch at HEAD...");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##branch_filter", ICON_MS_SEARCH " Filter", &m_filter);
    const Palette& p = theme().palette();
    auto& history = m_session.history();
    for (const auto& b : m_snapshot->branches) {
        if (!containsNoCase(b.name, m_filter))
            continue;
        std::string label = b.name;
        if (!b.upstream.empty()) {
            label += "  \xe2\x86\x92 " + b.upstream;
            if (b.upstreamGone)
                label += " (gone)";
            if (b.ahead)
                label += " \xe2\x86\x91" + std::to_string(b.ahead);
            if (b.behind)
                label += " \xe2\x86\x93" + std::to_string(b.behind);
        }
        if (!b.worktree.empty())
            label += "  [" + b.worktree + "]";
        const std::string full = "refs/heads/" + b.name;
        if (visibilityRow("branch_" + b.name, label, history.refVisible(full), b.isHead,
                b.isHead ? p.branchCurrent : ImGui::GetColorU32(ImGuiCol_Text)))
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        branchMenu(b);
    }
    // Remote-tracking branches under their remote.
    std::map<std::string, std::vector<const core::RemoteBranchInfo*>> byRemote;
    for (const auto& r : m_snapshot->remoteBranches)
        if (containsNoCase(r.name, m_filter))
            byRemote[r.remote].push_back(&r);
    for (const auto& [remote, list] : byRemote) {
        ImGui::PushID(("remote_group_" + remote).c_str());
        const core::RemoteInfo* info = nullptr;
        for (const auto& r : m_snapshot->remotes)
            if (r.name == remote)
                info = &r;
        const bool nodeOpen = ImGui::TreeNodeEx(remote.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        // The remote has the Remotes panel's menu.
        if (info && ImGui::BeginPopupContextItem("##remote_menu")) {
            remoteMenuItems(m_session, *info);
            ImGui::EndPopup();
        }
        if (nodeOpen) {
            for (const auto* r : list) {
                const std::string full = "refs/remotes/" + r->name;
                if (visibilityRow("rbranch_" + r->name, r->name, history.refVisible(full), false, p.remote))
                    history.toggleRef(full, ImGui::GetIO().KeyCtrl);
                if (ImGui::BeginPopupContextItem(("##rbranch_menu_" + rowId(r->name)).c_str())) {
                    if (ImGui::MenuItem("Reveal"))
                        m_session.revealCommit(r->target);
                    if (ImGui::MenuItem("Copy name"))
                        ImGui::SetClipboardText(r->name.c_str());
                    // ... and its remote's menu.
                    if (info && ImGui::BeginMenu(("Remote " + remote).c_str())) {
                        remoteMenuItems(m_session, *info);
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// ---- Tags ---------------------------------------------------------------------------------------

void TagsPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Tags, open)) {
        ImGui::End();
        return;
    }
    const bool freeTags = m_session.actions().busy().empty();
    ImGui::BeginDisabled(!freeTags || m_snapshot->head.isNull());
    if (ImGui::Button(ICON_MS_ADD "###create_tag"))
        m_session.showCreateTagDialog(m_snapshot->head.hex());
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Create tag at HEAD...");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##tag_filter", ICON_MS_SEARCH " Filter", &m_filter);
    auto& history = m_session.history();
    for (const auto& t : m_snapshot->tags) {
        if (!containsNoCase(t.name, m_filter))
            continue;
        const std::string full = "refs/tags/" + t.name;
        if (visibilityRow("tag_" + t.name, t.name, history.refVisible(full), false, theme().palette().tag))
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        if (t.annotated && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !t.message.empty())
            ImGui::SetTooltip("%s", t.message.c_str());
        if (ImGui::BeginPopupContextItem(("##tag_menu_" + rowId(t.name)).c_str())) {
            if (ImGui::MenuItem("Reveal"))
                m_session.revealCommit(t.target);
            if (ImGui::MenuItem("Copy name"))
                ImGui::SetClipboardText(t.name.c_str());
            ImGui::Separator();
            auto& actions = m_session.actions();
            const bool free = actions.busy().empty();
            if (ImGui::MenuItem("Delete", nullptr, false, free))
                actions.deleteTag(t.name);
            if (ImGui::BeginMenu("Push tag", free && !m_snapshot->remotes.empty())) {
                for (const auto& r : m_snapshot->remotes)
                    if (ImGui::MenuItem(r.name.c_str()))
                        actions.pushTag(r.name, t.name);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Delete on remote", free && !m_snapshot->remotes.empty())) {
                for (const auto& r : m_snapshot->remotes)
                    if (ImGui::MenuItem(r.name.c_str()))
                        actions.deleteRemoteTag(r.name, t.name);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// ---- Worktrees ----------------------------------------------------------------------------------

void WorktreesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Worktrees, open)) {
        ImGui::End();
        return;
    }
    const auto snap = m_session.snapshot();
    const bool free = m_session.actions().busy().empty();
    const bool canAdd = free && !snap->headUnborn;
    ImGui::BeginDisabled(!canAdd);
    if (ImGui::Button(ICON_MS_ADD "###add_worktree"))
        m_session.showAddWorktreeDialog();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(snap->headUnborn ? "Add worktree... (HEAD has no commit yet)" : "Add worktree...");
    for (const auto& w : snap->worktrees) {
        std::string label = w.name;
        if (w.isMain)
            label += " (main)";
        if (w.bare)
            label += " (bare)";
        if (w.locked)
            label += " " ICON_MS_LOCK;
        if (w.missing)
            label += " (missing)";
        if (w.prunable)
            label += " (prunable)";
        label += "  " + (w.branch.empty() ? (w.head.isNull() ? std::string("-") : w.head.shortHex(8)) : w.branch);
        ImGui::PushID(("worktree_" + w.name).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(w.missing ? ImGuiCol_TextDisabled : ImGuiCol_Text));
        ImGui::Selectable((label + "###row").c_str(), w.isCurrent);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            std::string tip = w.path.string();
            if (w.isCurrent)
                tip += "\nShown in this window";
            if (w.locked)
                tip += "\nLocked" + (w.lockReason.empty() ? std::string() : ": " + w.lockReason)
                    + " (git does not prune, move or remove it)";
            if (w.missing)
                tip += w.locked ? "\nMissing: its directory is gone (kept while locked; Repair if it was moved)"
                                : "\nMissing: its directory is gone. Prune removes its records; Repair reconnects it if it "
                                  "was moved";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (ImGui::BeginPopupContextItem("##worktree_menu")) {
            if (ImGui::MenuItem("Copy name"))
                ImGui::SetClipboardText(w.name.c_str());
            if (ImGui::MenuItem("Copy path"))
                ImGui::SetClipboardText(w.path.string().c_str());
            if (ImGui::MenuItem("Reveal HEAD", nullptr, false, !w.head.isNull()))
                m_session.revealCommit(w.head);
            if (ImGui::MenuItem("Open directory", nullptr, false, !w.missing))
                openInFileManager(w.path);
            ImGui::Separator();
            const std::string path = w.path.string();
            if (w.isCurrent)
                disabledMenuItem("Open here", "This window shows this worktree");
            else if (w.missing)
                disabledMenuItem("Open here", "Its directory is gone");
            else if (ImGui::MenuItem("Open here"))
                m_session.app().openRepository(w.path);
            if (w.missing)
                disabledMenuItem("Open in new window", "Its directory is gone");
            else if (ImGui::MenuItem("Open in new window"))
                m_session.actions().openInNewWindow(path);
            ImGui::Separator();
            if (ImGui::MenuItem("Add...", nullptr, false, canAdd))
                m_session.showAddWorktreeDialog();
            if (w.isMain)
                disabledMenuItem("Remove...", "The main worktree cannot be removed");
            else if (w.isCurrent)
                disabledMenuItem("Remove...", "This window shows this worktree: open another one first");
            else if (ImGui::MenuItem("Remove...", nullptr, false, free))
                m_session.showRemoveWorktreeDialog(w);
            if (w.isMain)
                disabledMenuItem("Lock...", "The main worktree cannot be locked");
            else if (w.locked ? ImGui::MenuItem("Unlock", nullptr, false, free) : ImGui::MenuItem("Lock...", nullptr, false, free)) {
                if (w.locked)
                    m_session.actions().unlockWorktree(path);
                else
                    m_session.showLockWorktreeDialog(w);
            }
            if (ImGui::MenuItem("Prune...", nullptr, false, free))
                m_session.showPruneWorktreesDialog();
            if (ImGui::MenuItem("Repair...", nullptr, false, free))
                m_session.showRepairWorktreeDialog(w);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// ---- Remotes ------------------------------------------------------------------------------------

void RemotesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Remotes, open)) {
        ImGui::End();
        return;
    }
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const auto snap = m_session.snapshot();
    ImGui::BeginDisabled(!free);
    if (ImGui::Button(ICON_MS_ADD "###add_remote"))
        m_session.showAddRemoteDialog();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Add remote...");
    ImGui::SameLine();
    if (ImGui::Button("Fetch all##fetch_all") && !snap->remotes.empty())
        actions.fetch("", false, false);
    ImGui::EndDisabled();
    for (const auto& r : snap->remotes) {
        ImGui::PushID(("remote_" + r.name).c_str());
        ImGui::Selectable((r.name + "  " + r.url + (r.pruneOnFetch ? "  (prune)" : "") + "###row").c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("fetch: %s\npush: %s%s", r.url.c_str(), r.pushUrl.empty() ? r.url.c_str() : r.pushUrl.c_str(),
                r.pruneOnFetch ? "\nprune on fetch" : "");
        if (ImGui::BeginPopupContextItem("##remote_menu")) {
            remoteMenuItems(m_session, r);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (snap->remotes.empty())
        ImGui::TextDisabled("No remotes");
    ImGui::End();
}

// ---- Stashes ------------------------------------------------------------------------------------

void StashesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Stashes, open)) {
        ImGui::End();
        return;
    }
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const auto status = m_session.status();
    ImGui::BeginDisabled(!free || !status || status->empty());
    if (ImGui::Button("Stash changes...##stash_changes"))
        m_session.showStashDialog();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!free || m_snapshot->stashes.empty());
    if (ImGui::Button("Clear all...##clear_stashes"))
        m_session.showClearStashesDialog();
    ImGui::EndDisabled();
    for (const auto& s : m_snapshot->stashes) {
        ImGui::PushID(("stash_" + std::to_string(s.index)).c_str());
        const std::string label = "stash@{" + std::to_string(s.index) + "} " + s.message;
        const bool selected = m_session.selection().kind == SelKind::Stash && m_session.selection().id == s.commit;
        if (ImGui::Selectable((label + "###row").c_str(), selected))
            m_session.select(Selection{SelKind::Stash, s.commit, s.index});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            idTooltip(s.commit.hex(), m_session.shortId(s.commit).size(),
                "base " + m_session.shortId(s.base) + "\n" + core::formatTime(s.time)
                    + (s.hasIndexChanges ? "\nhas index changes" : "") + (s.hasUntracked ? "\nhas untracked files" : ""));
        // The menu belongs to the row (the last item before it must be the Selectable).
        if (ImGui::BeginPopupContextItem("##stash_menu")) {
            if (ImGui::MenuItem("Apply", nullptr, false, free))
                actions.stashApply(s.index, false, false);
            if (ImGui::MenuItem("Apply (restore index)", nullptr, false, free))
                actions.stashApply(s.index, false, true);
            if (ImGui::MenuItem("Pop", nullptr, false, free))
                actions.stashApply(s.index, true, false);
            if (ImGui::MenuItem("Pop (restore index)", nullptr, false, free))
                actions.stashApply(s.index, true, true);
            ImGui::Separator();
            if (ImGui::MenuItem("Branch from stash...", nullptr, false, free))
                m_session.showBranchFromStashDialog(s.index);
            if (ImGui::MenuItem("Drop...", nullptr, false, free))
                m_session.showDropStashDialog(s.index);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s  %s", s.base.shortHex(7).c_str(), core::formatTime(s.time).c_str());
        ImGui::PopID();
    }
    if (m_snapshot->stashes.empty())
        ImGui::TextDisabled("No stashes");
    ImGui::End();
}

// ---- Reflog -------------------------------------------------------------------------------------

void ReflogPanel::onSnapshot(const core::SnapshotPtr& snapshot)
{
    m_snapshot = snapshot;
    reload();
}

void ReflogPanel::reload()
{
    m_request = m_session.engine().reflog(m_ref);
    m_requested = true;
}

void ReflogPanel::choose(const std::string& ref)
{
    m_ref = ref;
    m_reflog.reset();
    reload();
}

void ReflogPanel::onReflog(const core::ReflogEvent& event)
{
    if (event.request != m_request)
        return;
    m_reflog = event.reflog;
}

void ReflogPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Reflog, open)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::BeginCombo("##reflog_ref", m_ref.c_str())) {
        std::vector<std::string> refs{"HEAD"};
        for (const auto& b : m_snapshot->branches)
            refs.push_back("refs/heads/" + b.name);
        if (!m_snapshot->stashes.empty())
            refs.push_back("refs/stash");
        for (const auto& r : refs)
            if (ImGui::Selectable((r + "###ref_" + rowId(r)).c_str(), r == m_ref))
                choose(r);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##reflog_filter", ICON_MS_SEARCH " Filter", &m_filter);
    if (m_reflog) {
        if (ImGui::BeginTable("##reflog_table", 3,
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Commits");
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Date");
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < m_reflog->entries.size(); ++i) {
                const auto& e = m_reflog->entries[i];
                if (!containsNoCase(e.message, m_filter) && !containsNoCase(e.newId.hex(), m_filter)
                    && !containsNoCase(e.oldId.hex(), m_filter))
                    continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(("r" + std::to_string(i)).c_str());
                const std::string label = (e.oldId.isNull() ? std::string("0000000") : e.oldId.shortHex()) + " \xe2\x86\x92 "
                    + e.newId.shortHex() + "###reflog_" + std::to_string(i);
                ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::BeginPopupContextItem("##reflog_menu")) {
                    copyIdMenuItem("Copy new ID", m_session.shortId(e.newId), e.newId.hex());
                    copyIdMenuItem("Copy old ID", m_session.shortId(e.oldId), e.oldId.hex(), !e.oldId.isNull());
                    if (ImGui::MenuItem("Reveal new commit"))
                        m_session.revealCommit(e.newId);
                    if (ImGui::MenuItem("Reveal old commit", nullptr, false, !e.oldId.isNull()))
                        m_session.revealCommit(e.oldId);
                    ImGui::Separator();
                    const bool free = m_session.actions().busy().empty();
                    if (ImGui::MenuItem("Create branch from new...", nullptr, false, free))
                        m_session.showCreateBranchDialog(e.newId.hex());
                    if (ImGui::MenuItem("Create branch from old...", nullptr, false, free && !e.oldId.isNull()))
                        m_session.showCreateBranchDialog(e.oldId.hex());
                    ImGui::EndPopup();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(e.message.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(core::formatTime(e.time).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// ---- Operations ---------------------------------------------------------------------------------

void OperationsPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Operations, open)) {
        ImGui::End();
        return;
    }
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    ImGui::BeginDisabled(!free);
    if (ImGui::Button(ICON_MS_UNDO " Undo###ops_undo"))
        actions.undo(false);
    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_REDO " Redo###ops_redo"))
        actions.undo(true);
    ImGui::EndDisabled();
    if (!m_session.hooksInstalled()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Undo covers ggui and git gg only; use the Reflog for plain git operations.");
    }
    const auto& ops = m_session.operations();
    if (ImGui::BeginTable("##ops_table", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            const auto& op = *it;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(("op_" + op.id).c_str());
            const std::string time = core::formatTime(op.time / 1000, true);
            ImGui::Selectable((time + "###row").c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !op.refs.empty()) {
                ImGui::BeginTooltip();
                for (const auto& r : op.refs)
                    ImGui::Text("%s: %s \xe2\x86\x92 %s", r.ref.c_str(), r.oldValue.substr(0, 10).c_str(), r.newValue.substr(0, 10).c_str());
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopupContextItem("##op_menu")) {
                if (ImGui::MenuItem("Restore (undo this operation)", nullptr, false, free && op.restorable()))
                    actions.restore(op.id);
                if (ImGui::MenuItem("Copy operation ID"))
                    ImGui::SetClipboardText(op.id.c_str());
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(op.src == "git" ? "git" : op.src.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s%s", op.label.c_str(), op.ok ? "" : " (failed)");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace ggui
