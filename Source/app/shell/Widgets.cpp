// imgui_internal.h wants IMGUI_DEFINE_MATH_OPERATORS defined before imgui.h is first included.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "shell/Widgets.hpp"

#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <span>
#include <utility>
#include <string>
#include <vector>

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

namespace {

// `fill` as a button: hover lightens, press darkens. `text` is the text colour meant for the fill.
void pushFillColors(ImU32 fill, ImU32 text)
{
    const auto shade = [](ImU32 c, float toward, float amount) {
        const ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x + (toward - v.x) * amount, v.y + (toward - v.y) * amount,
            v.z + (toward - v.z) * amount, v.w));
    };
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, shade(fill, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, shade(fill, 0.0f, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, text);
}

} // namespace

void pushDangerColors()
{
    // errorBg is the palette's solid red and `error` the text colour meant for it.
    const Palette& p = theme().palette();
    pushFillColors(p.errorBg, p.error);
}

void popDangerColors()
{
    ImGui::PopStyleColor(4);
}

void pushPrimaryColors()
{
    const Palette& p = theme().palette();
    pushFillColors(p.primary, p.primaryText);
}

void popPrimaryColors()
{
    ImGui::PopStyleColor(4);
}

bool dangerButton(const char* icon, const char* label, ImVec2 size)
{
    pushDangerColors();
    const bool pressed = button(icon, label, size);
    popDangerColors();
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

bool buttonCorners(const char* label, ImDrawFlags corners)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size = ImGui::CalcItemSize(ImVec2(0, 0), label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderNavHighlight(bb, id);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, col, style.FrameRounding, corners);
    if (style.FrameBorderSize > 0.0f)
        window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, corners, style.FrameBorderSize);
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, nullptr, &label_size, style.ButtonTextAlign, &bb);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
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
    // In a tree, the highlight reaches left of the label by 2 pixels (not half the item spacing): it stays off the trunk.
    const bool inTree = treeActive();
    if (inTree)
        ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, treeHighlightPad() * 2.0f);
    const bool pressed = ImGui::Selectable(label, selected, flags, size);
    if (inTree)
        ImGui::PopStyleVar();
    if (selected)
        ImGui::PopStyleColor(3);
    return pressed;
}

float rowActionsReserve(std::span<const RowAction> actions)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || actions.empty())
        return 0.0f;
    // The row about to be built starts at the cursor: its band is the one rowActions finds from the built row
    // (a row's item includes half the item spacing on each side), and the hover test is rowActions' too.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacingUp = std::trunc(style.ItemSpacing.y * 0.5f);
    const float top = ImGui::GetCursorScreenPos().y - spacingUp;
    const float bottom = top + ImGui::GetTextLineHeight() + style.ItemSpacing.y;
    const bool hovered = g.HoveredWindow == window && (g.ActiveId == 0 || g.ActiveIdAllowOverlap)
        && ImGui::IsMouseHoveringRect(ImVec2(window->InnerRect.Min.x, top), ImVec2(window->InnerRect.Max.x, bottom))
        && ImGui::IsWindowContentHoverable(window, ImGuiHoveredFlags_None);
    if (!hovered)
        return 0.0f;
    // The same widths as rowActions' buttons: the glyph and a gap each.
    const float gap = std::trunc(style.ItemSpacing.x * 0.5f);
    float total = 0.0f;
    for (const RowAction& a : actions) {
        if (a.visible)
            total += ImGui::CalcTextSize((std::string(a.icon) + "###" + a.id).c_str(), nullptr, true).x + gap;
    }
    return total;
}

