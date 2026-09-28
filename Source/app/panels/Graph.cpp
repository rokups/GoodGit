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
    return std::max(0.0f, rowHeight * 0.32f + thickness * 2.0f - laneWidth * 0.5f);
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
    if (row.parents.size() > 1) {
        // Merge bubble: click toggles its merged history (+ collapsed, − expanded).
        const float r = rowHeight * 0.32f;
        const bool hovered = mergeToggle && ImGui::IsMouseHoveringRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        dl->AddCircleFilled(c, r, hovered ? withAlpha(col, 90) : ImGui::GetColorU32(ImGuiCol_WindowBg));
        dl->AddCircle(c, r, col, 0, thickness);
        if (mergeToggle) {
            const float a = r * 0.55f;
            dl->AddLine(ImVec2(c.x - a, c.y), ImVec2(c.x + a, c.y), col, thickness);
            if (row.collapsed)
                dl->AddLine(ImVec2(c.x, c.y - a), ImVec2(c.x, c.y + a), col, thickness);
        }
    } else {
        const float r = rowHeight * 0.22f;
        dl->AddCircleFilled(c, r, col);
    }
    if (head)
        dl->AddCircle(c, rowHeight * (row.parents.size() > 1 ? 0.32f : 0.22f) + thickness * 1.5f,
            ImGui::GetColorU32(ImGuiCol_Text), 0, thickness);
}

} // namespace ggui::graph
