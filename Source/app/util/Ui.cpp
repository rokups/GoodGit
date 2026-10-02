#include "util/Ui.hpp"

#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"

#include <libgg/GitRunner.hpp>

#include <IconsMaterialSymbols.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ggui {

std::string remoteHost(const std::string& url)
{
    std::string authority;
    const size_t scheme = url.find("://");
    if (scheme != std::string::npos) {
        if (url.compare(0, scheme, "file") == 0)
            return {};
        const size_t start = scheme + 3;
        authority = url.substr(start, std::min(url.find_first_of("/?#", start), url.size()) - start);
        if (const size_t at = authority.rfind('@'); at != std::string::npos)
            authority.erase(0, at + 1);
        // Port: after the last ':' unless inside an IPv6 literal's brackets.
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos && (authority.empty() || authority.back() != ']'))
            authority.erase(colon);
        return authority;
    }
    // scp-like "[user@]host:path": a colon before any slash, and more than a drive letter before it.
    const size_t colon = url.find(':');
    if (colon == std::string::npos || colon < 2 || url.find('/') < colon)
        return {};
    authority = url.substr(0, colon);
    if (const size_t at = authority.rfind('@'); at != std::string::npos)
        authority.erase(0, at + 1);
    return authority;
}

void sameLineIfFits(float nextWidth)
{
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < nextWidth)
        ImGui::NewLine();
}

float checkboxWidth(const char* label)
{
    const float text = ImGui::CalcTextSize(label, nullptr, true).x;
    return ImGui::GetFrameHeight() + (text > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + text : 0.0f);
}

float comboWidth(std::initializer_list<const char*> options)
{
    float widest = 0.0f;
    for (const char* option : options)
        widest = std::max(widest, ImGui::CalcTextSize(option).x);
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}

float labelledWidth(float fieldWidth, const char* label)
{
    const float text = ImGui::CalcTextSize(label, nullptr, true).x;
    return fieldWidth + (text > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x + text : 0.0f);
}

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
        text += " " ICON_MS_ARROW_RIGHT_ALT " " + s.upstream;
        if (s.ahead)
            text += " " ICON_MS_ARROW_UPWARD_ALT + std::to_string(s.ahead);
        if (s.behind)
            text += " " ICON_MS_ARROW_DOWNWARD_ALT + std::to_string(s.behind);
    }
    return text;
}

void disabledMenuItem(const char* icon, const char* label, const char* reason, const char* shortcut)
{
    ImGui::MenuItemEx(label, icon, shortcut, false, false);
    if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        tooltip("%s", reason);
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

namespace {

void textItem(const char* label, const char* end, size_t dimFrom, ImGuiID id)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return;
    if (id != 0) {
        // Hover is tracked (tooltips, context menus, the test engine) but never drawn.
        ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
        [[maybe_unused]] ImGuiContext& g = *ImGui::GetCurrentContext(); // used by the test-engine hook
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, ImGuiItemStatusFlags_None);
    }
    ImDrawList* dl = window->DrawList;
    const char* split = label + std::min(dimFrom, static_cast<size_t>(end - label));
    dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), label, split);
    if (split < end) {
        const float x = ImGui::CalcTextSize(label, split).x;
        dl->AddText(ImVec2(pos.x + x, pos.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), split, end);
    }
}

} // namespace

void plainText(const char* label)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    textItem(label, end, std::string::npos, *end ? ImGui::GetID(label) : 0);
}

void idText(const std::string& hex, size_t shortLen, const char* id)
{
    const std::string label = id ? hex + "###" + id : hex;
    textItem(label.c_str(), label.c_str() + hex.size(), shortLen, id ? ImGui::GetID(label.c_str()) : 0);
}

// Tooltip text wraps at this many font sizes and is cut after this many wrapped lines.
static float tooltipWrapWidth() { return ImGui::GetFontSize() * 40.0f; }
static constexpr int kTooltipMaxLines = 10;

void tooltipText(std::string_view text)
{
    if (text.empty())
        return;
    const float wrap = tooltipWrapWidth();
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    // Walk the lines the way ImGui's wrapped text rendering breaks them (the explicit '\n' ends a line too).
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const char* line = begin;
    const char* cut = nullptr;
    for (int i = 0; i < kTooltipMaxLines && line < end; ++i) {
        const char* eol = font->CalcWordWrapPosition(size, line, end, wrap);
        const char* next = ImTextCalcWordWrapNextLineStart(eol, end);
        if (i == kTooltipMaxLines - 1 && next < end) {
            // The last line shown also holds the ellipsis: wrap it narrower so the ellipsis fits.
            static const char* const ellipsis = "\xE2\x80\xA6";
            eol = font->CalcWordWrapPosition(size, line, end, wrap - ImGui::CalcTextSize(ellipsis).x);
            while (eol > line && ImCharIsBlankA(eol[-1]))
                --eol;
            cut = eol;
        }
        line = next;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap);
    if (cut) {
        const std::string shown = std::string(begin, cut) + "\xE2\x80\xA6";
        ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
    } else {
        ImGui::TextUnformatted(begin, end);
    }
    ImGui::PopTextWrapPos();
}

void tooltip(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string text(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0)
        std::vsnprintf(text.data(), text.size() + 1, fmt, args);
    va_end(args);
    // As SetTooltip does: a tooltip submitted earlier in the frame is replaced, not appended to.
    if (!ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None))
        return;
    tooltipText(text);
    ImGui::EndTooltip();
}

void idTooltip(const std::string& hex, size_t shortLen, const std::string& rest)
{
    if (!ImGui::BeginTooltip())
        return;
    idText(hex, shortLen);
    if (!rest.empty())
        tooltipText(rest);
    ImGui::EndTooltip();
}

