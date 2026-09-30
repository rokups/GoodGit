#include "panels/DiffPanel.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/PatchBuilder.hpp"
#include "util/Ui.hpp"
#include "shell/Widgets.hpp"
#include <limits>
#include <IconsMaterialSymbols.h>

#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace ggui {

namespace fs = std::filesystem;

namespace {

const TextEditor::Language* languageFor(const std::string& path)
{
    using L = TextEditor::Language;
    static const std::pair<const char*, const L* (*)()> kByExtension[] = {
        {".c", L::C},        {".cc", L::Cpp},     {".cpp", L::Cpp},     {".cxx", L::Cpp},        {".h", L::Cpp},
        {".hh", L::Cpp},     {".hpp", L::Cpp},    {".hxx", L::Cpp},     {".inl", L::Cpp},        {".cs", L::Cs},
        {".lua", L::Lua},    {".py", L::Python},  {".glsl", L::Glsl},   {".vert", L::Glsl},      {".frag", L::Glsl},
        {".hlsl", L::Hlsl},  {".json", L::Json},  {".md", L::Markdown}, {".markdown", L::Markdown}, {".sql", L::Sql},
    };
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [e, language] : kByExtension)
        if (ext == e)
            return language();
    return nullptr;
}

std::string stripCr(const std::string& text)
{
    return !text.empty() && text.back() == '\r' ? text.substr(0, text.size() - 1) : text;
}

ImU32 withAlpha(ImU32 color, int alpha) { return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT); }

// Read-only diff editor. Unlike a plain TextEditor it does not take the keyboard focus when it
// first appears: diffs show up as a side effect of clicks elsewhere (and would close their menus).
class DiffEditor : public TextEditor {
public:
    DiffEditor() { focusOnEditor = false; }

    // Sets the text with syntax colors, the given lines drawn dimmed instead. The colors are
    // computed here (not on the next render, which would overwrite the dimming).
    void setContent(const std::string& text, const Language* lang, const std::vector<int>& dimmed)
    {
        SetLanguage(lang);
        SetText(text);
        for (int l : dimmed)
            if (l >= 0 && l < static_cast<int>(document.size()))
                for (auto& glyph : document[static_cast<size_t>(l)])
                    glyph.color = Color::whitespace; // recolored to the theme's dim color (whitespace is not drawn)
        languageChanged = false; // already colorized by SetText
    }
};

// Popup shared by the gutter handles (the editor's own text menu shows the same items).
const ImGuiID kMenuId = ImHashStr("##diff_line_menu");

std::string imageText(const core::DiffFile& f)
{
    auto one = [](const std::string& dims, std::uint64_t size) {
        if (size == 0 && dims.empty())
            return std::string("(none)");
        return (dims.empty() ? std::string("?") : dims) + " (" + std::to_string(size) + " bytes)";
    };
    return one(f.oldImage, f.oldSize) + " \xe2\x86\x92 " + one(f.newImage, f.newSize);
}

std::string modeText(std::uint32_t mode)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06o", mode);
    return buf;
}

} // namespace

DiffPanel::DiffPanel(Session& session) : m_session(session) { }
DiffPanel::~DiffPanel() = default;

core::DiffQuery DiffPanel::queryFor(const Selection& sel, const FileRow& row, const CompareTarget& compare,
    const core::SnapshotPtr&)
{
    core::DiffQuery q;
    q.path = row.path;
    switch (row.group) {
    case FileGroup::Commit:
        if (compare.kind == CompareTarget::Rev) {
            q.kind = core::DiffKind::Commits;
            q.against = compare.rev;
            q.b = sel.id;
        } else if (compare.kind == CompareTarget::WorkTree) {
            q.kind = core::DiffKind::WorktreeCommit;
            q.b = sel.id;
        } else {
            q.kind = core::DiffKind::Commit;
            q.a = sel.id;
        }
        break;
    case FileGroup::Staged: q.kind = core::DiffKind::Staged; break;
    case FileGroup::Unstaged:
    case FileGroup::Untracked:
    case FileGroup::Conflicted: q.kind = core::DiffKind::Unstaged; break;
    case FileGroup::StashWorktree: q.kind = core::DiffKind::StashWorktree; q.a = sel.id; break;
    case FileGroup::StashIndex: q.kind = core::DiffKind::StashIndex; q.a = sel.id; break;
    case FileGroup::StashUntracked: q.kind = core::DiffKind::StashUntracked; q.a = sel.id; break;
    }
    return q;
}

bool DiffPanel::canCompare() const
{
    // A commit's or stash's file (the working tree and index already compare with HEAD's side);
    // not when the Changes panel already compares the whole commit.
    return m_file && (m_selection.kind == SelKind::Commit || m_selection.kind == SelKind::Stash)
        && m_compare.kind == CompareTarget::None;
}

void DiffPanel::onSelection(const Selection& sel)
{
    m_selection = sel;
    clear();
}

void DiffPanel::clear()
{
    m_file.reset();
    m_diff.reset();
    m_rows.clear();
    m_gapShown.clear();
    m_anchorRow = -1;
    m_full = false;
    m_fileCompareText.clear();
    m_fileCompare = {};
    m_loading = false;
    m_request = 0; // a diff still on its way is for the old selection
    m_viewsDirty = true;
}

