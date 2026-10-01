#include "panels/HistoryPanel.hpp"
#include "panels/CommitMenu.hpp"
#include "panels/Graph.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <ctime>

namespace ggui {

using graph::inset;
using graph::laneX;

HistoryPanel::HistoryPanel(Session& session) : m_session(session) { }

core::HistoryScope HistoryPanel::buildScope() const
{
    core::HistoryScope scope;
    scope.toggledMerges = m_toggledMerges;
    if (m_hidden.empty())
        return scope;
    scope.allRefs = false;
    const auto& s = *m_snapshot;
    for (const auto& b : s.branches)
        if (refVisible("refs/heads/" + b.name))
            scope.refs.push_back("refs/heads/" + b.name);
    for (const auto& r : s.remoteBranches)
        if (refVisible("refs/remotes/" + r.name))
            scope.refs.push_back("refs/remotes/" + r.name);
    for (const auto& t : s.tags)
        if (refVisible("refs/tags/" + t.name))
            scope.refs.push_back("refs/tags/" + t.name);
    // HEAD follows its branch; a detached HEAD is always shown.
    if (s.headDetached || refVisible("refs/heads/" + s.headBranch))
        scope.refs.push_back("HEAD");
    return scope;
}

void HistoryPanel::reload()
{
    if (m_query)
        m_session.engine().cancel(m_query);
    m_limit = std::max(m_limit, kPageSize);
    m_query = m_session.engine().loadHistory(buildScope(), m_limit, m_snapshot);
    m_loading = true;
}

void HistoryPanel::onSnapshot(const core::SnapshotPtr& snapshot, bool refsChanged)
{
    m_snapshot = snapshot;
    // Drop hidden refs that no longer exist.
    if (refsChanged)
        reload();
}

void HistoryPanel::onStatus(const core::StatusPtr& status)
{
    m_hasStaged = !status->staged.empty();
    m_hasWorktreeChanges = !status->empty();
    m_nativeConflicts = !status->conflicted.empty();
    if (!m_hasStaged && m_session.selection().kind == SelKind::Index)
        m_session.select(Selection{SelKind::WorkingTree, {}, -1});
}

void HistoryPanel::onHistory(core::HistoryEvent& event)
{
    auto& batch = *event.batch;
    if (batch.query != m_query && event.request != m_query) {
        // Batches from reveal/show-more belong to the current query's walk.
        if (batch.query != m_query)
            return;
    }
    if (batch.reset) {
        m_stagedRows.clear();
        m_stagedIndex.clear();
        m_staging = !m_rows.empty();
        m_stageTarget = m_rows.size();
        if (!m_staging) {
            m_rows.clear();
            m_index.clear();
            m_visibleDirty = true;
        }
    }
    auto& rows = m_staging ? m_stagedRows : m_rows;
    auto& index = m_staging ? m_stagedIndex : m_index;
    for (auto& row : batch.rows) {
        index[row.id] = static_cast<int>(rows.size());
        rows.push_back(std::move(row));
    }
    batch.rows.clear();
    for (const auto& [merge, count] : batch.collapsedCounts)
        if (auto it = index.find(merge); it != index.end())
            rows[static_cast<size_t>(it->second)].collapsedCount = count;
    if (m_staging) {
        if (m_stagedRows.size() < m_stageTarget && !batch.complete && !batch.truncated)
            return; // the previous rows stay until the new ones fill their place
        m_rows = std::move(m_stagedRows);
        m_index = std::move(m_stagedIndex);
        m_stagedRows.clear();
        m_stagedIndex.clear();
        m_staging = false;
    }
    m_complete = batch.complete;
    m_truncated = batch.truncated;
    m_maxLanes = std::max(1, batch.maxLanes);
    m_visibleDirty = true;
    if (batch.complete || batch.truncated)
        m_loading = false;
    if (m_pendingReveal && m_index.count(*m_pendingReveal)) {
        m_session.selectCommit(*m_pendingReveal);
        m_pendingReveal.reset();
        m_scrollToSelection = true;
    }
    if (!m_appliedFilter.empty() && (batch.complete || batch.truncated))
        m_searchRequest = m_session.engine().searchHistory(m_appliedFilter);
}

void HistoryPanel::onTaskFinished(const core::TaskFinishedEvent& event)
{
    if (event.request == m_query)
        m_loading = false;
    if (event.request == m_revealRequest) {
        m_revealRequest = 0;
        if (event.cancelled)
            m_pendingReveal.reset();
    }
}

void HistoryPanel::onConflicts(const core::ConflictsEvent& event)
{
    for (const auto& id : event.scanned)
        if (auto it = m_index.find(id); it != m_index.end())
            m_rows[static_cast<size_t>(it->second)].conflicted = false;
    for (const auto& c : event.commits)
        if (auto it = m_index.find(c.commit); it != m_index.end())
            m_rows[static_cast<size_t>(it->second)].conflicted = true;
    if (m_conflictedOnly)
        m_visibleDirty = true;
}

void HistoryPanel::selectConflicted(int direction)
{
    if (m_visibleDirty) {
        m_visible = visibleIndexes();
        m_visibleDirty = false;
    }
    int pos = direction > 0 ? -1 : static_cast<int>(m_visible.size());
    const Selection& sel = m_session.selection();
    if (sel.kind == SelKind::Commit)
        for (size_t i = 0; i < m_visible.size(); ++i)
            if (m_rows[static_cast<size_t>(m_visible[i])].id == sel.id)
                pos = static_cast<int>(i);
    for (int i = pos + direction; i >= 0 && i < static_cast<int>(m_visible.size()); i += direction) {
        const auto& row = m_rows[static_cast<size_t>(m_visible[static_cast<size_t>(i)])];
        if (row.conflicted) {
            m_session.selectCommit(row.id);
            m_scrollToSelection = true;
            return;
        }
    }
}

void HistoryPanel::onReveal(const core::RevealEvent& event)
{
    if (event.request != m_revealRequest)
        return;
    m_revealRequest = 0;
    if (event.found) {
        m_session.selectCommit(event.id);
        m_scrollToSelection = true;
    } else {
        m_session.app().notify(App::Notice::Warning, "Reveal", "Commit " + event.id.shortHex(10) + " is not in the current history scope");
    }
    m_pendingReveal.reset();
}

void HistoryPanel::onSearch(const core::SearchEvent& event)
{
    if (event.request != m_searchRequest)
        return;
    m_matches.clear();
    for (const auto& id : event.matches)
        m_matches.insert(id);
    m_visibleDirty = true;
}

void HistoryPanel::reveal(const core::Oid& id)
{
    if (m_index.count(id)) {
        m_session.selectCommit(id);
        m_scrollToSelection = true;
        // Clear a filter that would hide it.
        if (!m_appliedFilter.empty() && !m_matches.count(id)) {
            m_filter.clear();
            m_appliedFilter.clear();
            m_visibleDirty = true;
        }
        return;
    }
    m_pendingReveal = id;
    m_revealRequest = m_session.engine().revealCommit(id);
}

void HistoryPanel::showMore()
{
    m_limit += kPageSize;
    m_loading = true;
    m_session.engine().showMoreHistory(kPageSize);
}

void HistoryPanel::toggleRef(const std::string& fullName, bool only)
{
    if (only) {
        m_hidden.clear();
        const auto& s = *m_snapshot;
        for (const auto& b : s.branches)
            m_hidden.insert("refs/heads/" + b.name);
        for (const auto& r : s.remoteBranches)
            m_hidden.insert("refs/remotes/" + r.name);
        for (const auto& t : s.tags)
            m_hidden.insert("refs/tags/" + t.name);
        m_hidden.erase(fullName);
    } else if (m_hidden.count(fullName)) {
        m_hidden.erase(fullName);
    } else {
        m_hidden.insert(fullName);
    }
    reload();
}

void HistoryPanel::showAllRefs()
{
    m_hidden.clear();
    reload();
}

void HistoryPanel::setRefsVisible(const std::vector<std::string>& fullNames, bool visible)
{
    for (const auto& name : fullNames) {
        if (visible)
            m_hidden.erase(name);
        else
            m_hidden.insert(name);
    }
    reload();
}

bool HistoryPanel::descendsFrom(const core::Oid& id, const core::Oid& ancestor) const
{
    std::vector<core::Oid> todo{id};
    std::unordered_set<core::Oid, core::OidHash> seen;
    while (!todo.empty()) {
        const core::Oid c = todo.back();
        todo.pop_back();
        if (!seen.insert(c).second)
            continue;
        const auto* r = row(c);
        if (!r)
            continue;
        for (const auto& p : r->parents) {
            if (p == ancestor)
                return true;
            todo.push_back(p);
        }
    }
    return false;
}

void HistoryPanel::toggleMerge(const core::Oid& id)
{
    auto it = std::find(m_toggledMerges.begin(), m_toggledMerges.end(), id);
    if (it == m_toggledMerges.end())
        m_toggledMerges.push_back(id);
    else
        m_toggledMerges.erase(it);
    reload();
}

const core::HistoryRow* HistoryPanel::row(const core::Oid& id) const
{
    auto it = m_index.find(id);
    return it == m_index.end() ? nullptr : &m_rows[static_cast<size_t>(it->second)];
}

std::vector<core::Oid> HistoryPanel::visibleIds() const
{
    std::vector<core::Oid> ids;
    for (int i : visibleIndexes())
        ids.push_back(m_rows[static_cast<size_t>(i)].id);
    return ids;
}

std::vector<int> HistoryPanel::visibleIndexes() const
{
    std::vector<int> out;
    if (m_conflictedOnly) {
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].conflicted && (m_appliedFilter.empty() || m_matches.count(m_rows[i].id)))
                out.push_back(static_cast<int>(i));
        return out;
    }
    if (m_appliedFilter.empty()) {
        out.resize(m_rows.size());
        for (size_t i = 0; i < m_rows.size(); ++i)
            out[i] = static_cast<int>(i);
        return out;
    }
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_matches.count(m_rows[i].id))
            out.push_back(static_cast<int>(i));
    return out;
}