void copyId(const std::string& shortId, const std::string& fullId)
{
    ImGui::SetClipboardText((ImGui::GetIO().KeyShift ? fullId : shortId).c_str());
}

bool copyIdMenuItem(const char* prefix, const std::string& shortId, const std::string& fullId, bool enabled)
{
    // The label follows Shift ("Short ID" / "Full ID"); the ID after ### stays the same.
    const bool full = ImGui::GetIO().KeyShift;
    std::string label = std::string(prefix) + (full ? "full ID" : "short ID");
    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    label += std::string("###") + prefix + "ID";
    if (!menuItem(ICON_MS_CONTENT_COPY, label.c_str(), nullptr, false, enabled))
        return false;
    copyId(shortId, fullId);
    return true;
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
        tooltip("%s", text);
}

std::string dateText(std::int64_t unixSeconds) { return core::formatTime(unixSeconds); }

bool hotkey(ImGuiKeyChord chord, ImGuiInputFlags flags, ImGuiID owner)
{
    const ImGuiID id = owner ? owner : ImGui::GetCurrentContext()->CurrentFocusScopeId;
    if (!ImGui::Shortcut(chord, flags, id))
        return false;
    // Only the key is locked: locking a modifier would make io.KeyAlt / KeyMods read as released while
    // it is held. Shortcut() already claimed the mods (so releasing Alt does not toggle the menu layer).
    ImGui::SetKeyOwner((ImGuiKey)(chord & ~ImGuiMod_Mask_), id,
        (chord & ImGuiMod_Alt) ? ImGuiInputFlags_LockUntilRelease : ImGuiInputFlags_LockThisFrame);
    return true;
}

bool contextMenuKeyPressed()
{
    if (!ImGui::IsItemFocused() || ImGui::GetIO().WantTextInput)
        return false;
    return hotkey(ImGuiMod_Alt | ImGuiKey_Space, ImGuiInputFlags_RouteFocused, ImGui::GetItemID());
}

PressSource pressSource()
{
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiID id = ImGui::GetItemID();
    if (g.NavJustMovedToId == id)
        return PressSource::NavMove;
    if (g.NavActivateId == id)
        return PressSource::NavActivate;
    return PressSource::Mouse;
}

ImGuiKeyChord pressMods()
{
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    return g.NavJustMovedToId == ImGui::GetItemID() ? g.NavJustMovedToKeyMods : g.IO.KeyMods;
}

void flattenNextTable()
{
    // BeginTable reads the next-window data for its scroll child; ASSIGN the flags (OR would inherit stale ones).
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.NextWindowData.HasFlags |= ImGuiNextWindowDataFlags_HasChildFlags;
    g.NextWindowData.ChildFlags = ImGuiChildFlags_NavFlattened;
}

namespace {
struct OpenList {
    ImVec2 maxPos; // the window's CursorMaxPos before the child
    float overhang; // how far the child reaches below the content area
};
std::vector<OpenList> g_openLists;
}

void beginListChild(const char* name, float width)
{
    // The child spans the window edge to edge (the scrollbar sits at the edge) and carries the window's own
    // horizontal padding, so its rows sit where they would directly in the window and what they draw past
    // their rect (the current-branch outline, the selection highlight) is not clipped at the child's edge.
    // Vertically it grows by a row's reach past its text (the larger half of the item spacing) and a pixel on
    // both sides: the highlight and the outline of the first and last row.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 pad(window->WindowPadding.x, spacing - std::trunc(spacing * 0.5f) + 1.0f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size((width > 0.0f ? width : avail.x) + 2.0f * pad.x, std::max(avail.y + 2.0f * pad.y, 1.0f));
    g_openLists.push_back({window->DC.CursorMaxPos, pad.y});
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - pad.x, ImGui::GetCursorPosY() - pad.y));
    // No child background of its own: the rows sit on the window, as they did before the list was a child.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild(name, size, ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void endListChild()
{
    ImGui::EndChild();
    // The child overhangs the content area by the padding: that must neither make the window scrollable nor
    // leave the cursor (and so a group around the list) below the content. Its item rect, which an
    // EndGroup() takes in, is the part inside the content area.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImRect& item = ImGui::GetCurrentContext()->LastItemData.Rect;
    item.ClipWith(window->WorkRect);
    const OpenList open = g_openLists.back();
    g_openLists.pop_back();
    window->DC.CursorPos.y -= open.overhang;
    window->DC.CursorMaxPos = ImVec2(std::max(open.maxPos.x, std::min(window->DC.CursorMaxPos.x, window->WorkRect.Max.x)),
        std::max(open.maxPos.y, std::min(window->DC.CursorMaxPos.y, window->WorkRect.Max.y)));
}

void beginList(float width)
{
    const ImGuiID windowId = ImGui::GetCurrentWindow()->ID;
    beginListChild("##list", width);
    ImGui::PushOverrideID(windowId);
}

void endList()
{
    ImGui::PopID();
    endListChild();
}

void openPopupBelowItem(ImGuiID id, ImGuiPopupFlags flags)
{
    ImGui::OpenPopupEx(id, flags);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (!g.OpenPopupStack.empty() && g.OpenPopupStack.back().PopupId == id)
        g.OpenPopupStack.back().OpenPopupPos = ImVec2(g.LastItemData.Rect.Min.x, g.LastItemData.Rect.Max.y);
}

bool beginContextMenu(const char* strId, ImGuiPopupFlags flags)
{
    if (contextMenuKeyPressed())
        openPopupBelowItem(strId ? ImGui::GetID(strId) : ImGui::GetItemID(), flags);
    return ImGui::BeginPopupContextItem(strId, flags);
}

} // namespace ggui