void DiffPanel::showFile(const Selection& sel, const FileRow& row, const CompareTarget& compare)
{
    const bool sameFile = m_file && m_file->key() == row.key() && m_selection == sel;
    m_selection = sel;
    m_file = row;
    m_compare = compare;
    if (!sameFile) {
        m_gapShown.clear();
        m_anchorRow = -1;
        m_full = false;
        m_fileCompareText.clear();
        m_fileCompare = {};
        m_conflictView = 0;
        m_termView = 0;
    }
    request();
}

void DiffPanel::refreshIfShowing()
{
    if (m_file)
        request();
}

DiffPanel::StagingMode DiffPanel::stagingMode() const
{
    // Only asked while a diff is shown (so there is a file).
    if (m_fileCompare.kind != CompareTarget::None)
        return StagingMode::None;
    if (m_diff->query.kind == core::DiffKind::Unstaged
        && (m_file->group == FileGroup::Unstaged || m_file->group == FileGroup::Untracked))
        return StagingMode::Unstaged;
    if (m_diff->query.kind == core::DiffKind::Staged && m_file->group == FileGroup::Staged)
        return StagingMode::Staged;
    return StagingMode::None;
}

LineSet DiffPanel::selectedLines() const
{
    LineSet set;
    for (int i : selectedRows()) {
        const Row& r = m_rows[static_cast<size_t>(i)];
        if (r.kind == Row::Line)
            set.emplace(r.hunk, r.line);
        else if (r.kind == Row::Hunk)
            for (const auto& l : hunkLines(m_diff->files.front(), r.hunk))
                set.insert(l);
    }
    return set;
}

std::vector<int> DiffPanel::selectedRows() const
{
    std::vector<int> rows;
    const View* v = m_active; // set once the views are built
    const auto sel = v->editor->GetMainCursorSelection();
    if (sel.start.line == sel.end.line && sel.start.column == sel.end.column)
        return rows;
    int a = sel.start.line, b = sel.end.line;
    if (sel.end.column == 0 && b > a)
        --b;
    for (int i = a; i <= b && i < static_cast<int>(v->lines.size()); ++i) {
        const EditorLine& l = v->lines[static_cast<size_t>(i)];
        if ((l.kind == EditorLine::Line || l.kind == EditorLine::Hunk) && (rows.empty() || rows.back() != l.row))
            rows.push_back(l.row);
    }
    return rows;
}

void DiffPanel::applyLines(const LineSet& lines, StagingAction action)
{
    const core::DiffFile& f = m_diff->files.front();
    auto& actions = m_session.actions();
    switch (action) {
    case StagingAction::Stage:
        actions.applyPatch("stage lines of " + f.path(), buildPatch(f, lines, false), true, false);
        break;
    case StagingAction::Unstage:
        actions.applyPatch("unstage lines of " + f.path(), buildPatch(f, lines, true), true, true);
        break;
    case StagingAction::Discard:
        actions.applyPatch("discard lines of " + f.path(), buildPatch(f, lines, true), false, true);
        break;
    }
    m_anchorRow = -1;
}

void DiffPanel::request()
{
    if (!m_file)
        return;
    const auto& settings = m_session.app().settings().data();
    core::DiffQuery q = queryFor(m_selection, *m_file, m_compare, m_session.snapshot());
    if (m_fileCompare.kind != CompareTarget::None && canCompare()) {
        // This file of the commit (or stash) against the revision or the working tree.
        q.a = {};
        q.b = m_selection.id;
        q.against.clear();
        if (m_fileCompare.kind == CompareTarget::WorkTree) {
            q.kind = core::DiffKind::WorktreeCommit;
        } else {
            q.kind = core::DiffKind::Commits;
            q.against = m_fileCompare.rev;
        }
    }
    q.context = settings.diffContext;
    q.whitespace = static_cast<core::Whitespace>(settings.diffWhitespace);
    q.full = m_full;
    if (m_termView > 0 && m_termView <= termSides()) {
        q.kind = core::DiffKind::Term;
        q.a = m_selection.kind == SelKind::Commit ? m_selection.id : core::Oid();
        q.stageB = m_termView - 1;
    }
    if (m_file->group == FileGroup::Conflicted && !m_file->firstClass && m_conflictView > 0) {
        static const int stages[4][2] = {{0, 0}, {1, 2}, {1, 3}, {2, 3}};
        q.kind = core::DiffKind::Stages;
        q.stageA = stages[m_conflictView][0];
        q.stageB = stages[m_conflictView][1];
    }
    m_loading = true;
    m_request = m_session.engine().diff(q, kSlot);
}

void DiffPanel::onDiff(const core::DiffEvent& event)
{
    if (event.request != m_request)
        return; // stale
    m_loading = false;
    const bool newFile = !m_diff || m_diff->files.empty() || event.diff->files.empty()
        || m_diff->files[0].path() != event.diff->files[0].path();
    m_diff = event.diff;
    m_resetScroll = m_resetScroll || newFile;
    buildRows();
    m_viewsDirty = true;
}

