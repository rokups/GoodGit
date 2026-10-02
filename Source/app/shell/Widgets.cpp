// imgui_internal.h wants IMGUI_DEFINE_MATH_OPERATORS defined before imgui.h is first included.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "shell/Widgets.hpp"

#include "shell/Theme.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <string>

namespace ggui {

bool menuItem(const char* icon, const char* label, const char* shortcut, bool selected, bool enabled)
{
    return ImGui::MenuItemEx(label, icon, shortcut, selected, enabled);
}

bool menuItem(const char* icon, const char* label, const char* shortcut, bool* p_selected, bool enabled)
{
    if (ImGui::MenuItemEx(label, icon, shortcut, p_selected ? *p_selected : false, enabled)) {
        if (p_selected)
            *p_selected = !*p_selected;
        return true;
    }
    return false;
}

bool beginMenu(const char* icon, const char* label, bool enabled)
{
    return ImGui::BeginMenuEx(label, icon, enabled);
}

namespace {

// "icon  visible-label", the text button()/smallButton() render.
std::string visibleText(const char* icon, const char* label)
{
    std::string visible(icon);
    visible += "  ";
    visible.append(label, ImGui::FindRenderedTextEnd(label));
    return visible;
}

// Shared by button()/smallButton(): mirrors ImGui::ButtonEx exactly (ItemSize/ItemAdd/
// ButtonBehavior/RenderNavHighlight/RenderFrame/RenderText + the test-engine item info call),
// except the rendered text is "icon  visible-label" while the ID and the label registered with
// the test engine stay `label` unchanged (ImGui::GetID(label), same as plain ImGui::Button).
bool iconButtonEx(const char* icon, const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const std::string visible = visibleText(icon, label);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
    const ImVec2 label_size = ImGui::CalcTextSize(visible.c_str(), nullptr, true);
    const ImVec2 size
        = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, visible.c_str(), nullptr, &label_size,
        style.ButtonTextAlign, &bb);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

} // namespace

bool button(const char* icon, const char* label, ImVec2 size)
{
    return iconButtonEx(icon, label, size, ImGuiButtonFlags_None);
}

bool dangerButton(const char* icon, const char* label, ImVec2 size)
{
    // errorBg is the palette's solid red and `error` the text colour meant for it; hover lightens, press darkens.
    const Palette& p = theme().palette();
    const auto shade = [](ImU32 c, float toward, float amount) {
        const ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x + (toward - v.x) * amount, v.y + (toward - v.y) * amount,
            v.z + (toward - v.z) * amount, v.w));
    };
    ImGui::PushStyleColor(ImGuiCol_Button, p.errorBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, shade(p.errorBg, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, shade(p.errorBg, 0.0f, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, p.error);
    const bool pressed = button(icon, label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool iconButton(const char* icon, const char* id, ImVec2 size_arg)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID gid = window->GetID(id);
    const float frame = ImGui::GetFrameHeight();
    const ImVec2 size = ImGui::CalcItemSize(size_arg, frame, frame);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, gid))
        return false;

    bool hovered, held;
    const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held, ImGuiButtonFlags_None);
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderNavHighlight(bb, gid);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

    unsigned int c = 0;
    ImTextCharFromUtf8(&c, icon, nullptr);
    const ImVec2 centre = bb.GetCenter();
    if (const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyph(static_cast<ImWchar>(c))) {
        // Vertically the icon stays where text sits (the font's GlyphOffset puts it on the baseline
        // of neighbouring labels); horizontally its ink is centred, not its advance.
        const ImVec2 at(IM_TRUNC(centre.x - (glyph->X0 + glyph->X1) * 0.5f), IM_TRUNC(centre.y - g.FontSize * 0.5f));
        window->DrawList->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), icon);
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(gid, id, g.LastItemData.StatusFlags);
    return pressed;
}

bool smallButton(const char* icon, const char* label)
{
    ImGuiContext& g = *GImGui;
    const float backup_padding_y = g.Style.FramePadding.y;
    g.Style.FramePadding.y = 0.0f;
    const bool pressed = iconButtonEx(icon, label, ImVec2(0, 0), ImGuiButtonFlags_AlignTextBaseLine);
    g.Style.FramePadding.y = backup_padding_y;
    return pressed;
}

