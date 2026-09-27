// History panel (REBUILD_PLAN §4.2): commit graph with lanes, badges, virtual Working tree and
// Index rows, scope, search, reveal, "Show more" and merge collapsing.
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ggui {

class HistoryPanel {
public:
    static constexpr int kPageSize = 2000;

    explicit HistoryPanel(Session& session);

    void onSnapshot(const core::SnapshotPtr& snapshot, bool refsChanged);
    void onStatus(const core::StatusPtr& status);
    void onHistory(core::HistoryEvent& event);
    void onReveal(const core::RevealEvent& event);
    void onSearch(const core::SearchEvent& event);
    void onTaskFinished(const core::TaskFinishedEvent& event);
    void onConflicts(const core::ConflictsEvent& event);
    void draw(bool* open);

    void reload();
    void reveal(const core::Oid& id);
    void showMore();

    // Scope (side panels): visibility of refs by full name.
    bool refVisible(const std::string& fullName) const { return !m_hidden.count(fullName); }
    void toggleRef(const std::string& fullName, bool only);
    void showAllRefs();

    void toggleMerge(const core::Oid& id);
    const core::HistoryRow* row(const core::Oid& id) const;
    // Whether `id` has `ancestor` among its ancestors in the loaded history (UI thread).
    bool descendsFrom(const core::Oid& id, const core::Oid& ancestor) const;
    // Commits Ctrl-clicked in addition to the selection (New with several parents = merge).
    const std::vector<core::Oid>& extraSelection() const { return m_extra; }
    const std::vector<core::HistoryRow>& rows() const { return m_rows; }
    // Ids of the rows currently shown (after the filter), in order.
    std::vector<core::Oid> visibleIds() const;
    bool searchActive() const { return !m_appliedFilter.empty(); }
    bool conflictedOnly() const { return m_conflictedOnly; }
    // Selects the next (+1) / previous (-1) conflicted commit (F7 / Shift+F7).
    void selectConflicted(int direction);
    bool loading() const { return m_loading; }
    bool truncated() const { return m_truncated; }
    // Indexes into rows() of the rows currently shown, in order.
    std::vector<int> visibleIndexes() const;
    // Last frame: every drawn row started exactly one row height below the previous one.
    bool rowPitchConsistent() const { return m_rowPitchOk; }

private:
    core::HistoryScope buildScope() const;
    void drawRow(const core::HistoryRow& row, int index, float laneWidth);
    void drawVirtualRow(const char* id, const char* label, SelKind kind, float laneWidth);
    void drawGraphCell(const core::HistoryRow& row, float laneWidth, float rowHeight, ImVec2 origin);
    void drawRowMenu(const core::HistoryRow& row);
    void moveSelection(int delta);

    Session& m_session;
    core::SnapshotPtr m_snapshot;
    std::vector<core::HistoryRow> m_rows;
    std::unordered_map<core::Oid, int, core::OidHash> m_index;
    core::RequestId m_query = 0;
    core::RequestId m_revealRequest = 0;
    bool m_loading = false;
    bool m_complete = false;
    bool m_truncated = false;
    int m_maxLanes = 1;
    int m_limit = kPageSize;
    bool m_hasStaged = false;
    bool m_hasWorktreeChanges = false;
    bool m_nativeConflicts = false;

    std::set<std::string> m_hidden;
    std::vector<core::Oid> m_toggledMerges; // differ from the default (collapsed)

    std::string m_filter;
    std::string m_appliedFilter;
    core::RequestId m_searchRequest = 0;
    std::unordered_set<core::Oid, core::OidHash> m_matches;

    bool m_conflictedOnly = false;
    std::vector<core::Oid> m_extra;
    std::optional<core::Oid> m_pendingReveal;
    bool m_scrollToSelection = false;
    std::vector<int> m_visible; // indexes into m_rows (filtered)
    bool m_visibleDirty = true;
    std::vector<std::pair<int, float>> m_rowTops; // (display index, top y) drawn this frame
    // Drag and drop: where the branch badges were (last frame), and a commit dropped without a
    // modifier waiting for the chooser (source, target).
    std::unordered_map<core::Oid, std::vector<std::pair<ImRect, std::string>>, core::OidHash> m_badgeRects;
    std::optional<std::pair<core::Oid, core::Oid>> m_pendingDrop;
    std::vector<std::pair<ImRect, std::string>> m_dragBadges;
    void dragAndDrop(const core::HistoryRow& row);
    void drawDropChooser();
    bool m_rowPitchOk = true;
};

} // namespace ggui