void DiffPanel::buildRows()
{
    m_rows.clear();
    if (m_diff->files.empty())
        return;
    const core::DiffFile& f = m_diff->files.front();
    const int total = f.newText ? static_cast<int>(f.newText->size()) : -1;
    int prevNewEnd = 0; // last new-side line shown (1-based)
    int prevOldEnd = 0;
    int gapIndex = 0;
    auto addGap = [&](int newFrom, int newTo, int oldOffset) {
        if (total < 0 || newTo < newFrom)
            return;
        Row g;
        g.kind = Row::Gap;
        g.gap = gapIndex;
        g.gapStart = newFrom;
        g.gapCount = newTo - newFrom + 1;
        g.oldOffset = oldOffset;
        m_rows.push_back(g);
        ++gapIndex;
    };
    for (size_t h = 0; h < f.hunks.size(); ++h) {
        const auto& hunk = f.hunks[h];
        const int newStart = hunk.newLines == 0 ? hunk.newStart + 1 : hunk.newStart;
        addGap(prevNewEnd + 1, newStart - 1, prevOldEnd - prevNewEnd);
        m_rows.push_back(Row{Row::Hunk, static_cast<int>(h)});
        for (size_t l = 0; l < hunk.lines.size(); ++l)
            m_rows.push_back(Row{Row::Line, static_cast<int>(h), static_cast<int>(l)});
        prevNewEnd = hunk.newStart + hunk.newLines - (hunk.newLines == 0 ? 0 : 1);
        prevOldEnd = hunk.oldStart + hunk.oldLines - (hunk.oldLines == 0 ? 0 : 1);
    }
    if (!f.hunks.empty() && total >= 0)
        addGap(prevNewEnd + 1, total, prevOldEnd - prevNewEnd);
}

std::string DiffPanel::selectedText() const
{
    // Only code: hunk rows, the "N unchanged lines" placeholders and side-by-side fillers are
    // left out, whatever the selection covers.
    const View* v = m_active;
    if (!v)
        return {};
    const TextEditor& e = *v->editor;
    std::string out;
    for (size_t c = 0; c < e.GetNumberOfCursors(); ++c) {
        const auto sel = e.GetCursorSelection(c);
        if (sel.start.line == sel.end.line && sel.start.column == sel.end.column)
            continue;
        for (int i = sel.start.line; i <= sel.end.line && i < static_cast<int>(v->lines.size()); ++i) {
            if (i == sel.end.line && i > sel.start.line && sel.end.column == 0)
                break; // the selection stops at the start of this line
            const EditorLine::Kind kind = v->lines[static_cast<size_t>(i)].kind;
            if (kind != EditorLine::Line && kind != EditorLine::GapLine)
                continue;
            const int from = i == sel.start.line ? sel.start.column : 0;
            const int to = i == sel.end.line ? sel.end.column : std::numeric_limits<int>::max();
            out += e.GetSectionText(i, from, i, to);
            if (i < sel.end.line)
                out.push_back('\n');
        }
    }
    return out;
}

void DiffPanel::renderEditor(View& v, const char* id, float width)
{
    // The editor copies its own selection on Ctrl+C / Ctrl+Insert: replace that with the code only.
    const ImGuiIO& io = ImGui::GetIO();
    const bool copyKey = io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_C) || ImGui::IsKeyPressed(ImGuiKey_Insert));
    const std::string before = copyKey ? std::string(ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "") : std::string();
    v.editor->Render(id, ImVec2(width, 0));
    if (copyKey) {
        const char* now = ImGui::GetClipboardText();
        if (now && before != now) {
            View* saved = m_active;
            m_active = &v;
            const std::string text = selectedText();
            m_active = saved;
            ImGui::SetClipboardText((text.empty() ? before : text).c_str());
        }
    }
}

DiffPanel::View& DiffPanel::primaryView()
{
    return m_session.app().settings().data().diffSideBySide ? m_left : m_unified;
}

void DiffPanel::setupView(View& v, Side side)
{
    if (!v.editor) {
        v.editor = std::make_unique<DiffEditor>();
        v.editor->SetReadOnlyEnabled(true);
        v.editor->SetShowLineNumbersEnabled(false);
        v.editor->SetShowMatchingBrackets(false);
        v.editor->SetShowScrollbarMiniMapEnabled(false);
        v.editor->SetShowPanScrollIndicatorEnabled(false);
        v.editor->SetShowWhitespacesEnabled(false);
        v.editor->SetTextContextMenuCallback([this, &v](int line, int) {
            // Right-click on a line outside the selection selects that line first.
            if (ImGui::IsWindowAppearing()) {
                m_active = &v;
                const auto rows = selectedRows();
                if (line >= 0 && line < static_cast<int>(v.lines.size())) {
                    const int row = v.lines[static_cast<size_t>(line)].row;
                    const bool inside = std::find(rows.begin(), rows.end(), row) != rows.end();
                    if (!inside && v.lines[static_cast<size_t>(line)].kind != EditorLine::Filler
                        && v.editor->GetMainCursorSelection().start.line == v.editor->GetMainCursorSelection().end.line)
                        v.editor->SelectLine(line);
                }
            }
            drawMenuItems();
        });
        v.editor->SetLineDecorator(side == Side::Unified ? -14.0f : -9.0f,
            [this, &v](TextEditor::Decorator& d) { drawGutter(v, d.line, d.width, d.height); });
    }
    v.side = side;
    v.lines.clear();
    v.firstLine.assign(m_rows.size(), -1);
    v.lastLine.assign(m_rows.size(), -1);
}

