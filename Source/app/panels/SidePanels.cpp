#include "panels/SidePanels.hpp"
#include "panels/CommitMenu.hpp"

#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <libgg/Keep.hpp>

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <functional>
#include <map>
#include <span>
#include <string_view>

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
    int action = -1;            // the index of the hover button clicked in `acts`
    float labelLeft = 0;        // screen X where the label's text starts
};

// A ref row: the eye icon toggles visibility in History; the label is the row's item (menus and
// tooltips attach to it). Rows without a double-click action are plain text. `acts` are the row's hover buttons.
// `dim` is a byte range of the label drawn dimmed (the tail of a short ID); only on a row with a double-click action.
RowEvents visibilityRow(const std::string& rawId, const std::string& label, bool visible, bool outlined, ImU32 color,
    bool doubleClickable, std::span<const RowAction> acts = {}, std::pair<size_t, size_t> dim = {})
{
    RowEvents events;
    const std::string id = rowId(rawId);
    ImGui::PushID(id.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // The eye is for the mouse: the label is the row's one nav stop (menus attach to it), and Space on it
    // toggles the visibility (below).
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    events.toggle = ImGui::SmallButton(visible ? ICON_MS_VISIBILITY "###eye" : ICON_MS_VISIBILITY_OFF "###eye");
    ImGui::PopItemFlag();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        tooltip(visible ? "Hide in History (Ctrl-click: show only this)" : "Show in History (Ctrl-click: show only this)");
    ImGui::SameLine();
    events.labelLeft = ImGui::GetCursorScreenPos().x;
    ImGui::PushStyleColor(ImGuiCol_Text, visible ? color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const std::string item = label + "###" + id;
    if (doubleClickable) {
        // With buttons over it the row must be flagged as overlappable for IsItemHovered() too (Selectable's own
        // AllowOverlap only reaches its click handling): else a double click on a button is also the row's.
        if (!acts.empty())
            ImGui::SetNextItemAllowOverlap();
        const ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick | (acts.empty() ? 0 : ImGuiSelectableFlags_AllowOverlap);
        if (dim.second > dim.first)
            selectableDimRange(item.c_str(), dim.first, dim.second, false, flags);
        else
            selectable(item.c_str(), false, flags);
        events.doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    } else {
        if (!acts.empty())
            ImGui::SetNextItemAllowOverlap();
        plainText(item.c_str());
    }
    ImGui::PopStyleColor();
    // Space on the row toggles its visibility like a click on the eye (Ctrl+Space: only this one).
    if (ImGui::IsItemFocused() && (ImGui::GetIO().KeyMods & ~ImGuiMod_Ctrl) == 0 && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        events.toggle = true;
    if (outlined) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(ImGuiCol_Text), 2.0f, 0, 1.5f);
    }
    if (!acts.empty())
        events.action = rowActions(acts, false, doubleClickable);
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
        size_t leaves = 1;           // the refs at or below this entry
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
            out.push_back({rest, index, {}, false, 1});
        else
            groups[rest.substr(0, slash)].push_back({index, rest});
    }
    for (auto& [first, members] : groups) {
        if (members.size() == 1) {
            out.push_back({members.front().second, members.front().first, {}, false, 1});
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
        auto children = buildTree(below);
        size_t leaves = 0;
        for (const auto& c : children)
            leaves += c.leaves;
        out.push_back({prefix, 0, std::move(children), true, leaves});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.label < b.label; });
    return out;
}

// The eye of a group row: shows or hides, in History, the refs listed under the row. All visible: a click
// hides them, else it shows them; Ctrl-click shows only these. Mixed is drawn dimmed. Mouse only.
void groupEye(HistoryPanel& history, std::span<const std::string> refs)
{
    size_t visible = 0;
    for (const auto& r : refs)
        visible += history.refVisible(r) ? 1 : 0;
    const bool all = visible == refs.size();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(visible > 0 && !all ? ImGuiCol_TextDisabled : ImGuiCol_Text));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool clicked = ImGui::SmallButton(visible > 0 ? ICON_MS_VISIBILITY "###eye" : ICON_MS_VISIBILITY_OFF "###eye");
    ImGui::PopItemFlag();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        tooltip(all ? "Hide these in History (Ctrl-click: show only these)"
                    : "Show these in History (Ctrl-click: show only these)");
    if (clicked) {
        const std::vector<std::string> list(refs.begin(), refs.end());
        if (ImGui::GetIO().KeyCtrl)
            history.showOnlyRefs(list);
        else
            history.setRefsVisible(list, !all);
    }
}

// Branches: group rows get an eye over the full refs below them. `refOf` maps a leaf index to its full ref.
struct GroupEyes {
    HistoryPanel& history;
    std::function<std::string(size_t)> refOf;
};

// The full refs of the leaves under `entries`, in the order they are drawn.
void collectRefs(const std::vector<NameTree::Entry>& entries, const GroupEyes& eyes, std::vector<std::string>& out)
{
    for (const auto& e : entries) {
        if (e.group)
            collectRefs(e.children, eyes, out);
        else
            out.push_back(eyes.refOf(e.item));
    }
}

