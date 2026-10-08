// imgui_internal.h wants IMGUI_DEFINE_MATH_OPERATORS defined before imgui.h is first included.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "shell/Widgets.hpp"

#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <span>
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
    if (!hovered && g.ActiveId == 0)
        return -1; // the common case, for every row of every frame: nothing is built
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
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

// Draws the visible part of `label` at `pos`: the byte ranges (ascending, disjoint) dimmed, the rest in Text.
void drawDimRangesText(ImVec2 pos, const char* label, const char* end, std::span<const std::pair<size_t, size_t>> ranges)
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
    bool selected, ImGuiSelectableFlags flags, ImVec2 size)
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
        drawDimRangesText(pos, text.c_str(), text.c_str() + text.size(), std::span(ranges.begin(), ranges.size()));
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
    ImGui::RenderTextEllipsis(window->DrawList, bb.Min, bb.Max, bb.Max.x, begin, end, &size);
    if (cut && tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && beginTooltip()) {
        tooltipText(line);
        ImGui::EndTooltip();
    }
    return cut;
}

} // namespace ggui