void DiffPanel::finishView(View& v, const std::string& text)
{
    // A final newline keeps whole-line selections (and copies) of the last line complete.
    // Hunk rows (function context) are drawn dimmed.
    std::vector<int> hunkRows;
    for (size_t i = 0; i < v.lines.size(); ++i)
        if (v.lines[i].kind == EditorLine::Hunk)
            hunkRows.push_back(static_cast<int>(i));
    static_cast<DiffEditor&>(*v.editor).setContent(text + "\n", languageFor(m_file->path), hunkRows);
    v.editor->ClearMarkers();
    const Palette& p = theme().palette();
    for (size_t i = 0; i < v.lines.size(); ++i) {
        const EditorLine& l = v.lines[i];
        ImU32 bg = 0;
        if (l.kind == EditorLine::Hunk)
            bg = withAlpha(p.hunkHeader, 40);
        else if (l.kind == EditorLine::Filler)
            bg = withAlpha(p.dim, 24);
        else if (l.origin == '+')
            bg = p.addedBg;
        else if (l.origin == '-')
            bg = p.removedBg;
        if (bg)
            v.editor->AddMarker(static_cast<int>(i), 0, bg, "", "");
    }
    for (size_t i = 0; i < v.lines.size(); ++i) {
        const int row = v.lines[i].row;
        if (row < 0)
            continue;
        auto& first = v.firstLine[static_cast<size_t>(row)];
        if (first < 0)
            first = static_cast<int>(i);
        v.lastLine[static_cast<size_t>(row)] = static_cast<int>(i);
    }
}

void DiffPanel::buildViews()
{
    m_viewsDirty = false;
    const core::DiffFile& f = m_diff->files.front();
    auto palette = theme().theme() == Theme::Light ? TextEditor::GetLightPalette() : TextEditor::GetDarkPalette();
    palette[static_cast<size_t>(TextEditor::Color::whitespace)] = theme().palette().dim; // hunk rows, dimmed
    // A gap: the lines revealed from its top, the placeholder for the rest, the lines revealed
    // from its bottom.
    auto gapLines = [&](const Row& r, auto&& emit) {
        const GapShown g = gapShown(r.gap);
        const int top = g.all ? r.gapCount : std::min(g.top, r.gapCount);
        const int bottom = g.all ? 0 : std::min(g.bottom, r.gapCount - top);
        auto line = [&](int k) {
            const int newNo = r.gapStart + k;
            EditorLine l{EditorLine::GapLine};
            l.gap = r.gap;
            l.newNo = newNo;
            l.oldNo = newNo + r.oldOffset;
            emit(l, stripCr((*f.newText)[static_cast<size_t>(newNo - 1)]));
        };
        for (int k = 0; k < top; ++k)
            line(k);
        if (top + bottom < r.gapCount) {
            EditorLine l{EditorLine::GapHidden};
            l.gap = r.gap;
            emit(l, "\xe2\x8b\xaf " + std::to_string(r.gapCount - top - bottom) + " unchanged lines");
        }
        for (int k = r.gapCount - bottom; k < r.gapCount; ++k)
            line(k);
    };

    // Unified: one line per row (gaps: revealed lines + a placeholder).
    setupView(m_unified, Side::Unified);
    std::string text;
    auto emitU = [&](EditorLine l, const std::string& t) {
        m_unified.lines.push_back(l);
        text += t;
        text.push_back('\n');
    };
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if (r.kind == Row::Gap) {
            gapLines(r, [&](EditorLine l, const std::string& t) { l.row = static_cast<int>(i); emitU(l, t); });
        } else if (r.kind == Row::Hunk) {
            // Only the function context (after the closing "@@"): no range text anywhere in the
            // editor, so it can never be selected or copied. The gutter hosts the hunk's buttons.
            EditorLine l{EditorLine::Hunk, static_cast<int>(i), r.hunk};
            const std::string& header = f.hunks[static_cast<size_t>(r.hunk)].header;
            const size_t close = header.find("@@", 2);
            std::string context = close == std::string::npos ? std::string() : header.substr(close + 2);
            const size_t b = context.find_first_not_of(" \t");
            context = b == std::string::npos ? std::string() : context.substr(b);
            emitU(l, stripCr(context));
        } else {
            const auto& hl = f.hunks[static_cast<size_t>(r.hunk)].lines[static_cast<size_t>(r.line)];
            EditorLine l{EditorLine::Line, static_cast<int>(i), r.hunk, r.line};
            l.origin = hl.origin;
            l.oldNo = hl.oldNo;
            l.newNo = hl.newNo;
            emitU(l, stripCr(hl.text));
        }
    }
    if (!text.empty())
        text.pop_back();
    m_unified.editor->SetPalette(palette);
    finishView(m_unified, text);

    // Side by side: removed lines left, added lines right, aligned with filler lines.
    setupView(m_left, Side::Left);
    setupView(m_right, Side::Right);
    std::string left, right;
    auto emitBoth = [&](const EditorLine& l, const EditorLine& r, const std::string& lt, const std::string& rt) {
        m_left.lines.push_back(l);
        m_right.lines.push_back(r);
        left += lt;
        left.push_back('\n');
        right += rt;
        right.push_back('\n');
    };
    for (size_t i = 0; i < m_rows.size();) {
        const Row& r = m_rows[i];
        if (r.kind == Row::Gap) {
            gapLines(r, [&](EditorLine l, const std::string& t) { l.row = static_cast<int>(i); emitBoth(l, l, t, t); });
            ++i;
            continue;
        }
        if (r.kind == Row::Hunk) {
            // Only code: the gap placeholders already mark what lies between hunks.
            ++i;
            continue;
        }
        const auto& lines = f.hunks[static_cast<size_t>(r.hunk)].lines;
        auto lineOf = [&](size_t row) {
            const Row& x = m_rows[row];
            const auto& hl = lines[static_cast<size_t>(x.line)];
            EditorLine l{EditorLine::Line, static_cast<int>(row), x.hunk, x.line};
            l.origin = hl.origin;
            l.oldNo = hl.oldNo;
            l.newNo = hl.newNo;
            return std::make_pair(l, stripCr(hl.text));
        };
        if (lines[static_cast<size_t>(r.line)].origin == ' ') {
            const auto [l, t] = lineOf(i);
            emitBoth(l, l, t, t);
            ++i;
            continue;
        }
        // A block of changes: '-' rows then '+' rows (in any order), paired line by line.
        std::vector<size_t> removed, added;
        size_t j = i;
        while (j < m_rows.size() && m_rows[j].kind == Row::Line && m_rows[j].hunk == r.hunk
            && lines[static_cast<size_t>(m_rows[j].line)].origin != ' ') {
            (lines[static_cast<size_t>(m_rows[j].line)].origin == '-' ? removed : added).push_back(j);
            ++j;
        }
        for (size_t k = 0; k < std::max(removed.size(), added.size()); ++k) {
            std::pair<EditorLine, std::string> a{EditorLine{EditorLine::Filler}, ""}, b{EditorLine{EditorLine::Filler}, ""};
            if (k < removed.size())
                a = lineOf(removed[k]);
            if (k < added.size())
                b = lineOf(added[k]);
            emitBoth(a.first, b.first, a.second, b.second);
        }
        i = j;
    }
    if (!left.empty()) {
        left.pop_back();
        right.pop_back();
    }
    m_left.editor->SetPalette(palette);
    m_right.editor->SetPalette(palette);
    finishView(m_left, left);
    finishView(m_right, right);
    if (!m_active)
        m_active = &primaryView();
}

