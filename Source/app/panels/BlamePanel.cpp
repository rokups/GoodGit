#include "panels/BlamePanel.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/SyntaxHighlight.hpp"
#include "util/Ui.hpp"

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ggui {

namespace {

const ImGuiID kMenuId = ImHashStr("##blame_menu");
const char kNotCommitted[] = "Not committed";
const float kAuthorColumns = 20; // longer author names are cut in the gutter

ImU32 withAlpha(ImU32 color, int alpha) { return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT); }

} // namespace

// Read-only editor of the blamed file. Like the diff's editor it does not take the keyboard focus when
// it first appears.
class BlameEditor : public TextEditor {
public:
    BlameEditor() { focusOnEditor = false; }

    // Selects the lines from `anchor` to `line` (up to the end of the last one) and leaves the cursor on
    // `line`, so the arrows go on from the line pressed. Nothing scrolls: that line is on the screen.
    void selectRows(int anchor, int line)
    {
        const int last = document.lineCount() - 1;
        anchor = std::clamp(anchor, 0, last);
        line = std::clamp(line, 0, last);
        if (line >= anchor) {
            moveTo(Coordinate(anchor, 0), false);
            moveTo(document.getEndOfLine(Coordinate(line, 0)), true);
        } else {
            moveTo(document.getEndOfLine(Coordinate(anchor, 0)), false);
            moveTo(Coordinate(line, 0), true);
        }
        ensureCursorIsVisible = false;
    }

    // The line the selection was started on: a Shift press extends from it.
    int anchorLine() { return cursors.getMain().getInteractiveStart().line; }
    // Ends the text selection where the cursor is, without scrolling to it.
    void collapseSelection() { cursors.setCursor(cursors.getMain().getInteractiveEnd()); }
    std::string mainSelectionText() const { return GetCursorText(cursors.getMainIndex()); }
};

BlamePanel::BlamePanel(Session& session) : m_session(session), m_editor(std::make_unique<BlameEditor>())
{
    m_editor->SetReadOnlyEnabled(true);
    m_editor->SetShowLineNumbersEnabled(true);
    m_editor->SetShowMatchingBrackets(false);
    m_editor->SetShowScrollbarMiniMapEnabled(false);
    m_editor->SetShowPanScrollIndicatorEnabled(false);
    m_editor->SetShowWhitespacesEnabled(false);
    // Right click does not move the cursor: the menus work on the line under the mouse.
    m_editor->SetTextContextMenuCallback([this](int line, int) { drawTextMenu(line); });
    m_editor->SetLineNumberContextMenuCallback([this](int line) { drawLineMenuItems(line, false); });
}

BlamePanel::~BlamePanel() = default;

void BlamePanel::open(const std::string& path, const core::Oid& commit, bool before, int line, int row)
{
    saveView();
    core::BlameQuery q;
    q.path = path;
    q.commit = commit;
    q.beforeCommit = before;
    q.scrollToLine = line;
    if (m_pos + 1 < static_cast<int>(m_history.size())) {
        m_history.resize(static_cast<size_t>(m_pos + 1));
        m_views.resize(m_history.size());
    }
    m_history.push_back(q);
    m_views.emplace_back();
    m_pos = static_cast<int>(m_history.size()) - 1;
    m_scrollRow = row;
    request();
}

void BlamePanel::show(const std::string& path, const core::Oid& commit)
{
    // The history is the blames of one file: a file that was renamed has its old name in an entry too.
    const auto named = [&](const core::BlameQuery& q) { return q.path == path; };
    if (m_pos >= 0 && !std::any_of(m_history.begin(), m_history.end(), named)) {
        m_history.clear();
        m_views.clear();
        m_pos = -1;
    }
    if (m_pos >= 0) {
        // Asked for again what is shown: no copy of it in the history.
        const auto& q = m_history[static_cast<size_t>(m_pos)];
        if (q.path == path && q.commit == commit && !q.beforeCommit) {
            if (commit.isNull()) {
                // The working tree may have changed since: blame it again in place, the view kept.
                saveView();
                m_scrollRow = -1;
                request();
                if (m_views[static_cast<size_t>(m_pos)].firstLine >= 0)
                    m_restore = m_views[static_cast<size_t>(m_pos)];
            } else if (!m_blame && !m_loading) {
                request();
            }
            return;
        }
    }
    open(path, commit);
}

