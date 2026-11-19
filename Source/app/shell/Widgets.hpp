// Menu items and buttons that show a Material Symbols icon (`icon`, an ICON_MS_* glyph from
// IconsMaterialSymbols.h) alongside their label, without changing the widget's ImGui ID: tests
// address widgets by label-derived IDs (Source/tests/Harness.hpp), so icons never get prepended
// into the label string itself.
#pragma once

#include <imgui.h>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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
// The colors of dangerButton() for a button drawn another way: pair the two calls around it.
void pushDangerColors();
void popDangerColors();
// The accent colors (blueish in the dark theme) of the confirm button of a dialog: pair the two calls around it.
void pushPrimaryColors();
void popPrimaryColors();
// Icon-only square-ish button (`id` like "##name", ID = ImGui::GetID(id)) that centres the
// glyph's drawn bounds in the frame, ignoring the icon font's baseline GlyphOffset and advance.
bool iconButton(const char* icon, const char* id, ImVec2 size);
// ImGui::Button with only the corners in `corners` (ImDrawFlags_RoundCorners*) rounded, for buttons that share one
// shape. The label is the plain ImGui label (an icon glyph may lead it).
bool buttonCorners(const char* label, ImDrawFlags corners);
// ImGui::SmallButton counterpart: same ID/icon contract as button(), fits within a text line.
bool smallButton(const char* icon, const char* label);
// ImGui::Selectable that stays highlighted while hovered when selected (ImGui draws a hovered row with
// HeaderHovered, the neutral hover overlay, which would hide the selection).
bool selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
// selectable() for free text: `text` is drawn as is (ImGui cuts a label at a "##"), the item has the ID of the
// label "text###id" and the size of the whole text. Use it where `text` is a user string (a commit summary).
bool selectableText(const std::string& text, const std::string& id, bool selected = false, ImGuiSelectableFlags flags = 0,
    ImVec2 size = ImVec2(0, 0));
// One icon button of a list row's hover actions (see rowActions). `id` names the button ("act_checkout": the
// label is `icon###id`, so tests address it as <row scope>/###act_checkout); `tip` is its tooltip.
struct RowAction {
    const char* icon;
    const char* id;
    const char* tip;
    bool enabled = true; // false: drawn disabled
    bool visible = true; // false: not drawn at all
};
// The most common actions of a list row as icon buttons at the right edge of the window's visible area, shown
// only while the mouse is on the row's band across the window. Call right after the row's item, inside its ID
// scope and before its menu and tooltip code: the row is the last item again on return. The row must allow
// overlap: call ImGui::SetNextItemAllowOverlap() before its item, a selectable or plainText alike.
// ImGuiSelectableFlags_AllowOverlap alone is not enough: the buttons work, but the row still counts as hovered
// under them, so a double click or right click on a button is also the row's. Each button is as tall as the band and its glyph is centred in it, where the
// row's label is. `paddedRow`: the row is a selectable, whose rect already includes half the item spacing on
// each side and which draws a hover highlight (the strip behind the buttons repeats it; `rowSelected` picks the
// selection's hover colour); false: a plain text row, as tall as its text. Returns the index of the button
// clicked, or -1.
int rowActions(std::span<const RowAction> actions, bool rowSelected = false, bool paddedRow = true);
// Called with the cursor at the start of the row, before its label is built: the width of rowActions' strip
// for `actions` while the row is hovered (the buttons are shown), else 0. What the label leaves free at the
// right edge so that it does not run under the buttons; the row is at full width while not hovered.
float rowActionsReserve(std::span<const RowAction> actions);
// menuItem()/selectable() whose visible label starts with `dimLen` bytes (a folder prefix) drawn in
// the dimmed text colour; the rest keeps the normal colour. The widget is laid out, identified and
// registered exactly like the plain call with the same `label`.
bool menuItemDimPrefix(const char* icon, const char* label, size_t dimLen, const char* shortcut = nullptr);
// True on the frame Delete is pressed while the last item is hovered (and no text field is being
// edited): a list entry's "remove" key.
bool hoveredDeletePressed();
bool selectableDimPrefix(const char* label, size_t dimLen, bool selected, ImGuiSelectableFlags flags, ImVec2 size);
// selectable() whose visible label has bytes [dimBegin, dimEnd) drawn dimmed (e.g. a "@host" suffix).
// Draws the visible part of `label` (up to its ###) at `pos` with bytes [dimBegin, dimEnd) dimmed, the
// rest in the Text colour: the overlay for a widget whose own text is drawn transparent.
void drawDimRange(ImVec2 pos, const char* label, size_t dimBegin, size_t dimEnd);
// The same for the text [label, end): a "##" in it is text, not the end of the visible part.
void drawDimRange(ImVec2 pos, const char* label, const char* end, size_t dimBegin, size_t dimEnd);
// selectableDimRange() with several (ascending, disjoint) dimmed byte ranges.
bool selectableDimRanges(const char* label, std::initializer_list<std::pair<size_t, size_t>> ranges, bool selected = false,
    ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
bool selectableDimRange(const char* label, size_t dimBegin, size_t dimEnd, bool selected = false, ImGuiSelectableFlags flags = 0,
    ImVec2 size = ImVec2(0, 0));
// The two above for free text: `text` is drawn as is, the item ID is that of "text###id" (see selectableText).
// The first `accentLen` bytes (an icon) are drawn in the colour `accent`; the dim ranges start after them.
bool selectableTextDimRanges(const std::string& text, const std::string& id, std::initializer_list<std::pair<size_t, size_t>> ranges,
    bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0), size_t accentLen = 0, ImU32 accent = 0);