void DiffPanel::selectRows(View& v, int row, bool extend)
{
    m_active = &v;
    if (!extend || m_anchorRow < 0)
        m_anchorRow = row;
    const int a = std::min(m_anchorRow, row), b = std::max(m_anchorRow, row);
    int first = -1, last = -1;
    for (int r = a; r <= b; ++r) {
        if (v.firstLine[static_cast<size_t>(r)] < 0)
            continue;
        if (first < 0)
            first = v.firstLine[static_cast<size_t>(r)];
        last = v.lastLine[static_cast<size_t>(r)];
    }
    if (first >= 0)
        v.editor->SelectLines(first, last);
}

void DiffPanel::drawGutter(View& v, int index, float width, float height)
{
    if (index >= static_cast<int>(v.lines.size())) // the editor's last line, after the final newline
        return;
    const EditorLine& l = v.lines[static_cast<size_t>(index)];
    const core::DiffFile& f = m_diff->files.front();
    const Palette& p = theme().palette();
    // Stable IDs directly under the editor window ("###line_4"), not under the editor's per-line ID.
    ImGui::PushOverrideID(ImGui::GetCurrentWindow()->ID);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool primary = v.side != Side::Right;
    const bool free = m_session.actions().busy().empty();
    auto handle = [&](const std::string& id, float w) {
        ImGui::InvisibleButton(id.c_str(), ImVec2(std::max(1.0f, w), height));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            selectRows(v, l.row, ImGui::GetIO().KeyShift);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            m_active = &v;
            const auto rows = selectedRows();
            if (std::find(rows.begin(), rows.end(), l.row) == rows.end())
                selectRows(v, l.row, false);
            ImGui::OpenPopupEx(kMenuId);
        }
    };
    char nums[40] = {};
    switch (l.kind) {
    case EditorLine::Line:
    case EditorLine::GapLine: {
        auto num = [](int n) { return n > 0 ? std::to_string(n) : std::string(); };
        const char mark = l.kind == EditorLine::GapLine ? ' ' : l.origin;
        if (v.side == Side::Unified)
            std::snprintf(nums, sizeof(nums), "%5s %5s %c", num(l.oldNo).c_str(), num(l.newNo).c_str(), mark);
        else
            std::snprintf(nums, sizeof(nums), "%5s %c", num(v.side == Side::Left ? l.oldNo : l.newNo).c_str(), mark);
        if (l.kind == EditorLine::Line)
            handle("###line_" + std::to_string(l.row), width);
        const ImU32 color = l.origin == '+' ? p.added : l.origin == '-' ? p.removed : p.lineNumber;
        dl->AddText(pos, color, nums);
        const auto& hl = l.hunk < 0 ? nullptr
            : &f.hunks[static_cast<size_t>(l.hunk)].lines[static_cast<size_t>(l.line)];
        if (hl && hl->noNewline) {
            dl->AddText(ImVec2(pos.x + width - ImGui::CalcTextSize("\\").x, pos.y), p.dim, "\\");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("No newline at end of file");
        }
        break;
    }
    case EditorLine::Hunk: {
        const StagingMode mode = stagingMode(); // hunk rows are in the unified view only
        const float button = ImGui::GetFontSize() * 1.3f;
        const int count = mode == StagingMode::Unstaged ? 2 : mode == StagingMode::Staged ? 1 : 0;
        handle("###hunk_" + std::to_string(l.hunk), width - button * static_cast<float>(count));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 0.0f));
        ImGui::BeginDisabled(!free);
        auto iconButton = [&](const char* icon, const std::string& id, const char* tip) {
            ImGui::SameLine(0, 0);
            // One text line tall, so the glyph keeps the editor line's baseline.
            const bool clicked = ImGui::Button((std::string(icon) + "###" + id).c_str(), ImVec2(button, ImGui::GetTextLineHeight()));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tip);
            return clicked;
        };
        const std::string n = std::to_string(l.hunk);
        if (mode == StagingMode::Unstaged) {
            if (iconButton(ICON_MS_ADD, "stage_hunk_" + n, "Stage hunk"))
                applyLines(hunkLines(f, l.hunk), StagingAction::Stage);
            if (iconButton(ICON_MS_UNDO, "discard_hunk_" + n, "Discard hunk"))
                applyLines(hunkLines(f, l.hunk), StagingAction::Discard);
        } else if (mode == StagingMode::Staged) {
            if (iconButton(ICON_MS_REMOVE, "unstage_hunk_" + n, "Unstage hunk"))
                applyLines(hunkLines(f, l.hunk), StagingAction::Unstage);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        break;
    }
    case EditorLine::GapHidden:
        if (primary) {
            // Two halves: more lines below the hunk above, more lines above the hunk below. The gap
            // before the first hunk has only the second; the one after the last only the first.
            const bool first = l.row == 0;
            const bool last = l.row + 1 == static_cast<int>(m_rows.size());
            const int buttons = (first ? 0 : 1) + (last ? 0 : 1);
            const float w = buttons == 2 ? (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f : width;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 0.0f));
            auto expand = [&](const char* icon, const char* id, const char* tip, int GapShown::*side) {
                if (ImGui::Button((std::string(icon) + "###" + id + std::to_string(l.gap)).c_str(),
                        ImVec2(std::max(1.0f, w), ImGui::GetTextLineHeight()))) {
                    auto& shown = m_gapShown[l.gap];
                    if (ImGui::GetIO().KeyShift)
                        shown.all = true;
                    else
                        shown.*side += 10;
                    m_viewsDirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
            };
            if (!first)
                expand(ICON_MS_KEYBOARD_ARROW_UP, "expand_up_", "Show 10 more lines below the hunk above (Shift+click: all)",
                    &GapShown::top);
            if (!first && !last)
                ImGui::SameLine();
            if (!last)
                expand(ICON_MS_KEYBOARD_ARROW_DOWN, "expand_down_", "Show 10 more lines above the hunk below (Shift+click: all)",
                    &GapShown::bottom);
            ImGui::PopStyleVar();
        }
        break;
    case EditorLine::Filler:
        break;
    }
    ImGui::PopID();
}
void DiffPanel::drawToolbar()
{
    auto& settings = m_session.app().settings();
    auto& d = settings.data();
    // The controls flow like words: one that does not fit starts the next line (the panel never
    // scrolls sideways).
    const float em = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* views[] = {"Unified", "Side by side"};
    int view = d.diffSideBySide ? 1 : 0;
    ImGui::SetNextItemWidth(comboWidth({views[0], views[1]}));
    if (ImGui::Combo("##diff_view", &view, views, 2)) {
        d.diffSideBySide = view == 1;
        settings.save();
    }
    const char* ws[] = {"Whitespace: normal", "Whitespace: ignore changes", "Whitespace: ignore all"};
    const float wsWidth = comboWidth({ws[0], ws[1], ws[2]});
    sameLineIfFits(wsWidth);
    ImGui::SetNextItemWidth(wsWidth);
    if (ImGui::Combo("##diff_ws", &d.diffWhitespace, ws, 3)) {
        settings.save();
        request();
    }
    // Context lines: a narrow number field (typing works) with its own icon -/+ (Ctrl: by 5).
    const float number = ImGui::CalcTextSize("100").x + style.FramePadding.x * 2.0f;
    const float square = ImGui::GetFrameHeight();
    sameLineIfFits(labelledWidth(number, "Context") + (style.ItemSpacing.x + square) * 2);
    ImGui::SetNextItemWidth(number);
    bool contextChanged = ImGui::InputInt("Context##diff_context", &d.diffContext, 0, 0);
    const int step = ImGui::GetIO().KeyCtrl ? 5 : 1;
    ImGui::SameLine();
    if (iconButton(ICON_MS_REMOVE, "##diff_context_dec", ImVec2(square, square))) {
        d.diffContext -= step;
        contextChanged = true;
    }
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    if (iconButton(ICON_MS_ADD, "##diff_context_inc", ImVec2(square, square))) {
        d.diffContext += step;
        contextChanged = true;
    }
    if (contextChanged) {
        d.diffContext = std::clamp(d.diffContext, 0, 100);
        settings.save();
        request();
    }
    if (const int sides = termSides(); sides > 0) {
        // First-class conflict: the raw markers, or what one side changed against the base.
        std::vector<std::string> terms{"Raw markers"};
        for (int k = 1; k <= sides; ++k)
            terms.push_back("Base \xe2\x86\x92 side " + std::to_string(k));
        float termWidth = comboWidth({terms[0].c_str()});
        for (const std::string& t : terms)
            termWidth = std::max(termWidth, comboWidth({t.c_str()}));
        sameLineIfFits(termWidth);
        ImGui::SetNextItemWidth(termWidth);
        if (ImGui::BeginCombo("##term_view", terms[static_cast<size_t>(std::clamp(m_termView, 0, sides))].c_str())) {
            for (int k = 0; k <= sides; ++k)
                if (selectable(terms[static_cast<size_t>(k)].c_str(), k == m_termView)) {
                    m_termView = k;
                    request();
                }
            ImGui::EndCombo();
        }
    }
    if (m_file && m_file->group == FileGroup::Conflicted && !m_file->firstClass) {
        const char* stageViews[] = {"Working tree", "Base \xe2\x86\x92 ours", "Base \xe2\x86\x92 theirs", "Ours \xe2\x86\x92 theirs"};
        const float stageWidth = comboWidth({stageViews[0], stageViews[1], stageViews[2], stageViews[3]});
        sameLineIfFits(stageWidth);
        ImGui::SetNextItemWidth(stageWidth);
        if (ImGui::Combo("##conflict_view", &m_conflictView, stageViews, 4))
            request();
    }
    // This file only; the Changes panel's "Compare with" switches the whole commit. It takes the
    // rest of the line (at least 6 em), leaving room for "loading..." while it shows.
    const float loading = m_loading ? ImGui::CalcTextSize("loading...").x + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    sameLineIfFits(em * 6 + loading);
    ImGui::BeginDisabled(!canCompare());
    ImGui::SetNextItemWidth(std::max(em * 6, ImGui::GetContentRegionAvail().x - loading));
    if (compareWithField("##diff_compare_with", m_fileCompareText)) {
        const CompareTarget target = CompareTarget::parse(m_fileCompareText);
        if (target != m_fileCompare) {
            m_fileCompare = target;
            m_gapShown.clear();
            request();
        }
    }
    ImGui::EndDisabled();
    if (m_loading) {
        ImGui::SameLine();
        ImGui::TextDisabled("loading...");
    }
}

