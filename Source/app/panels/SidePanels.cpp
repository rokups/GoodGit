#include "panels/SidePanels.hpp"
#include "panels/CommitMenu.hpp"

#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <map>

namespace ggui {

namespace {

// Row IDs replace '/' with ':' so test references can address them (ref names contain '/').
std::string rowId(std::string id)
{
    std::replace(id.begin(), id.end(), '/', ':');
    return id;
}

struct RowEvents {
    bool toggle = false;        // the eye icon was clicked (Ctrl: only this ref)
    bool doubleClicked = false; // the row itself (only rows with a double-click action react)
};

// A ref row: the eye icon toggles visibility in History; the label is the row's item (menus and
// tooltips attach to it). Rows without a double-click action are plain text.
RowEvents visibilityRow(const std::string& rawId, const std::string& label, bool visible, bool outlined, ImU32 color,
    bool doubleClickable)
{
    RowEvents events;
    const std::string id = rowId(rawId);
    ImGui::PushID(id.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    events.toggle = ImGui::SmallButton(visible ? ICON_MS_VISIBILITY "###eye" : ICON_MS_VISIBILITY_OFF "###eye");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(visible ? "Hide in History (Ctrl-click: show only this)" : "Show in History (Ctrl-click: show only this)");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, visible ? color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const std::string item = label + "###" + id;
    if (doubleClickable) {
        selectable(item.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
        events.doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    } else {
        plainText(item.c_str());
    }
    ImGui::PopStyleColor();
    if (outlined) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(ImGuiCol_Text), 2.0f, 0, 1.5f);
    }
    ImGui::PopID();
    return events;
}

// Ref names as a tree split on '/'. A group is named by the longest '/'-separated prefix its
// members share; a group of one is shown as that ref, unsplit.
struct NameTree {
    struct Entry {
        std::string label;           // group prefix, or the ref's name below its group
        size_t item = 0;             // leaf: index into the names
        std::vector<Entry> children; // group
        bool group = false;
    };
    std::vector<Entry> entries;
};

std::vector<std::string> splitSegments(const std::string& name)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t slash; (slash = name.find('/', start)) != std::string::npos; start = slash + 1)
        out.push_back(name.substr(start, slash - start));
    out.push_back(name.substr(start));
    return out;
}

std::vector<NameTree::Entry> buildTree(const std::vector<std::pair<size_t, std::string>>& items)
{
    std::vector<NameTree::Entry> out;
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> groups;
    for (const auto& [index, rest] : items) {
        const auto slash = rest.find('/');
        if (slash == std::string::npos)
            out.push_back({rest, index, {}, false});
        else
            groups[rest.substr(0, slash)].push_back({index, rest});
    }
    for (auto& [first, members] : groups) {
        if (members.size() == 1) {
            out.push_back({members.front().second, members.front().first, {}, false});
            continue;
        }
        // The longest common prefix of whole segments, leaving each member a name below it.
        std::vector<std::string> common = splitSegments(members.front().second);
        common.pop_back();
        for (const auto& m : members) {
            const auto segs = splitSegments(m.second);
            size_t k = 0;
            while (k < common.size() && k + 1 < segs.size() && segs[k] == common[k])
                ++k;
            common.resize(k);
        }
        std::string prefix;
        for (const auto& seg : common)
            prefix += (prefix.empty() ? "" : "/") + seg;
        std::vector<std::pair<size_t, std::string>> below;
        for (const auto& m : members)
            below.push_back({m.first, m.second.substr(prefix.size() + 1)});
        out.push_back({prefix, 0, buildTree(below), true});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.label < b.label; });
    return out;
}

