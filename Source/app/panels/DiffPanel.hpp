// Diff panel (product spec §4.5): unified and side-by-side views of one file, both read-only text
// editors (selectable text, syntax highlighting) with a gutter for line numbers, hunk staging
// buttons, selectable line handles and expandable context.
#pragma once

#include "panels/ChangesPanel.hpp"
#include "shell/Session.hpp"
#include "util/PatchBuilder.hpp"

#include <core/Engine.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class TextEditor;

namespace ggui {

class DiffPanel {
public:
    static constexpr int kSlot = 1;

    explicit DiffPanel(Session& session);
    ~DiffPanel();

    // The engine query that shows `row` for `sel`; a commit's file compared with `compare`.
    static core::DiffQuery queryFor(const Selection& sel, const FileRow& row, const CompareTarget& compare,
        const core::SnapshotPtr& snapshot);

    void onSelection(const Selection& sel);
    void showFile(const Selection& sel, const FileRow& row, const CompareTarget& compare);
    // This file can be compared with something else (a commit's or stash's file, while the
    // Changes panel does not compare the whole commit).
    bool canCompare() const;
    void refreshIfShowing();
    void clear();
    void onDiff(const core::DiffEvent& event);
    void draw(bool* open);

    enum class StagingMode { None, Unstaged, Staged };
    enum class StagingAction { Stage, Unstage, Discard };
    StagingMode stagingMode() const;
    // Lines (and whole hunks) covered by the current row selection.
    LineSet selectedLines() const;
    void applyLines(const LineSet& lines, StagingAction action);

    const core::DiffPtr& diff() const { return m_diff; }
    const std::optional<FileRow>& file() const { return m_file; }
    std::string languageName() const;
    std::string selectedText() const;
    // Hunk navigation (toolbar buttons, Alt+Down / Alt+Up): scroll the next / previous hunk (in
    // side by side its first code line) to the top. "Current" is the hunk last navigated to while
    // it is still on screen (the last hunks cannot reach the top), otherwise the first visible
    // line. Stops at the ends. `hunkTarget` is the editor line to go to, or -1 at the end.
    int hunkTarget(bool next) const;
    void goToHunk(bool next);
    // Tests: the first visible line of the shown view and the line where hunk `h` starts there.
    int topLine() const;
    int hunkLine(int h) const;
    // Lines revealed in a context gap: from its top (below the hunk above) and from its bottom
    // (above the hunk below); `all` reveals the whole gap.
    struct GapShown {
        int top = 0;
        int bottom = 0;
        bool all = false;
    };
    GapShown gapShown(int gap) const
    {
        auto it = m_gapShown.find(gap);
        return it == m_gapShown.end() ? GapShown{} : it->second;
    }

    // One rendered row of the unified view.
    struct Row {
        enum Kind { Hunk, Line, Gap } kind = Line;
        int hunk = -1;
        int line = -1;      // index in hunk lines
        int gap = -1;       // gap index
        int gapStart = 0;   // first new-side line number (1-based) of the gap
        int gapCount = 0;   // hidden lines in the gap
        int oldOffset = 0;  // old = new + offset inside the gap
    };

private:
    // One line of an editor view and what it shows.
    struct EditorLine {
        enum Kind { Hunk, Line, GapHidden, GapLine, Filler } kind = Filler;
        int row = -1;
        int hunk = -1;
        int line = -1;
        int gap = -1;
        int oldNo = 0;
        int newNo = 0;
        char origin = ' ';
    };
    enum class Side { Unified, Left, Right };
    struct View {
        std::unique_ptr<TextEditor> editor;
        std::vector<EditorLine> lines;
        std::vector<int> firstLine; // per row: first editor line (-1 = not in this view)
        std::vector<int> lastLine;
        Side side = Side::Unified;
        bool focused = false; // the editor (or a window over it) had the keyboard last frame
    };

    void request();
    void buildRows();
    void buildViews();
    void setupView(View& v, Side side);
    void finishView(View& v, const std::string& text);
    void renderEditor(View& v, const char* id, float width);
    void drawToolbar();
    void drawUnified();
    void drawSideBySide();
    void drawPlaceholder(const core::DiffFile& file);
    void drawGutter(View& v, int line, float width, float height);
    void drawMenuItems();
    void selectRows(View& v, int row, bool extend);
    std::vector<int> selectedRows() const;
    View& primaryView();
    const View& primaryView() const;
    bool sideBySideShown() const;
    std::vector<int> hunkStarts() const;

    Session& m_session;
    Selection m_selection;
    std::optional<FileRow> m_file;
    CompareTarget m_compare;      // from the Changes panel (the whole commit)
    std::string m_fileCompareText; // the Diff panel's "Compare with" field (this file only)
    CompareTarget m_fileCompare;
    core::DiffPtr m_diff;
    core::RequestId m_request = 0;
    bool m_full = false;
    bool m_loading = false;

    std::vector<Row> m_rows;
    std::map<int, GapShown> m_gapShown; // gap index → lines revealed

    View m_unified;
    View m_left;
    View m_right;
    View* m_active = nullptr;   // view the selection belongs to
    int m_anchorRow = -1;
    bool m_viewsDirty = true;
    bool m_cutKey = false;      // Ctrl+X or Shift+Delete went to the Diff window this frame
    bool m_resetScroll = true;
    int m_navLine = -1;         // editor line of the last hunk navigation ...
    bool m_navSideBySide = false; // ... in this view mode
    float m_syncedScroll = 0.0f;
    int m_paletteTheme = -1;
    int m_conflictView = 0; // native conflicts: 0 working tree, 1 base→ours, 2 base→theirs, 3 ours→theirs
    int m_termView = 0;     // first-class conflicts: 0 raw markers, k: base → side k
    int termSides() const;  // sides of the shown file's first-class conflict (0 = none)
};

} // namespace ggui