void DiffPanel::drawPlaceholder(const core::DiffFile& f)
{
    const Palette& p = theme().palette();
    auto info = [](const std::string& text, const char* id) { plainText((text + "###" + id).c_str()); };
    if (f.oldMode && f.newMode && f.oldMode != f.newMode) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.hunkHeader);
        info("Mode changed " + modeText(f.oldMode) + " \xe2\x86\x92 " + modeText(f.newMode), "diff_mode");
        ImGui::PopStyleColor();
    }
    if (f.submodule) {
        info(std::string(ICON_MS_ACCOUNT_TREE " Submodule ") + f.path() + ": "
                + (f.oldId.isNull() ? std::string("(none)") : f.oldId.shortHex(10)) + " \xe2\x86\x92 "
                + (f.newId.isNull() ? std::string("(none)") : f.newId.shortHex(10)),
            "diff_submodule");
    } else if (f.binary && f.image) {
        info(std::string(ICON_MS_DATASET " Image ") + f.path() + ": " + imageText(f), "diff_image");
    } else if (f.binary) {
        info(std::string(ICON_MS_DESCRIPTION " Binary file ") + f.path() + ": " + std::to_string(f.oldSize)
                + " \xe2\x86\x92 " + std::to_string(f.newSize) + " bytes",
            "diff_binary");
    }
    if (f.truncated) {
        ImGui::TextDisabled("Large diff: only the first lines are shown.");
        ImGui::SameLine();
        if (smallButton(ICON_MS_DOWNLOAD, "Load full diff##load_full")) {
            m_full = true;
            request();
        }
    }
}