bool selectableTextDimRange(const std::string& text, const std::string& id, size_t dimBegin, size_t dimEnd, bool selected = false,
    ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
// Width button()/smallButton() will take for this icon and label, for right-aligning rows.
float buttonWidth(const char* icon, const char* label);
// Makes the item just submitted (a commit-ID/ref input) a drop target for History's commits and branch badges:
// `text` becomes the commit's hex ID or the branch name. True when it was filled.
bool acceptCommitDrop(std::string& text);
// The first line of `text` (up to the first '\n' or '\r', trailing whitespace trimmed): what a commit
// subject, stash or tag message shows wherever it takes one line.
std::string_view firstLine(std::string_view text);
// ImGui::BeginDragDropSource() with an opaque preview tooltip. ImGui draws this tooltip with 60% of the
// alpha of the popup colour. The popup colour gets an alpha that the window code saturates to 1.
bool beginOpaqueDragSource(ImGuiDragDropFlags flags = ImGuiDragDropFlags_None);
// `text` cut to `width` pixels with an ellipsis (as it is when it already fits).
std::string fitText(const std::string& text, float width);
// `text` cut at its start to `width` pixels, with an ellipsis in front, so that its end stays (as it is when it
// already fits).
std::string elideStart(const std::string& text, float width);
// `text` as its first `prefix` and last `suffix` characters (UTF-8 codepoints, each at least 1) around an
// ellipsis; unchanged when it has at most prefix + suffix + 1 characters (cutting would not shorten it).
std::string elideMiddle(std::string_view text, int prefix, int suffix);
// `text` cut in the middle (an ellipsis between its head and tail) to fit `width` pixels; as it is if it fits.
std::string elideMiddleToWidth(const std::string& text, float width);
// firstLine(text) as one line of text in the current text colour that ends in an ellipsis where it does
// not fit `width` (the room left to the window's content edge when 0). A table cell clips without one.
// An item like TextUnformatted (laid out, SameLine and IsItemHovered work), but with no ID and no hover
// of its own, so it never takes hover or navigation from a row it sits on; `id` (label-derived like
// ImGui::GetID, relative to the ID stack) registers it for the test engine only. When the text was
// cut and `tooltip` is set, hovering shows the whole line: pass false where the row has its own tooltip.
// Returns true when the text was cut.
bool textElided(std::string_view text, const char* id = nullptr, bool tooltip = true, float width = 0);

// A custom-drawn tree (no ImGui tree widget): a thin trunk line from the parent, a short branch line to each row,
// and a marker dot on the line of a node that has children (filled: closed, ring: open). A row without children
// has no marker. All rows take the same height, and the indent step of a depth is fixed (treeLabelOffset()).
// Rows of one depth have their labels at the same x. A tree has no heap allocation per frame.
using TreeFlags = int;
enum : TreeFlags {
    TreeDefaultOpen = 1,     // the node is open until the user closes it
    TreeSelected = 2,        // the row has the colour ImGuiCol_Header as its background
    TreeAllowOverlap = 4,    // other items (hover buttons) can overlap the row
    TreeToggleOnMarker = 8,  // a mouse click toggles the node on the marker only (the label is for selection)
};
// The x distance from the start of a row to its label (and the indent step of a depth).
float treeLabelOffset();
// The room that a highlight keeps left of a label. True while a tree level is open.
float treeHighlightPad();
bool treeActive();
// A node row with a label. Returns true when the node has children and is open: then the caller draws the
// child rows and calls treeNodeEnd(). The row is the last item. `has_children` false: no marker, returns false.
// The open state is in the ImGui state storage (key: the ID of the label).
bool treeNode(const char* label, bool has_children, TreeFlags flags = 0);
void treeNodeEnd();
// The next treeNode() takes this open state (as ImGui::SetNextItemOpen with ImGuiCond_Always).
void treeSetNextOpen(bool open);
// True when the last treeNode() changed its open state with a click or a key.
bool treeNodeToggled();

// A leaf row (a row that the caller draws): treeLeafBegin() before it, treeLeafEnd() after it. The label of the
// row starts at the cursor x that treeLeafBegin() leaves. A leaf row has a branch line at its middle.
struct TreeLeaf {
    float x = 0;
    float y = 0;
};
TreeLeaf treeLeafBegin();
void treeLeafEnd(TreeLeaf leaf);
// A leaf row that is a node too: call it between treeLeafBegin() and the row's label, with the cursor at the
// start of the row. It draws the marker and handles the click. Returns the open state. The child rows are
// leaf rows (treeLeafBegin()) between the row and treeLeafEnd().
bool treeLeafMarker();
// Sets the open state of the node of treeLeafMarker() (a key press).
void treeLeafSetOpen(bool open);

} // namespace ggui