void BlamePanel::back()
{
    if (canGoBack()) {
        saveView();
        --m_pos;
        m_scrollRow = -1;
        request();
        if (m_views[static_cast<size_t>(m_pos)].firstLine >= 0)
            m_restore = m_views[static_cast<size_t>(m_pos)];
    }
}

void BlamePanel::forward()
{
    if (canGoForward()) {
        saveView();
        ++m_pos;
        m_scrollRow = -1;
        request();
        if (m_views[static_cast<size_t>(m_pos)].firstLine >= 0)
            m_restore = m_views[static_cast<size_t>(m_pos)];
    }
}

void BlamePanel::saveView()
{
    if (m_pos < 0 || !m_blame || m_loading || !m_viewDrawn)
        return;
    m_views[static_cast<size_t>(m_pos)] = {m_editor->GetFirstVisibleLine(), cursorLine(), m_selFirst, m_selLast};
}

void BlamePanel::clearHistory()
{
    m_history.clear();
    m_views.clear();
    m_pos = -1;
    m_request = 0; // a blame still on its way is dropped
    m_loading = false;
    m_restore.reset();
    m_scrollTo = 0;
    m_scrollRow = -1;
    m_blame.reset();
    m_parity.clear();
    m_editor->SetLanguage(nullptr);
    m_editor->SetText(std::string());
    m_gutterDirty = true;
    m_viewDrawn = false;
    m_filter.clear();
    m_filterApplied.clear();
    m_matches.clear();
    m_matchPos = -1;
    m_matchVisited = false;
    m_marksDirty = false;
    m_blameChanged = false;
    m_dragging = false;
    m_menuPending = 0;
    m_menuLine = -1;
    clearSelection();
}

void BlamePanel::request()
{
    if (m_pos < 0)
        return;
    m_loading = true;
    clearSelection();
    m_scrollTo = m_history[static_cast<size_t>(m_pos)].scrollToLine;
    m_restore.reset();
    m_request = m_session.engine().blame(m_history[static_cast<size_t>(m_pos)]);
}

void BlamePanel::onBlame(const core::BlameEvent& event)
{
    if (event.request != m_request)
        return;
    m_loading = false;
    m_blame = event.blame;
    ++m_blameRevision;

    // The editor holds the whole file and highlights it by the file's language. A newline separates the
    // lines: none follows the last one, so the editor has no empty line after it.
    std::string text;
    m_parity.clear();
    if (m_blame) {
        unsigned char parity = 1; // the first block is 0
        for (size_t i = 0; i < m_blame->lines.size(); ++i) {
            const auto& l = m_blame->lines[i];
            if (i > 0)
                text.push_back('\n');
            text += l.text;
            if (i == 0 || m_blame->lines[i - 1].commit != l.commit)
                parity ^= 1;
            m_parity.push_back(parity);
        }
    }
    m_gutterDirty = true; // the width needs the editor's font: it is made in draw()
    m_editor->SetLanguage(m_blame ? languageFor(m_blame->query.path) : nullptr);
    m_editor->SetText(text);
    m_marksDirty = true;
    m_blameChanged = true;
    m_dragging = false;
    clearSelection(); // the lines of the previous blame are gone
    m_viewDrawn = false;
    const int count = m_blame ? static_cast<int>(m_blame->lines.size()) : 0;
    if (m_restore && count > 0) {
        // Back or forward: the view the blame was left in. The filter does not scroll over it (it scrolls
        // only when its text changes).
        if (m_restore->selFirst >= 0 && m_restore->selLast < count) {
            selectRange(m_restore->selFirst, m_restore->selLast);
        } else {
            // SetText put the cursor on line 0: it goes back where it was (not a selection).
            m_editor->SetCursor(std::min(m_restore->cursor, count - 1), 0);
            m_cursorSeen = cursorState();
        }
        m_editor->ScrollToLine(std::min(m_restore->firstLine, count - 1), TextEditor::Scroll::alignTop);
    } else if (m_scrollTo > 0 && count > 0) {
        // The line asked for: the cursor is put on it, which is not a selection. It goes to the row it was
        // opened from, or to the middle.
        const int line = std::min(m_scrollTo, count) - 1;
        m_editor->SetCursor(line, 0);
        if (m_scrollRow >= 0)
            m_editor->ScrollToLine(std::max(0, line - m_scrollRow), TextEditor::Scroll::alignTop);
        else
            m_editor->ScrollToLine(line, TextEditor::Scroll::alignMiddle);
        m_cursorSeen = cursorState();
    }
    m_restore.reset();
    m_scrollTo = 0;
    m_scrollRow = -1;
    if (!m_blame)
        return;

    // "Blame before": remember the resolved parent so back/forward are exact.
    if (m_pos >= 0 && m_history[static_cast<size_t>(m_pos)].beforeCommit) {
        m_history[static_cast<size_t>(m_pos)].commit = m_blame->query.commit;
        m_history[static_cast<size_t>(m_pos)].path = m_blame->query.path;
        m_history[static_cast<size_t>(m_pos)].beforeCommit = false;
    }
}

