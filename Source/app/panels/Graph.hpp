// Commit graph cells shared by History and the interactive rebase preview (product spec §4.2,
// §4.13): lanes, edges and commit dots of one row, drawn from the worker's lane layout.
#pragma once

#include <core/Types.hpp>

#include <imgui.h>

namespace ggui::graph {

// Radius of every commit dot (commits, merge rings, working-tree/index nodes, HEAD ring base).
inline float dotRadius(float rowHeight) { return rowHeight * 0.18f; }
// Half-size of the merge ring's click target: comfortably larger than the dot itself.
inline float mergeHitHalf(float rowHeight) { return rowHeight * 0.32f; }
// Where the graph starts inside its cell: the current commit's outline reaches past half a lane,
// so the table's left edge must not clip it.
float inset(float laneWidth);
// Centre of `lane` in a cell starting at `cellX`.
float laneX(float cellX, int lane, float laneWidth);
// Edges, the commit dot (conflicted commits in the conflict colour; merges as a hollow ring, with
// trailing dots when their history is collapsed) and, for `head`, the outline of the checked-out
// commit. A merge that cannot be collapsed or expanded (`mergeToggle` false) is a plain ring.
void drawCell(const core::HistoryRow& row, float laneWidth, float rowHeight, ImVec2 origin, bool head,
    bool mergeToggle = true);

} // namespace ggui::graph