int rowActions(std::span<const RowAction> actions, bool rowSelected, bool paddedRow)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || actions.empty())
        return -1;
    const ImGuiLastItemData row = g.LastItemData;
    const ImGuiStyle& style = ImGui::GetStyle();
    // The row's band across the window: the mouse anywhere on it counts as on the row (a text row's item is
    // only as wide as its text, and the buttons sit past that). A selectable's rect already includes half the
    // item spacing on each side; a text row's does not.
    const float spacingUp = paddedRow ? 0.0f : std::trunc(style.ItemSpacing.y * 0.5f);
    const float spacingDown = paddedRow ? 0.0f : style.ItemSpacing.y - spacingUp;
    const float bandTop = row.Rect.Min.y - spacingUp, bandBottom = row.Rect.Max.y + spacingDown;
    // The buttons are there while the row is hovered; one being held keeps them (the row stops being hovered
    // while another item is active, and the click would never complete).
    const bool hovered = g.HoveredWindow == window && (g.ActiveId == 0 || g.ActiveIdAllowOverlap)
        && ImGui::IsMouseHoveringRect(ImVec2(window->InnerRect.Min.x, bandTop), ImVec2(window->InnerRect.Max.x, bandBottom))
        && ImGui::IsWindowContentHoverable(window, ImGuiHoveredFlags_None);
    if (!hovered) {
        // The common case, for every row of every frame: nothing is built. Only one of the row's own
        // buttons being held (a button id is the hash of "###id") continues.
        bool own = false;
        for (size_t i = 0; g.ActiveId != 0 && !own && i < actions.size(); ++i) {
            char key[96];
            std::snprintf(key, sizeof key, "###%s", actions[i].id);
            own = actions[i].visible && g.ActiveId == window->GetID(key);
        }
        if (!own)
            return -1;
    }
    ++g_rowActionBuilds;
    std::vector<std::string> labels;
    std::vector<size_t> shown;
    bool held = false;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (!actions[i].visible)
            continue;
        labels.push_back(std::string(actions[i].icon) + "###" + actions[i].id);
        shown.push_back(i);
        held = held || g.ActiveId == ImGui::GetID(labels.back().c_str());
    }
    if (shown.empty() || !(held || hovered))
        return -1;

    // Each button is as tall as the band and a gap wider than its glyph, side by side: no part of the strip
    // is left to the row beneath, so a click that is a little off still lands on a button.
    const float gap = std::trunc(style.ItemSpacing.x * 0.5f);
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& label : labels) {
        widths.push_back(ImGui::CalcTextSize(label.c_str(), nullptr, true).x + gap);
        total += widths.back();
    }
    const float right = window->InnerRect.Max.x;
    const float left = right - total;
    // Opaque strip behind the buttons (the label may run under them), in the row's hover colour.
    const ImVec2 stripMin(left, bandTop);
    const ImVec2 stripMax(right, bandBottom);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(stripMin, stripMax, ImGui::GetColorU32(ImGuiCol_WindowBg, 1.0f));
    if (paddedRow)
        dl->AddRectFilled(stripMin, stripMax, rowSelected ? theme().palette().selectionHovered : ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    // Placing the buttons leaves the layout as it was.
    const ImVec2 cursor = window->DC.CursorPos;
    const bool wasSetPos = window->DC.IsSetPos;
    const ImVec2 cursorPrevLine = window->DC.CursorPosPrevLine, maxPos = window->DC.CursorMaxPos, idealMax = window->DC.IdealMaxPos;
    const ImVec2 prevSize = window->DC.PrevLineSize, currSize = window->DC.CurrLineSize;
    const float prevBase = window->DC.PrevLineTextBaseOffset, currBase = window->DC.CurrLineTextBaseOffset;
    int clicked = -1;
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::PushItemFlag(ImGuiItemFlags_NoFocus, true); // a click does not move the keyboard focus onto a button
    float x = left;
    for (size_t k = 0; k < shown.size(); ++k) {
        const RowAction& a = actions[shown[k]];
        ImGui::SetCursorScreenPos(ImVec2(x, bandTop));
        window->DC.CurrLineTextBaseOffset = 0.0f;
        ImGui::BeginDisabled(!a.enabled);
        // The glyph is centred in the band, where the row's label is.
        if (ImGui::Button(labels[k].c_str(), ImVec2(widths[k], bandBottom - bandTop)))
            clicked = static_cast<int>(shown[k]);
        ImGui::EndDisabled();
        if (itemHoveredForTooltip(ImGuiHoveredFlags_AllowWhenDisabled))
            tooltip("%s", a.tip);
        x += widths[k];
    }
    ImGui::PopItemFlag();
    ImGui::PopItemFlag();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    window->DC.CursorPosPrevLine = cursorPrevLine;
    window->DC.CursorMaxPos = maxPos;
    window->DC.IdealMaxPos = idealMax;
    window->DC.PrevLineSize = prevSize;
    window->DC.CurrLineSize = currSize;
    window->DC.PrevLineTextBaseOffset = prevBase;
    window->DC.CurrLineTextBaseOffset = currBase;
    window->DC.CursorPos = cursor;
    window->DC.IsSetPos = wasSetPos;
    g.LastItemData = row; // menus and tooltips attach to the row
    return clicked;
}

