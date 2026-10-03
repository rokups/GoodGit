#include "panels/ChangesPanel.hpp"
#include "shell/Dialogs.hpp"

#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/CommitMenu.hpp"
#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"
#include "shell/Widgets.hpp"
#include <IconsMaterialSymbols.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <libgg/GitRunner.hpp>
#include <libgg/Markers.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ggui {

namespace fs = std::filesystem;

namespace {

// Side labels for the "Take side" / merge-tool menus (CONF-SIDE-LABELS): read cheaply, only
// while the menu holding them is open, from the working-tree file's first region. Empty when the
// file carries no non-default labels (defaults: "side N").
std::vector<std::string> workingTreeSideLabels(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const gg::markers::Parsed parsed = gg::markers::parse(ss.str());
    if (parsed.regions.empty())
        return {};
    std::vector<std::string> labels;
    for (size_t k = 0; k < parsed.regions.front().sides.size(); ++k) {
        const std::string& label = parsed.regions.front().sides[k].label;
        labels.push_back(label == "side " + std::to_string(k + 1) ? std::string() : label);
    }
    return labels;
}

} // namespace

CompareTarget CompareTarget::parse(const std::string& text)
{
    const std::string t = gg::trim(text);
    CompareTarget target;
    if (t.empty())
        return target;
    std::string lower;
    for (char c : t)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "work tree") {
        target.kind = WorkTree;
        return target;
    }
    target.kind = Rev;
    target.rev = t;
    return target;
}

bool compareWithField(const char* id, std::string& text)
{
    bool apply = ImGui::InputTextWithHint(id, "Compare with", &text, ImGuiInputTextFlags_EnterReturnsTrue);
    apply = apply || ImGui::IsItemDeactivatedAfterEdit();
    apply = acceptCommitDrop(text) || apply;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        tooltip("HEAD, a commit ID or ref, or Work Tree (Enter applies; empty: the parent)");
    if (beginContextMenu((std::string(id) + "_menu").c_str())) {
        for (const char* choice : {"HEAD", "Work Tree"})
            if (menuItem(ICON_MS_COMPARE_ARROWS, choice)) {
                text = choice;
                apply = true;
            }
        if (menuItem(ICON_MS_CLEAR, "Clear", nullptr, false, !text.empty())) {
            text.clear();
            apply = true;
        }
        ImGui::EndPopup();
    }
    return apply;
}

const char* groupName(FileGroup g)
{
    // In FileGroup's order.
    static const char* const kNames[] = {"Files", "Staged", "Unstaged", "Untracked", "Conflicted", "Working tree", "Index",
        "Untracked files"};
    return kNames[static_cast<size_t>(g)];
}

