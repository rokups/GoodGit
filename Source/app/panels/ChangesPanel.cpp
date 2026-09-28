#include "panels/ChangesPanel.hpp"
#include "shell/Dialogs.hpp"

#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <libgg/GitRunner.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace ggui {

namespace fs = std::filesystem;

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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("HEAD, a commit ID or ref, or Work Tree (Enter applies; empty: the parent)");
    if (ImGui::BeginPopupContextItem((std::string(id) + "_menu").c_str())) {
        for (const char* choice : {"HEAD", "Work Tree"})
            if (ImGui::MenuItem(choice)) {
                text = choice;
                apply = true;
            }
        if (ImGui::MenuItem("Clear", nullptr, false, !text.empty())) {
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

void ChangesPanel::drawFileMenu(const FileRow& row)
{
    if (!ImGui::BeginPopupContextItem("##file_menu"))
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
    std::vector<std::string> stage, unstage, discardTracked, discardUntracked, untracked, nativeConflicts,
        conflicts, existing;
    for (const FileRow* r : rows) {
        switch (r->group) {
        case FileGroup::Staged: unstage.push_back(r->path); break;
        case FileGroup::Unstaged:
            stage.push_back(r->path);
            if (!r->intentToAdd)
                discardTracked.push_back(r->path);
            break;
        case FileGroup::Untracked:
            stage.push_back(r->path);
            untracked.push_back(r->path);
            discardUntracked.push_back(r->path);
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
    if (ImGui::MenuItem("Open working-copy file", nullptr, false, free && rowExists))
        actions.openInEditor(row.path);
    if (ImGui::MenuItem("Open containing folder", nullptr, false, !snap->bare))
        openInFileManager((snap->workdir / row.path).parent_path());
    if (ImGui::BeginMenu("Copy")) {
        if (ImGui::MenuItem("Name"))
            ImGui::SetClipboardText(fs::path(row.path).filename().string().c_str());
        if (ImGui::MenuItem("Relative path"))
            ImGui::SetClipboardText(row.path.c_str());
        if (ImGui::MenuItem("Absolute path", nullptr, false, !snap->bare))
            ImGui::SetClipboardText((snap->workdir / row.path).lexically_normal().string().c_str());
        ImGui::EndMenu();
    }
    if (worktree) {
        ImGui::Separator();
        if (ImGui::MenuItem("Stage", "Space", false, free && !stage.empty()))
            actions.stage(stage);
        if (ImGui::MenuItem("Unstage", "Space", false, free && !unstage.empty()))
            actions.unstage(unstage);
        if (ImGui::MenuItem("Discard...", nullptr, false, free && (!discardTracked.empty() || !discardUntracked.empty())))
            m_session.showDiscardDialog(discardTracked, discardUntracked);
        if (ImGui::MenuItem("Intent to add", nullptr, false, free && !untracked.empty()))
            actions.intentToAdd(untracked);
        ImGui::Separator();
        const bool firstClassRow = row.group == FileGroup::Conflicted && row.firstClass;
        if (ImGui::MenuItem("Resolve with merge tool", nullptr, false,
                free && (nativeConflicts.size() == 1 || (firstClassRow && row.sides == 2 && conflicts.size() == 1)))) {
            if (firstClassRow)
                actions.mergeToolFirstClass(row.path);
            else
                actions.mergeTool(nativeConflicts.front());
        }
        if (firstClassRow && ImGui::BeginMenu("Take side", free)) {
            for (int k = 0; k < row.sides; ++k)
                if (ImGui::MenuItem(("Side " + std::to_string(k + 1) + " (whole file)").c_str()))
                    actions.takeConflictSide({row.path}, k);
            ImGui::Separator();
            if (ImGui::MenuItem("In one region...")) {
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
        if (ImGui::MenuItem("Take ours", nullptr, false, free && !nativeConflicts.empty()))
            actions.takeSide(nativeConflicts, Side::Ours);
        if (ImGui::MenuItem("Take theirs", nullptr, false, free && !nativeConflicts.empty()))
            actions.takeSide(nativeConflicts, Side::Theirs);
        if (ImGui::MenuItem("Mark resolved", nullptr, false, free && !conflicts.empty()))
            actions.markResolved(conflicts);
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Patch")) {
        // Of the selected files in this row's group, or of this file.
        core::DiffQuery q = patchQuery(row);
        if (m_selected.size() > 1) {
            q.path.clear();
            for (const auto& r : m_rows)
                if (m_selected.count(r.key()) && r.group == row.group)
                    q.paths.push_back(r.path);
        }
        if (ImGui::MenuItem("Copy"))
            m_session.engine().diff(q, kSlotCopyPatch);
        if (ImGui::MenuItem("Save...")) {
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
    if (ImGui::MenuItem("Blame file", nullptr, false, canBlame)) {
        core::Oid at;
        if (m_selection.kind == SelKind::Commit)
            at = m_selection.id;
        m_session.blameFile(row.path, at);
    }
    if (ImGui::BeginMenu("External diff", free && !snap->bare)) {
        const std::string commit = m_selection.kind == SelKind::Commit ? m_selection.id.hex() : std::string();
        if (ImGui::MenuItem("vs HEAD"))
            actions.externalDiff(row.path, "HEAD", commit);
        if (ImGui::MenuItem("vs parent", nullptr, false, !commit.empty()))
            actions.externalDiff(row.path, commit + "^", commit);
        ImGui::EndMenu();
    }
    if (m_selection.kind == SelKind::Commit && row.group == FileGroup::Commit) {
        // History editing on the commit's files (selection or this row).
        std::vector<std::string> paths;
        for (const FileRow* r : rows)
            paths.push_back(r->path);
        ImGui::Separator();
        const core::Oid id = m_selection.id;
        if (ImGui::MenuItem("Move to parent", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::Parent, paths, {});
        if (ImGui::MenuItem("Move to child", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::Child, paths, {});
        if (ImGui::MenuItem("Move to the working tree", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::WorkingTree, paths, {});
        if (ImGui::MenuItem("Revert", nullptr, false, free))
            actions.moveChanges(id, Actions::MoveTo::Revert, paths, {});
    }
    if (m_selection.kind == SelKind::Stash && row.group != FileGroup::StashIndex) {
        if (ImGui::MenuItem("Apply this file", nullptr, false, free))
            actions.stashApplyFile(m_selection.stashIndex, row.path);
    }
    ImGui::Separator();
    if (worktree && ImGui::MenuItem("Delete file...", nullptr, false, free && !existing.empty()))
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
    if ((added || row.group == FileGroup::Conflicted) && fs::exists(snap->workdir / row.path, ec)) {
        actions.openInEditor(row.path);
        return;
    }
    const std::string id = m_selection.id.hex();
    switch (row.group) {
    case FileGroup::Staged:
    case FileGroup::Unstaged: actions.externalDiff(row.path, "HEAD", ""); break;
    case FileGroup::Commit:
    case FileGroup::StashWorktree: actions.externalDiff(row.path, id + "^", id); break;
    case FileGroup::StashIndex: actions.externalDiff(row.path, id + "^1", id + "^2"); break;
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
        label += row.oldPath + " \xe2\x86\x92 " + row.path;
    else
        label += row.path;
    if (row.intentToAdd)
        label += "  (intent to add)";
    if (!row.conflict.empty())
        label += "  (" + row.conflict + ")";
    ImGui::PushStyleColor(ImGuiCol_Text, row.firstClass ? theme().palette().conflict : kindColor(row.kind));
    const std::string id = label + "###file_" + row.path;
    if (ImGui::Selectable(id.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openFile(row);
        } else if (io.KeyCtrl) {
            if (selected)
                m_selected.erase(key);
            else
                m_selected.insert(key);
            m_anchor = key;
            setCurrent(key);
        } else if (io.KeyShift && !m_anchor.empty()) {
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", row.path.c_str());
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
    const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    if (worktreeGroup)
        dropTarget(group);
    auto& actions = m_session.actions();
    const bool free = actions.busy().empty();
    ImGui::BeginDisabled(!free);
    if (group == FileGroup::Unstaged || group == FileGroup::Untracked) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFontSize() * (group == FileGroup::Unstaged ? 13.5f : 5.0f));
        if (ImGui::SmallButton("Stage all##stage_all"))
            actions.stageAll();
        if (group == FileGroup::Unstaged) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stage modified##stage_modified"))
                actions.stageModified();
        }
    } else if (group == FileGroup::Staged) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFontSize() * 6.0f);
        if (ImGui::SmallButton("Unstage all##unstage_all"))
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
    std::string title;
    switch (m_selection.kind) {
    case SelKind::WorkingTree:
        // Git's zero ID stands for the working tree (as in `git diff --raw`).
        title = std::string(m_session.shortIdLength(), '0') + " Working tree";
        break;
    case SelKind::Index: title = "Index (staged)"; break;
    case SelKind::Commit: {
        const auto* row = m_session.history().row(m_selection.id);
        title = m_session.shortId(m_selection.id) + (row ? " " + row->subject : std::string());
        break;
    }
    case SelKind::Stash: title = "stash@{" + std::to_string(m_selection.stashIndex) + "}"; break;
    default: title = "Nothing selected"; break;
    }
    plainText((title + "###changes_title").c_str());
    if (m_scanning || m_loading) {
        ImGui::SameLine();
        ImGui::TextDisabled(m_scanning ? "scanning..." : "loading...");
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputTextWithHint("##changes_filter", ICON_MS_SEARCH " Filter", &m_filter);
    ImGui::SameLine();
    // Compares the whole commit with a revision or the working tree; the working tree and index
    // already compare with HEAD's side.
    ImGui::BeginDisabled(m_selection.kind != SelKind::Commit);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
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

    ImGui::BeginChild("##files", ImVec2(0, 0), ImGuiChildFlags_None);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    if (focused) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            moveCurrent(+1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            moveCurrent(-1);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
            m_selected.clear();
            for (const FileRow* r : visibleRows())
                m_selected.insert(r->key());
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            && (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index)) {
            if (const FileRow* cur = current())
                toggleStaging(actionRows(*cur));
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
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ggui