void HistoryPanel::moveSelection(int delta)
{
    m_extra.clear();
    // Order: Working tree, Index (if staged), commits.
    std::vector<Selection> order;
    if (!m_snapshot->bare) {
        order.push_back(Selection{SelKind::WorkingTree, {}, -1});
        if (m_hasStaged)
            order.push_back(Selection{SelKind::Index, {}, -1});
    }
    const Selection& cur = m_session.selection();
    int pos = -1;
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i].kind == cur.kind)
            pos = static_cast<int>(i);
    const int base = static_cast<int>(order.size());
    if (cur.kind == SelKind::Commit) {
        for (size_t i = 0; i < m_visible.size(); ++i)
            if (m_rows[static_cast<size_t>(m_visible[i])].id == cur.id)
                pos = base + static_cast<int>(i);
    }
    const int total = base + static_cast<int>(m_visible.size());
    if (total == 0)
        return;
    int next = pos < 0 ? 0 : std::clamp(pos + delta, 0, total - 1);
    if (next < base)
        m_session.select(order[static_cast<size_t>(next)]);
    else
        m_session.selectCommit(m_rows[static_cast<size_t>(m_visible[static_cast<size_t>(next - base)])].id);
    m_scrollToSelection = true;
}

void HistoryPanel::dragAndDrop(const core::HistoryRow& row)
{
    // Source: the commit, or a branch when the drag starts on its badge.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const ImVec2 start = ImGui::GetIO().MouseClickedPos[0];
        std::string branch;
        for (const auto& [rect, name] : m_dragBadges)
            if (rect.Contains(start))
                branch = name;
        if (!branch.empty()) {
            ImGui::SetDragDropPayload("GG_BRANCH", branch.data(), branch.size());
            ImGui::Text("Move %s", branch.c_str());
        } else {
            const std::string hex = row.id.hex();
            ImGui::SetDragDropPayload("GG_COMMIT", hex.data(), hex.size());
            ImGui::Text("%s %s", row.shortId.c_str(), row.subject.c_str());
        }
        ImGui::EndDragDropSource();
    }
    if (!ImGui::BeginDragDropTarget())
        return;
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GG_COMMIT"); p && free) {
        const core::Oid source = core::Oid::fromHex(std::string(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize)));
        const ImGuiIO& io = ImGui::GetIO();
        if (source != row.id) {
            if (io.KeyCtrl && io.KeyShift)
                actions.reorder(source, row.id, false, false);
            else if (io.KeyShift)
                actions.reorder(source, row.id, true, false);
            else if (io.KeyCtrl)
                actions.squash(source, row.id.hex(), true);
            else if (io.KeyAlt)
                actions.rebaseOnto(source, row.id.hex(), true);
            else {
                m_pendingDrop = std::make_pair(source, row.id);
                m_openChooser = true;
            }
        }
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GG_BRANCH"); p && free)
        actions.moveBranch(std::string(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize)), row.id.hex());
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GG_FILES"); p && free) {
        // The source ("@<commit>" or a Changes group), then the paths.
        auto lines = gg::splitLines(std::string(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize)));
        const std::string from = lines.front();
        lines.erase(lines.begin());
        std::erase(lines, std::string());
        if (from.rfind("@", 0) == 0) {
            // A commit's files: into its parent, its child or the checked-out commit.
            const core::Oid source = core::Oid::fromHex(from.substr(1));
            const auto* src = this->row(source);
            const bool toParent = src && !src->parents.empty() && src->parents.front() == row.id;
            const bool toChild = !row.parents.empty() && row.parents.front() == source;
            const bool toHead = row.id == m_snapshot->head;
            if (toParent)
                actions.moveChanges(source, Actions::MoveTo::Parent, lines, {});
            else if (toChild)
                actions.moveChanges(source, Actions::MoveTo::Child, lines, {});
            else if (toHead)
                actions.moveChanges(source, Actions::MoveTo::Active, lines, {});
            else
                m_session.app().notify(App::Notice::Warning, "Move changes",
                    "Drop a commit's files on its parent, its child or the checked-out commit.");
        } else {
            actions.absorb(row.id, lines); // working tree files folded into the commit
        }
    }
    ImGui::EndDragDropTarget();
}

