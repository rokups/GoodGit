#include "util/Ui.hpp"

#include "shell/Theme.hpp"

#include <libgg/GitRunner.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace ggui {

bool containsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}

std::string summaryText(const core::RepoSummary& s)
{
    if (!s.exists)
        return "missing";
    std::string text = s.detached ? std::string("detached") : s.branch;
    if (!s.upstream.empty()) {
        text += " \xe2\x86\x92 " + s.upstream;
        if (s.ahead)
            text += " \xe2\x86\x91" + std::to_string(s.ahead);
        if (s.behind)
            text += " \xe2\x86\x93" + std::to_string(s.behind);
    }
    return text;
}

void disabledMenuItem(const char* label, const char* reason, const char* shortcut)
{
    ImGui::MenuItem(label, shortcut, false, false);
    if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", reason);
}

void drawBadge(const char* label, ImU32 color, bool outlined)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 textSize = ImGui::CalcTextSize(label, end);
    // The item is exactly one text line tall so the badge keeps the line's baseline and height
    // (history rows have a fixed pitch); the pill is drawn slightly outside it, into the padding.
    const float padX = ImGui::GetFontSize() * 0.3f;
    const float padY = 1.0f;
    const ImVec2 size(textSize.x + padX * 2, textSize.y);
    // Sits on the line's text baseline (next to framed widgets the text is lower by the frame
    // padding: CurrLineTextBaseOffset).
    const float baseline = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 pos(cursor.x, cursor.y + baseline);
    // A registered item (tests and tooltips find it) that does not take hover or clicks: those
    // belong to what the badge sits on (a history row, the toolbar).
    ImGui::ItemSize(ImVec2(size.x, size.y + baseline), baseline);
    const ImGuiID id = ImGui::GetID(label);
    ImGui::ItemAdd(ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)), id);
    [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext(); // used by the test-engine hook
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, ImGuiItemStatusFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetFontSize() * 0.25f;
    const ImVec2 a(pos.x, pos.y - padY), b(pos.x + size.x, pos.y + size.y + padY);
    dl->AddRectFilled(a, b, color, rounding);
    if (outlined)
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 220), rounding, 0, 1.5f);
    dl->AddText(ImVec2(pos.x + padX, pos.y), theme().palette().badgeText, label, end);
}

void spinner(const char* id, float radius)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(radius * 2, ImGui::GetFrameHeight());
    ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(pos.x + radius, pos.y + size.y * 0.5f);
    const float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
    const int segments = 24;
    dl->PathClear();
    for (int i = 0; i <= segments; ++i) {
        const float a = t + static_cast<float>(i) / segments * 4.2f;
        dl->PathLineTo(ImVec2(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius));
    }
    dl->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), 0, radius * 0.35f);
}

void openInFileManager(const std::filesystem::path& path)
{
    const std::string p = path.string();
    std::thread([p] {
#ifdef _WIN32
        gg::RunRequest r;
        r.args = {"explorer", p};
        r.gitEnvironment = false;
        gg::run(r);
#else
        gg::RunRequest r;
        r.args = {"xdg-open", p};
        r.gitEnvironment = false;
        r.cLocale = false;
        gg::run(r);
#endif
    }).detach();
}

void helpMarker(const char* text)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", text);
}

std::string dateText(std::int64_t unixSeconds) { return core::formatTime(unixSeconds); }

} // namespace ggui