namespace {

// The status letter (ChangeKind's values are git's letters: A M D R C T ? U).
std::string kindIcon(core::ChangeKind k) { return std::string(1, static_cast<char>(k)); }

ImU32 kindColor(core::ChangeKind k)
{
    const Palette& p = theme().palette();
    switch (k) {
    case core::ChangeKind::Added:
    case core::ChangeKind::Copied: return p.added;
    case core::ChangeKind::Deleted: return p.removed;
    case core::ChangeKind::Untracked: return p.untracked;
    case core::ChangeKind::Conflicted: return p.conflict;
    default: return p.unstaged;
    }
}

FileRow rowFromStatus(FileGroup g, const core::StatusEntry& e)
{
    FileRow r;
    r.group = g;
    r.path = e.path;
    r.oldPath = e.oldPath;
    r.kind = e.kind;
    r.intentToAdd = e.intentToAdd;
    r.conflict = e.conflictDescription;
    r.sides = e.sides;
    r.firstClass = e.firstClass;
    r.brokenMarkerLines = e.brokenMarkerLines;
    return r;
}

std::vector<FileRow> rowsFromDiff(FileGroup g, const core::DiffResult& d)
{
    std::vector<FileRow> rows;
    for (const auto& f : d.files) {
        FileRow r;
        r.group = g;
        r.path = f.path();
        if (f.kind == core::ChangeKind::Renamed || f.kind == core::ChangeKind::Copied)
            r.oldPath = f.oldPath;
        r.kind = f.kind == core::ChangeKind::Untracked ? core::ChangeKind::Added : f.kind;
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace

ChangesPanel::ChangesPanel(Session& session) : m_session(session) { }

void ChangesPanel::onSelection(const Selection& sel)
{
    m_selection = sel;
    m_rows.clear();
    m_selected.clear();
    m_current.clear();
    m_anchor.clear();
    for (auto& p : m_stashParts)
        p.clear();
    if (sel.kind == SelKind::WorkingTree || sel.kind == SelKind::Index)
        rebuildFromStatus();
    else
        requestFiles();
}

void ChangesPanel::requestFiles()
{
    auto& engine = m_session.engine();
    const auto snap = m_session.snapshot();
    if (m_selection.kind == SelKind::Commit) {
        FileRow commitFiles;
        commitFiles.group = FileGroup::Commit;
        core::DiffQuery q = DiffPanel::queryFor(m_selection, commitFiles, m_compare, snap);
        q.path.clear();
        q.withHunks = false;
        m_filesError.clear();
        m_loading = true;
        engine.diff(q, kSlotFiles);
    } else if (m_selection.kind == SelKind::Stash) {
        core::DiffQuery q;
        q.withHunks = false;
        q.a = m_selection.id;
        q.kind = core::DiffKind::StashWorktree;
        engine.diff(q, kSlotFiles);
        q.kind = core::DiffKind::StashIndex;
        engine.diff(q, kSlotStashIndex);
        bool untracked = false;
        for (const auto& s : snap->stashes)
            if (s.commit == m_selection.id)
                untracked = s.hasUntracked;
        if (untracked) {
            q.kind = core::DiffKind::StashUntracked;
            engine.diff(q, kSlotStashUntracked);
        }
        m_loading = true;
    }
}

void ChangesPanel::rebuildFromStatus()
{
    const auto status = m_session.status();
    const std::string keepCurrent = m_current;
    m_rows.clear();
    if (!status)
        return;
    m_scanning = status->partial;
    m_everScanned = m_everScanned || status->partial;
    if (m_selection.kind == SelKind::Index) {
        for (const auto& e : status->staged)
            m_rows.push_back(rowFromStatus(FileGroup::Staged, e));
    } else {
        for (const auto& e : status->conflicted)
            m_rows.push_back(rowFromStatus(FileGroup::Conflicted, e));
        for (const auto& e : status->staged)
            m_rows.push_back(rowFromStatus(FileGroup::Staged, e));
        for (const auto& e : status->unstaged)
            m_rows.push_back(rowFromStatus(FileGroup::Unstaged, e));
        for (const auto& e : status->untracked)
            m_rows.push_back(rowFromStatus(FileGroup::Untracked, e));
    }
    // Keep selection for rows that still exist.
    std::set<std::string> keys;
    for (const auto& r : m_rows)
        keys.insert(r.key());
    std::erase_if(m_selected, [&](const std::string& k) { return !keys.count(k); });
    if (!keepCurrent.empty() && !keys.count(keepCurrent))
        m_current.clear();
    else if (!keepCurrent.empty())
        m_session.diff().refreshIfShowing();
}

void ChangesPanel::onStatus(const core::StatusPtr&)
{
    if (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index)
        rebuildFromStatus();
}

void ChangesPanel::onDiff(const core::DiffEvent& event)
{
    const auto& d = *event.diff;
    if (event.slot == kSlotCopyPatch) {
        ImGui::SetClipboardText(d.patch.c_str());
        return;
    }
    if (event.slot == kSlotSavePatch) {
        const std::string path = m_savePatchPath;
        const std::string text = d.patch;
        m_session.app().io().post([path, text]() -> std::function<void()> {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << text;
            return {};
        });
        return;
    }
    // Ignore results for an older selection.
    if (m_selection.kind == SelKind::Commit) {
        const bool matches = (d.query.kind == core::DiffKind::Commit && d.query.a == m_selection.id)
            || ((d.query.kind == core::DiffKind::Commits || d.query.kind == core::DiffKind::WorktreeCommit)
                && d.query.b == m_selection.id);
        if (!matches) // (a commit's files come in kSlotFiles)
            return;
        m_filesError = d.error;
        m_rows = rowsFromDiff(FileGroup::Commit, d);
        markConflicts();
    } else if (m_selection.kind == SelKind::Stash) {
        if (d.query.a != m_selection.id)
            return;
        const int part = event.slot == kSlotFiles ? 0 : event.slot == kSlotStashIndex ? 1 : 2;
        const FileGroup g = part == 0 ? FileGroup::StashWorktree : part == 1 ? FileGroup::StashIndex : FileGroup::StashUntracked;
        m_stashParts[part] = rowsFromDiff(g, d);
        m_rows.clear();
        for (const auto& p : m_stashParts)
            m_rows.insert(m_rows.end(), p.begin(), p.end());
    } else {
        return;
    }
    m_loading = false;
}

// Flags the commit's files that hold first-class conflicts (Readers.cpp wording); the scan may
// finish after the file list, so draw() repeats it. Not `firstClass`: the working-tree actions
// (stage, take a side, mark resolved) do not apply to a commit's rows.
void ChangesPanel::markConflicts()
{
    if (m_selection.kind != SelKind::Commit)
        return;
    const ConflictList* conflicts = m_session.conflictsOf(m_selection.id);
    for (auto& r : m_rows) {
        r.sides = 0;
        r.conflict.clear();
        if (!conflicts)
            continue;
        for (const auto& [path, sides] : *conflicts)
            if (path == r.path) {
                r.sides = sides;
                r.conflict = std::to_string(sides) + "-sided conflict";
            }
    }
}

const FileRow* ChangesPanel::current() const
{
    for (const auto& r : m_rows)
        if (r.key() == m_current)
            return &r;
    return nullptr;
}

std::vector<const FileRow*> ChangesPanel::visibleRows() const
{
    std::vector<const FileRow*> out;
    for (const auto& r : m_rows)
        if (containsNoCase(r.path, m_filter) || containsNoCase(r.oldPath, m_filter))
            out.push_back(&r);
    return out;
}

void ChangesPanel::setCurrent(const std::string& key)
{
    m_current = key;
    m_session.diff().showFile(m_selection, *current(), m_compare);
}

void ChangesPanel::moveCurrent(int direction)
{
    const auto rows = visibleRows();
    if (rows.empty())
        return;
    int pos = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i]->key() == m_current)
            pos = static_cast<int>(i);
    int next = pos < 0 ? (direction > 0 ? 0 : static_cast<int>(rows.size()) - 1)
                       : std::clamp(pos + direction, 0, static_cast<int>(rows.size()) - 1);
    const std::string key = rows[static_cast<size_t>(next)]->key();
    m_selected = {key};
    m_anchor = key;
    setCurrent(key);
}

core::DiffQuery ChangesPanel::patchQuery(const FileRow& row) const
{
    core::DiffQuery q = DiffPanel::queryFor(m_selection, row, m_compare, m_session.snapshot());
    q.context = 3;
    q.whitespace = core::Whitespace::Normal;
    return q;
}

std::vector<const FileRow*> ChangesPanel::actionRows(const FileRow& row) const
{
    std::vector<const FileRow*> out;
    if (!m_selected.count(row.key())) {
        out.push_back(&row);
        return out;
    }
    for (const auto& r : m_rows)
        if (m_selected.count(r.key()))
            out.push_back(&r);
    return out;
}

void ChangesPanel::toggleStaging(const std::vector<const FileRow*>& rows)
{
    std::vector<std::string> stage, unstage;
    for (const FileRow* r : rows) {
        if (r->group == FileGroup::Staged)
            unstage.push_back(r->path);
        else if (r->group == FileGroup::Unstaged || r->group == FileGroup::Untracked || r->group == FileGroup::Conflicted)
            stage.push_back(r->path);
    }
    auto& actions = m_session.actions();
    if (!actions.busy().empty())
        return;
    // All staged → unstage; otherwise stage what is not staged yet.
    if (stage.empty() && !unstage.empty())
        actions.unstage(unstage);
    else if (!stage.empty())
        actions.stage(stage);
}

ChangesPanel::DiscardPlan ChangesPanel::discardPlan(const std::vector<const FileRow*>& rows) const
{
    DiscardPlan plan;
    const auto status = m_session.status();
    const bool unborn = m_session.snapshot()->headUnborn;
    auto in = [](const std::vector<core::StatusEntry>& list, const std::string& path) {
        return std::any_of(list.begin(), list.end(), [&](const auto& e) { return e.path == path; });
    };
    for (const FileRow* r : rows) {
        switch (r->group) {
        case FileGroup::Staged: {
            if (status && in(status->unstaged, r->path)) {
                plan.partial = true;
                break;
            }
            StagedDiscard s{r->path, {}, false};
            if (unborn || r->kind == core::ChangeKind::Added || r->kind == core::ChangeKind::Copied)
                s.remove = true;
            else if (r->kind == core::ChangeKind::Renamed) {
                s.remove = true;
                s.oldPath = r->oldPath;
            }
            plan.staged.push_back(std::move(s));
            break;
        }
        case FileGroup::Unstaged:
            if (status && in(status->staged, r->path))
                plan.partial = true;
            else if (!r->intentToAdd)
                plan.tracked.push_back(r->path);
            break;
        case FileGroup::Untracked: plan.untracked.push_back(r->path); break;
        default: break;
        }
    }
    return plan;
}

void ChangesPanel::drawFileMenu(const FileRow& row)
{
    if (!beginContextMenu("##file_menu"))
        return;
    if (!m_selected.count(row.key())) {
        m_selected = {row.key()};
        setCurrent(row.key());
    }
    const auto snap = m_session.snapshot();
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    const bool worktree = m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index;
    const auto rows = actionRows(row);
    std::vector<std::string> stage, unstage, untracked, nativeConflicts,
        conflicts, existing;
    for (const FileRow* r : rows) {
        switch (r->group) {
        case FileGroup::Staged: unstage.push_back(r->path); break;
        case FileGroup::Unstaged:
            stage.push_back(r->path);
            break;
        case FileGroup::Untracked:
            stage.push_back(r->path);
            untracked.push_back(r->path);
            break;
        case FileGroup::Conflicted:
            conflicts.push_back(r->path);
            if (!r->firstClass)
                nativeConflicts.push_back(r->path);
            break;
        default: break;
        }
        std::error_code ec;
        if (!snap->bare && fs::exists(snap->workdir / r->path, ec))
            existing.push_back(r->path);
    }
    const bool rowExists = std::find(existing.begin(), existing.end(), row.path) != existing.end();
    if (menuItem(ICON_MS_FILE_OPEN, "Open working-copy file", nullptr, false, free && rowExists))
        actions.openInEditor(row.path);
    if (menuItem(ICON_MS_FOLDER_OPEN, "Open containing folder", nullptr, false, !snap->bare))
        openInFileManager((snap->workdir / row.path).parent_path());
    if (beginMenu(ICON_MS_CONTENT_COPY, "Copy")) {
        if (menuItem(ICON_MS_CONTENT_COPY, "Name"))
            ImGui::SetClipboardText(fs::path(row.path).filename().string().c_str());
        if (menuItem(ICON_MS_CONTENT_COPY, "Relative path"))
            ImGui::SetClipboardText(row.path.c_str());
        if (menuItem(ICON_MS_CONTENT_COPY, "Absolute path", nullptr, false, !snap->bare))
            ImGui::SetClipboardText((snap->workdir / row.path).lexically_normal().string().c_str());
        ImGui::EndMenu();
    }
    if (worktree) {
        ImGui::Separator();
        // One item, like Space: all staged → unstage; otherwise stage what is not staged yet.
        if (stage.empty() && !unstage.empty()) {
            if (menuItem(ICON_MS_REMOVE, "Unstage", "Space", false, free))
                actions.unstage(unstage);
        } else if (menuItem(ICON_MS_ADD, "Stage", "Space", false, free && !stage.empty())) {
            actions.stage(stage);
        }
        const DiscardPlan discardPlanForRows = discardPlan(rows);
        if (menuItem(ICON_MS_UNDO, "Discard...", "D", false, free && discardPlanForRows.enabled()))
            discard(discardPlanForRows);
        if (menuItem(ICON_MS_PLAYLIST_ADD, "Intent to add", nullptr, false, free && !untracked.empty()))
            actions.intentToAdd(untracked);
        ImGui::Separator();
        const bool firstClassRow = row.group == FileGroup::Conflicted && row.firstClass;
        const bool firstClassPairEligible = firstClassRow && row.sides >= 2 && conflicts.size() == 1;
        // Cheap: only while a menu that shows them is actually open (BeginMenu/BeginPopup gate).
        const std::vector<std::string> sideLabels = firstClassRow && !snap->bare
            ? workingTreeSideLabels(snap->workdir / row.path) : std::vector<std::string>();
        auto sideMenuLabel = [&](int k) {
            std::string text = "Side " + std::to_string(k + 1);
            if (static_cast<size_t>(k) < sideLabels.size() && !sideLabels[k].empty())
                text += " — " + sideLabels[k];
            return text;
        };
        if (firstClassRow && row.sides > 2) {
            if (beginMenu(ICON_MS_CALL_MERGE, "Resolve with merge tool", free && firstClassPairEligible)) {
                for (int k = 0; k + 1 < row.sides; ++k) {
                    const std::string text = "Sides " + std::to_string(k + 1) + " and " + std::to_string(k + 2)
                        + "###pair" + std::to_string(k);
                    if (menuItem(ICON_MS_CALL_MERGE, text.c_str()))
                        actions.mergeToolFirstClass(row.path, k);
                }
                ImGui::EndMenu();
            }
        } else if (menuItem(ICON_MS_CALL_MERGE, "Resolve with merge tool", nullptr, false,
                       free && (nativeConflicts.size() == 1 || (firstClassRow && row.sides == 2 && conflicts.size() == 1)))) {
            if (firstClassRow)
                actions.mergeToolFirstClass(row.path);
            else
                actions.mergeTool(nativeConflicts.front());
        }
        if (firstClassRow && beginMenu(ICON_MS_CHECK, "Take side", free)) {
            for (int k = 0; k < row.sides; ++k) {
                const std::string text = sideMenuLabel(k) + " (whole file)###wholeside" + std::to_string(k);
                if (menuItem(ICON_MS_CHECK, text.c_str()))
                    actions.takeConflictSide({row.path}, k);
            }
            ImGui::Separator();
            if (menuItem(ICON_MS_CHECK, "In one region...")) {
                Form f;
                f.title = "Take side in a region";
                f.add(Field{Field::Text, "region", "Region (1 = the first in the file)", "1"});
                f.add(Field{Field::Text, "side", "Side", "1"});
                const std::string path = row.path;
                f.buttons.push_back({"Take", [&actions, path](Form& form) {
                                         const int region = std::max(1, std::atoi(form.text("region").c_str()));
                                         const int side = std::max(1, std::atoi(form.text("side").c_str()));
                                         actions.takeConflictSide({path}, side - 1, region - 1);
                                     }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
            }
            ImGui::EndMenu();
        }
        if (menuItem(ICON_MS_CHECK, "Take ours", nullptr, false, free && !nativeConflicts.empty()))
            actions.takeSide(nativeConflicts, Side::Ours);
        if (menuItem(ICON_MS_CHECK, "Take theirs", nullptr, false, free && !nativeConflicts.empty()))
            actions.takeSide(nativeConflicts, Side::Theirs);
        if (menuItem(ICON_MS_CHECK_CIRCLE, "Mark resolved", nullptr, false, free && !conflicts.empty()))
            actions.markResolved(conflicts);
    }
    ImGui::Separator();
    if (beginMenu(ICON_MS_DIFFERENCE, "Patch")) {
        // Of the selected files in this row's group, or of this file.
        core::DiffQuery q = patchQuery(row);
        if (m_selected.size() > 1) {
            q.path.clear();
            for (const auto& r : m_rows)
                if (m_selected.count(r.key()) && r.group == row.group)
                    q.paths.push_back(r.path);
        }
        if (menuItem(ICON_MS_CONTENT_COPY, "Copy"))
            m_session.engine().diff(q, kSlotCopyPatch);
        if (menuItem(ICON_MS_SAVE, "Save...")) {
            const std::string name = fs::path(row.path).filename().string() + ".patch";
            m_session.app().pickSaveFile("Save patch", name, [this, q](const std::string& path) {
                if (path.empty())
                    return;
                m_savePatchPath = path;
                m_session.engine().diff(q, kSlotSavePatch);
            });
        }
        ImGui::EndMenu();
    }
    const bool canBlame = row.kind != core::ChangeKind::Deleted && row.group != FileGroup::StashUntracked;
    if (menuItem(ICON_MS_PERSON_SEARCH, "Blame file", nullptr, false, canBlame)) {
        core::Oid at;
        if (m_selection.kind == SelKind::Commit)
            at = m_selection.id;
        m_session.blameFile(row.path, at);
    }
    {
        // "Compare": Before is the file's old side, After its new side. With "Compare with" set, a commit's
        // file compares Before with that target (it is the left side, as in the internal diff).
        const bool menuOn = free && !snap->bare;
        const std::string id = m_selection.id.hex();
        using V = std::vector<std::string>;
        V show, beforeWt, afterWt;
        bool canMenu = true, hasBefore = true, hasAfter = true, hasShow = true;
        switch (row.group) {
        case FileGroup::Commit: {
            const auto* h = m_session.history().row(m_selection.id);
            hasBefore = !(h && h->parents.empty()); // a root commit has no parent
            show = {id + "^", id};
            beforeWt = {id + "^"};
            afterWt = {id};
            break;
        }
        case FileGroup::Staged:
            show = {"--cached", "HEAD"};
            beforeWt = {"HEAD"};
            break;
        case FileGroup::Unstaged: hasAfter = false; break; // index vs working tree: After is the working tree
        case FileGroup::StashWorktree:
            show = {id + "^", id};
            beforeWt = {id + "^"};
            afterWt = {id};
            break;
        case FileGroup::StashIndex:
            show = {id + "^1", id + "^2"};
            beforeWt = {id + "^1"};
            afterWt = {id + "^2"};
            break;
        default: canMenu = false; break;
        }
        if (canMenu && row.group == FileGroup::Commit && m_compare.kind != CompareTarget::None) {
            const bool rev = m_compare.kind == CompareTarget::Rev;
            if (menuItem(ICON_MS_OPEN_IN_NEW, "Compare", nullptr, false, menuOn && hasBefore))
                actions.externalDiff(row.path, rev ? V{m_compare.rev, id + "^"} : V{id + "^"});
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
                if (rev)
                    tooltip("Compare the file before this commit with %s", m_compare.rev.c_str());
                else
                    tooltip("Compare the file before this commit with the working tree");
            }
        } else if (beginMenu(ICON_MS_OPEN_IN_NEW, "Compare", menuOn && canMenu)) {
            if (menuItem(ICON_MS_COMPARE_ARROWS, "Show diff", nullptr, false, hasShow && hasBefore))
                actions.externalDiff(row.path, show);
            if (menuItem(ICON_MS_COMPARE_ARROWS, "\"Before\" vs Working tree", nullptr, false, hasBefore))
                actions.externalDiff(row.path, beforeWt);
            if (menuItem(ICON_MS_COMPARE_ARROWS, "\"After\" vs Working tree", nullptr, false, hasAfter))
                actions.externalDiff(row.path, afterWt);
            ImGui::EndMenu();
        }
    }
    if (m_selection.kind == SelKind::Commit && row.group == FileGroup::Commit) {
        // History editing on the commit's files (selection or this row).
        std::vector<std::string> paths;
        for (const FileRow* r : rows)
            paths.push_back(r->path);
        ImGui::Separator();
        const core::Oid id = m_selection.id;
        // HEAD has no child to take the changes: they go to the working tree instead.
        if (id == m_session.snapshot()->head) {
            if (menuItem(ICON_MS_ARROW_UPWARD, "Move to working tree", nullptr, false, free))
                actions.moveChanges(id, Actions::MoveTo::WorkingTree, paths, {});
        } else if (menuItem(ICON_MS_ARROW_UPWARD, "Move to child", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::Child, paths, {});
        if (menuItem(ICON_MS_ARROW_DOWNWARD, "Move to parent", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::Parent, paths, {});
        ImGui::Separator();
        // Shift toggles: Revert (index and working tree) / Revert and commit (a new commit on HEAD).
        const bool shift = ImGui::GetIO().KeyShift;
        if (menuItem(ICON_MS_SETTINGS_BACKUP_RESTORE, shift ? "Revert and commit" : "Revert", nullptr, false,
                free && !m_session.snapshot()->headUnborn))
            actions.revertChanges(id, paths, {}, shift);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            tooltip("%s", shift ? "A new commit on HEAD that undoes this change to the selected files. Text conflicts become first-class conflicts."
                                : "Undo this change to the selected files in the index and working tree, without committing. Conflicts stop as in a revert; "
                                  "added or deleted files may not apply (Shift: Revert and commit handles them).");
        if (menuItem(ICON_MS_UNDO, "Discard", "D", false, free))
            actions.moveChanges(id, Actions::MoveTo::Discard, paths, {});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            tooltip("Rewrites the commit so it no longer makes this change (its descendants are rebased). Undo restores it.");
    }
    if (m_selection.kind == SelKind::Stash && row.group != FileGroup::StashIndex) {
        if (menuItem(ICON_MS_CONTENT_PASTE, "Apply this file", nullptr, false, free))
            actions.stashApplyFile(m_selection.stashIndex, row.path);
    }
    if (m_selection.kind == SelKind::Commit || m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index) {
        // Restore the selected files from another commit: in the commit (rewrite), or in the
        // working tree / index. Untracked and conflicted files have nothing to restore.
        std::vector<std::string> restorable;
        bool skipped = false;
        for (const FileRow* r : rows) {
            const bool ok = worktree ? r->group == FileGroup::Staged || r->group == FileGroup::Unstaged : r->group == FileGroup::Commit;
            if (ok)
                restorable.push_back(r->path);
            else
                skipped = true;
        }
        const bool can = free && !restorable.empty() && !skipped;
        const bool hit = menuItem(ICON_MS_RESTORE, "Restore from...", nullptr, false, can);
        disabledHint(!restorable.empty() && skipped, "Untracked and conflicted files cannot be restored from a commit.");
        if (hit) {
            std::string prefill = "HEAD";
            if (m_selection.kind == SelKind::Commit) {
                const core::HistoryRow* hr = m_session.history().row(m_selection.id);
                prefill = hr && !hr->parents.empty() ? hr->parents.front().hex() : std::string();
                if (m_compare.kind == CompareTarget::Rev)
                    prefill = m_compare.rev;
            }
            showRestoreDialog(m_session, m_selection, restorable, prefill);
        }
    }
    if (worktree) {
        // Stash the selected files (an untracked one brings --include-untracked; all-staged rows stash only
        // their index part). Conflicted files cannot be stashed.
        std::vector<std::string> paths;
        bool anyConflict = false, anyUntracked = false, allStaged = true;
        for (const FileRow* r : rows) {
            paths.push_back(r->path);
            anyConflict |= r->group == FileGroup::Conflicted;
            anyUntracked |= r->group == FileGroup::Untracked;
            allStaged &= r->group == FileGroup::Staged;
        }
        const bool hit = menuItem(ICON_MS_INVENTORY_2, "Stash selected...", nullptr, false, free && !paths.empty() && !anyConflict);
        disabledHint(anyConflict, "Conflicted files cannot be stashed. Resolve them first.");
        if (hit)
            showStashFilesDialog(m_session, paths, anyUntracked, allStaged && !anyUntracked);
    }
    ImGui::Separator();
    if (worktree && menuItem(ICON_MS_DELETE, "Delete file...", nullptr, false, free && !existing.empty()))
        m_session.showDeleteFilesDialog(existing);
    ImGui::EndPopup();
}

void ChangesPanel::openFile(const FileRow& row)
{
    const auto snap = m_session.snapshot();
    if (snap->bare || !m_session.actions().busy().empty())
        return;
    auto& actions = m_session.actions();
    std::error_code ec;
    const bool added = row.kind == core::ChangeKind::Added || row.kind == core::ChangeKind::Untracked;
    // A conflicted commit's file has markers the diff tool cannot read: at HEAD the working copy is it.
    const bool headConflict = row.group == FileGroup::Commit && row.sides > 0 && m_selection.id == snap->head;
    if ((added || headConflict || row.group == FileGroup::Conflicted) && fs::exists(snap->workdir / row.path, ec)) {
        actions.openInEditor(row.path);
        return;
    }
    const std::string id = m_selection.id.hex();
    switch (row.group) {
    case FileGroup::Staged:
    case FileGroup::Unstaged: actions.externalDiff(row.path, {"HEAD"}); break;
    case FileGroup::Commit:
    case FileGroup::StashWorktree: actions.externalDiff(row.path, {id + "^", id}); break;
    case FileGroup::StashIndex: actions.externalDiff(row.path, {id + "^1", id + "^2"}); break;
    default: break;
    }
}

void ChangesPanel::drawFile(const FileRow& row, int)
{
    ImGui::PushID(row.path.c_str());
    const std::string key = row.key();
    const bool selected = m_selected.count(key) != 0;
    std::string label = kindIcon(row.kind) + "  ";
    if (!row.oldPath.empty())
        label += row.oldPath + " " ICON_MS_ARROW_RIGHT_ALT " " + row.path;
    else
        label += row.path;
    if (row.intentToAdd)
        label += "  (intent to add)";
    if (!row.conflict.empty())
        label += "  (" + row.conflict + ")";
    const bool broken = !row.brokenMarkerLines.empty();
    if (broken)
        label += "  \xE2\x9A\xA0 broken conflict markers"; // this edit broke a conflict region
    ImGui::PushStyleColor(ImGuiCol_Text, broken ? theme().palette().warning
                                                 : (row.firstClass || row.sides > 0 ? theme().palette().conflict : kindColor(row.kind)));
    const std::string id = label + "###file_" + row.path;
    // SelectOnNav: the nav cursor (arrows) and the selection are one thing; the cursor reaching a row presses it.
    const bool pressed = selectable(id.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav);
    {
        // The cursor row is always current (Ctrl+arrow moves the cursor without pressing the row), and the keys
        // below act only while the cursor is on a file row (read from the previous frame).
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        if (g.NavJustMovedToId == ImGui::GetItemID() && key != m_current)
            setCurrent(key);
        if (g.NavId == ImGui::GetItemID())
            m_navOnFileNow = true;
    }
    if (pressed) {
        const ImGuiIO& io = ImGui::GetIO();
        const PressSource source = pressSource();
        const bool shift = (pressMods() & ImGuiMod_Shift) != 0;
        if (source == PressSource::NavActivate && !io.KeyCtrl && !shift) {
            // Space / Enter on the cursor row keeps the selection (the staging toggle acts on all of it).
            if (!selected) {
                m_selected = {key};
                m_anchor = key;
                setCurrent(key);
            }
        } else if (source == PressSource::Mouse && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openFile(row);
        } else if (io.KeyCtrl) {
            if (selected)
                m_selected.erase(key);
            else
                m_selected.insert(key);
            m_anchor = key;
            setCurrent(key);
        } else if (shift && !m_anchor.empty()) {
            const auto rows = visibleRows();
            int a = -1, b = -1;
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i]->key() == m_anchor)
                    a = static_cast<int>(i);
                if (rows[i]->key() == key)
                    b = static_cast<int>(i);
            }
            if (a >= 0 && b >= 0) {
                m_selected.clear();
                for (int i = std::min(a, b); i <= std::max(a, b); ++i)
                    m_selected.insert(rows[static_cast<size_t>(i)]->key());
            }
            setCurrent(key);
        } else {
            m_selected = {key};
            m_anchor = key;
            setCurrent(key);
        }
    }
    ImGui::PopStyleColor();
    if (tooltipAllowed() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        if (broken) {
            // The row's ID is its path; the marker lines change when the status is read again.
            tooltip("%s", cachedTooltipText(ImGui::GetItemID(),
                ImHashData(row.brokenMarkerLines.data(), row.brokenMarkerLines.size() * sizeof(size_t)), [&] {
                    std::string lines;
                    for (size_t i = 0; i < row.brokenMarkerLines.size(); ++i)
                        lines += (i ? ", " : "") + std::to_string(row.brokenMarkerLines[i]);
                    return "Conflict markers left at line " + lines
                        + ": this edit broke a conflict region, so the file no longer counts as conflicted. Fix the "
                          "markers or remove them.";
                }).c_str());
        } else {
            tooltip("%s", row.path.c_str());
        }
    }
    // Drag files between Staged and Unstaged, or onto a commit in History. The payload is the
    // group name ("@<commit>" for a commit's files), then one path per line.
    const bool fromCommit = row.group == FileGroup::Commit && m_selection.kind == SelKind::Commit;
    const bool draggable = row.group == FileGroup::Staged || row.group == FileGroup::Unstaged
        || row.group == FileGroup::Untracked || fromCommit;
    if (draggable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        std::string payload = fromCommit ? "@" + m_selection.id.hex() : std::string(groupName(row.group));
        for (const FileRow* r : actionRows(row))
            if (r->group == row.group)
                payload += "\n" + r->path;
        ImGui::SetDragDropPayload("GG_FILES", payload.data(), payload.size());
        ImGui::TextUnformatted(row.path.c_str());
        ImGui::EndDragDropSource();
    }
    drawFileMenu(row);
    ImGui::PopID();
}

void ChangesPanel::dropTarget(FileGroup group)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GG_FILES")) {
        const std::string data(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        auto lines = gg::splitLines(data); // the source group, then the paths
        const std::string from = lines.front();
        lines.erase(lines.begin());
        auto& actions = m_session.actions();
        if (actions.busy().empty()) {
            if (group == FileGroup::Staged && from != groupName(FileGroup::Staged))
                actions.stage(lines);
            else if (group == FileGroup::Unstaged && from == groupName(FileGroup::Staged))
                actions.unstage(lines);
        }
    }
    ImGui::EndDragDropTarget();
}