// Draws `entries`; `leaf(index, label)` draws one ref. Groups open by default and while filtering. With `eyes`,
// a group row starts with its eye; `refs` are the refs under `entries` (collected here when empty).
template <typename Leaf>
void drawTree(const std::vector<NameTree::Entry>& entries, const std::string& idPrefix, bool filtering, Leaf&& leaf,
    const GroupEyes* eyes = nullptr, std::span<const std::string> refs = {})
{
    std::vector<std::string> collected;
    if (eyes && refs.empty()) {
        collectRefs(entries, *eyes, collected);
        refs = collected;
    }
    size_t offset = 0; // of the current entry's refs in `refs`
    for (const auto& e : entries) {
        const size_t first = offset;
        offset += e.leaves;
        if (!e.group) {
            leaf(e.item, e.label);
            continue;
        }
        const std::string id = idPrefix + e.label + "/";
        const auto below = eyes ? refs.subspan(first, e.leaves) : std::span<const std::string>();
        if (eyes) {
            ImGui::PushID(("group_" + rowId(id)).c_str());
            groupEye(eyes->history, below);
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (filtering) // after the eye: any item consumes the next-item data
            ImGui::SetNextItemOpen(true);
        bool open;
        {
            const SectionHeaderColors neutral;
            open = ImGui::TreeNodeEx((e.label + "###group_" + rowId(id)).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (open) {
            drawTree(e.children, id, filtering, leaf, eyes, below);
            ImGui::TreePop();
        }
    }
}

// Pull in a remote's menu and on its row: only for the current branch when it tracks this remote.
bool remotePullable(const core::Snapshot& snap, const core::RemoteInfo& r, bool free)
{
    const auto* current = snap.currentBranch();
    return free && current && current->upstream.rfind(r.name + "/", 0) == 0;
}

// The Remotes panel's menu for a remote; also on the remote's node in Branches (remote-level items
// only: a remote-tracking branch has its own branch menu).
void remoteMenuItems(Session& session, const core::RemoteInfo& r)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const auto snap = session.snapshot();
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(r.name.c_str());
    if (menuItem(ICON_MS_LINK, "Copy URL", nullptr, false, !r.url.empty()))
        ImGui::SetClipboardText(r.url.c_str());
    ImGui::Separator();
    if (menuItem(ICON_MS_DOWNLOAD, "Fetch", nullptr, false, free))
        actions.fetch(r.name, false, false);
    if (menuItem(ICON_MS_DELETE_SWEEP, "Fetch and prune", nullptr, false, free))
        actions.fetch(r.name, true, false);
    if (menuItem(ICON_MS_ARROW_DOWNWARD, "Pull", nullptr, false, remotePullable(*snap, r, free)))
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

// Push on a local branch's row or menu: to its upstream, or the Push to... dialog without one.
void pushBranch(Session& session, const core::BranchInfo& b)
{
    if (!b.upstream.empty()) {
        const auto slash = b.upstream.find('/');
        session.actions().push(b.upstream.substr(0, slash), b.name, b.upstream.substr(slash + 1), false, false);
    } else {
        session.showPushToDialog(b.name);
    }
}

// Check out on a remote-tracking branch's row or menu: a local branch of that name is checked out;
// otherwise one is created to track it.
void checkoutRemoteBranch(Session& session, const core::Snapshot& snapshot, const core::RemoteBranchInfo& r)
{
    const std::string shortName = r.name.substr(std::min(r.name.size(), r.remote.size() + 1));
    if (snapshot.findBranch(shortName))
        session.actions().checkout(shortName, false);
    else
        session.actions().createBranch(shortName, r.name, true);
}

} // namespace

// ---- Branches -----------------------------------------------------------------------------------

void branchMenuItems(Session& session, const core::Snapshot& snap, const core::BranchInfo& b)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const bool hasRemotes = !snap.remotes.empty();
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        session.revealCommit(b.target);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(b.name.c_str());
    ImGui::Separator();
    if (menuItem(ICON_MS_SWAP_HORIZ, "Check out", nullptr, false, free && !b.isHead))
        actions.checkout(b.name, false);
    const bool headAttached = !snap.headDetached && !snap.headUnborn;
    if (menuItem(ICON_MS_MERGE, "Merge into HEAD...", nullptr, false, free && !b.isHead && !snap.headUnborn))
        showMergeDialog(session, b.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase HEAD onto branch", nullptr, false, free && !b.isHead && headAttached))
        actions.rebaseHeadOnto(b.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Interactive rebase onto...", nullptr, false, free))
        showInteractiveRebaseDialog(session, b.name);
    if (b.isHead)
        disabledMenuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", "Checked out in this worktree");
    else if (!b.worktree.empty())
        disabledMenuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", ("Checked out in " + b.worktree).c_str());
    else if (menuItem(ICON_MS_OPEN_IN_NEW, "Check out in new worktree...", nullptr, false, free))
        session.showAddWorktreeDialog(1, b.name);
    if (menuItem(ICON_MS_UPLOAD, "Push", nullptr, false, free && hasRemotes))
        pushBranch(session, b);
    if (menuItem(ICON_MS_UPLOAD, "Push to...", nullptr, false, free && hasRemotes))
        session.showPushToDialog(b.name);
    if (menuItem(ICON_MS_ARROW_DOWNWARD, "Pull", nullptr, false, free && b.isHead && !b.upstream.empty()))
        actions.pull(PullMode::Config);
    if (menuItem(ICON_MS_SYNC_ALT, "Reconcile with remote or branch...", nullptr, false, free && b.isHead)) {
        Form f;
        f.title = "Reconcile";
        f.message = b.upstream.empty() ? b.name + " has no upstream: name the branch to reconcile with."
                                       : b.name + " and " + b.upstream + " have diverged.";
        f.add(commitInfo(session, "HEAD (" + b.name + ")", b.target));
        f.add(commitField(session, "with", "Reconcile with (branch, tag or commit)", b.upstream));
        Field how{Field::Combo, "how", "How"};
        how.options = {"Rebase my commits onto it", "Merge it in"};
        f.add(how);
        Session* s = &session;
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
        session.app().dialogs().open(std::move(f));
    }
    ImGui::Separator();
    if (menuItem(ICON_MS_DRIVE_FILE_RENAME_OUTLINE, "Rename...", nullptr, false, free))
        session.showRenameBranchDialog(b.name);
    if (beginMenu(ICON_MS_DELETE, "Delete", free)) {
        if (menuItem(ICON_MS_DELETE, "Local", nullptr, false, !b.isHead))
            session.showDeleteBranchDialog(b.name, 0);
        if (menuItem(ICON_MS_DELETE, "On its remote", nullptr, false, !b.upstream.empty()))
            session.showDeleteBranchDialog(b.name, 1);
        if (menuItem(ICON_MS_DELETE, "Local and all remotes", nullptr, false, !b.isHead))
            session.showDeleteBranchDialog(b.name, 2);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menuItem(ICON_MS_LINK, "Set upstream...", nullptr, false, free))
        session.showSetUpstreamDialog(b.name);
    if (menuItem(ICON_MS_LINK_OFF, "Unset upstream", nullptr, false, free && !b.upstream.empty()))
        actions.unsetUpstream(b.name);
    if (menuItem(ICON_MS_FAST_FORWARD, "Fast-forward to upstream", nullptr, false, free && !b.upstream.empty() && b.behind > 0 && b.ahead == 0))
        actions.fastForward(b.name);
}

void BranchesPanel::branchMenu(const core::BranchInfo& b)
{
    if (!beginContextMenu(("##branch_menu_" + rowId(b.name)).c_str()))
        return;
    branchMenuItems(m_session, *m_snapshot, b);
    ImGui::EndPopup();
}

void remoteBranchMenuItems(Session& session, const core::Snapshot& snap, const core::RemoteBranchInfo& r)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const std::string shortName = r.name.substr(std::min(r.name.size(), r.remote.size() + 1));
    const bool headAttached = !snap.headDetached && !snap.headUnborn;
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        session.revealCommit(r.target);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(r.name.c_str());
    ImGui::Separator();
    // A local branch of that name is checked out; otherwise one is created to track this branch.
    if (menuItem(ICON_MS_SWAP_HORIZ, "Check out", nullptr, false, free))
        checkoutRemoteBranch(session, snap, r);
    if (menuItem(ICON_MS_ADD, "Create local branch...", nullptr, false, free))
        session.showCreateBranchDialog(r.name, shortName);
    if (menuItem(ICON_MS_MERGE, "Merge into HEAD...", nullptr, false, free && !snap.headUnborn))
        showMergeDialog(session, r.name);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase HEAD onto branch", nullptr, false, free && headAttached))
        actions.rebaseHeadOnto(r.name);
    ImGui::Separator();
    if (menuItem(ICON_MS_DELETE, "Delete on remote...", nullptr, false, free))
        session.showDeleteRemoteBranchDialog(r.name);
}

void BranchesPanel::remoteBranchMenu(const core::RemoteBranchInfo& r)
{
    if (!beginContextMenu(("##rbranch_menu_" + rowId(r.name)).c_str()))
        return;
    remoteBranchMenuItems(m_session, *m_snapshot, r);
    ImGui::EndPopup();
}

// The menu of a kept commit (a row of the Detached node): History's commit actions on it.
void keptMenuItems(Session& session, const core::KeptInfo& k, bool current, const IdSlot& idSlot)
{
    captureIdCopyClick(idSlot);
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const std::string hex = k.id.hex();
    if (menuItem(ICON_MS_SWAP_HORIZ, "Check out", nullptr, false, free && !current))
        actions.checkout(hex, true);
    if (menuItem(ICON_MS_ADD, "Create branch here...", nullptr, false, free))
        session.showCreateBranchDialog(hex);
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        session.revealCommit(k.id);
    idCopyMenuItem(hex);
    ImGui::Separator();
    if (menuItem(ICON_MS_DELETE_FOREVER, "Abandon...", nullptr, false, free))
        showAbandonDialog(session, k.id);
}

void BranchesPanel::keptMenu(const core::KeptInfo& k, bool current, const IdSlot& idSlot)
{
    if (!beginContextMenu(("##kept_menu_" + k.id.hex()).c_str()))
        return;
    keptMenuItems(m_session, k, current, idSlot);
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
        tooltip("Create branch at HEAD...");
    auto& history = m_session.history();
    // Show all / Hide all: every local and remote-tracking branch and kept commit in History.
    std::vector<std::string> all;
    for (const auto& b : m_snapshot->branches)
        all.push_back("refs/heads/" + b.name);
    for (const auto& r : m_snapshot->remoteBranches)
        all.push_back("refs/remotes/" + r.name);
    for (const auto& k : m_snapshot->kept)
        all.push_back(gg::keep::refName(k.id.hex()));
    sameLineIfFits(ImGui::GetFrameHeight());
    if (ImGui::Button(ICON_MS_VISIBILITY "###show_all_branches"))
        history.setRefsVisible(all, true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        tooltip("Show all branches in History");
    sameLineIfFits(ImGui::GetFrameHeight());
    if (ImGui::Button(ICON_MS_VISIBILITY_OFF "###hide_all_branches"))
        history.setRefsVisible(all, false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        tooltip("Hide all branches in History");
    sameLineIfFits(ImGui::GetFontSize() * 6);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##branch_filter", ICON_MS_SEARCH " Filter", &m_filter);
    const Palette& p = theme().palette();
    const bool filtering = !m_filter.empty();
    // Rows keep IDs directly under the window ("branch_<name>") whatever group they sit in.
    const ImGuiID windowId = ImGui::GetCurrentWindow()->ID;
    // Only the list scrolls: the controls above stay put.
    beginList();

    std::vector<std::pair<size_t, std::string>> locals;
    for (size_t i = 0; i < m_snapshot->branches.size(); ++i)
        if (containsNoCase(m_snapshot->branches[i].name, m_filter))
            locals.push_back({i, m_snapshot->branches[i].name});
    const GroupEyes localEyes{history, [&](size_t index) { return "refs/heads/" + m_snapshot->branches[index].name; }};
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
        // The hover buttons do what the menu's Check out and Push do.
        const RowAction acts[] = {
            {ICON_MS_SWAP_HORIZ, "act_checkout", "Check out", free && !b.isHead, !b.isHead},
            {ICON_MS_UPLOAD, "act_push", "Push", free && !m_snapshot->remotes.empty()},
        };
        const RowEvents events = visibilityRow("branch_" + b.name, label, history.refVisible(full), b.isHead,
            b.isHead ? p.branchCurrentText : ImGui::GetColorU32(ImGuiCol_Text), true, acts);
        if (shortName != b.name && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            tooltip("%s", b.name.c_str());
        if (events.toggle)
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        // Double-click checks the branch out.
        if (events.doubleClicked && free && !b.isHead)
            m_session.actions().checkout(b.name, false);
        if (events.action == 0)
            m_session.actions().checkout(b.name, false);
        else if (events.action == 1)
            pushBranch(m_session, b);
        branchMenu(b);
        ImGui::PopID();
    }, &localEyes);

    // Kept commits (a detached HEAD's commits no branch reaches) between the local and the remote branches.
    std::vector<const core::KeptInfo*> kept;
    for (const auto& k : m_snapshot->kept)
        if (containsNoCase(k.id.hex(), m_filter) || containsNoCase(k.summary, m_filter))
            kept.push_back(&k);
    if (!kept.empty()) {
        ImGui::PushID("detached_group");
        std::vector<std::string> listed;
        for (const auto* k : kept)
            listed.push_back(gg::keep::refName(k->id.hex()));
        groupEye(history, listed);
        ImGui::SameLine();
        bool nodeOpen;
        {
            const SectionHeaderColors neutral;
            nodeOpen = ImGui::TreeNodeEx("Detached", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (nodeOpen) {
            for (const auto* k : kept) {
                const std::string hex = k->id.hex();
                const std::string full = gg::keep::refName(hex);
                const bool current = m_snapshot->headDetached && m_snapshot->head == k->id;
                const std::string shortId = k->id.shortHex(kShortIdLength);
                std::string label = shortId + "  " + k->summary;
                for (const auto& w : m_snapshot->worktrees)
                    if (!w.isCurrent && w.branch.empty() && w.head == k->id)
                        label += "  [" + w.name + "]";
                ImGui::PushOverrideID(windowId);
                ImGui::PushID("detached_group");
                const RowAction acts[] = {
                    {ICON_MS_SWAP_HORIZ, "act_checkout", "Check out", free && !current, !current},
                    {ICON_MS_ADD, "act_branch", "Create branch here...", free},
                };
                const RowEvents events = visibilityRow("detached_" + hex, label, history.refVisible(full), current,
                    current ? p.branchCurrentText : ImGui::GetColorU32(ImGuiCol_Text), true, acts,
                    {kIdPrefixLength, kShortIdLength});
                if (events.toggle)
                    history.toggleRef(full, ImGui::GetIO().KeyCtrl);
                // Double-click checks the commit out.
                if ((events.doubleClicked && free && !current) || events.action == 0)
                    m_session.actions().checkout(hex, true);
                else if (events.action == 1)
                    m_session.showCreateBranchDialog(hex);
                keptMenu(*k, current, idSlotAt(events.labelLeft, shortId, kIdPrefixLength));
                ImGui::PopID();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

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
        const auto remoteTree = buildTree(list);
        const GroupEyes remoteEyes{history, [&](size_t index) { return "refs/remotes/" + m_snapshot->remoteBranches[index].name; }};
        std::vector<std::string> listed;
        collectRefs(remoteTree, remoteEyes, listed);
        groupEye(history, listed);
        ImGui::SameLine();
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
            drawTree(remoteTree, "remote:" + remote + "/", filtering, [&](size_t index, const std::string& shortName) {
                const auto& r = m_snapshot->remoteBranches[index];
                const std::string full = "refs/remotes/" + r.name;
                // Rows keep the IDs they had before groups: <window>/remote_group_<remote>/<remote>/...
                ImGui::PushOverrideID(windowId);
                ImGui::PushID(("remote_group_" + remote).c_str());
                ImGui::PushID(remote.c_str());
                const RowAction acts[] = {{ICON_MS_SWAP_HORIZ, "act_checkout", "Check out", free}};
                const RowEvents events = visibilityRow("rbranch_" + r.name, shortName,
                    history.refVisible(full), false, p.remoteText, false, acts);
                if (events.toggle)
                    history.toggleRef(full, ImGui::GetIO().KeyCtrl);
                if (events.action == 0)
                    checkoutRemoteBranch(m_session, *m_snapshot, r);
                remoteBranchMenu(r);
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            }, &remoteEyes, listed);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    endList();
    ImGui::End();
}

// ---- Tags ---------------------------------------------------------------------------------------

// Delete in a tag's menu: one item for a tag only here; a submenu (Local, then each remote that has it)
// when a remote has it too. A remote whose tags are still being read or could not be read is offered
// with a note (the tag may be there).
static void tagDeleteItems(Session& session, const core::Snapshot& snap, const std::string& name, bool local)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    const auto& remoteTags = session.remoteTags();
    std::vector<std::string> remotes; // menu labels, the remote's name first
    std::vector<std::string> names;
    for (const auto& r : snap.remotes) {
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
}

// The Tags panel's menu for a tag; also on the tag's badge in History.
void tagMenuItems(Session& session, const core::Snapshot& snap, const core::TagInfo& t)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal"))
        session.revealCommit(t.target);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(t.name.c_str());
    ImGui::Separator();
    tagDeleteItems(session, snap, t.name, true);
    if (beginMenu(ICON_MS_UPLOAD, "Push tag", free && !snap.remotes.empty())) {
        for (const auto& r : snap.remotes)
            if (menuItem(ICON_MS_UPLOAD, r.name.c_str()))
                actions.pushTag(r.name, t.name);
        ImGui::EndMenu();
    }
}

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
        tooltip("Create tag at HEAD...");
    sameLineIfFits(ImGui::GetFontSize() * 6);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##tag_filter", ICON_MS_SEARCH " Filter", &m_filter);
    // Only the list scrolls: the controls above stay put.
    beginList();
    auto& history = m_session.history();
    // Tags on the remotes: read while this panel is shown (and again after fetch, pull or push).
    m_session.requestRemoteTagsIfStale();
    const auto& remoteTags = m_session.remoteTags();
    for (const auto& t : m_snapshot->tags) {
        if (!containsNoCase(t.name, m_filter))
            continue;
        const std::string full = "refs/tags/" + t.name;
        // The hover button does what the menu's Reveal does.
        const RowAction acts[] = {
            {ICON_MS_MY_LOCATION, "act_reveal", "Reveal"},
        };
        const RowEvents events = visibilityRow("tag_" + t.name, t.name, history.refVisible(full), false,
            theme().palette().tagText, false, acts);
        if (events.toggle)
            history.toggleRef(full, ImGui::GetIO().KeyCtrl);
        if (events.action == 0)
            m_session.revealCommit(t.target);
        if (t.annotated && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !t.message.empty() && beginTooltip()) {
            // The message (it can run to several paragraphs), wrapped and cut after the line limit.
            tooltipText(t.message);
            ImGui::EndTooltip();
        }
        if (beginContextMenu(("##tag_menu_" + rowId(t.name)).c_str())) {
            tagMenuItems(m_session, *m_snapshot, t);
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
            tooltip("Only on %s: fetch to get it here", where.c_str());
        if (beginContextMenu("##rtag_menu")) {
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
                ImGui::SetClipboardText(name.c_str());
            ImGui::Separator();
            tagDeleteItems(m_session, *m_snapshot, name, false);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    endList();
    ImGui::End();
}

// ---- Repositories -------------------------------------------------------------------------------

// A tree node with the look of a selected row: the selection colours of selectable(). The arrow opens
// it, so a click or a double-click on the label does not toggle it (the keyboard does: Enter or Space
// on the focused node, and the arrow keys, as on any tree node).
static bool selectableTreeNode(const char* label, bool selected)
{
    if (selected) {
        const Palette& p = theme().palette();
        ImGui::PushStyleColor(ImGuiCol_Header, p.selection);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.selectionHovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.selectionHovered);
    }
    const bool open = ImGui::TreeNodeEx(label,
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | (selected ? ImGuiTreeNodeFlags_Selected : 0));
    if (selected)
        ImGui::PopStyleColor(3);
    return open;
}

// A group is a tree node, open by default; a repository is a row. `toOpen` gets the path of the one
// double-clicked. Both ids come from the group path or the label ('/' becomes ':'); equal labels
// among siblings get a "#n" suffix. The row of the open repository (for a linked worktree: of its main
// repository, GG-15; a worktree entry in the list is a plain row) is a node, closed by default, with
// its worktrees below it when it has more than the main one. A repository row is a drag source and a
// group node a drop target (repoDropTarget): the drop puts the repository in the group.
void RepositoriesPanel::drawNodes(const std::vector<RepoNode>& nodes, std::string& toOpen)
{
    std::map<std::string, int> seen;
    for (const auto& node : nodes) {
        if (node.isGroup()) {
            const bool open = ImGui::TreeNodeEx((node.label + "###group_" + rowId(node.group)).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            repoDropTarget(node.group);
            if (open) {
                drawNodes(node.children, toOpen);
                ImGui::TreePop();
            }
            continue;
        }
        const std::string id = rowId(node.label);
        const int n = seen[id]++;
        ImGui::PushID(("repo_" + id + (n ? "#" + std::to_string(n) : std::string())).c_str());
        // The open repository has the text colour of the current branch; the selected look is the selection only.
        const bool isOpen = node.path == m_openPath;
        const auto& snap = m_session.snapshot();
        const bool hasWorktrees = isOpen && snap
            && (snap->worktrees.size() > 1 || (snap->worktrees.size() == 1 && !snap->worktrees.front().isMain));
        const bool selected = !m_selectedWorktree && node.path == m_selected;
        const std::string label = node.label + "###row";
        ImGui::PushStyleColor(ImGuiCol_Text, isOpen ? theme().palette().branchCurrentText : ImGui::GetColorU32(ImGuiCol_Text));
        bool nodeOpen = false;
        if (hasWorktrees) {
            nodeOpen = selectableTreeNode(label.c_str(), selected);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                m_selected = node.path;
                m_selectedWorktree = false;
            }
        } else if (selectable(label.c_str(), selected)) {
            m_selected = node.path;
            m_selectedWorktree = false;
        }
        ImGui::PopStyleColor();
        // Drag the repository onto a group: the payload is its path, the preview its label.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            m_dragLabel = node.label;
            ImGui::SetDragDropPayload("GG_REPO", node.path.data(), node.path.size());
            ImGui::TextUnformatted(node.label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !isOpen)
            toOpen = node.path;
        if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            tooltip("%s", node.path.c_str());
        // `node` is part of the tree copy that draw made, so removing the entry from the settings keeps the loop valid.
        if (beginContextMenu("##repo_menu")) {
            if (isOpen)
                disabledMenuItem(ICON_MS_FOLDER_OPEN, "Open", "This window shows this repository");
            else if (menuItem(ICON_MS_FOLDER_OPEN, "Open"))
                toOpen = node.path;
            if (menuItem(ICON_MS_DRIVE_FILE_RENAME_OUTLINE, "Set alias..."))
                m_session.showSetAliasDialog(node.path);
            ImGui::Separator();
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy path"))
                ImGui::SetClipboardText(node.path.c_str());
            if (menuItem(ICON_MS_CLOSE, "Remove from list")) {
                m_session.app().settings().removeRepository(node.path);
                // Its worktree rows go with the open repository, so a selected one too.
                if (m_selectedWorktree ? isOpen : m_selected == node.path) {
                    m_selected.clear();
                    m_selectedWorktree = false;
                }
            }
            ImGui::EndPopup();
        }
        if (nodeOpen) {
            drawWorktrees(toOpen);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// A drop of a repository on the last item puts it in `group` ("" is the top level). The alias gets the
// new group part; an alias that does not change is not stored, so the settings are not saved.
void RepositoriesPanel::repoDropTarget(const std::string& group)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GG_REPO")) {
        const std::string path(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        auto& settings = m_session.app().settings();
        std::string alias;
        for (const auto& entry : settings.data().repositories)
            if (entry.path == path)
                alias = normalizeAlias(entry.alias);
        const std::string newAlias = aliasWithGroup(alias, m_dragLabel, group);
        if (newAlias != alias)
            settings.setAlias(path, newAlias);
    }
    ImGui::EndDragDropTarget();
}

// The worktrees of the open repository as rows below its node. The current one has the text colour of the
// current branch and a missing one is dimmed. A double-click opens a worktree, not the current or a missing one.
void RepositoriesPanel::drawWorktrees(std::string& toOpen)
{
    for (const auto& w : m_session.snapshot()->worktrees) {
        std::string label = w.name;
        if (w.isMain)
            label += " (main)";
        label += "  ";
        label += w.branch.empty() ? (w.head.isNull() ? std::string("-") : w.head.shortHex(kShortIdLength)) : w.branch;
        const std::string path = w.path.string();
        ImGui::PushID(("worktree_" + w.name).c_str());
        const ImU32 color = w.isCurrent ? theme().palette().branchCurrentText
                                        : ImGui::GetColorU32(w.missing ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (selectable((label + "###row").c_str(), m_selectedWorktree && path == m_selected)) {
            m_selected = path;
            m_selectedWorktree = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !w.isCurrent && !w.missing)
            toOpen = path;
        if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            tooltip("%s", path.c_str());
        ImGui::PopID();
    }
}

void RepositoriesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Repositories, open)) {
        ImGui::End();
        return;
    }
    // The repository list entry of the window: the main repository when it shows a linked worktree (GG-15), so
    // the row of the main repository is the open one, with the worktrees below it.
    const std::string sessionPath = normalizeRepoPath(m_session.path().string());
    m_openPath = m_session.repositoryListPath();
    if (m_openPath.empty())
        m_openPath = sessionPath; // before the first snapshot
    const auto tree = buildRepoTree(m_session.app().settings().data().repositories);
    std::string toOpen;
    beginList();
    drawNodes(tree, toOpen);
    // The empty area below the rows is the top level as a drop target: a repository dropped here leaves its group.
    ImGui::InvisibleButton("###top_level", ImVec2(-FLT_MIN, std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight())));
    repoDropTarget(std::string());
    endList();
    if (!m_selected.empty() && ImGui::Shortcut(ImGuiKey_Enter, ImGuiInputFlags_RouteFocused)) {
        if (!m_selectedWorktree) {
            if (m_selected != m_openPath)
                toOpen = m_selected;
        } else if (const auto& snap = m_session.snapshot()) {
            // The current worktree and a missing one do not open, as with a double-click.
            for (const auto& w : snap->worktrees)
                if (w.path.string() == m_selected && !w.isCurrent && !w.missing)
                    toOpen = m_selected;
        }
    }
    // Not the one the session shows: opening it again would replace the session and cancel its work. The
    // comparison is with the session path, not m_openPath: the main worktree row of a linked worktree opens
    // the main worktree, which is m_openPath. The open repository row sets no toOpen.
    if (!toOpen.empty() && normalizeRepoPath(toOpen) != sessionPath)
        m_session.app().openRepository(toOpen);
    ImGui::End();
}

// ---- Worktrees ----------------------------------------------------------------------------------

// The Worktrees panel's menu for a worktree; also on a worktree badge in History.
void worktreeMenuItems(Session& session, const core::Snapshot& snap, const core::WorktreeInfo& w)
{
    const bool free = session.actions().busy().empty();
    const bool canAdd = free && !snap.headUnborn;
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
        ImGui::SetClipboardText(w.name.c_str());
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy path"))
        ImGui::SetClipboardText(w.path.string().c_str());
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal HEAD", nullptr, false, !w.head.isNull()))
        session.revealCommit(w.head);
    if (menuItem(ICON_MS_OPEN_IN_NEW, "Open directory", nullptr, false, !w.missing))
        openInFileManager(w.path);
    ImGui::Separator();
    const std::string path = w.path.string();
    if (w.isCurrent)
        disabledMenuItem(ICON_MS_FOLDER_OPEN, "Open here", "This window shows this worktree");
    else if (w.missing)
        disabledMenuItem(ICON_MS_FOLDER_OPEN, "Open here", "Its directory is gone");
    else if (menuItem(ICON_MS_FOLDER_OPEN, "Open here"))
        session.app().openRepository(w.path);
    if (w.missing)
        disabledMenuItem(ICON_MS_LAUNCH, "Open in new window", "Its directory is gone");
    else if (menuItem(ICON_MS_LAUNCH, "Open in new window"))
        session.actions().openInNewWindow(path);
    ImGui::Separator();
    if (menuItem(ICON_MS_ADD, "Add...", nullptr, false, canAdd))
        session.showAddWorktreeDialog();
    if (w.isMain)
        disabledMenuItem(ICON_MS_DELETE, "Remove...", "The main worktree cannot be removed");
    else if (w.isCurrent)
        disabledMenuItem(ICON_MS_DELETE, "Remove...", "This window shows this worktree: open another one first");
    else if (menuItem(ICON_MS_DELETE, "Remove...", nullptr, false, free))
        session.showRemoveWorktreeDialog(w);
    if (w.isMain)
        disabledMenuItem(ICON_MS_LOCK, "Lock...", "The main worktree cannot be locked");
    else if (w.locked ? menuItem(ICON_MS_LOCK_OPEN, "Unlock", nullptr, false, free) : menuItem(ICON_MS_LOCK, "Lock...", nullptr, false, free)) {
        if (w.locked)
            session.actions().unlockWorktree(path);
        else
            session.showLockWorktreeDialog(w);
    }
    if (menuItem(ICON_MS_DELETE_SWEEP, "Prune...", nullptr, false, free))
        session.showPruneWorktreesDialog();
    if (menuItem(ICON_MS_HANDYMAN, "Repair...", nullptr, false, free))
        session.showRepairWorktreeDialog(w);
}

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
        tooltip(snap->headUnborn ? "Add worktree... (HEAD has no commit yet)" : "Add worktree...");
    // Only the list scrolls: the controls above stay put.
    beginList();
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
        label += "  ";
        // A detached HEAD's ID is drawn split into its highlighted prefix and the dimmed rest.
        size_t dimFrom = 0, dimTo = 0;
        if (w.branch.empty() && !w.head.isNull()) {
            dimFrom = label.size() + kIdPrefixLength;
            dimTo = label.size() + kShortIdLength;
        }
        label += w.branch.empty() ? (w.head.isNull() ? std::string("-") : w.head.shortHex(kShortIdLength)) : w.branch;
        ImGui::PushID(("worktree_" + w.name).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(w.missing ? ImGuiCol_TextDisabled : ImGuiCol_Text));
        ImGui::SetNextItemAllowOverlap();
        selectableDimRange((label + "###row").c_str(), dimFrom, dimTo, w.isCurrent, ImGuiSelectableFlags_AllowOverlap);
        ImGui::PopStyleColor();
        // The hover buttons do what the menu's Open here and Open directory do (Open here: not for the current one).
        const RowAction acts[] = {
            {ICON_MS_FOLDER_OPEN, "act_open_here", "Open here", !w.missing, !w.isCurrent},
            {ICON_MS_OPEN_IN_NEW, "act_open_dir", "Open directory", !w.missing},
        };
        const int action = rowActions(acts, w.isCurrent);
        if (action == 0)
            m_session.app().openRepository(w.path);
        else if (action == 1)
            openInFileManager(w.path);
        if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            tooltip("%s", cachedTooltipText(ImGui::GetItemID(), snap->generation, [&] {
                std::string text = w.path.string();
                if (w.isCurrent)
                    text += "\nShown in this window";
                if (w.locked)
                    text += "\nLocked" + (w.lockReason.empty() ? std::string() : ": " + w.lockReason)
                        + " (git does not prune, move or remove it)";
                if (w.missing)
                    text += w.locked ? "\nMissing: its directory is gone (kept while locked; Repair if it was moved)"
                                     : "\nMissing: its directory is gone. Prune removes its records; Repair reconnects it "
                                       "if it was moved";
                return text;
            }).c_str());
        }
        if (beginContextMenu("##worktree_menu")) {
            worktreeMenuItems(m_session, *snap, w);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    endList();
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
        tooltip("Add remote...");
    sameLineIfFits(buttonWidth(ICON_MS_DOWNLOAD, "Fetch all"));
    if (button(ICON_MS_DOWNLOAD, "Fetch all##fetch_all") && !snap->remotes.empty())
        actions.fetch("", false, false);
    ImGui::EndDisabled();
    // Only the list scrolls: the controls above stay put.
    beginList();
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
        ImGui::SetNextItemAllowOverlap();
        selectableDimRange((label + "###row").c_str(), dimBegin, label.size(), false, ImGuiSelectableFlags_AllowOverlap);
        // The hover buttons do what the menu's Fetch and Pull do.
        const RowAction acts[] = {
            {ICON_MS_DOWNLOAD, "act_fetch", "Fetch", free},
            {ICON_MS_ARROW_DOWNWARD, "act_pull", "Pull", remotePullable(*snap, r, free)},
        };
        const int action = rowActions(acts);
        if (action == 0)
            actions.fetch(r.name, false, false);
        else if (action == 1)
            actions.pull(PullMode::Config);
        if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            tooltip("%s", cachedTooltipText(ImGui::GetItemID(), snap->generation, [&] {
                const std::string push = r.pushUrl.empty() ? r.url : r.pushUrl;
                std::string text = push == r.url ? r.url : "Fetch: " + r.url + "\nPush: " + push;
                if (r.pruneOnFetch)
                    text += "\nPrunes on fetch";
                return text;
            }).c_str());
        }
        if (beginContextMenu("##remote_menu")) {
            remoteMenuItems(m_session, r);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (snap->remotes.empty())
        ImGui::TextDisabled("No remotes");
    endList();
    ImGui::End();
}

// ---- Stashes ------------------------------------------------------------------------------------

// The Stashes panel's menu for a stash; also on a stash badge in History.
void stashMenuItems(Session& session, const core::StashInfo& s, const IdSlot& baseSlot)
{
    auto& actions = session.actions();
    const bool free = actions.busy().empty();
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
        session.showBranchFromStashDialog(s.index);
    if (menuItem(ICON_MS_DELETE, "Drop...", nullptr, false, free))
        session.showDropStashDialog(s.index);
    ImGui::Separator();
    // One item for the ID the right click was on; off the base ID (and from the keyboard) the stash's
    // commit, and the base's after it.
    idCopyMenuItems({{s.commit.hex(), IdSlot{}}, {s.base.hex(), baseSlot, "base"}});
}

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
        tooltip("Stash the changes in the working tree...");
    sameLineIfFits(buttonWidth(ICON_MS_OUTBOX, "Pop"));
    ImGui::BeginDisabled(!free || !hasStashes);
    if (button(ICON_MS_OUTBOX, "Pop##stash_pop"))
        actions.stashApply(popIndex, true, false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        tooltip("Apply stash@{%d} and drop it", popIndex);
    sameLineIfFits(buttonWidth(ICON_MS_DELETE_SWEEP, "Clear all..."));
    ImGui::BeginDisabled(!free || m_snapshot->stashes.empty());
    if (button(ICON_MS_DELETE_SWEEP, "Clear all...##clear_stashes"))
        m_session.showClearStashesDialog();
    ImGui::EndDisabled();
    // Only the list scrolls: the controls above stay put.
    beginList();
    m_baseSlots.resize(m_snapshot->stashes.size());
    size_t row = 0;
    for (const auto& s : m_snapshot->stashes) {
        IdSlot& baseSlot = m_baseSlots[row++];
        ImGui::PushID(("stash_" + std::to_string(s.index)).c_str());
        // The message's first line, cut to leave the base and date after it (the ID after ### is unchanged);
        // in a panel too narrow for both the message keeps a minimum and the trailing text is clipped.
        const std::string prefix = "stash@{" + std::to_string(s.index) + "} ";
        const std::string trailing = s.base.shortHex(kShortIdLength) + "  " + core::formatTime(s.time);
        const float room = std::max(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(prefix.c_str()).x
                - ImGui::CalcTextSize(trailing.c_str()).x - ImGui::GetStyle().ItemSpacing.x,
            ImGui::GetFontSize() * 8);
        const std::string label = prefix + fitText(std::string(firstLine(s.message)), room);
        const bool selected = m_session.selection().kind == SelKind::Stash && m_session.selection().id == s.commit;
        // SelectOnNav: the nav cursor (arrows) and the selection are one thing; the cursor reaching a row selects it.
        ImGui::SetNextItemAllowOverlap();
        if (selectable((label + "###row").c_str(), selected, ImGuiSelectableFlags_SelectOnNav | ImGuiSelectableFlags_AllowOverlap))
            m_session.select(Selection{SelKind::Stash, s.commit, s.index});
        const ImGuiLastItemData rowItem = ImGui::GetCurrentContext()->LastItemData;
        if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            idTooltip(s.commit.hex(), cachedTooltipText(ImGui::GetItemID(), m_snapshot->generation, [&] {
                return "base " + m_session.shortId(s.base) + "\n" + core::formatTime(s.time)
                    + (s.hasIndexChanges ? "\nhas index changes" : "") + (s.hasUntracked ? "\nhas untracked files" : "");
            }));
        // The menu belongs to the row (the last item before it must be the Selectable).
        if (beginContextMenu("##stash_menu")) {
            stashMenuItems(m_session, s, baseSlot);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        baseSlot = rowIdText(s.base.hex());
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("  %s", core::formatTime(s.time).c_str());
        // The hover buttons do what the menu's Apply and Pop do. Last, so that the base ID and the date (drawn
        // after the row) do not end up over the buttons: the row is the last item again for rowActions.
        ImGui::GetCurrentContext()->LastItemData = rowItem;
        const RowAction acts[] = {
            {ICON_MS_UNARCHIVE, "act_apply", "Apply", free},
            {ICON_MS_OUTBOX, "act_pop", "Pop", free},
        };
        const int action = rowActions(acts, selected);
        if (action == 0)
            actions.stashApply(s.index, false, false);
        else if (action == 1)
            actions.stashApply(s.index, true, false);
        ImGui::PopID();
    }
    if (m_snapshot->stashes.empty())
        ImGui::TextDisabled("No stashes");
    endList();
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
        flattenNextTable(); // the entries are part of the panel's nav layer: the arrows walk them
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
                const std::string oldText = e.oldId.isNull() ? std::string(kShortIdLength, '0') : e.oldId.shortHex(kShortIdLength);
                const std::string arrow = " " ICON_MS_ARROW_RIGHT_ALT " ";
                const std::string label = oldText + arrow + e.newId.shortHex(kShortIdLength) + "###reflog_" + std::to_string(i);
                const size_t newAt = oldText.size() + arrow.size();
                // Where the two IDs are drawn (the selectable's text starts at the cursor), for the menu's copy item.
                const float oldLeft = ImGui::GetCursorScreenPos().x;
                const float newLeft = oldLeft + ImGui::CalcTextSize(label.c_str(), label.c_str() + newAt).x;
                const IdSlot oldSlot = idSlotAt(oldLeft, oldText, kIdPrefixLength);
                const IdSlot newSlot = idSlotAt(newLeft, e.newId.shortHex(kShortIdLength), kIdPrefixLength);
                selectableDimRanges(label.c_str(),
                    {{kIdPrefixLength, kShortIdLength}, {newAt + kIdPrefixLength, newAt + kShortIdLength}}, false,
                    ImGuiSelectableFlags_SpanAllColumns);
                if (beginContextMenu("##reflog_menu")) {
                    // One item for the ID the right click was on. Off the IDs (and from the keyboard) the new ID's,
                    // and the old ID's after it so that it stays reachable.
                    idCopyMenuItems({{e.newId.hex(), newSlot}, {e.oldId.hex(), oldSlot, "old", !e.oldId.isNull()}});
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
                textElided(e.message);
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

namespace {

// A ref as the tooltip names it: a keep ref is "detached <short ID>", the others by their full name.
std::string refChangeName(const std::string& ref)
{
    if (!gg::keep::isKeepRef(ref))
        return ref;
    return "detached " + ref.substr(std::string(gg::keep::kPrefix).size()).substr(0, 10);
}

} // namespace

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
    flattenNextTable(); // the rows are part of the panel's nav layer: the arrows walk them
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !op.refs.empty() && beginTooltip()) {
                // A running operation records its refs as it goes (merged per ref: the new value changes): the revision
                // follows them. The text is the lines joined with '\n', drawn one tooltipText per line as before.
                ImGuiID revision = ImHashData(&op.ended, sizeof op.ended, static_cast<ImGuiID>(op.refs.size()));
                for (const auto& r : op.refs)
                    revision = ImHashStr(r.newValue.c_str(), r.newValue.size(), revision);
                const std::string* const lines = &cachedTooltipText(ImGui::GetItemID(), revision, [&] {
                    std::string text;
                    for (const auto& r : op.refs)
                        text += (text.empty() ? "" : "\n") + refChangeName(r.ref) + ": " + r.oldValue.substr(0, 10)
                            + " " ICON_MS_ARROW_RIGHT_ALT " " + r.newValue.substr(0, 10);
                    return text;
                });
                for (size_t from = 0; from <= lines->size();) {
                    const size_t eol = std::min(lines->find('\n', from), lines->size());
                    tooltipText(std::string_view(*lines).substr(from, eol - from));
                    from = eol + 1;
                }
                ImGui::EndTooltip();
            }
            if (beginContextMenu("##op_menu")) {
                if (menuItem(ICON_MS_RESTORE, "Restore (undo this operation)", nullptr, false, free && op.restorable() && !op.keepOnly()))
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
