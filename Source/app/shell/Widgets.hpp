// Menu items and buttons that show a Material Symbols icon (`icon`, an ICON_MS_* glyph from
// IconsMaterialSymbols.h) alongside their label, without changing the widget's ImGui ID: tests
// address widgets by label-derived IDs (Source/tests/Harness.hpp), so icons never get prepended
// into the label string itself.
#pragma once

#include <imgui.h>

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
// ImGui::SmallButton counterpart: same ID/icon contract as button(), fits within a text line.
bool smallButton(const char* icon, const char* label);
// ImGui::Selectable that stays blue while hovered when selected (ImGui draws a hovered row with
// HeaderHovered, the neutral hover overlay, which would hide the selection).
bool selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
// Width button()/smallButton() will take for this icon and label, for right-aligning rows.
float buttonWidth(const char* icon, const char* label);

} // namespace ggui