void ChangesPanel::drawGroup(FileGroup group, const char* title, int count)
{
    const bool worktreeGroup = group == FileGroup::Staged || group == FileGroup::Unstaged;
    if (count == 0 && !(worktreeGroup && ImGui::GetDragDropPayload()))
        return;
    ImGui::PushID(groupName(group));
    const std::string header = std::string(title) + " (" + std::to_string(count) + ")###group";
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    bool open;
    {
        const SectionHeaderColors neutral;
        open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    }
    if (worktreeGroup)
        dropTarget(group);
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    ImGui::BeginDisabled(!free);
    if (group == FileGroup::Unstaged || group == FileGroup::Untracked) {
        float width = buttonWidth(ICON_MS_ADD, "Stage all");
        if (group == FileGroup::Unstaged)
            width += ImGui::GetStyle().ItemSpacing.x + buttonWidth(ICON_MS_ADD, "Stage modified");
        ImGui::SameLine(ImGui::GetContentRegionMax().x - width);
        if (smallButton(ICON_MS_ADD, "Stage all##stage_all"))
            actions.stageAll();
        if (group == FileGroup::Unstaged) {
            ImGui::SameLine();
            if (smallButton(ICON_MS_ADD, "Stage modified##stage_modified"))
                actions.stageModified();
        }
    } else if (group == FileGroup::Staged) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth(ICON_MS_REMOVE, "Unstage all"));
        if (smallButton(ICON_MS_REMOVE, "Unstage all##unstage_all"))
            actions.unstageAll();
    }
    ImGui::EndDisabled();
    if (open) {
        int i = 0;
        for (const FileRow* r : visibleRows())
            if (r->group == group)
                drawFile(*r, i++);
    }
    ImGui::PopID();
}

void ChangesPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Changes, open)) {
        ImGui::End();
        return;
    }
    const auto snap = m_session.snapshot();
    markConflicts();
    std::string title;
    const char* note = m_scanning ? "scanning..." : m_loading ? "loading..." : nullptr;
    switch (m_selection.kind) {
    case SelKind::WorkingTree:
        // Git's zero ID stands for the working tree (as in `git diff --raw`).
        title = std::string(m_session.shortIdLength(), '0') + " Working tree";
        break;
    case SelKind::Index: title = "Index (staged)"; break;
    case SelKind::Commit: {
        const auto* row = m_session.history().row(m_selection.id);
        title = m_session.shortId(m_selection.id);
        if (row) {
            // The subject takes what the id and the note (only while it is shown) leave of the line.
            const float noteWidth = note ? ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(note).x : 0.0f;
            const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize((title + " ").c_str()).x - noteWidth;
            title += " " + fitText(std::string(firstLine(row->subject)), std::max(room, ImGui::GetFontSize() * 6));
        }
        break;
    }
    case SelKind::Stash: title = "stash@{" + std::to_string(m_selection.stashIndex) + "}"; break;
    default: title = "Nothing selected"; break;
    }
    plainText((title + "###changes_title").c_str());
    if (note) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", note);
    }
    // The filter and "Compare with" share the row; each is at least wide enough for its hint.
    const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float filterMin = ImGui::CalcTextSize(ICON_MS_SEARCH " Filter").x + pad;
    const float compareMin = ImGui::CalcTextSize("Compare with").x + pad;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    ImGui::SetNextItemWidth(std::max(filterMin, half));
    ImGui::InputTextWithHint("##changes_filter", ICON_MS_SEARCH " Filter", &m_filter);
    sameLineIfFits(compareMin);
    // Compares the whole commit with a revision or the working tree; the working tree and index
    // already compare with HEAD's side.
    ImGui::BeginDisabled(m_selection.kind != SelKind::Commit);
    ImGui::SetNextItemWidth(std::max(compareMin, ImGui::GetContentRegionAvail().x));
    if (compareWithField("##compare_with", m_compareText)) {
        const CompareTarget target = CompareTarget::parse(m_compareText);
        if (target != m_compare) {
            m_compare = target;
            m_rows.clear();
            m_current.clear();
            m_selected.clear();
            m_filesError.clear();
            requestFiles();
            m_session.diff().clear();
        }
    }
    ImGui::EndDisabled();
    if (!m_filesError.empty() && m_selection.kind == SelKind::Commit) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme().palette().conflict);
        plainText((m_filesError + "###compare_error").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // Nav-flattened: the rows are part of the panel's nav layer, so the arrows walk them.
    beginListChild("##files");
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    const bool navOnFile = m_navOnFileNow || ImGui::GetCurrentContext()->NavId == 0; // last frame's cursor; 0 = mouse-only
    m_navOnFileNow = false;
    if (focused) {
        // Up / Down / Page / Home / End are ImGui's nav; the row Selectables (SelectOnNav) turn the cursor
        // reaching a row into a selection.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
            m_selected.clear();
            for (const FileRow* r : visibleRows())
                m_selected.insert(r->key());
        }
        // Plain Space / Enter keep activating the focused row through ImGui's nav as well; Alt+Space is the
        // context-menu chord (a routed shortcut) and must not stage anything.
        if ((ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && navOnFile
            && (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index)) {
            if (const FileRow* cur = current())
                toggleStaging(actionRows(*cur));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false) && !ImGui::GetIO().KeyMods && navOnFile
            && (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index)
            && m_session.actions().busy().empty()) {
            if (const FileRow* cur = current()) {
                const DiscardPlan plan = discardPlan(actionRows(*cur));
                if (plan.enabled())
                    discard(plan);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false) && !ImGui::GetIO().KeyMods && navOnFile && m_selection.kind == SelKind::Commit
            && m_session.actions().busy().empty()) {
            // Discard from the commit: a rewrite (one Undo), published commits ask first.
            if (const FileRow* cur = current(); cur && cur->group == FileGroup::Commit) {
                std::vector<std::string> paths;
                for (const FileRow* r : actionRows(*cur))
                    paths.push_back(r->path);
                m_session.actions().moveChanges(m_selection.id, Actions::MoveTo::Discard, paths, {});
            }
        }
    }
    if (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index) {
        int counts[8] = {};
        for (const auto& r : m_rows)
            ++counts[static_cast<int>(r.group)];
        drawGroup(FileGroup::Conflicted, "Conflicted", counts[static_cast<int>(FileGroup::Conflicted)]);
        drawGroup(FileGroup::Staged, "Staged", counts[static_cast<int>(FileGroup::Staged)]);
        drawGroup(FileGroup::Unstaged, "Unstaged", counts[static_cast<int>(FileGroup::Unstaged)]);
        drawGroup(FileGroup::Untracked, "Untracked", counts[static_cast<int>(FileGroup::Untracked)]);
        if (m_rows.empty() && !m_scanning)
            ImGui::TextDisabled("No changes");
    } else if (m_selection.kind == SelKind::Stash) {
        int counts[8] = {};
        for (const auto& r : m_rows)
            ++counts[static_cast<int>(r.group)];
        drawGroup(FileGroup::StashWorktree, "Working tree", counts[static_cast<int>(FileGroup::StashWorktree)]);
        drawGroup(FileGroup::StashIndex, "Index", counts[static_cast<int>(FileGroup::StashIndex)]);
        drawGroup(FileGroup::StashUntracked, "Untracked", counts[static_cast<int>(FileGroup::StashUntracked)]);
    } else {
        int i = 0;
        for (const FileRow* r : visibleRows())
            drawFile(*r, i++);
        if (m_rows.empty() && !m_loading && m_selection.kind == SelKind::Commit)
            ImGui::TextDisabled("No file changes");
    }
    endListChild();
    ImGui::End();
}

} // namespace ggui
