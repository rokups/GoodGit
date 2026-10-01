// Small UI helpers shared by the shell and panels.
#pragma once

#include <core/Types.hpp>

#include <imgui.h>

#include <filesystem>
#include <initializer_list>
#include <string>

namespace ggui {

// The host of a remote URL ("github.com" for https://user@github.com:443/o/r.git, ssh://git@github.com/o/r
// and git@github.com:o/r.git), without user or port; empty for local paths and file:// URLs.
std::string remoteHost(const std::string& url);
bool containsNoCase(const std::string& haystack, const std::string& needle);
// "main → origin/main ↑1 ↓2" style summary of a recent repository.
std::string summaryText(const core::RepoSummary& s);
// A disabled menu item with a tooltip explaining why. `icon` should match the icon used by the
// enabled counterpart of this item so disabled entries line up with enabled ones.
void disabledMenuItem(const char* icon, const char* label, const char* reason, const char* shortcut = nullptr);
// A small coloured badge (label may contain ##id); hover and clicks go to what it sits on.
void drawBadge(const char* label, ImU32 color, bool outlined = false);
// Plain text registered as an item (tests find it; tooltips and context menus attach to it) with
// no hover or click highlight: for text whose click does nothing. label may contain ##id.
void plainText(const char* label);
// A full commit ID: the first `shortLen` characters in the text colour, the rest dimmed.
// Registered like plainText under `id` when given.
void idText(const std::string& hex, size_t shortLen, const char* id = nullptr);
// A tooltip that starts with a full commit ID (drawn as idText) followed by more lines.
void idTooltip(const std::string& hex, size_t shortLen, const std::string& rest);
// "Copy ID" copies the short ID, or the full ID while Shift is held.
void copyId(const std::string& shortId, const std::string& fullId);
// A "Copy ID"-style menu item: "<prefix>short ID", or "<prefix>full ID" while Shift is held (the
// first letter capitalised), with the stable ID "###<prefix>ID". Returns true when clicked (after
// copying).
bool copyIdMenuItem(const char* prefix, const std::string& shortId, const std::string& fullId, bool enabled = true);
// Alt+Space on the keyboard-focused item (the keyboard equivalent of the right click that opens its
// context menu). Call right after the item. The chord is a routed shortcut owned by the item, so
// Space does not also activate it and releasing Alt does not toggle the menu layer; it does nothing
// while text is being typed.
bool contextMenuKeyPressed();
// An app hotkey: ImGui::Shortcut() (routed to the focused window by default) that, when it fires, also
// locks the chord's key (not its modifiers) to its owner (`owner` 0 = the current focus scope, as Shortcut() does), so no
// other reader (ImGui's nav, a widget, a raw ImGui::IsKeyPressed elsewhere) sees the press. The key is
// locked for the frame, and until released when the chord has Alt (a held Alt+arrow must not leak).
// Call it every frame the binding is live. Routing already keeps the nav away before the call.
bool hotkey(ImGuiKeyChord chord, ImGuiInputFlags flags = ImGuiInputFlags_RouteFocused, ImGuiID owner = 0);
// What made a Selectable (created with ImGuiSelectableFlags_SelectOnNav) report pressed. Call it right
// after the Selectable returned true. NavMove: the nav cursor just moved onto the row (arrow, Page keys,
// Home/End; Shift is held for a range, Ctrl never presses). NavActivate: Space/Enter on the cursor row
// (Ctrl+Space toggles it). Mouse: a click (or anything else).
enum class PressSource { Mouse, NavMove, NavActivate };
PressSource pressSource();
// The modifiers (ImGuiMod_*) that belonged to the press: for NavMove those held when the arrow was
// pressed (the press is seen a frame later, when a quick Shift+Down may already be released), else the
// current ones.
ImGuiKeyChord pressMods();
// Opens popup `id` with its top-left corner at the bottom-left corner of the last item (where a
// keyboard-opened context menu appears).
void openPopupBelowItem(ImGuiID id, ImGuiPopupFlags flags = ImGuiPopupFlags_None);
// ImGui::BeginPopupContextItem that also opens the popup on Alt+Space while the last item has
// keyboard focus (below the item's bottom-left corner, not at the mouse). Same arguments and
// return value.
bool beginContextMenu(const char* strId = nullptr, ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);
// Indeterminate spinner of the given radius.
void spinner(const char* id, float radius);
// Opens a directory or file with the desktop's default handler (off the UI thread).
void openInFileManager(const std::filesystem::path& path);
// Help marker "(?)" with a tooltip.
void helpMarker(const char* text);
// Layout helpers so toolbars flow like words instead of clipping at the panel edge.
// ImGui::SameLine(), except that when fewer than `nextWidth` pixels remain the next item starts a new line.
void sameLineIfFits(float nextWidth);
// Width of ImGui::Checkbox(label): box + inner spacing + visible label text.
float checkboxWidth(const char* label);
// Width of an ImGui::Combo wide enough for its widest option (text + frame padding + arrow button).
float comboWidth(std::initializer_list<const char*> options);
// Width of an item of `fieldWidth` followed by its visible `label` (as InputText/Combo draw it).
float labelledWidth(float fieldWidth, const char* label);
// Formats "YYYY-MM-DD HH:MM".
std::string dateText(std::int64_t unixSeconds);

} // namespace ggui