std::string BlamePanel::blockText(int index, int* first, int* last) const
{
    const auto& lines = m_blame->lines;
    const core::Oid id = lines[static_cast<size_t>(index)].commit;
    int a = index, b = index;
    while (a > 0 && lines[static_cast<size_t>(a - 1)].commit == id)
        --a;
    while (b + 1 < static_cast<int>(lines.size()) && lines[static_cast<size_t>(b + 1)].commit == id)
        ++b;
    if (first)
        *first = a;
    if (last)
        *last = b;
    std::string text;
    for (int i = a; i <= b; ++i) {
        text += lines[static_cast<size_t>(i)].text;
        text.push_back('\n');
    }
    return text;
}

void BlamePanel::updateGutter(float fontSize)
{
    m_gutterDirty = false;
    m_gutterFont = fontSize;
    m_gutterFace = ImGui::GetFont();
    const float glyph = ImGui::CalcTextSize("0").x; // the editor's monospace font is current
    // The author column is as wide as the longest author present, up to kAuthorColumns glyphs.
    float author = 0, date = 0;
    bool uncommitted = false;
    std::unordered_set<std::string> authors, dates;
    if (m_blame)
        for (const auto& l : m_blame->lines) {
            if (l.commit.isNull()) {
                uncommitted = true;
            } else {
                if (authors.insert(l.author).second)
                    author = std::max(author, ImGui::CalcTextSize(l.author.c_str()).x);
                const std::string when = core::formatTime(l.time);
                if (dates.insert(when).second)
                    date = std::max(date, ImGui::CalcTextSize(when.c_str()).x);
            }
        }
    m_authorWidth = std::min(author, kAuthorColumns * glyph);
    m_gutterWidth = static_cast<float>(kShortIdLength + 2) * glyph + m_authorWidth + 2 * glyph + date + glyph;
    if (uncommitted)
        m_gutterWidth = std::max(m_gutterWidth, ImGui::CalcTextSize(kNotCommitted).x + glyph);
    m_gutterWidth = std::ceil(m_gutterWidth);
    // Positive: pixels.
    m_editor->SetLineDecorator(m_gutterWidth,
        [this](TextEditor::Decorator& d) { drawGutter(d.line, d.width, d.height, d.glyphSize.x); });
}

