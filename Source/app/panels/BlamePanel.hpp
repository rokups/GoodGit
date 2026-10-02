// Blame panel (product spec §4.6).
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

private:
    using CursorState = std::array<int, 6>; // the main cursor's selection and position

    void request();
    void drawGutter(int index, float width, float height, float glyph);
    void drawLineMenuItems(int index);
    void drawTextMenu(int index);
    // A press on a line's gutter: selects the line, with `extend` the lines from the anchor to it.
    void selectLine(int index, bool extend);
    void selectBlock(int index);
    // The selection follows a cursor or text selection changed in the editor by the user.
    void followEditor();
    CursorState cursorState() const;
    // Marks the lines matching the filter; `scroll` brings the first one into view.
    void applyFilter(bool scroll);
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
    int m_authorColumns = 0; // the width of the gutter's author column, in characters
    std::string m_filter;
    std::string m_filterApplied; // the filter the markers were made for
    bool m_marksDirty = false;   // the text or the theme changed: the markers are made again
    std::vector<int> m_matches;  // lines matching the filter
    int m_selFirst = -1;
    int m_selLast = -1;
    int m_selLine = -1; // the line interacted with last: its change is shown in Change information
    CursorState m_cursorSeen{}; // the editor's cursor as the panel left it: a difference is the user's doing
    int m_menuLine = -1;        // the line of the gutter menu
    std::optional<Selection> m_published; // what was given to Session::setInfoOverride()
    int m_scrollTo = 0;
};

} // namespace ggui