void DiffPanel::drawMenuItems()
{
    const std::string text = selectedText();
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy", "Ctrl+C", false, !text.empty()))
        ImGui::SetClipboardText(text.c_str());
    if (menuItem(ICON_MS_PERSON_SEARCH, "Blame file", nullptr, false, m_file.has_value())) {
        core::Oid at;
        if (m_selection.kind == SelKind::Commit)
            at = m_selection.id;
        m_session.blameFile(m_file->path, at);
    }
    const StagingMode mode = stagingMode();
    const bool free = m_session.actions().busy().empty();
    const LineSet lines = selectedLines();
    bool changes = false;
    LineSet hunks; // every line of the hunks the selection touches
    for (const auto& [h, l] : lines) {
        changes = changes || m_diff->files.front().hunks[static_cast<size_t>(h)].lines[static_cast<size_t>(l)].origin != ' ';
        for (const auto& hl : hunkLines(m_diff->files.front(), h))
            hunks.insert(hl);
    }
    if (mode == StagingMode::Unstaged) {
        ImGui::Separator();
        if (menuItem(ICON_MS_ADD, "Stage line(s)", nullptr, false, free && changes))
            applyLines(lines, StagingAction::Stage);
        if (menuItem(ICON_MS_UNDO, "Discard line(s)", nullptr, false, free && changes))
            applyLines(lines, StagingAction::Discard);
        if (menuItem(ICON_MS_ADD, "Stage hunk(s)", nullptr, false, free && !hunks.empty()))
            applyLines(hunks, StagingAction::Stage);
        if (menuItem(ICON_MS_UNDO, "Discard hunk(s)", nullptr, false, free && !hunks.empty()))
            applyLines(hunks, StagingAction::Discard);
    } else if (mode == StagingMode::Staged) {
        ImGui::Separator();
        if (menuItem(ICON_MS_REMOVE, "Unstage line(s)", nullptr, false, free && changes))
            applyLines(lines, StagingAction::Unstage);
        if (menuItem(ICON_MS_REMOVE, "Unstage hunk(s)", nullptr, false, free && !hunks.empty()))
            applyLines(hunks, StagingAction::Unstage);
    }
    // History editing on the selected lines of a commit's change.
    if (m_selection.kind == SelKind::Commit && m_diff->query.kind == core::DiffKind::Commit) {
        ImGui::Separator();
        const std::string patch = changes ? buildPatch(m_diff->files.front(), lines, false) : std::string();
        const bool can = free && !patch.empty();
        const core::Oid id = m_selection.id;
        auto& actions = m_session.actions();
        if (menuItem(ICON_MS_ARROW_UPWARD, "Move line(s) to parent", nullptr, false, can))
            actions.moveChanges(id, Actions::MoveTo::Parent, {}, patch);
        if (menuItem(ICON_MS_ARROW_DOWNWARD, "Move line(s) to child", nullptr, false, can))
            actions.moveChanges(id, Actions::MoveTo::Child, {}, patch);
        if (menuItem(ICON_MS_MY_LOCATION, "Move line(s) to active commit", nullptr, false, can))
            actions.moveChanges(id, Actions::MoveTo::Active, {}, patch);
        if (menuItem(ICON_MS_DRIVE_FILE_MOVE, "Move line(s) to working tree", nullptr, false, can))
            actions.moveChanges(id, Actions::MoveTo::WorkingTree, {}, patch);
        if (menuItem(ICON_MS_UNDO, "Revert line(s)", nullptr, false, can))
            actions.moveChanges(id, Actions::MoveTo::Revert, {}, patch);
    }
}