float buttonWidth(const char* icon, const char* label)
{
    return ImGui::CalcTextSize(visibleText(icon, label).c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    // The theme's Header colours are neutral (menus, plain rows); selection uses its own stronger neutral highlight.
    if (selected) {
        const Palette& p = theme().palette();
        ImGui::PushStyleColor(ImGuiCol_Header, p.selection);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.selectionHovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.selectionHovered);
    }
    const bool pressed = ImGui::Selectable(label, selected, flags, size);
    if (selected)
        ImGui::PopStyleColor(3);
    return pressed;
}

namespace {

// Draws the visible part of `label` at `pos`: bytes [dimBegin, dimEnd) dimmed, the rest in Text.
void drawDimRangeText(ImVec2 pos, const char* label, size_t dimBegin, size_t dimEnd)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    const size_t total = static_cast<size_t>(end - label);
    dimEnd = std::min(dimEnd, total);
    dimBegin = std::min(dimBegin, dimEnd);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const char* parts[4] = {label, label + dimBegin, label + dimEnd, end};
    for (int i = 0; i < 3; ++i) {
        if (parts[i] == parts[i + 1])
            continue;
        dl->AddText(pos, i == 1 ? dim : text, parts[i], parts[i + 1]);
        pos.x += ImGui::CalcTextSize(parts[i], parts[i + 1]).x;
    }
}

void drawDimPrefixText(ImVec2 pos, const char* label, size_t dimLen)
{
    drawDimRangeText(pos, label, 0, dimLen);
}

// Invisible placeholder text: the plain widget still does the sizing, hit-testing and test-engine
// registration, and the caller draws the coloured text on top.
struct HiddenText {
    HiddenText() { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0)); }
    ~HiddenText() { ImGui::PopStyleColor(); }
};

} // namespace

bool menuItemDimPrefix(const char* icon, const char* label, size_t dimLen, const char* shortcut)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    bool pressed;
    {
        HiddenText hidden;
        pressed = ImGui::MenuItemEx(label, icon, shortcut, false, true);
    }
    if (ImGui::IsItemVisible()) {
        const ImGuiMenuColumns& cols = window->DC.MenuColumns;
        if (icon && icon[0])
            window->DrawList->AddText(ImVec2(pos.x + cols.OffsetIcon, pos.y), ImGui::GetColorU32(ImGuiCol_Text), icon);
        drawDimPrefixText(ImVec2(pos.x + cols.OffsetLabel, pos.y), label, dimLen);
    }
    return pressed;
}

bool hoveredDeletePressed()
{
    return ImGui::IsItemHovered() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
}

bool selectableDimPrefix(const char* label, size_t dimLen, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    // Where Selectable() puts its text: the cursor (plus the baseline once), not the item rect, which reaches
    // half the item spacing past it for the highlight.
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    bool pressed;
    {
        HiddenText hidden;
        pressed = selectable(label, selected, flags, size);
    }
    if (ImGui::IsItemVisible())
        drawDimPrefixText(pos, label, dimLen);
    return pressed;
}

bool selectableDimRange(const char* label, size_t dimBegin, size_t dimEnd, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    // Where Selectable() puts its text: the cursor (plus the baseline once), not the item rect, which reaches
    // half the item spacing past it for the highlight.
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    bool pressed;
    {
        HiddenText hidden;
        pressed = selectable(label, selected, flags, size);
    }
    if (ImGui::IsItemVisible())
        drawDimRangeText(pos, label, dimBegin, dimEnd);
    return pressed;
}

bool acceptCommitDrop(std::string& text)
{
    if (!ImGui::BeginDragDropTarget())
        return false;
    bool filled = false;
    for (const char* type : {"GG_COMMIT", "GG_BRANCH"})
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(type)) {
            text.assign(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
            filled = true;
        }
    ImGui::EndDragDropTarget();
    return filled;
}

} // namespace ggui
