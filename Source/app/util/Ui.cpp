#include "util/Ui.hpp"

#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"

#include <libgg/GitRunner.hpp>

#include <IconsMaterialSymbols.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <thread>

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

void idTooltip(const std::string& hex, size_t shortLen, const std::string& rest)
{
    if (!ImGui::BeginTooltip())
        return;
    idText(hex, shortLen);
    if (!rest.empty())
        ImGui::TextUnformatted(rest.c_str());
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
        ImGui::SetTooltip("%s", text);
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