void BlamePanel::drawLineMenuItems(int index, bool gutter)
{
    if (!m_blame || index < 0 || index >= static_cast<int>(m_blame->lines.size()))
        return;
    const auto& line = m_blame->lines[static_cast<size_t>(index)];
    const bool committed = !line.commit.isNull();
    // The gutter shows the ID on the first line of a block only; a click anywhere else counts as the rest of the ID.
    const bool idDrawn = committed && (index == 0 || m_blame->lines[static_cast<size_t>(index - 1)].commit != line.commit);
    captureIdCopyClick(gutter && idDrawn ? m_idPrefixRange : ImVec2());
    // The line stays on its screen row in the blame opened from it.
    const int row = std::max(0, index - m_editor->GetFirstVisibleLine());
    if (menuItem(ICON_MS_PERSON_SEARCH, "Blame before this change", nullptr, false, committed))
        open(line.origPath, line.commit, true, line.origLine, row);
    if (menuItem(ICON_MS_SOURCE, "Show originating source", nullptr, false, committed))
        open(line.origPath, line.commit, false, line.origLine, row);
    ImGui::Separator();
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal commit", nullptr, false, committed))
        m_session.revealCommit(line.commit);
    idCopyMenuItem(line.commit.hex(), false, committed);
    ImGui::Separator();
    if (menuItem(ICON_MS_SELECT_ALL, "Select change block"))
        selectBlock(index);
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy change block"))
        ImGui::SetClipboardText(blockText(index, nullptr, nullptr).c_str());
}

void BlamePanel::drawTextMenu(int index)
{
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy", "Ctrl+C", false, m_editor->AnyCursorHasSelection()))
        m_editor->Copy();
    if (menuItem(ICON_MS_SELECT_ALL, "Select all", "Ctrl+A"))
        m_editor->SelectAll(); // followed as any selection made in the editor
    if (!m_blame || index < 0 || index >= static_cast<int>(m_blame->lines.size()))
        return;
    ImGui::Separator();
    drawLineMenuItems(index, false);
}

BlamePanel::CursorState BlamePanel::cursorState() const
{
    const auto sel = m_editor->GetMainCursorSelection();
    const auto pos = m_editor->GetMainCursorPosition();
    return {sel.start.line, sel.start.column, sel.end.line, sel.end.column, pos.line, pos.column};
}

void BlamePanel::clearSelection()
{
    m_selFirst = m_selLast = m_selLine = -1;
    m_editor->collapseSelection();
    m_cursorSeen = cursorState();
}

void BlamePanel::selectLine(int index, bool extend)
{
    const int count = static_cast<int>(m_blame->lines.size());
    // The range runs from the line the selection was started on to this line.
    m_dragAnchor = extend && m_selFirst >= 0 ? std::min(m_editor->anchorLine(), count - 1) : index;
    selectRange(m_dragAnchor, index);
}

void BlamePanel::selectRange(int anchor, int index)
{
    m_editor->selectRows(anchor, index);
    m_selFirst = std::min(anchor, index);
    m_selLast = std::max(anchor, index);
    m_selLine = index;
    m_cursorSeen = cursorState();
}

void BlamePanel::followGutterDrag()
{
    if (!m_dragging)
        return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_dragging = false;
        return;
    }
    if (!m_blame || m_blame->lines.empty() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left) || m_rowHeight <= 0)
        return;
    // Whole rows from the pressed one to the one under the mouse, at most to the first or last one in view.
    // While the pressed row's button is active the editor's window is not hovered, so its own drag selection
    // does not run: this is the only writer.
    const int count = static_cast<int>(m_blame->lines.size());
    int line = static_cast<int>(std::floor((ImGui::GetMousePos().y - m_rowTop) / m_rowHeight));
    const int first = m_editor->GetFirstVisibleLine();
    line = std::clamp(line, first, std::max(first, m_editor->GetLastVisibleLine()));
    line = std::clamp(line, 0, count - 1);
    selectRange(m_dragAnchor, line);
}

void BlamePanel::selectBlock(int index)
{
    blockText(index, &m_selFirst, &m_selLast);
    m_selLine = index;
    m_editor->selectRows(m_selFirst, m_selLast);
    m_cursorSeen = cursorState();
}

