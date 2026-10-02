// Menu items and buttons that show a Material Symbols icon (`icon`, an ICON_MS_* glyph from
// IconsMaterialSymbols.h) alongside their label, without changing the widget's ImGui ID: tests
// address widgets by label-derived IDs (Source/tests/Harness.hpp), so icons never get prepended
// into the label string itself.
#pragma once

#include <imgui.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace ggui {

// ImGui::MenuItem with an icon drawn in the built-in icon column (ImGui::MenuItemEx). `label`
// keeps driving the ID and the visible text exactly as ImGui::MenuItem would.
bool menuItem(const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false,
    bool enabled = true);
// Checkbox-style overload: toggles *p_selected and shows its checkmark, mirroring ImGui::MenuItem.
bool menuItem(const char* icon, const char* label, const char* shortcut, bool* p_selected, bool enabled = true);
// ImGui::BeginMenu with an icon (ImGui::BeginMenuEx). Pair with ImGui::EndMenu() as usual.
bool beginMenu(const char* icon, const char* label, bool enabled = true);
// ImGui::Button whose ID stays exactly ImGui::GetID(label) (same as plain ImGui::Button(label)),
// but which renders "icon  visible-text" (visible text = label up to "##"). `size` as for Button.
bool button(const char* icon, const char* label, ImVec2 size = ImVec2(0, 0));
// button() drawn in the palette's error red (errorBg, text `error`) for actions that rewrite or destroy
// history; fades like any button inside BeginDisabled.
bool dangerButton(const char* icon, const char* label, ImVec2 size = ImVec2(0, 0));
// Icon-only square-ish button (`id` like "##name", ID = ImGui::GetID(id)) that centres the
// glyph's drawn bounds in the frame, ignoring the icon font's baseline GlyphOffset and advance.
bool iconButton(const char* icon, const char* id, ImVec2 size);
// ImGui::SmallButton counterpart: same ID/icon contract as button(), fits within a text line.
bool smallButton(const char* icon, const char* label);
// ImGui::Selectable that stays highlighted while hovered when selected (ImGui draws a hovered row with
// HeaderHovered, the neutral hover overlay, which would hide the selection).
bool selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
// menuItem()/selectable() whose visible label starts with `dimLen` bytes (a folder prefix) drawn in
// the dimmed text colour; the rest keeps the normal colour. The widget is laid out, identified and
// registered exactly like the plain call with the same `label`.
bool menuItemDimPrefix(const char* icon, const char* label, size_t dimLen, const char* shortcut = nullptr);
// True on the frame Delete is pressed while the last item is hovered (and no text field is being
// edited): a list entry's "remove" key.
bool hoveredDeletePressed();
bool selectableDimPrefix(const char* label, size_t dimLen, bool selected, ImGuiSelectableFlags flags, ImVec2 size);
// selectable() whose visible label has bytes [dimBegin, dimEnd) drawn dimmed (e.g. a "@host" suffix).
bool selectableDimRange(const char* label, size_t dimBegin, size_t dimEnd, bool selected = false, ImGuiSelectableFlags flags = 0,
    ImVec2 size = ImVec2(0, 0));
// Width button()/smallButton() will take for this icon and label, for right-aligning rows.
float buttonWidth(const char* icon, const char* label);
// Makes the item just submitted (a commit-ID/ref input) a drop target for History's commits and branch badges:
// `text` becomes the commit's hex ID or the branch name. True when it was filled.
bool acceptCommitDrop(std::string& text);
// The first line of `text` (up to the first '\n' or '\r', trailing whitespace trimmed): what a commit
// subject, stash or tag message shows wherever it takes one line.
std::string_view firstLine(std::string_view text);
// `text` cut to `width` pixels with an ellipsis (as it is when it already fits).
std::string fitText(const std::string& text, float width);
// firstLine(text) as one line of text in the current text colour that ends in an ellipsis where it does
// not fit `width` (the room left to the window's content edge when 0). A table cell clips without one.
// An item like TextUnformatted (laid out, SameLine and IsItemHovered work), but with no ID and no hover
// of its own, so it never takes hover or navigation from a row it sits on; `id` (label-derived like
// ImGui::GetID, relative to the ID stack) registers it for the test engine only. When the text was
// cut and `tooltip` is set, hovering shows the whole line: pass false where the row has its own tooltip.
// Returns true when the text was cut.
bool textElided(std::string_view text, const char* id = nullptr, bool tooltip = true, float width = 0);

} // namespace ggui
