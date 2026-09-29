#include "panels/Graph.hpp"

#include "shell/Theme.hpp"

#include <algorithm>

namespace ggui::graph {

namespace {
ImU32 withAlpha(ImU32 color, int alpha) { return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT); }
} // namespace

float inset(float laneWidth)
{
    const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
    const float thickness = std::max(1.5f, ImGui::GetFontSize() * 0.12f);
    return std::max(0.0f, dotRadius(rowHeight) + thickness * 2.0f - laneWidth * 0.5f);
}

float laneX(float cellX, int lane, float laneWidth)
{
    return cellX + inset(laneWidth) + laneWidth * (static_cast<float>(lane) + 0.5f);
}

void drawCell(const core::HistoryRow& row, float laneWidth, float rowHeight, ImVec2 origin, bool head, bool mergeToggle)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = origin.y - ImGui::GetStyle().CellPadding.y;
    const float x0 = laneX(origin.x, 0, laneWidth);
    const float ys[3] = {top, top + rowHeight * 0.5f, top + rowHeight};
    const Palette& p = theme().palette();
    const float thickness = std::max(1.5f, ImGui::GetFontSize() * 0.12f);
    for (const auto& l : row.lines) {
        const ImVec2 a(x0 + l.fromLane * laneWidth, ys[l.fromPos]);
        const ImVec2 b(x0 + l.toLane * laneWidth, ys[l.toPos]);
        const ImU32 col = p.lanes[l.color % 8];
        if (l.fromLane == l.toLane) {
            dl->AddLine(a, b, col, thickness);
        } else {
            const float dy = (b.y - a.y) * 0.6f;
            dl->AddBezierCubic(a, ImVec2(a.x, a.y + dy), ImVec2(b.x, b.y - dy), b, col, thickness);
        }
    }
    const ImVec2 c(x0 + row.lane * laneWidth, ys[1]);
    const ImU32 col = row.conflicted ? p.conflict : p.lanes[row.color % 8];
    const float r = dotRadius(rowHeight);
    if (row.parents.size() > 1) {
        // Merge: hollow ring; a collapsed one trails a fading "⋯" to its right (hidden history).
        const float hit = mergeHitHalf(rowHeight);
        const bool hovered = mergeToggle && ImGui::IsMouseHoveringRect(ImVec2(c.x - hit, c.y - hit), ImVec2(c.x + hit, c.y + hit));
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(ImGuiCol_WindowBg));
        if (hovered)
            dl->AddCircleFilled(c, r, withAlpha(col, 110));
        dl->AddCircle(c, r, col, 0, thickness);
        if (mergeToggle && row.collapsed) {
            // Three dots right of the ring, spaced by their own size so they read as an ellipsis.
            const float dr = std::max(1.5f, thickness * 0.8f);
            const float first = r + thickness * 0.5f + dr * 2.2f;
            const float spacing = dr * 3.2f;
            static constexpr int alphas[3] = {255, 190, 130};
            for (int i = 0; i < 3; ++i) {
                const float dx = first + spacing * static_cast<float>(i);
                dl->AddCircleFilled(ImVec2(c.x + dx, c.y), dr, withAlpha(col, alphas[i]));
            }
        }
    } else {
        dl->AddCircleFilled(c, r, col);
    }
    if (head)
        dl->AddCircle(c, r + thickness * 1.5f, ImGui::GetColorU32(ImGuiCol_Text), 0, thickness);
}

} // namespace ggui::graph