void BlamePanel::followEditor()
{
    // Only the user moves the cursor behind the panel's back (the mouse, the arrows, Ctrl+A ...): what the
    // panel does itself is noted in m_cursorSeen. So this needs no focus test, which a click would fail
    // (the editor takes the click a frame before it has the focus).
    const CursorState now = cursorState();
    if (now == m_cursorSeen)
        return;
    m_cursorSeen = now;
    if (!m_blame || m_blame->lines.empty())
        return;
    const int count = static_cast<int>(m_blame->lines.size());
    // A selection ending at the start of a later line leaves that line out (a triple click and a press on a
    // line number select up to there; Shift+Down onto an empty line looks the same). The exception is a
    // selection of the whole text (Ctrl+A), which includes an empty last line.
    const bool atEnd = now[0] == 0 && now[1] == 0 && now[2] == count - 1 && m_blame->lines[static_cast<size_t>(count - 1)].text.empty();
    m_selFirst = std::min(now[0], count - 1);
    m_selLast = std::clamp(now[2] - (now[3] == 0 && now[2] > now[0] && !atEnd ? 1 : 0), m_selFirst, count - 1);
    // The line shown in Change information is the one the cursor (the moving end) is on.
    m_selLine = now[4] == now[2] && now[5] == now[3] ? m_selLast : m_selFirst;
}

void BlamePanel::applyFilter(bool scroll)
{
    // The position starts again when the filter text or the blame changed, not for the markers alone (a theme).
    const bool reset = m_filter != m_filterApplied || m_blameChanged;
    m_filterApplied = m_filter;
    m_marksDirty = false;
    m_blameChanged = false;
    m_matches.clear();
    m_editor->ClearMarkers();
    if (reset) {
        m_matchPos = -1;
        m_matchVisited = false;
    }
    if (m_filter.empty())
        return;
    const ImU32 color = theme().palette().warning;
    for (size_t i = 0; i < m_blame->lines.size(); ++i) {
        const auto& l = m_blame->lines[i];
        if (!containsNoCase(l.text, m_filter) && !containsNoCase(l.author, m_filter) && !containsNoCase(l.commit.hex(), m_filter)
            && !containsNoCase(l.summary, m_filter))
            continue;
        m_matches.push_back(static_cast<int>(i));
        m_editor->AddMarker(static_cast<int>(i), withAlpha(color, 0x60), withAlpha(color, 0x38), "", "");
    }
    if (m_matches.empty())
        m_matchPos = -1;
    else if (reset || m_matchPos < 0)
        m_matchPos = 0;
    else
        m_matchPos = std::min(m_matchPos, static_cast<int>(m_matches.size()) - 1);
    // The cursor stays (and with it the selection); the view moves only when the first match is outside it.
    if (scroll && !m_matches.empty()
        && (m_matches.front() < m_editor->GetFirstVisibleLine() || m_matches.front() > m_editor->GetLastVisibleLine()))
        m_editor->ScrollToLine(m_matches.front(), TextEditor::Scroll::alignMiddle);
}

void BlamePanel::stepMatch(int direction, bool focusEditor)
{
    if (m_matches.empty() || !m_blame)
        return;
    const int count = static_cast<int>(m_matches.size());
    // The first step lands on the first match (the last one going back): the position after a new filter
    // is that match, not yet visited.
    if (!m_matchVisited)
        m_matchPos = direction > 0 ? 0 : count - 1;
    else
        m_matchPos = (m_matchPos + direction + count) % count;
    m_matchVisited = true;
    const int line = m_matches[static_cast<size_t>(m_matchPos)];
    selectLine(line, false);
    if (line < m_editor->GetFirstVisibleLine() || line > m_editor->GetLastVisibleLine())
        m_editor->ScrollToLine(line, TextEditor::Scroll::alignMiddle);
    if (focusEditor) {
        // The focused window changes, but the filter would stay the active item and keep the keys.
        if (ImGui::GetActiveID() == ImGui::GetID("##blame_filter"))
            ImGui::ClearActiveID();
        m_editor->SetFocus();
    }
}

