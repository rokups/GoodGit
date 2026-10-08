// Small UI helpers shared by the shell and panels.
#pragma once

#include <core/Types.hpp>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ggui {

// The host of a remote URL ("github.com" for https://user@github.com:443/o/r.git, ssh://git@github.com/o/r
// and git@github.com:o/r.git), without user or port; empty for local paths and file:// URLs.
std::string remoteHost(const std::string& url);
bool containsNoCase(const std::string& haystack, const std::string& needle);
// "main → origin/main ↑1 ↓2" style summary of a recent repository. With `elidePrefix` > 0 the branch and the
// upstream name are shortened in the middle (see elideMiddle).
std::string summaryText(const core::RepoSummary& s, int elidePrefix = 0, int elideSuffix = 0);
// The tooltip lines with the full names that summaryText() elides: "\nBranch: ..." and "\nUpstream: ..." (empty parts left out).
std::string summaryNamesText(const core::RepoSummary& s);
// A disabled menu item with a tooltip explaining why. `icon` should match the icon used by the
// enabled counterpart of this item so disabled entries line up with enabled ones.
void disabledMenuItem(const char* icon, const char* label, const char* reason, const char* shortcut = nullptr);
// A small coloured badge (label may contain ##id); hover and clicks go to what it sits on.
void drawBadge(const char* label, ImU32 color, bool outlined = false);
// Plain text registered as an item (tests find it; tooltips and context menus attach to it) with
// no hover or click highlight: for text whose click does nothing. label may contain ##id.
void plainText(const char* label);
// plainText() for free text: `text` is drawn as is (ImGui cuts a label at a "##"); the item has the ID of "text###id".
void plainText(const std::string& text, const std::string& id);
// Commit ID rules: a short ID is always kShortIdLength characters; wherever an ID is drawn, its first
// kIdPrefixLength characters (a short ID) or kShortIdLength characters (a full ID) are in the text
// colour and the rest is dimmed. Every place that shows an ID draws it with commitId or rowIdText (below), or as
// part of a selectable's label with idSlotAt giving its place, and offers its copy item with commitId,
// idCopyMenuItems or the two-step captureIdCopyClick / idCopyMenuItem, so they look and behave alike.
using core::kIdPrefixLength;
using core::kShortIdLength;
// A list counts as scrolling until its scroll position has been still for this long (seconds).
constexpr double kScrollSettleSeconds = 0.3;
// True while the current window, or the window it is a child of, has scrolled within kScrollSettleSeconds
// (wheel, scrollbar, keyboard or code). Call it inside the list; cheap enough for every item of every frame.
// A window seen for the first time (or not drawn in the last frame) counts as not scrolling.
bool listScrolling();
// Updates the scroll record of every window; the app calls it once at the end of each frame, so listScrolling
// compares with the previous frame whenever it is asked.
void trackScrolling();
// Tooltips never show over a scrolling list or editor gutter, only once it is stationary.
bool tooltipAllowed();
// ImGui::BeginTooltip() that does nothing (false) while the list is scrolling; every tooltip goes through it.
// Check tooltipAllowed() before building a tooltip's text, so no text is built while scrolling.
bool beginTooltip();
// The text of the tooltip `key`, built once and kept while it is asked for every frame and `revision` stays the same.
const std::string& cachedTooltipText(ImGuiID key, uint64_t revision, const std::function<std::string()>& build);
// Tests: how many times cachedTooltipText has called its `build`.
int tooltipTextBuilds();
// A tooltip that starts with a full commit ID (split after kShortIdLength) followed by more lines.
void idTooltip(const std::string& hex, const std::string& rest);
// Text inside an open tooltip: word-wrapped at 40 font sizes and cut with "…" after 10 lines
// ('\n' in the text ends a line too). Every tooltip with text goes through this.
void tooltipText(std::string_view text);
// A tooltip (like SetTooltip) with the text wrapped and cut by tooltipText; the text may be any length.
void tooltip(const char* fmt, ...) IM_FMTARGS(1);
// The one "copy ID" menu item every commit ID offers. `longId` is a full ID (its highlighted prefix is the 7
// characters) rather than a short one (3). Which part was clicked and whether Shift was held decide the item:
//   long ID:  the 7-character prefix -> "Copy <7 characters>"; the rest -> "Copy full ID";
//   short ID: Shift -> "Copy full ID"; else the 3-character prefix -> "Copy <3 characters>", the rest -> "Copy <7 characters>".
struct IdCopyChoice {
    std::string label; // the item's text
    std::string text;  // what it copies
};
IdCopyChoice idCopyChoice(const std::string& hex, bool longId, bool onPrefix, bool shift);
// The item of a menu opened from the keyboard (Alt+Space), for a long or a short ID: "Copy <7 characters>", with
// Shift "Copy full ID"; never the 3 characters.
IdCopyChoice idCopyChoiceKeyboard(const std::string& hex, bool shift);
// X ranges [min, max) on screen of a drawn ID's whole text and of its highlighted prefix; ImVec2() = none.
struct IdSlot {
    ImVec2 text;
    ImVec2 prefix;
};
struct IdOptions {
    bool full = false; // the full ID (split after 7) instead of the short one (3 of 7)
};
// A standalone commit ID (an item of its own, `id` is its item ID): draws it, copies on left click (the highlighted
// prefix copies that part, the rest the full ID), and owns its context menu "##<id>_menu" whose only item is the
// ###copy_id item. Returns its slot.
IdSlot commitId(const std::string& hex, const char* id, const IdOptions& options = {});
// A commit ID drawn as text inside a row or a line of text: no click, tooltip or menu of its own (the row's own
// tooltip and menu stay). Registered like plainText under `id` when given. Returns its slot, for the row's menu.
IdSlot rowIdText(const std::string& hex, const char* id = nullptr, bool full = false);
// The slot of an ID the caller draws itself as part of a selectable's label: `shown` is the ID text starting at
// screen X `left`, `lead` the number of highlighted characters.
IdSlot idSlotAt(float left, std::string_view shown, size_t lead);
// The copy item(s) of a row's context menu; call inside the open menu. entries[0] is the row's own ID: a right
// click elsewhere on the row or a menu opened from the keyboard offers it. A right click on the drawn text of
// entry k offers only entry k (unnamed). Otherwise every further enabled entry is offered after the first as
// "Copy <what> ..." under the ID "<what>/###copy_id".
struct IdMenuEntry {
    std::string hex;
    IdSlot slot;
    const char* what = nullptr;
    bool enabled = true;
};
void idCopyMenuItems(std::initializer_list<IdMenuEntry> entries);
// The two-step form of idCopyMenuItems for a menu whose copy item sits in a submenu (the submenu's first frame is
// not the click) or after other items that depend on the ID: call captureIdCopyClick first inside the menu itself
// (on its first frame it records whether the right click was inside the slot's prefix and whether Shift was held),
// then idCopyMenuItem where the item goes.
// A menu opened from the keyboard offers the 7 characters, with Shift the full ID.
void captureIdCopyClick(const IdSlot& slot);
bool idCopyMenuItem(const std::string& hex, bool enabled = true);
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
// Call right before ImGui::BeginTable() of a ScrollY table: its scroll child becomes nav-flattened, so the
// arrows reach its rows from the surrounding panel (a plain child is a separate nav layer).
void flattenNextTable();
// A window's scrolling list under controls that stay put (buttons, a filter field): only the list scrolls.
// A nav-flattened child ("##list") filling the rest of the window, whose widgets keep the IDs they would
// have directly in the window (`width` 0 = the window's). Pair with endList().
void beginList(float width = 0.0f);
void endList();
// The list's child alone, named `name`, for a list whose IDs live under the child (the Changes files).
// It spans the window edge to edge with the window's padding inside, so rows look as they would in the
// window itself and their outlines and highlights are not clipped. Pair with endListChild().
void beginListChild(const char* name, float width = 0.0f);
void endListChild();
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