// Draws `entries`; `leaf(index, label)` draws one ref. Groups open by default and while filtering.
template <typename Leaf>
void drawTree(const std::vector<NameTree::Entry>& entries, const std::string& idPrefix, bool filtering, Leaf&& leaf)
{
    for (const auto& e : entries) {
        if (!e.group) {
            leaf(e.item, e.label);
            continue;
        }
        if (filtering)
            ImGui::SetNextItemOpen(true);
        const std::string id = idPrefix + e.label + "/";
        bool open;
        {
            const SectionHeaderColors neutral;
            open = ImGui::TreeNodeEx((e.label + "###group_" + rowId(id)).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (open) {
            drawTree(e.children, id, filtering, leaf);
            ImGui::TreePop();
        }
    }
}

// The Remotes panel's menu for a remote; also on the remote's node in Branches (remote-level items
// only: a remote-tracking branch has its own branch menu).
void remoteMenuItems(Session& session, const core::RemoteInfo& r)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const auto snap = session.snapshot();
    const auto* current = snap->currentBranch();
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(r.name.c_str());
    if (menuItem(ICON_MS_LINK, "Copy URL", nullptr, false, !r.url.empty()))
        ImGui::SetClipboardText(r.url.c_str());
    ImGui::Separator();
    if (menuItem(ICON_MS_DOWNLOAD, "Fetch", nullptr, false, free))
        actions.fetch(r.name, false, false);
    if (menuItem(ICON_MS_DELETE_SWEEP, "Fetch and prune", nullptr, false, free))
        actions.fetch(r.name, true, false);
    const bool pullable = free && current && current->upstream.rfind(r.name + "/", 0) == 0;
    if (menuItem(ICON_MS_ARROW_DOWNWARD, "Pull", nullptr, false, pullable))
        actions.pull(PullMode::Config);
    bool prune = r.pruneOnFetch;
    if (menuItem(ICON_MS_DELETE_SWEEP, "Prune on fetch", nullptr, &prune, free))
        actions.setPruneOnFetch(r.name, prune);
    if (menuItem(ICON_MS_EDIT, "Edit URL...", nullptr, false, free))
        session.showEditRemoteDialog(r.name);
    if (menuItem(ICON_MS_DELETE, "Delete", nullptr, false, free)) {
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
    if (!beginContextMenu(("##branch_menu_" + rowId(b.name)).c_str()))
        return;
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const bool hasRemotes = !m_snapshot->remotes.empty();
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        m_session.revealCommit(b.target);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(b.name.c_str());
    ImGui::Separator();
    if (menuItem(ICON_MS_SWAP_HORIZ, "Check out", nullptr, false, free && !b.isHead))
        actions.checkout(b.name, false);
    const bool headAttached = !m_snapshot->headDetached && !m_snapshot->headUnborn;
    if (menuItem(ICON_MS_MERGE, "Merge into HEAD...", nullptr, false, free && !b.isHead && !m_snapshot->headUnborn))
        showMergeDialog(m_session, b.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase HEAD onto branch", nullptr, false, free && !b.isHead && headAttached))
        actions.rebaseHeadOnto(b.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Interactive rebase onto...", nullptr, false, free))
        showInteractiveRebaseDialog(m_session, b.name);
    if (b.isHead)
        disabledMenuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", "Checked out in this worktree");
    else if (!b.worktree.empty())
        disabledMenuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", ("Checked out in " + b.worktree).c_str());
    else if (menuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", nullptr, false, free))
        m_session.showAddWorktreeDialog(1, b.name);
    if (menuItem(ICON_MS_UPLOAD, "Push", nullptr, false, free && hasRemotes)) {
        if (!b.upstream.empty()) {
            const auto slash = b.upstream.find('/');
            actions.push(b.upstream.substr(0, slash), b.name, b.upstream.substr(slash + 1), false, false);
        } else {
            m_session.showPushToDialog(b.name);
        }
    }
    if (menuItem(ICON_MS_UPLOAD, "Push to...", nullptr, false, free && hasRemotes))
        m_session.showPushToDialog(b.name);
    if (menuItem(ICON_MS_ARROW_DOWNWARD, "Pull", nullptr, false, free && b.isHead && !b.upstream.empty()))
        actions.pull(PullMode::Config);
    if (menuItem(ICON_MS_SYNC_ALT, "Reconcile with remote or branch...", nullptr, false, free && b.isHead)) {
        Form f;
        f.title = "Reconcile";
        f.message = b.upstream.empty() ? b.name + " has no upstream: name the branch to reconcile with."
                                       : b.name + " and " + b.upstream + " have diverged.";
        f.add(commitInfo(m_session, "HEAD (" + b.name + ")", b.target));
        f.add(commitField(m_session, "with", "Reconcile with (branch, tag or commit)", b.upstream));
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
    if (menuItem(ICON_MS_DRIVE_FILE_RENAME_OUTLINE, "Rename...", nullptr, false, free))
        m_session.showRenameBranchDialog(b.name);
    if (beginMenu(ICON_MS_DELETE, "Delete", free)) {
        if (menuItem(ICON_MS_DELETE, "Local", nullptr, false, !b.isHead))
            m_session.showDeleteBranchDialog(b.name, 0);
        if (menuItem(ICON_MS_DELETE, "On its remote", nullptr, false, !b.upstream.empty()))
            m_session.showDeleteBranchDialog(b.name, 1);
        if (menuItem(ICON_MS_DELETE, "Local and all remotes", nullptr, false, !b.isHead))
            m_session.showDeleteBranchDialog(b.name, 2);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menuItem(ICON_MS_LINK, "Set upstream...", nullptr, false, free))
        m_session.showSetUpstreamDialog(b.name);
    if (menuItem(ICON_MS_LINK_OFF, "Unset upstream", nullptr, false, free && !b.upstream.empty()))
        actions.unsetUpstream(b.name);
    if (menuItem(ICON_MS_FAST_FORWARD, "Fast-forward to upstream", nullptr, false, free && !b.upstream.empty() && b.behind > 0 && b.ahead == 0))
        actions.fastForward(b.name);
    ImGui::EndPopup();
}

void BranchesPanel::remoteBranchMenu(const core::RemoteBranchInfo& r)
{
    if (!beginContextMenu(("##rbranch_menu_" + rowId(r.name)).c_str()))
        return;
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const std::string shortName = r.name.substr(std::min(r.name.size(), r.remote.size() + 1));
    const bool headAttached = !m_snapshot->headDetached && !m_snapshot->headUnborn;
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        m_session.revealCommit(r.target);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(r.name.c_str());
    ImGui::Separator();
    // A local branch of that name is checked out; otherwise one is created to track this branch.
    if (menuItem(ICON_MS_SWAP_HORIZ, "Check out", nullptr, false, free)) {
        if (m_snapshot->findBranch(shortName))
            actions.checkout(shortName, false);
        else
            actions.createBranch(shortName, r.name, true);
    }
    if (menuItem(ICON_MS_ADD, "Create local branch...", nullptr, false, free))
        m_session.showCreateBranchDialog(r.name, shortName);
    if (menuItem(ICON_MS_MERGE, "Merge into HEAD...", nullptr, false, free && !m_snapshot->headUnborn))
        showMergeDialog(m_session, r.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase HEAD onto branch", nullptr, false, free && headAttached))
        actions.rebaseHeadOnto(r.name);
    ImGui::Separator();
    if (menuItem(ICON_MS_DELETE, "Delete on remote...", nullptr, false, free))
        m_session.showDeleteRemoteBranchDialog(r.name);
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
    auto& history = m_session.history();
    // Show all / Hide all: every local and remote-tracking branch in History.
    std::vector<std::string> all;
    for (const auto& b : m_snapshot->branches)
        all.push_back("refs/heads/" + b.name);
    for (const auto& r : m_snapshot->remoteBranches)
        all.push_back("refs/remotes/" + r.name);
    sameLineIfFits(ImGui::GetFrameHeight());
    if (ImGui::Button(ICON_MS_VISIBILITY "###show_all_branches"))
        history.setRefsVisible(all, true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Show all branches in History");
    sameLineIfFits(ImGui::GetFrameHeight());
    if (ImGui::Button(ICON_MS_VISIBILITY_OFF "###hide_all_branches"))
        history.setRefsVisible(all, false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Hide all branches in History");
    sameLineIfFits(ImGui::GetFontSize() * 6);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##branch_filter", ICON_MS_SEARCH " Filter", &m_filter);
    const Palette& p = theme().palette();
    const bool filtering = !m_filter.empty();
    // Rows keep IDs directly under the window ("branch_<name>") whatever group they sit in.
    const ImGuiID windowId = ImGui::GetCurrentWindow()->ID;

    std::vector<std::pair<size_t, std::string>> locals;
    for (size_t i = 0; i < m_snapshot->branches.size(); ++i)
        if (containsNoCase(m_snapshot->branches[i].name, m_filter))
            locals.push_back({i, m_snapshot->branches[i].name});
    drawTree(buildTree(locals), "local:", filtering, [&](size_t index, const std::string& shortName) {
        const auto& b = m_snapshot->branches[index];
        std::string label = shortName;
        if (!b.upstream.empty()) {
            label += "  " ICON_MS_ARROW_RIGHT_ALT " " + b.upstream;
            if (b.upstreamGone)
                label += " (gone)";
            if (b.ahead)
                label += " " ICON_MS_ARROW_UPWARD_ALT + std::to_string(b.ahead);
            if (b.behind)
                label += " " ICON_MS_ARROW_DOWNWARD_ALT + std::to_string(b.behind);
        }
        if (!b.worktree.empty())
            label += "  [" + b.worktree + "]";
        const std::string full = "refs/heads/" + b.name;
        ImGui::PushOverrideID(windowId);
        const RowEvents events = visibilityRow("branch_" + b.name, label, history.refVisible(full), b.isHead,
            b.isHead ? p.branchCurrentText : ImGui::GetColorU32(ImGuiCol_Text), true);
        if (shortName != b.name && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", b.name.c_str());
        if (events.toggle)
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        // Double-click checks the branch out.
        if (events.doubleClicked && free && !b.isHead)
            m_session.actions().checkout(b.name, false);
        branchMenu(b);
        ImGui::PopID();
    });

    // Remote-tracking branches under their remote.
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> byRemote;
    for (size_t i = 0; i < m_snapshot->remoteBranches.size(); ++i) {
        const auto& r = m_snapshot->remoteBranches[i];
        if (containsNoCase(r.name, m_filter))
            byRemote[r.remote].push_back({i, r.name.substr(std::min(r.name.size(), r.remote.size() + 1))});
    }
    for (const auto& [remote, list] : byRemote) {
        ImGui::PushID(("remote_group_" + remote).c_str());
        const core::RemoteInfo* info = nullptr;
        for (const auto& r : m_snapshot->remotes)
            if (r.name == remote)
                info = &r;
        bool nodeOpen;
        {
            const SectionHeaderColors neutral;
            nodeOpen = ImGui::TreeNodeEx(remote.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        // The remote has the Remotes panel's menu, plus History visibility of its branches.
        if (info && beginContextMenu("##remote_menu")) {
            remoteMenuItems(m_session, *info);
            ImGui::Separator();
            std::vector<std::string> refs;
            for (const auto& r : m_snapshot->remoteBranches)
                if (r.remote == remote)
                    refs.push_back("refs/remotes/" + r.name);
            if (menuItem(ICON_MS_VISIBILITY, "Show all branches in History"))
                history.setRefsVisible(refs, true);
            if (menuItem(ICON_MS_VISIBILITY_OFF, "Hide all branches in History"))
                history.setRefsVisible(refs, false);
            ImGui::EndPopup();
        }
        if (nodeOpen) {
            drawTree(buildTree(list), "remote:" + remote + "/", filtering, [&](size_t index, const std::string& shortName) {
                const auto& r = m_snapshot->remoteBranches[index];
                const std::string full = "refs/remotes/" + r.name;
                // Rows keep the IDs they had before groups: <window>/remote_group_<remote>/<remote>/...
                ImGui::PushOverrideID(windowId);
                ImGui::PushID(("remote_group_" + remote).c_str());
                ImGui::PushID(remote.c_str());
                const RowEvents events = visibilityRow("rbranch_" + r.name, shortName,
                    history.refVisible(full), false, p.remoteText, false);
                if (events.toggle)
                    history.toggleRef(full, ImGui::GetIO().KeyCtrl);
                remoteBranchMenu(r);
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            });
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
    sameLineIfFits(ImGui::GetFontSize() * 6);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##tag_filter", ICON_MS_SEARCH " Filter", &m_filter);
    auto& history = m_session.history();
    // Tags on the remotes: read while this panel is shown (and again after fetch, pull or push).
    m_session.requestRemoteTagsIfStale();
    const auto& remoteTags = m_session.remoteTags();
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    // Delete: one item for a tag only here; a submenu (Local, then each remote that has it) when a
    // remote has it too. A remote whose tags are still being read or could not be read is offered
    // with a note (the tag may be there).
    auto deleteItems = [&](const std::string& name, bool local) {
        std::vector<std::string> remotes; // menu labels, the remote's name first
        std::vector<std::string> names;
        for (const auto& r : m_snapshot->remotes) {
            auto it = remoteTags.find(r.name);
            if (it == remoteTags.end()) {
                remotes.push_back(r.name + " (checking...)");
                names.push_back(r.name);
            } else if (!it->second.ok) {
                remotes.push_back(r.name + " (not checked)");
                names.push_back(r.name);
            } else if (it->second.tags.count(name)) {
                remotes.push_back(r.name);
                names.push_back(r.name);
            }
        }
        if (local && remotes.empty()) {
            if (menuItem(ICON_MS_DELETE, "Delete", nullptr, false, free))
                actions.deleteTag(name);
            return;
        }
        if (!beginMenu(ICON_MS_DELETE, "Delete", free))
            return;
        if (menuItem(ICON_MS_DELETE, "Local", nullptr, false, local))
            actions.deleteTag(name);
        ImGui::Separator();
        for (size_t i = 0; i < remotes.size(); ++i)
            if (menuItem(ICON_MS_DELETE, remotes[i].c_str()))
                actions.deleteRemoteTag(names[i], name);
        ImGui::EndMenu();
    };
    for (const auto& t : m_snapshot->tags) {
        if (!containsNoCase(t.name, m_filter))
            continue;
        const std::string full = "refs/tags/" + t.name;
        if (visibilityRow("tag_" + t.name, t.name, history.refVisible(full), false, theme().palette().tagText, false).toggle)
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        if (t.annotated && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !t.message.empty())
            ImGui::SetTooltip("%s", t.message.c_str());
        if (beginContextMenu(("##tag_menu_" + rowId(t.name)).c_str())) {
            if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
                m_session.revealCommit(t.target);
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
                ImGui::SetClipboardText(t.name.c_str());
            ImGui::Separator();
            deleteItems(t.name, true);
            if (beginMenu(ICON_MS_UPLOAD, "Push tag", free && !m_snapshot->remotes.empty())) {
                for (const auto& r : m_snapshot->remotes)
                    if (menuItem(ICON_MS_UPLOAD, r.name.c_str()))
                        actions.pushTag(r.name, t.name);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    // Tags only on remotes: listed dimmed, with where they are.
    std::map<std::string, std::vector<std::string>> remoteOnly;
    for (const auto& [remote, r] : remoteTags)
        for (const auto& name : r.tags)
            if (containsNoCase(name, m_filter)
                && std::none_of(m_snapshot->tags.begin(), m_snapshot->tags.end(), [&](const auto& t) { return t.name == name; }))
                remoteOnly[name].push_back(remote);
    for (const auto& [name, remotes] : remoteOnly) {
        std::string where;
        for (const auto& r : remotes)
            where += (where.empty() ? "" : ", ") + r;
        const std::string id = "rtag_" + rowId(name);
        ImGui::PushID(id.c_str());
        ImGui::TextDisabled(ICON_MS_CLOUD);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        plainText((name + "  (" + where + ")###" + id).c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Only on %s: fetch to get it here", where.c_str());
        if (beginContextMenu("##rtag_menu")) {
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
                ImGui::SetClipboardText(name.c_str());
            ImGui::Separator();
            deleteItems(name, false);
            ImGui::EndPopup();
        }
        ImGui::PopID();
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
        selectable((label + "###row").c_str(), w.isCurrent);
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
        if (beginContextMenu("##worktree_menu")) {
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
                ImGui::SetClipboardText(w.name.c_str());
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy path"))
                ImGui::SetClipboardText(w.path.string().c_str());
            if (menuItem(ICON_MS_MY_LOCATION, "Reveal HEAD", nullptr, false, !w.head.isNull()))
                m_session.revealCommit(w.head);
            if (menuItem(ICON_MS_OPEN_IN_NEW, "Open directory", nullptr, false, !w.missing))
                openInFileManager(w.path);
            ImGui::Separator();
            const std::string path = w.path.string();
            if (w.isCurrent)
                disabledMenuItem(ICON_MS_FOLDER_OPEN, "Open here", "This window shows this worktree");
            else if (w.missing)
                disabledMenuItem(ICON_MS_FOLDER_OPEN, "Open here", "Its directory is gone");
            else if (menuItem(ICON_MS_FOLDER_OPEN, "Open here"))
                m_session.app().openRepository(w.path);
            if (w.missing)
                disabledMenuItem(ICON_MS_LAUNCH, "Open in new window", "Its directory is gone");
            else if (menuItem(ICON_MS_LAUNCH, "Open in new window"))
                m_session.actions().openInNewWindow(path);
            ImGui::Separator();
            if (menuItem(ICON_MS_ADD, "Add...", nullptr, false, canAdd))
                m_session.showAddWorktreeDialog();
            if (w.isMain)
                disabledMenuItem(ICON_MS_DELETE, "Remove...", "The main worktree cannot be removed");
            else if (w.isCurrent)
                disabledMenuItem(ICON_MS_DELETE, "Remove...", "This window shows this worktree: open another one first");
            else if (menuItem(ICON_MS_DELETE, "Remove...", nullptr, false, free))
                m_session.showRemoveWorktreeDialog(w);
            if (w.isMain)
                disabledMenuItem(ICON_MS_LOCK, "Lock...", "The main worktree cannot be locked");
            else if (w.locked ? menuItem(ICON_MS_LOCK_OPEN, "Unlock", nullptr, false, free) : menuItem(ICON_MS_LOCK, "Lock...", nullptr, false, free)) {
                if (w.locked)
                    m_session.actions().unlockWorktree(path);
                else
                    m_session.showLockWorktreeDialog(w);
            }
            if (menuItem(ICON_MS_DELETE_SWEEP, "Prune...", nullptr, false, free))
                m_session.showPruneWorktreesDialog();
            if (menuItem(ICON_MS_HANDYMAN, "Repair...", nullptr, false, free))
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
    sameLineIfFits(buttonWidth(ICON_MS_DOWNLOAD, "Fetch all"));
    if (button(ICON_MS_DOWNLOAD, "Fetch all##fetch_all") && !snap->remotes.empty())
        actions.fetch("", false, false);
    ImGui::EndDisabled();
    for (const auto& r : snap->remotes) {
        ImGui::PushID(("remote_" + r.name).c_str());
        // "name@host" with the "@host" (and the prune note) dimmed; a local remote shows just its name.
        const std::string host = remoteHost(r.url);
        std::string label = r.name;
        const size_t dimBegin = label.size();
        if (!host.empty())
            label += "@" + host;
        if (r.pruneOnFetch)
            label += "  (prune)";
        selectableDimRange((label + "###row").c_str(), dimBegin, label.size());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            const std::string push = r.pushUrl.empty() ? r.url : r.pushUrl;
            std::string tip = push == r.url ? r.url : "Fetch: " + r.url + "\nPush: " + push;
            if (r.pruneOnFetch)
                tip += "\nPrunes on fetch";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (beginContextMenu("##remote_menu")) {
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
    // Pop takes the selected stash, else the newest.
    const Selection& sel = m_session.selection();
    int popIndex = 0;
    if (sel.kind == SelKind::Stash)
        for (const auto& s : m_snapshot->stashes)
            if (s.commit == sel.id)
                popIndex = s.index;
    const bool hasStashes = !m_snapshot->stashes.empty();
    ImGui::BeginDisabled(!free || !status || status->empty());
    if (button(ICON_MS_INVENTORY_2, "Push##stash_push"))
        m_session.showStashDialog();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Stash the changes in the working tree...");
    sameLineIfFits(buttonWidth(ICON_MS_OUTBOX, "Pop"));
    ImGui::BeginDisabled(!free || !hasStashes);
    if (button(ICON_MS_OUTBOX, "Pop##stash_pop"))
        actions.stashApply(popIndex, true, false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Apply stash@{%d} and drop it", popIndex);
    sameLineIfFits(buttonWidth(ICON_MS_DELETE_SWEEP, "Clear all..."));
    ImGui::BeginDisabled(!free || m_snapshot->stashes.empty());
    if (button(ICON_MS_DELETE_SWEEP, "Clear all...##clear_stashes"))
        m_session.showClearStashesDialog();
    ImGui::EndDisabled();
    for (const auto& s : m_snapshot->stashes) {
        ImGui::PushID(("stash_" + std::to_string(s.index)).c_str());
        const std::string label = "stash@{" + std::to_string(s.index) + "} " + s.message;
        const bool selected = m_session.selection().kind == SelKind::Stash && m_session.selection().id == s.commit;
        if (selectable((label + "###row").c_str(), selected))
            m_session.select(Selection{SelKind::Stash, s.commit, s.index});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            idTooltip(s.commit.hex(), m_session.shortId(s.commit).size(),
                "base " + m_session.shortId(s.base) + "\n" + core::formatTime(s.time)
                    + (s.hasIndexChanges ? "\nhas index changes" : "") + (s.hasUntracked ? "\nhas untracked files" : ""));
        // The menu belongs to the row (the last item before it must be the Selectable).
        if (beginContextMenu("##stash_menu")) {
            if (menuItem(ICON_MS_UNARCHIVE, "Apply", nullptr, false, free))
                actions.stashApply(s.index, false, false);
            if (menuItem(ICON_MS_UNARCHIVE, "Apply (restore index)", nullptr, false, free))
                actions.stashApply(s.index, false, true);
            if (menuItem(ICON_MS_OUTBOX, "Pop", nullptr, false, free))
                actions.stashApply(s.index, true, false);
            if (menuItem(ICON_MS_OUTBOX, "Pop (restore index)", nullptr, false, free))
                actions.stashApply(s.index, true, true);
            ImGui::Separator();
            if (menuItem(ICON_MS_FORK_RIGHT, "Branch from stash...", nullptr, false, free))
                m_session.showBranchFromStashDialog(s.index);
            if (menuItem(ICON_MS_DELETE, "Drop...", nullptr, false, free))
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
            if (selectable((r + "###ref_" + rowId(r)).c_str(), r == m_ref))
                choose(r);
        ImGui::EndCombo();
    }
    sameLineIfFits(ImGui::GetFontSize() * 6);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##reflog_filter", ICON_MS_SEARCH " Filter", &m_filter);
    acceptCommitDrop(m_filter);
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
                const std::string label = (e.oldId.isNull() ? std::string("0000000") : e.oldId.shortHex()) + " " ICON_MS_ARROW_RIGHT_ALT " "
                    + e.newId.shortHex() + "###reflog_" + std::to_string(i);
                selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                if (beginContextMenu("##reflog_menu")) {
                    copyIdMenuItem("Copy new ", m_session.shortId(e.newId), e.newId.hex());
                    copyIdMenuItem("Copy old ", m_session.shortId(e.oldId), e.oldId.hex(), !e.oldId.isNull());
                    if (menuItem(ICON_MS_MY_LOCATION, "Reveal new commit"))
                        m_session.revealCommit(e.newId);
                    if (menuItem(ICON_MS_MY_LOCATION, "Reveal old commit", nullptr, false, !e.oldId.isNull()))
                        m_session.revealCommit(e.oldId);
                    ImGui::Separator();
                    const bool free = m_session.actions().busy().empty();
                    if (menuItem(ICON_MS_ADD, "Create branch from new...", nullptr, false, free))
                        m_session.showCreateBranchDialog(e.newId.hex());
                    if (menuItem(ICON_MS_ADD, "Create branch from old...", nullptr, false, free && !e.oldId.isNull()))
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
    if (button(ICON_MS_UNDO, "Undo###ops_undo"))
        actions.undo(false);
    sameLineIfFits(buttonWidth(ICON_MS_REDO, "Redo"));
    if (button(ICON_MS_REDO, "Redo###ops_redo"))
        actions.undo(true);
    ImGui::EndDisabled();
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
            selectable((time + "###row").c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !op.refs.empty()) {
                ImGui::BeginTooltip();
                for (const auto& r : op.refs)
                    ImGui::Text("%s: %s " ICON_MS_ARROW_RIGHT_ALT " %s", r.ref.c_str(), r.oldValue.substr(0, 10).c_str(), r.newValue.substr(0, 10).c_str());
                ImGui::EndTooltip();
            }
            if (beginContextMenu("##op_menu")) {
                if (menuItem(ICON_MS_RESTORE, "Restore (undo this operation)", nullptr, false, free && op.restorable()))
                    actions.restore(op.id);
                if (menuItem(ICON_MS_CONTENT_COPY, "Copy operation ID"))
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