void HistoryPanel::drawDropChooser()
{
    if (!m_pendingDrop)
        return;
    if (m_openChooser) {
        ImGui::OpenPopup("##dnd_chooser");
        m_openChooser = false;
    }
    if (!ImGui::BeginPopup("##dnd_chooser")) { // closed without a choice (Escape, a click elsewhere)
        m_pendingDrop.reset();
        return;
    }
    const auto [source, target] = *m_pendingDrop;
    auto& actions = m_session.actions();
    bool chosen = true;
    if (menuItem(ICON_MS_MOVE_UP, "Move before", "Ctrl+Shift"))
        actions.reorder(source, target, false, false);
    else if (menuItem(ICON_MS_MOVE_DOWN, "Move after", "Shift"))
        actions.reorder(source, target, true, false);
    else if (menuItem(ICON_MS_CONTENT_COPY, "Copy after"))
        actions.reorder(source, target, true, true);
    else if (menuItem(ICON_MS_JOIN_INNER, "Squash into", "Ctrl"))
        actions.squash(source, target.hex(), true);
    else if (menuItem(ICON_MS_LOW_PRIORITY, "Rebase onto", "Alt"))
        actions.rebaseOnto(source, target.hex(), true);
    else
        chosen = false;
    if (chosen) {
        m_pendingDrop.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

namespace {

// A row's Selectable, stretched over the table's cell padding so that neighbouring rows' highlights
// (selected or hovered) meet without a gap: ImGui grows a Selectable by half the item spacing above
// and below, and the table keeps ItemSpacing.y at 0.
bool rowSelectable(const char* label, bool selected, ImGuiSelectableFlags flags)
{
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, ImGui::GetStyle().CellPadding.y * 2);
    const bool pressed = selectable(label, selected, flags);
    ImGui::PopStyleVar();
    return pressed;
}

} // namespace

void HistoryPanel::drawVirtualRow(const char* id, const char* label, SelKind kind, float laneWidth)
{
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2);
    ImGui::TableSetColumnIndex(0);
    const ImVec2 cellStart = ImGui::GetCursorScreenPos();
    const Selection& sel = m_session.selection();
    const bool selected = sel.kind == kind;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    const std::string sid = std::string(label) + "###" + id;
    if (rowSelectable(sid.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
        m_session.select(Selection{kind, {}, -1});
    ImGui::PopStyleColor();
    if (kind == SelKind::WorkingTree && ImGui::BeginPopupContextItem("##wt_menu")) {
        auto& actions = m_session.actions();
        const bool free = actions.busy().empty();
        const bool dirty = m_hasWorktreeChanges;
        const bool unborn = m_snapshot->headUnborn;
        if (menuItem(ICON_MS_CHECK, "Commit...", nullptr, false, free))
            m_session.showCommitDialog(false);
        if (menuItem(ICON_MS_EDIT_NOTE, "Amend into HEAD...", nullptr, false, free && !unborn))
            m_session.showCommitDialog(true);
        if (menuItem(ICON_MS_UNDO, "Discard changes...", nullptr, false, free && dirty && !unborn))
            m_session.showDiscardAllDialog();
        if (menuItem(ICON_MS_ADD, "Stash changes...", nullptr, false, free && dirty && !unborn))
            m_session.showStashDialog();
        ImGui::Separator();
        if (menuItem(ICON_MS_ADD, "Stage all", nullptr, false, free && dirty))
            actions.stageAll();
        if (menuItem(ICON_MS_REMOVE, "Unstage all", nullptr, false, free && m_hasStaged))
            actions.unstageAll();
        ImGui::EndPopup();
    }
    const ImU32 text = (kind == SelKind::WorkingTree && m_nativeConflicts) ? theme().palette().conflict : ImGui::GetColorU32(ImGuiCol_Text);
    if (!m_graphShown) {
        ImGui::SetCursorScreenPos(cellStart);
        ImGui::PushStyleColor(ImGuiCol_Text, text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        return;
    }
    // Node in lane 0 (hollow), dashed line hint toward HEAD.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The HEAD lane (0) continues from here down to HEAD's row; the Index row sits on it.
    const float pad = ImGui::GetStyle().CellPadding.y;
    const ImVec2 min = ImGui::GetItemRectMin();
    const float h = ImGui::GetItemRectSize().y;
    const ImVec2 c(laneX(min.x, 0, laneWidth), min.y + h * 0.5f);
    const Palette& p = theme().palette();
    const ImU32 col = (kind == SelKind::WorkingTree && m_nativeConflicts) ? p.conflict : p.dim;
    const float r = graph::dotRadius(h);
    const float thickness = std::max(1.5f, ImGui::GetFontSize() * 0.12f);
    if (kind == SelKind::Index)
        dl->AddLine(ImVec2(c.x, min.y - pad), ImVec2(c.x, c.y - r), col, thickness);
    dl->AddCircle(c, r, col, 0, thickness);
    dl->AddLine(ImVec2(c.x, c.y + r), ImVec2(c.x, min.y + h + pad), col, thickness);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void HistoryPanel::drawRowMenu(const core::HistoryRow& row)
{
    if (!ImGui::BeginPopupContextItem("##row_menu"))
        return;
    // The table runs with zero vertical item spacing; the menu uses the regular one.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, m_menuItemSpacing);
    // A click on a row outside the selection selects just that row; on a selected row it keeps the selection.
    if (m_session.selection().kind != SelKind::Commit || (m_session.selection().id != row.id
            && std::find(m_extra.begin(), m_extra.end(), row.id) == m_extra.end())) {
        m_extra.clear();
        m_session.selectCommit(row.id);
    }
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const SelectionShape sel = selectionShape(m_session);
    const std::string hex = row.id.hex();
    const bool single = sel.single();
    std::vector<std::string> branchesHere;
    for (const auto& b : m_snapshot->branches)
        if (b.target == row.id)
            branchesHere.push_back(b.name);
    // "New" advances a branch (HEAD's, or the only one here); elsewhere only "New detached" exists.
    // Holding Alt swaps "New" for "New detached" (the same variants as the N / Alt+N hotkeys).
    const bool attach = !m_session.newCommitBranch(row.id).empty();
    if (attach && !ImGui::GetIO().KeyAlt) {
        if (menuItem(ICON_MS_ADD, "New", "N", false, free && single))
            m_session.newCommitOn(row.id, false);
    } else {
        if (menuItem(ICON_MS_ADD_CIRCLE, "New detached", attach ? "Alt+N" : "N", false, free && single))
            m_session.newCommitOn(row.id, true);
        if (!attach)
            disabledHint(true, "Only a branch's tip (HEAD's branch, or a commit with exactly one branch) can get a new attached commit; here it is detached.");
    }
    if (!single)
        disabledHint(true, "Needs a single selected commit.");
    // Items acting on the row: disabled unless it is the only selected commit.
    auto needOne = [&] {
        if (!single)
            disabledHint(true, "Needs a single selected commit.");
    };
    if (beginMenu(ICON_MS_SWAP_HORIZ, "Check out", free && single)) {
        for (const auto& b : branchesHere)
            if (menuItem(ICON_MS_SWAP_HORIZ, b.c_str()))
                actions.checkout(b, false);
        if (!branchesHere.empty())
            ImGui::Separator();
        if (menuItem(ICON_MS_SWAP_HORIZ, "Detached HEAD"))
            actions.checkout(hex, true);
        ImGui::EndMenu();
    }
    needOne();
    if (menuItem(ICON_MS_ADD, "Create branch...", nullptr, false, free && single))
        m_session.showCreateBranchDialog(hex);
    needOne();
    if (menuItem(ICON_MS_ADD, "Create tag...", nullptr, false, free && single))
        m_session.showCreateTagDialog(hex);
    needOne();
    std::vector<std::string> movable;
    for (const auto& b : m_snapshot->branches)
        if (b.target != row.id)
            movable.push_back(b.name);
    if (beginMenu(ICON_MS_DRIVE_FILE_MOVE, "Move branch", free && single && !movable.empty())) {
        for (const auto& name : movable)
            if (menuItem(ICON_MS_DRIVE_FILE_MOVE, name.c_str()))
                m_session.showMoveBranchDialog(name, hex);
        ImGui::EndMenu();
    }
    if (!single)
        disabledHint(true, "Needs a single selected commit.");
    else
        disabledHint(movable.empty(), "No other branch to move here.");
    ImGui::Separator();
    if (beginMenu(ICON_MS_CONTENT_COPY, "Copy", single)) {
        copyIdMenuItem("", row.shortId, hex);
        if (menuItem(ICON_MS_CONTENT_COPY, "Full description")) {
            std::string text = hex + " " + row.subject + "\nAuthor: " + row.author + " <" + row.authorEmail
                + ">\nDate: " + core::formatTime(row.time, true);
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::EndMenu();
    }
    needOne();
    if (mergeToggle(row)) {
        ImGui::Separator();
        if (menuItem(row.collapsed ? ICON_MS_UNFOLD_MORE : ICON_MS_UNFOLD_LESS, row.collapsed ? "Expand merged history" : "Collapse merged history",
                nullptr, false, single))
            toggleMerge(row.id);
        needOne();
    }
    ImGui::Separator();
    drawCommitEditItems(m_session, row);
    if (menuItem(ICON_MS_LOW_PRIORITY, "Interactive rebase selection...", nullptr, false, free && sel.range()))
        openInteractiveRebaseSelection(m_session, sel.ids);
    disabledHint(!sel.range(), sel.count() < 2 ? "Needs two or more adjacent commits selected (Ctrl-click to add)."
                                               : "The selected commits are not adjacent (gaps between them).");
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

// Merge commits carry an icon before the subject: bright while the merged history is collapsed, dim
// once expanded; clicking it toggles that (where collapsing hides anything). Stashes are merge-shaped
// but already marked by their badge. The item is a button over the row's Selectable, so a click on
// it does not change the selection.
void HistoryPanel::drawMergeIcon(const core::HistoryRow& row)
{
    if (row.parents.size() < 2)
        return;
    for (const auto& ref : row.refs)
        if (ref.kind == core::RefKind::Stash)
            return;
    const bool toggle = mergeToggle(row);
    const Palette& p = theme().palette();
    const ImVec2 size = ImGui::CalcTextSize(ICON_MS_CALL_MERGE);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##merge_icon", size);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && toggle)
        toggleMerge(row.id);
    ImU32 color = row.collapsed ? ImGui::GetColorU32(ImGuiCol_Text, 0.75f)
                                : (p.dim & ~IM_COL32_A_MASK) | (static_cast<ImU32>(150) << IM_COL32_A_SHIFT);
    if (hovered)
        color = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddText(pos, color, ICON_MS_CALL_MERGE);
    if (hovered && toggle)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hovered && !m_scrolling) {
        const size_t n = row.parents.size();
        if (!toggle)
            ImGui::SetTooltip("Merge commit \xE2\x80\x94 %d parents", static_cast<int>(n));
        else if (row.collapsed)
            ImGui::SetTooltip("Merge commit \xE2\x80\x94 %d parents; merged history collapsed (click to expand)", static_cast<int>(n));
        else
            ImGui::SetTooltip("Merge commit \xE2\x80\x94 %d parents; merged history expanded (click to collapse)", static_cast<int>(n));
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
}

void HistoryPanel::drawRow(const core::HistoryRow& row, int index, float laneWidth)
{
    const Palette& p = theme().palette();
    // Fixed row pitch: the graph cell draws edge to edge, so no row may grow taller.
    const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableSetColumnIndex(0);
    const Selection& sel = m_session.selection();
    const bool extra = std::find(m_extra.begin(), m_extra.end(), row.id) != m_extra.end();
    const bool selected = (sel.kind == SelKind::Commit && sel.id == row.id) || extra;
    ImGui::PushID(row.id.hex().c_str());
    const ImVec2 cellStart = ImGui::GetCursorScreenPos();
    m_dragBadges = m_badgeRects[row.id]; // last frame's badges, for a drag starting now
    m_badgeRects[row.id].clear();
    m_rowTops.emplace_back(index, cellStart.y);
    const std::string label = row.shortId + " " + row.subject + "###row_" + row.id.hex();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    if (rowSelectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        if (ImGui::GetIO().KeyCtrl && sel.kind == SelKind::Commit && sel.id != row.id) {
            // Ctrl-click adds (or removes) further commits.
            if (extra)
                m_extra.erase(std::find(m_extra.begin(), m_extra.end(), row.id));
            else
                m_extra.push_back(row.id);
        } else {
            m_extra.clear();
            m_session.selectCommit(row.id);
        }
    }
    ImGui::PopStyleColor();
    if (selected && m_scrollToSelection) {
        ImGui::SetScrollHereY(0.4f);
        m_scrollToSelection = false;
    }
    if (!m_scrolling && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        idTooltip(row.id.hex(), row.shortId.size(),
            row.author + " <" + row.authorEmail + ">\n" + core::formatTime(row.time, true));
    dragAndDrop(row);
    drawRowMenu(row);
    if (m_graphShown)
        graph::drawCell(row, laneWidth, rowHeight, cellStart, row.id == m_session.snapshot()->head);

    if (m_graphShown)
        ImGui::TableSetColumnIndex(1);
    else
        ImGui::SetCursorScreenPos(cellStart); // the row's Selectable shares the first column
    ImGui::PushStyleColor(ImGuiCol_Text, p.dim);
    ImGui::TextUnformatted(row.shortId.c_str());
    ImGui::PopStyleColor();
    const bool showStashes = m_session.app().settings().data().historyShowStashes;
    for (const auto& ref : row.refs) {
        if (ref.kind == core::RefKind::Stash && !showStashes)
            continue;
        // A hidden ref loses its badge even where other refs keep the commit in view.
        if ((ref.kind == core::RefKind::LocalBranch && !refVisible("refs/heads/" + ref.name))
            || (ref.kind == core::RefKind::RemoteBranch && !refVisible("refs/remotes/" + ref.name))
            || (ref.kind == core::RefKind::Tag && !refVisible("refs/tags/" + ref.name)))
            continue;
        ImGui::SameLine();
        ImU32 color = p.branch;
        const char* icon = "";
        switch (ref.kind) {
        case core::RefKind::LocalBranch: color = ref.current ? p.branchCurrent : p.branch; icon = ICON_MS_CALL_SPLIT; break;
        case core::RefKind::RemoteBranch: color = p.remote; icon = ICON_MS_CLOUD_DOWNLOAD; break;
        case core::RefKind::Tag: color = p.tag; icon = ICON_MS_SELL; break;
        case core::RefKind::Head: color = p.head; icon = ""; break;
        case core::RefKind::Worktree: color = p.worktree; icon = ICON_MS_FOLDER; break;
        case core::RefKind::Stash: color = p.stash; icon = ICON_MS_INVENTORY_2; break;
        }
        const std::string badge = std::string(icon) + (icon[0] ? " " : "") + ref.name + "###badge_" + ref.name;
        drawBadge(badge.c_str(), color, ref.current);
        if (ref.kind == core::RefKind::LocalBranch)
            m_badgeRects[row.id].emplace_back(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), ref.name);
    }
    ImGui::SameLine();
    drawMergeIcon(row);
    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    if (row.conflicted)
        textColor = p.conflict;
    else if (!row.published)
        textColor = p.unpublished;
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    if (row.conflicted) {
        ImGui::TextUnformatted(ICON_MS_WARNING);
        ImGui::SameLine(0, 2);
    }
    if (row.subject.empty())
        ImGui::TextDisabled("(no description)");
    else
        ImGui::TextUnformatted(row.subject.c_str());
    ImGui::PopStyleColor();

    ImGui::TableSetColumnIndex(column(2));
    ImGui::TextUnformatted(row.author.c_str());
    ImGui::TableSetColumnIndex(column(3));
    ImGui::TextUnformatted(core::formatTime(row.time).c_str());
    ImGui::PopID();
}

void HistoryPanel::restoreScrollAnchor(int virtualRows, float pitch)
{
    if (virtualRows != m_virtualRows)
        m_restoreAnchor = true;
    m_virtualRows = virtualRows;
    if (m_scrollToSelection) {
        // Scrolling to the selection wins.
        m_restoreAnchor = false;
        m_wantScroll.reset();
    }
    if (m_restoreAnchor && m_anchor) {
        std::unordered_map<core::Oid, size_t, core::OidHash> wanted;
        for (size_t k = 0; k < m_anchor->ids.size(); ++k)
            wanted.emplace(m_anchor->ids[k], k);
        size_t best = m_anchor->ids.size();
        int pos = -1;
        for (size_t i = 0; i < m_visible.size() && best > 0; ++i)
            if (auto it = wanted.find(m_rows[static_cast<size_t>(m_visible[i])].id); it != wanted.end() && it->second < best) {
                best = it->second;
                pos = static_cast<int>(i);
            }
        if (pos >= 0) {
            const int delta = virtualRows + pos - m_anchor->slots[best];
            m_wantScroll = std::max(0.0f, m_anchor->scrollY + static_cast<float>(delta) * pitch);
            m_wantScrollFrames = 0;
            m_restoreAnchor = false;
        } else if (!m_loading) {
            m_restoreAnchor = false; // gone: leave the scroll where it is
        }
    }
    if (m_wantScroll)
        ImGui::SetNextWindowScroll(ImVec2(-1.0f, *m_wantScroll));
}

void HistoryPanel::captureScrollAnchor(int virtualRows)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (m_wantScroll) {
        // Scrolling is clamped to last frame's content height: when rows were added the target
        // may be reached a frame later.
        if (std::abs(w->Scroll.y - *m_wantScroll) <= 0.5f || ++m_wantScrollFrames > 3)
            m_wantScroll.reset();
        return;
    }
    if (m_restoreAnchor)
        return; // still waiting for the anchored rows (a reload streams in)
    std::vector<std::pair<float, int>> inView; // (top, display index)
    const float top = w->InnerClipRect.Min.y + ImGui::TableGetHeaderRowHeight();
    for (const auto& [index, y] : m_rowTops)
        if (y >= top - 0.5f && y < w->InnerClipRect.Max.y)
            inView.emplace_back(y, index);
    std::sort(inView.begin(), inView.end());
    ScrollAnchor a;
    a.scrollY = w->Scroll.y;
    for (const auto& [y, index] : inView) {
        a.ids.push_back(m_rows[static_cast<size_t>(m_visible[static_cast<size_t>(index)])].id);
        a.slots.push_back(virtualRows + index);
    }
    if (a.ids.empty())
        m_anchor.reset();
    else
        m_anchor = std::move(a);
}

void HistoryPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::History, open)) {
        ImGui::End();
        return;
    }
    // Header: filter, toggles.
    // Wide enough for its hint, never wider than the panel; the toggles after it flow to the next line when short of room.
    const char* filterHint = ICON_MS_SEARCH " Filter: message, ID, branch, tag";
    ImGui::SetNextItemWidth(std::clamp(ImGui::CalcTextSize(filterHint).x + ImGui::GetStyle().FramePadding.x * 2.0f,
        std::min(ImGui::GetFontSize() * 8, ImGui::GetContentRegionAvail().x), ImGui::GetContentRegionAvail().x));
    bool filterChanged = ImGui::InputTextWithHint("##hist_filter", filterHint, &m_filter);
    filterChanged = acceptCommitDrop(m_filter) || filterChanged;
    if (filterChanged) {
        m_appliedFilter = m_filter;
        m_matches.clear();
        m_visibleDirty = true;
        if (!m_appliedFilter.empty())
            m_searchRequest = m_session.engine().searchHistory(m_appliedFilter);
    }
    sameLineIfFits(checkboxWidth("Conflicted only"));
    if (ImGui::Checkbox("Conflicted only##hist_conflicted", &m_conflictedOnly))
        m_visibleDirty = true;
    sameLineIfFits(checkboxWidth("Stashes"));
    bool showStashes = m_session.app().settings().data().historyShowStashes;
    if (ImGui::Checkbox("Stashes##hist_stashes", &showStashes)) {
        m_session.app().settings().data().historyShowStashes = showStashes;
        m_session.app().settings().save();
    }
    if (!m_hidden.empty()) {
        sameLineIfFits(buttonWidth(ICON_MS_VISIBILITY, "Show all refs"));
        if (smallButton(ICON_MS_VISIBILITY, "Show all refs##hist_all"))
            showAllRefs();
    }
    if (m_loading) {
        ImGui::SameLine();
        spinner("##hist_loading", ImGui::GetFontSize() * 0.4f);
        ImGui::SameLine();
        ImGui::TextDisabled("Loading... %zu", m_rows.size());
    }

    if (m_visibleDirty) {
        m_visible = visibleIndexes();
        m_visibleDirty = false;
        m_restoreAnchor = true;
    }

    // Keyboard while the panel is focused (§4.2 keys).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            moveSelection(+1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            moveSelection(-1);
        const ImGuiIO& io = ImGui::GetIO();
        const Selection& sel = m_session.selection();
        const bool free = m_session.actions().busy().empty();
        if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
            selectConflicted(io.KeyShift ? -1 : +1);
        // Squash (S) also takes a range of adjacent selected commits.
        if (sel.kind == SelKind::Commit && free && !m_extra.empty() && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt
            && ImGui::IsKeyPressed(ImGuiKey_S, false))
            squashSelection(m_session);
        // The commit keys act on a single selected commit.
        if (sel.kind == SelKind::Commit && free && !io.KeyCtrl && m_extra.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_N, false))
                m_session.newCommitOn(sel.id, io.KeyAlt);
            else if (ImGui::IsKeyPressed(ImGuiKey_E, false) && !io.KeyAlt)
                m_session.actions().editCommit(sel.id);
            else if (const auto* r = row(sel.id))
                handleCommitEditKeys(m_session, *r);
        }
    }

    const float laneWidth = ImGui::GetFontSize() * 0.9f;
    const float graphWidth = std::min(inset(laneWidth) + laneWidth * static_cast<float>(std::max(1, m_maxLanes)) + laneWidth * 0.5f,
        ImGui::GetContentRegionAvail().x * 0.4f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    m_menuItemSpacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(m_menuItemSpacing.x, 0));
    m_graphShown = m_appliedFilter.empty() && !m_conflictedOnly;
    const float pitch = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    const int virtualRows = !m_snapshot->bare ? (m_hasStaged ? 2 : 1) : 0;
    restoreScrollAnchor(virtualRows, pitch);
    if (ImGui::BeginTable(m_graphShown ? "##hist_table" : "##hist_table_filtered", m_graphShown ? 4 : 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        if (m_graphShown)
            ImGui::TableSetupColumn("##graph", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, graphWidth);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
        // The graph column follows the lane count (the table would otherwise keep its first or
        // saved width). Only before the first row (the layout is locked after that) and once the
        // table has been laid out (on its first frame the setup width applies).
        ImGuiTable* table = ImGui::GetCurrentTable();
        if (m_graphShown && table->MinColumnWidth > 0.0f && std::abs(table->Columns[0].WidthRequest - graphWidth) > 0.5f)
            ImGui::TableSetColumnWidth(0, graphWidth);
        ImGui::TableHeadersRow();
        // Wheel, scrollbar or keyboard: any change of the scroll position this frame.
        const float scrollY = ImGui::GetScrollY();
        if (std::abs(scrollY - m_lastScrollY) > 0.5f)
            m_scrolledAt = ImGui::GetTime();
        m_lastScrollY = scrollY;
        m_scrolling = m_scrolledAt >= 0.0 && ImGui::GetTime() - m_scrolledAt < 0.3;
        if (!m_snapshot->bare) {
            std::string wtLabel = "Working tree";
            if (m_nativeConflicts)
                wtLabel = std::string(ICON_MS_WARNING) + " Working tree (conflicts)";
            else if (!m_hasWorktreeChanges)
                wtLabel += " (clean)";
            drawVirtualRow("row_wt", wtLabel.c_str(), SelKind::WorkingTree, laneWidth);
            if (m_hasStaged)
                drawVirtualRow("row_index", "Index (staged)", SelKind::Index, laneWidth);
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_visible.size()));
        // Keep the selected row inside the clipper range so it can scroll into view.
        if (m_scrollToSelection && m_session.selection().kind == SelKind::Commit) {
            for (size_t i = 0; i < m_visible.size(); ++i)
                if (m_rows[static_cast<size_t>(m_visible[i])].id == m_session.selection().id) {
                    clipper.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }
        }
        m_rowTops.clear();
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                drawRow(m_rows[static_cast<size_t>(m_visible[static_cast<size_t>(i)])], i, laneWidth);
        }
        if (m_truncated) {
            // The walk stops after a page: the last row loads the next one.
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2);
            ImGui::TableSetColumnIndex(column(1));
            ImGui::BeginDisabled(m_loading);
            const std::string more = std::string(ICON_MS_EXPAND_MORE " Load more (") + std::to_string(m_rows.size())
                + " commits shown)###hist_load_more";
            if (rowSelectable(more.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                showMore();
            ImGui::EndDisabled();
        }
        captureScrollAnchor(virtualRows);
        ImGui::EndTable();
        drawDropChooser();
        m_rowPitchOk = true;
        for (size_t k = 1; k < m_rowTops.size(); ++k)
            if (m_rowTops[k].first == m_rowTops[k - 1].first + 1
                && std::abs(m_rowTops[k].second - m_rowTops[k - 1].second - pitch) > 0.5f)
                m_rowPitchOk = false;
    } else {
        ImGui::GetCurrentContext()->NextWindowData.ClearFlags(); // the scroll was for the table
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace ggui