namespace {

// Draws the visible part of `label` at `pos`: the first `accentLen` bytes in `accent`, the byte ranges (ascending,
// disjoint, after the accent) dimmed, the rest in Text.
void drawDimRangesText(ImVec2 pos, const char* label, const char* end, std::span<const std::pair<size_t, size_t>> ranges,
    size_t accentLen = 0, ImU32 accent = 0)
{
    const size_t total = static_cast<size_t>(end - label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    size_t at = 0;
    auto draw = [&](size_t to, ImU32 color) {
        to = std::min(to, total);
        if (to <= at)
            return;
        dl->AddText(pos, color, label + at, label + to);
        pos.x += ImGui::CalcTextSize(label + at, label + to).x;
        at = to;
    };
    draw(accentLen, accent);
    for (const auto& [begin, finish] : ranges) {
        draw(begin, text);
        draw(finish, dim);
    }
    draw(total, text);
}

void drawDimRangesText(ImVec2 pos, const char* label, std::span<const std::pair<size_t, size_t>> ranges)
{
    drawDimRangesText(pos, label, ImGui::FindRenderedTextEnd(label), ranges);
}

void drawDimRangeText(ImVec2 pos, const char* label, size_t dimBegin, size_t dimEnd)
{
    const std::pair<size_t, size_t> range(dimBegin, dimEnd);
    drawDimRangesText(pos, label, std::span(&range, 1));
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

void drawDimRange(ImVec2 pos, const char* label, size_t dimBegin, size_t dimEnd)
{
    drawDimRangeText(pos, label, dimBegin, dimEnd);
}

void drawDimRange(ImVec2 pos, const char* label, const char* end, size_t dimBegin, size_t dimEnd)
{
    const std::pair<size_t, size_t> range(dimBegin, dimEnd);
    drawDimRangesText(pos, label, end, std::span(&range, 1));
}

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

bool selectableDimRanges(const char* label, std::initializer_list<std::pair<size_t, size_t>> ranges, bool selected,
    ImGuiSelectableFlags flags, ImVec2 size)
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
        drawDimRangesText(pos, label, std::span(ranges.begin(), ranges.size()));
    return pressed;
}

bool selectableDimRange(const char* label, size_t dimBegin, size_t dimEnd, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    return selectableDimRanges(label, {{dimBegin, dimEnd}}, selected, flags, size);
}

bool selectableTextDimRanges(const std::string& text, const std::string& id, std::initializer_list<std::pair<size_t, size_t>> ranges,
    bool selected, ImGuiSelectableFlags flags, ImVec2 size, size_t accentLen, ImU32 accent)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    // Where Selectable() puts its text (see above).
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    // The item gets an ID-only label, so ImGui cannot cut `text` at a "##". It takes the layout width of the whole
    // text (the cursor moves past it, as for a labelled selectable) and, as a labelled one does, spans the
    // available width for its highlight and clicks.
    if (size.x == 0.0f) {
        size.x = ImGui::CalcTextSize(text.c_str(), text.c_str() + text.size()).x;
        flags |= ImGuiSelectableFlags_SpanAvailWidth;
    }
    bool pressed;
    {
        HiddenText hidden;
        pressed = selectable(("###" + id).c_str(), selected, flags, size);
    }
    // The test engine shows the item under its text (as for a labelled selectable), not the empty label.
    [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext();
    IMGUI_TEST_ENGINE_ITEM_INFO(g.LastItemData.ID, (text + "###" + id).c_str(), g.LastItemData.StatusFlags);
    if (ImGui::IsItemVisible())
        drawDimRangesText(pos, text.c_str(), text.c_str() + text.size(), std::span(ranges.begin(), ranges.size()), accentLen, accent);
    return pressed;
}

bool selectableTextDimRange(const std::string& text, const std::string& id, size_t dimBegin, size_t dimEnd, bool selected,
    ImGuiSelectableFlags flags, ImVec2 size)
{
    return selectableTextDimRanges(text, id, {{dimBegin, dimEnd}}, selected, flags, size);
}

bool selectableText(const std::string& text, const std::string& id, bool selected, ImGuiSelectableFlags flags, ImVec2 size)
{
    return selectableTextDimRanges(text, id, {}, selected, flags, size);
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

std::string_view firstLine(std::string_view text)
{
    text = text.substr(0, text.find_first_of("\r\n"));
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool beginOpaqueDragSource(ImGuiDragDropFlags flags)
{
    // BeginTooltipEx multiplies the alpha of ImGuiCol_PopupBg by 0.6 and the window background saturates it.
    // The colour is only needed while BeginDragDropSource opens the tooltip window.
    ImVec4 popup = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    popup.w = 2.0f;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popup);
    const bool active = ImGui::BeginDragDropSource(flags);
    ImGui::PopStyleColor();
    return active;
}

std::string fitText(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    const float room = width - ImGui::CalcTextSize("\xE2\x80\xA6").x;
    size_t cut = 0;
    while (cut < text.size()) {
        size_t next = cut + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
            ++next;
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + next).x > room)
            break;
        cut = next;
    }
    return text.substr(0, cut) + "\xE2\x80\xA6";
}

std::string elideStart(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    const float room = width - ImGui::CalcTextSize("\xE2\x80\xA6").x;
    size_t cut = text.size();
    while (cut > 0) {
        size_t prev = cut - 1;
        while (prev > 0 && (static_cast<unsigned char>(text[prev]) & 0xC0) == 0x80)
            --prev;
        if (ImGui::CalcTextSize(text.c_str() + prev, text.c_str() + text.size()).x > room)
            break;
        cut = prev;
    }
    return "\xE2\x80\xA6" + text.substr(cut);
}

std::string elideMiddle(std::string_view text, int prefix, int suffix)
{
    const size_t head = static_cast<size_t>(std::max(prefix, 1));
    const size_t tail = static_cast<size_t>(std::max(suffix, 1));
    // Byte offsets of the codepoint starts, then the end.
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            starts.push_back(i);
    if (starts.size() <= head + tail + 1)
        return std::string(text);
    const size_t tailStart = starts[starts.size() - tail];
    return std::string(text.substr(0, starts[head])) + "\xE2\x80\xA6" + std::string(text.substr(tailStart));
}

std::string elideMiddleToWidth(const std::string& text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    // Keep n codepoints in all (the head one more than the tail), fewer until the cut text fits.
    size_t count = 0;
    for (const char c : text)
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    for (size_t keep = count; keep > 2; --keep) {
        const std::string cut = elideMiddle(text, static_cast<int>((keep + 1) / 2), static_cast<int>(keep / 2));
        if (ImGui::CalcTextSize(cut.c_str()).x <= width)
            return cut;
    }
    return elideMiddle(text, 1, 1);
}

bool textElided(std::string_view text, const char* id, bool tooltip, float width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const std::string_view line = firstLine(text);
    const char* begin = line.empty() ? "" : line.data();
    const char* end = begin + line.size();
    const ImVec2 size = ImGui::CalcTextSize(begin, end);
    const float room = std::max(width > 0 ? width : ImGui::GetContentRegionAvail().x, 1.0f);
    const bool cut = size.x > room;
    const ImVec2 itemSize(std::min(size.x, room), size.y);
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImRect bb(pos, ImVec2(pos.x + itemSize.x, pos.y + itemSize.y));
    ImGui::ItemSize(itemSize, 0.0f);
    const ImGuiID itemId = id ? ImGui::GetID(id) : 0;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true); // (a registered ID would otherwise be a nav target)
    const bool visible = ImGui::ItemAdd(bb, itemId);
    ImGui::PopItemFlag();
    if (!visible)
        return cut;
    if (itemId) {
        [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext(); // used by the test-engine hook
        IMGUI_TEST_ENGINE_ITEM_INFO(itemId, id, ImGuiItemStatusFlags_None);
    }
    // The box width (Max.x - Min.x) can come out a little less than the text width: a float sum with the position
    // loses low bits. ImGui then takes text that fits as cut and draws an ellipsis. Text that fits is not cut.
    const ImVec2 known = cut ? size : ImVec2(std::min(size.x, bb.Max.x - bb.Min.x), size.y);
    ImGui::RenderTextEllipsis(window->DrawList, bb.Min, bb.Max, bb.Max.x, begin, end, &known);
    if (cut && tooltip && itemHoveredForTooltip() && beginTooltip()) {
        tooltipText(line);
        ImGui::EndTooltip();
    }
    return cut;
}

namespace {

constexpr int kTreeMaxDepth = 32;

// The state of one depth: the line of the children of a row. `startY` is under the marker and `lastY` is the
// y of the last child branch; the trunk joins them.
struct TreeLevel {
    float trunkX = 0;
    float startY = 0;
    float lastY = 0;
    float midY = 0;
    ImGuiID markerId = 0;
    bool hasMarker = false;
    bool hasChild = false;
    // The last child branch waits here. It is a straight branch if a sibling follows, a rounded corner if not.
    float pendingEndX = 0;
    bool hasPending = false;
};

struct TreeState {
    TreeLevel level[kTreeMaxDepth];
    int depth = 0;
    int nextOpen = -1;
    bool toggled = false;
};

TreeState& treeState()
{
    static TreeState state;
    return state;
}

// Return the level slot of a depth, or null when the depth is outside the table. Deeper nodes draw no lines.
TreeLevel* levelAt(int depth)
{
    return (depth >= 0 && depth < kTreeMaxDepth) ? &treeState().level[depth] : nullptr;
}

// The lines come from the text colour, so they show on a dark and on a light theme.
ImU32 treeLineColor()
{
    ImVec4 c = ImGui::GetStyle().Colors[ImGuiCol_Text];
    c.w *= 0.4f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// One pixel at 100 percent scale, more at a larger font.
float treeLineThickness() { return std::max(1.0f, std::floor(ImGui::GetFontSize() / 14.0f + 0.5f)); }

// The centre of a line of thickness t on pixel centres: a sharp line.
float treeSnap(float v)
{
    const float t = treeLineThickness();
    return (static_cast<int>(t) % 2 == 1) ? std::floor(v) + 0.5f : std::floor(v + 0.5f);
}

float treeMarkerRadius() { return ImGui::GetFontSize() * 0.19f; }

// Draw a straight branch from the trunk of the level to `endX` at `midY`.
void treeStraightBranch(const TreeLevel& level, float midY, float endX)
{
    if (endX > level.trunkX && ImGui::IsRectVisible(ImVec2(level.trunkX, midY - 1.0f), ImVec2(endX, midY + 1.0f)))
        ImGui::GetWindowDrawList()->AddLine(ImVec2(treeSnap(level.trunkX), treeSnap(midY)), ImVec2(std::floor(endX), treeSnap(midY)),
            treeLineColor(), treeLineThickness());
}

// Record the branch of a child. The y is recorded also for a row that is not in view, so the trunk is
// complete. The branch of the previous sibling is not the last one, so it is drawn now as a straight branch.
void treeBranch(float midY, float endX)
{
    TreeState& s = treeState();
    if (s.depth <= 0)
        return;
    TreeLevel* parentPtr = levelAt(s.depth - 1);
    if (!parentPtr)
        return;
    TreeLevel& parent = *parentPtr;
    if (parent.hasPending)
        treeStraightBranch(parent, parent.lastY, parent.pendingEndX);
    parent.hasChild = true;
    parent.lastY = midY;
    parent.pendingEndX = endX;
    parent.hasPending = true;
}

// Draw the trunk and the last branch. With a marker, the last branch has a rounded corner.
void treeTrunk(const TreeLevel& level)
{
    if (!level.hasPending)
        return;
    const float by = treeSnap(level.lastY);
    const float tx = treeSnap(level.trunkX);
    const float ex = std::floor(level.pendingEndX);
    if (!level.hasMarker || level.lastY <= level.startY) {
        treeStraightBranch(level, level.lastY, level.pendingEndX);
        return;
    }
    if (!ImGui::IsRectVisible(ImVec2(tx - 1.0f, level.startY), ImVec2(std::max(ex, tx) + 1.0f, by + 1.0f)))
        return;
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float startY = std::floor(level.startY);
    const float r = std::min({ImGui::GetFontSize() * 0.3f, ex - tx, by - startY});
    if (r < 1.0f) {
        // No room for an arc: a plain corner.
        list->PathLineTo(ImVec2(tx, startY));
        list->PathLineTo(ImVec2(tx, by));
        if (ex > tx)
            list->PathLineTo(ImVec2(ex, by));
    } else {
        list->PathLineTo(ImVec2(tx, startY));
        // The centre is right of the trunk and above the branch. The arc goes from the left point
        // (angle pi) to the bottom point (angle pi/2, y down).
        list->PathArcTo(ImVec2(tx + r, by - r), r, IM_PI, IM_PI * 0.5f);
        list->PathLineTo(ImVec2(ex, by));
    }
    list->PathStroke(treeLineColor(), ImDrawFlags_None, treeLineThickness());
}

void treeMarker(float x, float y, bool open)
{
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    if (open)
        list->AddCircle(ImVec2(x, y), treeMarkerRadius(), color, 0, 1.5f);
    else
        list->AddCircleFilled(ImVec2(x, y), treeMarkerRadius(), color);
}

} // namespace

// The indent step of a depth. The marker (radius 0.19, centred at half the step) and its trunk fit in it.
float treeLabelOffset() { return ImGui::GetFontSize() * 0.95f; }

// The room that a highlight keeps left of a label: 2 pixels at 14 pixel font size.
float treeHighlightPad() { return std::max(1.0f, std::floor(ImGui::GetFontSize() / 7.0f + 0.5f)); }

bool treeActive() { return treeState().depth > 0; }

void treeSetNextOpen(bool open) { treeState().nextOpen = open ? 1 : 0; }

bool treeNodeToggled() { return treeState().toggled; }

// Show the hand cursor when the mouse is over the marker rectangle of the current window.
static void treeMarkerHandCursor(const ImRect& markerRect)
{
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(markerRect.Min, markerRect.Max))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

bool treeNode(const char* label, bool has_children, TreeFlags flags)
{
    TreeState& s = treeState();
    const int forced = std::exchange(s.nextOpen, -1);
    s.toggled = false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const float step = treeLabelOffset();
    const float radius = treeMarkerRadius();
    const ImVec2 pos = window->DC.CursorPos;
    const float height = ImGui::GetTextLineHeight();
    const ImRect bb(pos, ImVec2(std::max(window->WorkRect.Max.x, pos.x + step + 1.0f), pos.y + height));
    const float midY = pos.y + height * 0.5f;
    const float markerX = std::floor(pos.x + step * 0.5f) + 0.5f;
    const ImGuiID id = window->GetID(label);

    ImGui::ItemSize(bb.GetSize(), 0.0f);
    if (flags & TreeAllowOverlap)
        ImGui::SetNextItemAllowOverlap();
    const bool added = ImGui::ItemAdd(bb, id);

    ImGuiStorage* storage = ImGui::GetStateStorage();
    bool open = storage->GetInt(id, (flags & TreeDefaultOpen) ? 1 : 0) != 0;
    if (forced >= 0) {
        open = forced != 0;
        storage->SetInt(id, forced);
    }
    // The test engine finds the node by its label and opens or closes it by these status flags.
    [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext();
#ifdef IMGUI_ENABLE_TEST_ENGINE // imgui declares Openable and Opened only with the test engine
    if (has_children) {
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Openable;
        if (open)
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Opened;
    }
#endif
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    bool hovered = false, held = false;
    if (added) {
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held,
            ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_PressedOnDragDropHold
                | ((flags & TreeAllowOverlap) ? ImGuiButtonFlags_AllowOverlap : 0));
        bool toggle = false;
        if (has_children) {
            if (pressed) {
                const bool byMouse = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                toggle = !(flags & TreeToggleOnMarker) || !byMouse || ImGui::GetIO().MousePos.x < pos.x + step;
            }
            if (ImGui::IsItemFocused() && ImGui::GetIO().KeyMods == 0) {
                if ((ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !open) || (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && open))
                    toggle = true;
            }
        }
        if (toggle) {
            open = !open;
            storage->SetInt(id, open ? 1 : 0);
            s.toggled = true;
        }
        // The highlight starts at the label, so it does not cover the marker, the branch or the trunk.
        if ((flags & TreeSelected) || hovered || held)
            window->DrawList->AddRectFilled(ImVec2(pos.x + step - treeHighlightPad(), bb.Min.y), bb.Max,
                ImGui::GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
        ImGui::RenderNavCursor(bb, id);
        if (has_children) {
            treeMarker(markerX, midY, open);
            treeMarkerHandCursor(ImRect(pos, ImVec2(pos.x + step, pos.y + height)));
        }
        ImGui::RenderText(ImVec2(pos.x + step, pos.y), label, ImGui::FindRenderedTextEnd(label));
    }
    treeBranch(midY, has_children ? markerX - radius - 1.0f : pos.x + step - ImGui::GetFontSize() * 0.25f);

    if (!has_children || !open)
        return false;
    if (TreeLevel* level = levelAt(s.depth))
        *level = TreeLevel{markerX, midY + radius, 0.0f, midY, id, true, false};
    ++s.depth;
    ImGui::Indent(step);
    ImGui::PushOverrideID(id);
    return true;
}

void treeNodeEnd()
{
    TreeState& s = treeState();
    assert(s.depth > 0 && "treeNodeEnd without an open treeNode");
    if (s.depth <= 0)
        return;
    --s.depth;
    if (const TreeLevel* level = levelAt(s.depth))
        treeTrunk(*level);
    ImGui::PopID();
    ImGui::Unindent(treeLabelOffset());
}

TreeLeaf treeLeafBegin()
{
    TreeState& s = treeState();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float step = treeLabelOffset();
    if (TreeLevel* level = levelAt(s.depth))
        *level = TreeLevel{std::floor(at.x + step * 0.5f) + 0.5f, 0.0f, 0.0f, 0.0f, 0, false, false};
    ++s.depth;
    ImGui::Indent(step);
    return {at.x, at.y};
}

void treeLeafEnd(TreeLeaf leaf)
{
    TreeState& s = treeState();
    assert(s.depth > 0 && "treeLeafEnd without treeLeafBegin");
    if (s.depth <= 0)
        return;
    --s.depth;
    const TreeLevel* levelPtr = levelAt(s.depth);
    const float step = treeLabelOffset();
    if (!levelPtr) {
        ImGui::Unindent(step);
        return;
    }
    const TreeLevel level = *levelPtr;
    const float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    if (level.hasMarker)
        treeBranch(level.midY, level.trunkX - treeMarkerRadius() - 1.0f);
    else if (bottom > leaf.y)
        treeBranch((leaf.y + bottom) * 0.5f, leaf.x + step - ImGui::GetFontSize() * 0.25f);
    treeTrunk(level);
    ImGui::Unindent(step);
}

bool treeLeafMarker()
{
    TreeState& s = treeState();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (s.depth <= 0 || window->SkipItems)
        return false;
    TreeLevel* levelPtr = levelAt(s.depth - 1);
    if (!levelPtr)
        return false;
    TreeLevel& level = *levelPtr;
    const ImVec2 pos = window->DC.CursorPos;
    const float step = treeLabelOffset();
    const float height = ImGui::GetTextLineHeight();
    const float midY = pos.y + height * 0.5f;
    const ImRect bb(ImVec2(pos.x - step, pos.y), ImVec2(pos.x - std::ceil(ImGui::GetStyle().ItemSpacing.x * 0.5f), pos.y + height));
    const ImGuiID id = window->GetID("###expand");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    bool open = storage->GetInt(id, 0) != 0;
    const bool markerAdded = ImGui::ItemAdd(bb, id);
    {
        [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext();
#ifdef IMGUI_ENABLE_TEST_ENGINE
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Openable | (open ? ImGuiItemStatusFlags_Opened : 0);
#endif
        IMGUI_TEST_ENGINE_ITEM_INFO(id, "###expand", g.LastItemData.StatusFlags);
    }
    if (markerAdded) {
        bool hovered, held;
        if (ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick)) {
            open = !open;
            storage->SetInt(id, open ? 1 : 0);
        }
        treeMarker(level.trunkX, midY, open);
        treeMarkerHandCursor(bb);
    }
    level.hasMarker = true;
    level.markerId = id;
    level.midY = midY;
    level.startY = midY + treeMarkerRadius();
    return open;
}

void treeLeafSetOpen(bool open)
{
    TreeState& s = treeState();
    if (const TreeLevel* level = s.depth > 0 ? levelAt(s.depth - 1) : nullptr)
        ImGui::GetStateStorage()->SetInt(level->markerId, open ? 1 : 0);
}

} // namespace ggui
