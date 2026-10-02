// Blame panel (product spec §4.6).
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ImFont;

namespace ggui {

class BlameEditor;

class BlamePanel {
public:
    explicit BlamePanel(Session& session);
    ~BlamePanel();
    // Opens a new blame (pushes the current one to the back history).
    void open(const std::string& path, const core::Oid& commit, bool before = false, int line = 0);
    void back();
    void forward();
    void onBlame(const core::BlameEvent& event);
    void draw(bool* open);
    // Called instead of draw() while the panel is closed.
    void hidden() { publishInfoOverride(false); }
    // Drops the blame selection; the editor's text selection collapses to its cursor.
    void clearSelection();
    const core::BlamePtr& blame() const { return m_blame; }
    const core::BlameQuery* query() const { return m_pos >= 0 ? &m_history[static_cast<size_t>(m_pos)] : nullptr; }
    // The selected lines (0-based), -1 without a selection: the cursor the editor starts with is not one.
    int selectionFirst() const { return m_selFirst; }
    int selectionLast() const { return m_selLast; }
    // The editor's state (tests).
    int cursorLine() const;
    bool hasSelection() const;
    std::string selectedText() const;
    std::string languageName() const;
    int matchCount() const { return static_cast<int>(m_matches.size()); }
    // The match Enter / F3 stepped to last (0-based; the first one after a new filter), -1 without matches.
    int matchPos() const { return m_matchPos; }
    // The gutter's width in pixels (0 until drawn) and the parity (0 or 1) of the change block of a line
    // (0-based), -1 out of range: every second block has the alternate background.
    float gutterWidth() const { return m_gutterWidth; }
    int editorLines() const;
    int blockParity(int line) const { return line >= 0 && line < static_cast<int>(m_parity.size()) ? m_parity[static_cast<size_t>(line)] : -1; }

private:
    using CursorState = std::array<int, 6>; // the main cursor's selection and position

    void request();
    // Measures the gutter with the editor's font (current) and gives it to the editor.
    void updateGutter(float fontSize);
    void drawGutter(int index, float width, float height, float glyph);
    void drawLineMenuItems(int index);
    void drawTextMenu(int index);
    // A press on a line's gutter: selects the line, with `extend` the lines from the anchor to it.
    void selectLine(int index, bool extend);
    // Selects whole rows from `anchor` to `index`, the cursor on `index`.
    void selectRange(int anchor, int index);
    // A drag that started on a gutter row selects the rows from it to the one under the mouse.
    void followGutterDrag();
    void selectBlock(int index);
    // The selection follows a cursor or text selection changed in the editor by the user.
    void followEditor();
    CursorState cursorState() const;
    // Marks the lines matching the filter; `scroll` brings the first one into view.
    void applyFilter(bool scroll);
    // Moves to the next (`direction` 1) or previous match, wrapping: the line is selected and in view.
    // `focusEditor` gives the editor the keyboard (F3), otherwise it stays where it is (Enter in the filter).
    void stepMatch(int direction, bool focusEditor);
    std::string matchText() const; // "n of m" or "No matches"
    // Change information follows the selected line while the panel is shown (`shown`).
    void publishInfoOverride(bool shown);
    std::string blockText(int index, int* first, int* last) const;

    Session& m_session;
    std::vector<core::BlameQuery> m_history;
    int m_pos = -1;
    core::RequestId m_request = 0;
    core::BlamePtr m_blame;
    bool m_loading = false;
    std::unique_ptr<BlameEditor> m_editor; // holds the whole file: highlighting, selection and copying are its own
    int m_paletteTheme = -1;
    bool m_editorFocused = false; // the editor (the panel's only child window) or a menu over it had the keyboard last frame
    std::vector<unsigned char> m_parity; // per line: 0 or 1, flips with every change block
    float m_gutterWidth = 0;             // pixels
    float m_authorWidth = 0;             // the gutter's author column, pixels
    float m_gutterFont = 0;              // the font size the gutter was measured at
    ImFont* m_gutterFace = nullptr;      // and the font
    bool m_gutterDirty = false;          // a blame was loaded: the gutter is measured again
    std::string m_filter;
    std::string m_filterApplied; // the filter the markers were made for
    bool m_marksDirty = false;   // the text or the theme changed: the markers are made again
    std::vector<int> m_matches;  // lines matching the filter
    int m_matchPos = -1;         // the match stepped to last, in m_matches
    bool m_blameChanged = false; // a blame was loaded: the match position starts again
    bool m_matchVisited = false; // the cursor has been put on m_matchPos since the filter changed
    int m_menuPending = 0;       // frames left to open the menu of the cursor line (Alt+Space)
    int m_selFirst = -1;
    int m_selLast = -1;
    bool m_dragging = false; // the left button went down on a gutter row and is still down
    int m_dragAnchor = 0;    // the row the selection of that press started from
    float m_rowTop = 0;      // the screen y of line 0's row and the row height, as drawn last
    float m_rowHeight = 0;
    int m_selLine = -1; // the line interacted with last: its change is shown in Change information
    CursorState m_cursorSeen{}; // the editor's cursor as the panel left it: a difference is the user's doing
    int m_menuLine = -1;        // the line of the gutter menu
    std::optional<Selection> m_published; // what was given to Session::setInfoOverride()
    int m_scrollTo = 0;
};

} // namespace ggui