void BlamePanel::drawGutter(int index, float width, float height, float glyph)
{
    if (!m_blame || index >= static_cast<int>(m_blame->lines.size())) // the one line of an empty file
        return;
    const auto& l = m_blame->lines[static_cast<size_t>(index)];
    const Palette& p = theme().palette();
    const bool committed = !l.commit.isNull();
    const bool newBlock = index == 0 || m_blame->lines[static_cast<size_t>(index - 1)].commit != l.commit;
    // Stable IDs directly under the editor window ("###blame_line_4"), not under the editor's per-line ID.
    // The first line of a block carries what it shows as its label.
    ImGui::PushOverrideID(ImGui::GetCurrentWindow()->ID);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    m_idPrefixRange = ImVec2(pos.x, pos.x + static_cast<float>(kIdPrefixLength) * glyph); // the same on every line
    const ImVec2 size(std::max(1.0f, width), height);
    const std::string id = committed ? l.commit.shortHex(kShortIdLength) : std::string(kNotCommitted);
    const std::string label = (newBlock ? id : std::string()) + "###blame_line_" + std::to_string(l.lineNo);
    ImGui::InvisibleButton(label.c_str(), size);
    if (m_menuPending > 0 && index == m_menuLine) {
        // Alt+Space: the menu of the cursor line, below the bottom-left of its gutter.
        m_menuPending = 0;
        openPopupBelowItem(kMenuId);
    }
    // Where the rows are on the screen, for a drag over them.
    m_rowTop = pos.y - static_cast<float>(index) * height;
    m_rowHeight = height;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectLine(index, ImGui::GetIO().KeyShift);
        m_dragging = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_menuLine = index;
        m_menuPending = 0;
        ImGui::OpenPopupEx(kMenuId);
    }
    // The tooltip is on the gutter only: over the code it would be in the way of selecting text.
    if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        if (committed)
            // The gutter's IDs are the same in every blame: the revision counts the loaded blames.
            idTooltip(l.commit.hex(),
                cachedTooltipText(ImGui::GetItemID(), m_blameRevision, [&] {
                    return l.summary + "\n" + l.author + ", " + core::formatTime(l.time) + "\n" + l.origPath + ":"
                        + std::to_string(l.origLine);
                }));
        else
            tooltip("Not committed yet");
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Every second change block has the alternate row background, and a block starts under a separator.
    // A selected line has only the selection colour, which may be translucent (the light theme).
    const bool selected = index >= m_selFirst && index <= m_selLast;
    if (m_parity[static_cast<size_t>(index)] && !selected)
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
    if (selected)
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), m_editor->GetPalette().get(TextEditor::Color::selection)); // one bar with the code's selection
    if (newBlock && index > 0) {
        const float y = std::floor(pos.y);
        dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + 1.0f), ImGui::GetColorU32(ImGuiCol_Separator));
    }
    if (newBlock && !committed) {
        dl->AddText(pos, p.unstaged, kNotCommitted);
    } else if (newBlock) {
        // The ID is drawn split into its highlighted prefix and the dimmed rest.
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        const float authorAt = static_cast<float>(kShortIdLength + 2) * glyph;
        drawDimRange(pos, id.c_str(), kIdPrefixLength, kShortIdLength);
        dl->AddText(ImVec2(pos.x + authorAt, pos.y), text, fitText(l.author, m_authorWidth).c_str());
        dl->AddText(ImVec2(pos.x + authorAt + m_authorWidth + 2 * glyph, pos.y), text, core::formatTime(l.time).c_str());
    }
    ImGui::PopID();
}

std::string BlamePanel::matchText() const
{
    if (m_matches.empty())
        return "No matches";
    return std::to_string(m_matchPos + 1) + " of " + std::to_string(m_matches.size());
}

