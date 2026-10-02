#include "panels/BlamePanel.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace ggui {

BlamePanel::BlamePanel(Session& session) : m_session(session) { }

void BlamePanel::open(const std::string& path, const core::Oid& commit, bool before, int line)
{
    core::BlameQuery q;
    q.path = path;
    q.commit = commit;
    q.beforeCommit = before;
    q.scrollToLine = line;
    if (m_pos + 1 < static_cast<int>(m_history.size()))
        m_history.resize(static_cast<size_t>(m_pos + 1));
    m_history.push_back(q);
    m_pos = static_cast<int>(m_history.size()) - 1;
    request();
}

void BlamePanel::back()
{
    if (m_pos > 0) {
        --m_pos;
        request();
    }
}

void BlamePanel::forward()
{
    if (m_pos + 1 < static_cast<int>(m_history.size())) {
        ++m_pos;
        request();
    }
}

void BlamePanel::request()
{
    if (m_pos < 0)
        return;
    m_loading = true;
    clearSelection();
    m_scrollTo = m_history[static_cast<size_t>(m_pos)].scrollToLine;
    m_request = m_session.engine().blame(m_history[static_cast<size_t>(m_pos)]);
}

void BlamePanel::onBlame(const core::BlameEvent& event)
{
    if (event.request != m_request)
        return;
    m_loading = false;
    m_blame = event.blame;
    clearSelection(); // the lines of the previous blame are gone

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

void BlamePanel::drawLineMenu(int index)
{
    if (!beginContextMenu("##blame_menu"))
        return;
    const auto& line = m_blame->lines[static_cast<size_t>(index)];
    const bool committed = !line.commit.isNull();
    if (menuItem(ICON_MS_PERSON_SEARCH, "Blame before this change", nullptr, false, committed))
        open(line.origPath, line.commit, true, line.origLine);
    if (menuItem(ICON_MS_SOURCE, "Show originating source", nullptr, false, committed))
        open(line.origPath, line.commit, false, line.origLine);
    ImGui::Separator();
    if (menuItem(ICON_MS_MY_LOCATION, "Reveal commit", nullptr, false, committed))
        m_session.revealCommit(line.commit);
    copyIdMenuItems("Copy commit ", line.commit.hex(), committed);
    ImGui::Separator();
    if (menuItem(ICON_MS_SELECT_ALL, "Select change block")) {
        blockText(index, &m_selFirst, &m_selLast);
        m_selLine = index;
    }
    if (menuItem(ICON_MS_CONTENT_COPY, "Copy change block"))
        ImGui::SetClipboardText(blockText(index, nullptr, nullptr).c_str());
    ImGui::EndPopup();
}

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
    ImGui::BeginDisabled(m_pos <= 0);
    if (ImGui::Button(ICON_MS_ARROW_BACK "###blame_back"))
        back();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_pos + 1 >= static_cast<int>(m_history.size()));
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
    sameLineIfFits(filterMin);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##blame_filter", ICON_MS_SEARCH " Filter", &m_filter);
    if (!m_blame) {
        ImGui::TextDisabled("Use \"Blame file\" on a file to see who changed each line.");
        ImGui::End();
        publishInfoOverride(false);
        return;
    }
    // Esc drops the selection (and with it the change shown in Change information).
    if (m_selFirst >= 0 && hotkey(ImGuiKey_Escape))
        clearSelection();
    if (m_blame->truncated)
        ImGui::TextDisabled("Large file: only the first lines are blamed.");
    const Palette& p = theme().palette();
    std::vector<int> visible;
    for (size_t i = 0; i < m_blame->lines.size(); ++i) {
        const auto& l = m_blame->lines[i];
        if (m_filter.empty() || containsNoCase(l.text, m_filter) || containsNoCase(l.author, m_filter)
            || containsNoCase(l.commit.hex(), m_filter) || containsNoCase(l.summary, m_filter))
            visible.push_back(static_cast<int>(i));
    }
    ImGui::PushFont(theme().monoFont(), 0.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    flattenNextTable(); // the rows are part of the panel's nav layer: the arrows walk them
    if (ImGui::BeginTable("##blame_table", 5, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Commit");
        ImGui::TableSetupColumn("Author");
        ImGui::TableSetupColumn("Date");
        ImGui::TableSetupColumn("Line");
        ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        int scrollIndex = -1;
        if (m_scrollTo > 0)
            for (size_t i = 0; i < visible.size(); ++i)
                if (m_blame->lines[static_cast<size_t>(visible[i])].lineNo == m_scrollTo) {
                    scrollIndex = static_cast<int>(i);
                    clipper.IncludeItemByIndex(scrollIndex);
                }
        while (clipper.Step()) {
            for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
                const int i = visible[static_cast<size_t>(vi)];
                const auto& l = m_blame->lines[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(("l" + std::to_string(l.lineNo)).c_str());
                const bool selected = i >= m_selFirst && i <= m_selLast && m_selFirst >= 0;
                const bool newBlock = i == 0 || m_blame->lines[static_cast<size_t>(i - 1)].commit != l.commit;
                const std::string commitText = l.commit.isNull() ? std::string("Not committed") : l.commit.shortHex(kShortIdLength);
                const std::string label = (newBlock ? commitText : std::string()) + "###blame_line_" + std::to_string(l.lineNo);
                if (l.commit.isNull())
                    ImGui::PushStyleColor(ImGuiCol_Text, p.unstaged);
                // SelectOnNav: the nav cursor (arrows) and the selection are one thing.
                // A commit ID in the label is drawn split into its highlighted prefix and the dimmed rest.
                const size_t dimFrom = newBlock && !l.commit.isNull() ? kIdPrefixLength : 0;
                const size_t dimTo = newBlock && !l.commit.isNull() ? kShortIdLength : 0;
                if (selectableDimRange(label.c_str(), dimFrom, dimTo, selected,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav)) {
                    const bool shift = (pressMods() & ImGuiMod_Shift) != 0;
                    if (pressSource() == PressSource::NavActivate && !shift) {
                        // Space / Enter on the cursor row keeps the selection (the range stays).
                        if (!selected)
                            m_selFirst = m_selLast = m_selAnchor = m_selLine = i;
                    } else if (shift && m_selFirst >= 0) {
                        // The range runs from the anchor (the last plain press) to this line.
                        const int anchor = m_selAnchor >= m_selFirst && m_selAnchor <= m_selLast ? m_selAnchor : m_selFirst;
                        m_selFirst = std::min(anchor, i);
                        m_selLast = std::max(anchor, i);
                        m_selAnchor = anchor;
                        m_selLine = i;
                    } else {
                        m_selFirst = m_selLast = m_selAnchor = m_selLine = i;
                    }
                }
                if (l.commit.isNull())
                    ImGui::PopStyleColor();
                if (vi == scrollIndex) {
                    ImGui::SetScrollHereY(0.3f);
                    m_scrollTo = 0;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    if (l.commit.isNull())
                        tooltip("Not committed yet");
                    else
                        idTooltip(l.commit.hex(),
                            l.summary + "\n" + l.author + ", " + core::formatTime(l.time) + "\n" + l.origPath + ":"
                                + std::to_string(l.origLine));
                }
                drawLineMenu(i);
                ImGui::TableSetColumnIndex(1);
                if (newBlock && !l.commit.isNull())
                    ImGui::TextUnformatted(l.author.c_str());
                ImGui::TableSetColumnIndex(2);
                if (newBlock && !l.commit.isNull())
                    ImGui::TextUnformatted(core::formatTime(l.time).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%d", l.lineNo);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(l.text.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
    ImGui::End();
    publishInfoOverride(true);
}

} // namespace ggui
