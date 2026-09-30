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
    if (selected)
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme().palette().selectionHovered);
    const bool pressed = ImGui::Selectable(label, selected, flags, size);
    if (selected)
        ImGui::PopStyleColor();
    return pressed;
}

namespace {

// Draws the visible part of `label` at `pos`: the first dimLen bytes dimmed, the rest in Text.
void drawDimPrefixText(ImVec2 pos, const char* label, size_t dimLen)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    const size_t total = static_cast<size_t>(end - label);
    dimLen = std::min(dimLen, total);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (dimLen > 0) {
        dl->AddText(pos, dim, label, label + dimLen);
        pos.x += ImGui::CalcTextSize(label, label + dimLen).x;
    }
    dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), label + dimLen, end);
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

bool selectableDimPrefix(const char* label, size_t dimLen, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float baseline = window->DC.CurrLineTextBaseOffset;
    bool pressed;
    {
        HiddenText hidden;
        pressed = selectable(label, selected, flags, size);
    }
    if (ImGui::IsItemVisible()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        drawDimPrefixText(ImVec2(min.x, min.y + baseline), label, dimLen);
    }
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