void DiffPanel::drawUnified()
{
    if (m_resetScroll) {
        m_unified.editor->ScrollToLine(0, TextEditor::Scroll::alignTop);
        m_resetScroll = false;
    }
    ImGui::PushFont(theme().monoFont(), 0.0f);
    renderEditor(m_unified, "##diff_body", 0.0f);
    ImGui::PopFont();
}

void DiffPanel::drawSideBySide()
{
    if (m_resetScroll) {
        m_left.editor->ScrollToLine(0, TextEditor::Scroll::alignTop);
        m_right.editor->ScrollToLine(0, TextEditor::Scroll::alignTop);
        m_resetScroll = false;
    }
    ImGui::BeginChild("##diff_body", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushFont(theme().monoFont(), 0.0f);
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    renderEditor(m_left, "##sbs_left", half);
    ImGuiWindow* leftWindow = parent->DC.ChildWindows.back();
    ImGui::SameLine();
    renderEditor(m_right, "##sbs_right", 0.0f);
    ImGuiWindow* rightWindow = parent->DC.ChildWindows.back();
    ImGui::PopFont();
    // Keep both sides on the same lines: whichever side scrolled drives the other.
    float target = -1.0f;
    if (leftWindow->Scroll.y != m_syncedScroll)
        target = leftWindow->Scroll.y;
    else if (rightWindow->Scroll.y != m_syncedScroll)
        target = rightWindow->Scroll.y;
    if (target >= 0.0f) {
        ImGui::SetScrollY(leftWindow, target);
        ImGui::SetScrollY(rightWindow, target);
        m_syncedScroll = target;
    }
    ImGui::EndChild();
}

int DiffPanel::termSides() const
{
    if (!m_file)
        return 0;
    if (m_file->firstClass)
        return m_file->sides;
    if (m_selection.kind == SelKind::Commit)
        if (const ConflictList* c = m_session.conflictsOf(m_selection.id))
            for (const auto& [path, sides] : *c)
                if (path == m_file->path)
                    return sides;
    return 0;
}

std::string DiffPanel::languageName() const
{
    if (!m_file)
        return "None";
    const TextEditor::Language* l = languageFor(m_file->path);
    return l ? l->name : "None";
}

void DiffPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Diff, open)) {
        ImGui::End();
        return;
    }
    drawToolbar();
    if (m_file) {
        std::string title = m_file->oldPath.empty() ? m_file->path : m_file->oldPath + " \xe2\x86\x92 " + m_file->path;
        if (m_selection.kind == SelKind::Stash)
            title += "  [" + std::string(groupName(m_file->group)) + " part]";
        ImGui::TextUnformatted(title.c_str());
    }
    ImGui::Separator();
    if (!m_diff || m_diff->files.empty()) {
        if (m_file && !m_loading && m_diff && !m_diff->error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme().palette().conflict);
            plainText((m_diff->error + "###diff_error").c_str());
            ImGui::PopStyleColor();
        } else if (m_file && !m_loading) {
            ImGui::TextDisabled("No differences");
        }
        else if (!m_file)
            ImGui::TextDisabled("Select a file to see its changes");
        ImGui::End();
        return;
    }
    const core::DiffFile& f = m_diff->files.front();
    drawPlaceholder(f);
    if (m_paletteTheme != static_cast<int>(theme().theme())) {
        m_paletteTheme = static_cast<int>(theme().theme());
        m_viewsDirty = true;
    }
    if (m_viewsDirty)
        buildViews();
    const bool canSideBySide = !f.binary && !f.submodule && f.oldText && f.newText;
    if (!f.binary && !f.submodule) {
        if (m_session.app().settings().data().diffSideBySide && canSideBySide)
            drawSideBySide();
        else
            drawUnified();
        if (ImGui::BeginPopupEx(kMenuId, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                    | ImGuiWindowFlags_NoSavedSettings)) {
            drawMenuItems();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

} // namespace ggui