int BlamePanel::cursorLine() const { return m_editor->GetMainCursorPosition().line; }
bool BlamePanel::hasSelection() const { return m_editor->AnyCursorHasSelection(); }
std::string BlamePanel::selectedText() const { return m_editor->mainSelectionText(); }
int BlamePanel::editorLines() const { return m_editor->GetLineCount(); }
std::string BlamePanel::languageName() const { return m_editor->GetLanguageName(); }
std::string BlamePanel::text() const { return m_editor->GetText(); }
int BlamePanel::firstVisibleLine() const { return m_editor->GetFirstVisibleLine(); }
int BlamePanel::lastVisibleLine() const { return m_editor->GetLastVisibleLine(); }
bool BlamePanel::usesThemePalette() const { return m_editor->GetPalette() == editorPalette(); }

void BlamePanel::publishInfoOverride(bool shown)
{
    std::optional<Selection> want;
    if (shown && m_blame && !m_loading && m_selFirst >= 0 && m_selLine >= 0
        && m_selLine < static_cast<int>(m_blame->lines.size())) {
        const core::Oid& commit = m_blame->lines[static_cast<size_t>(m_selLine)].commit;
        want = commit.isNull() ? Selection{SelKind::WorkingTree, {}, -1} : Selection{SelKind::Commit, commit, -1};
    }
    if (want == m_published)
        return;
    m_published = want;
    m_session.setInfoOverride(want);
}

void BlamePanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Blame, open)) {
        ImGui::End();
        m_dragging = false;
        publishInfoOverride(false);
        return;
    }
    // Mouse back/forward buttons while hovering the panel.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton(3)))
            back();
        if (ImGui::IsMouseClicked(ImGuiMouseButton(4)))
            forward();
    }
    ImGui::BeginDisabled(!canGoBack());
    if (ImGui::Button(ICON_MS_ARROW_BACK "###blame_back"))
        back();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canGoForward());
    if (ImGui::Button(ICON_MS_ARROW_FORWARD "###blame_fwd"))
        forward();
    ImGui::EndDisabled();
    // The label and "loading..." come first; the filter takes the rest of the row (wrapping to its
    // own line when the label leaves too little).
    if (m_pos >= 0) {
        const auto& q = m_history[static_cast<size_t>(m_pos)];
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s at ", q.path.c_str());
        ImGui::SameLine(0, 0);
        if (q.commit.isNull())
            ImGui::TextUnformatted("working tree");
        else
            shortIdText(q.commit.hex());
    }
    if (m_loading) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("loading...");
    }
    const float filterMin = ImGui::GetFontSize() * 8;
    // "n of m" after the field (the width is that of the text shown last frame).
    const std::string count = m_blame && !m_filter.empty() ? matchText() : std::string();
    const float countWidth = count.empty() ? 0.0f : ImGui::CalcTextSize(count.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    sameLineIfFits(filterMin + countWidth);
    ImGui::SetNextItemWidth(-std::max(countWidth, FLT_MIN));
    // Ctrl+F is the filter's: the editor would open its own find window (Ctrl+Shift+F and Ctrl+G only act on
    // a text searched for there, so they do nothing here).
    if (hotkey(ImGuiMod_Ctrl | ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    // Enter keeps the field active, so it can be pressed again for the next match.
    ImGuiIO& io = ImGui::GetIO();
    const bool keepActive = io.ConfigInputTextEnterKeepActive;
    io.ConfigInputTextEnterKeepActive = true;
    const bool enter = ImGui::InputTextWithHint("##blame_filter", ICON_MS_SEARCH " Filter", &m_filter, ImGuiInputTextFlags_EnterReturnsTrue);
    io.ConfigInputTextEnterKeepActive = keepActive;
    if (!m_blame) {
        ImGui::TextDisabled("Use \"Blame file\" on a file to see who changed each line.");
        ImGui::End();
        publishInfoOverride(false);
        return;
    }
    if (m_paletteTheme != static_cast<int>(theme().theme())) {
        m_paletteTheme = static_cast<int>(theme().theme());
        m_editor->SetPalette(editorPalette());
        m_marksDirty = true;
    }
    if (m_marksDirty || m_filter != m_filterApplied)
        applyFilter(m_filter != m_filterApplied);
    if (enter)
        stepMatch(1, false);
    if (!m_filter.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", matchText().c_str());
    }
    // The panel's keys come before the editor is rendered: hotkey() locks its key for the frame, so the
    // editor, which reads the keys raw, does not act on it too.
    // Esc drops the selection (and with it the change shown in Change information).
    if (m_selFirst >= 0 && hotkey(ImGuiKey_Escape))
        clearSelection();
    // F3 and Shift+F3 step through the matches, also from the filter field.
    const ImGuiInputFlags stepFlags = ImGuiInputFlags_RouteFocused | ImGuiInputFlags_Repeat;
    if (hotkey(ImGuiKey_F3, stepFlags))
        stepMatch(1, true);
    if (hotkey(ImGuiMod_Shift | ImGuiKey_F3, stepFlags))
        stepMatch(-1, true);
    // Down moves the keyboard from the panel's buttons into the code; without a selection the cursor
    // line (the first of a fresh blame) is selected. The key is the panel's for that press, so the navigation does not move too. Then the
    // arrows are the editor's.
    if (!m_editorFocused && !m_blame->lines.empty() && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()
        && hotkey(ImGuiKey_DownArrow)) {
        m_editor->SetFocus();
        if (m_selFirst < 0) {
            const int line = std::min(cursorLine(), static_cast<int>(m_blame->lines.size()) - 1);
            selectLine(line, false);
            if (line < m_editor->GetFirstVisibleLine() || line > m_editor->GetLastVisibleLine())
                m_editor->ScrollToLine(line, TextEditor::Scroll::alignMiddle);
        }
    }
    // Alt+Space opens the menu of the cursor line, as a right click on its gutter does (the menu is opened
    // by the gutter, which is drawn by the editor below).
    if (m_editorFocused && m_selFirst >= 0 && hotkey(ImGuiMod_Alt | ImGuiKey_Space)) {
        m_menuPending = 3; // frames the gutter of a line scrolled out of view has to be drawn
        m_menuLine = std::min(cursorLine(), static_cast<int>(m_blame->lines.size()) - 1);
        if (m_menuLine < m_editor->GetFirstVisibleLine() || m_menuLine > m_editor->GetLastVisibleLine())
            m_editor->ScrollToLine(m_menuLine, TextEditor::Scroll::alignMiddle);
    }
    // The editor cuts on Ctrl+X and Shift+Delete even though it is read-only (lines would vanish and the
    // gutter would name the wrong changes): here a cut is a copy. Repeat keeps a held key locked on its
    // repeat frames too. Elsewhere in the panel the keys do nothing.
    const ImGuiInputFlags cutFlags = ImGuiInputFlags_RouteFocused | ImGuiInputFlags_Repeat;
    const bool cut = hotkey(ImGuiMod_Ctrl | ImGuiKey_X, cutFlags);
    if ((cut || hotkey(ImGuiMod_Shift | ImGuiKey_Delete, cutFlags)) && m_editorFocused)
        m_editor->Copy();
    if (m_blame->truncated)
        ImGui::TextDisabled("Large file: only the first lines are blamed.");
    ImGui::PushFont(theme().monoFont(), 0.0f);
    if (m_gutterDirty || ImGui::GetFontSize() != m_gutterFont || ImGui::GetFont() != m_gutterFace)
        updateGutter(ImGui::GetFontSize());
    m_editor->Render("##blame_editor", ImVec2(0, 0));
    m_viewDrawn = true;
    m_editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsWindowFocused();
    followGutterDrag();
    if (m_menuPending > 0)
        --m_menuPending;
    followEditor();
    // The menu of a line's gutter (the editor opens its own over the code and the line numbers).
    if (ImGui::BeginPopupEx(kMenuId, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoSavedSettings)) {
        drawLineMenuItems(m_menuLine, true);
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    ImGui::End();
    publishInfoOverride(true);
}

} // namespace ggui
