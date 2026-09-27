// Changes panel (REBUILD_PLAN §4.4): files of the selected commit, stash or comparison; for
// the working tree / index, Staged / Unstaged / Untracked / Conflicted groups.
#pragma once

#include "shell/Session.hpp"

#include <core/Engine.hpp>

#include <set>
#include <string>
#include <vector>

namespace ggui {

enum class FileGroup { Commit, Staged, Unstaged, Untracked, Conflicted, StashWorktree, StashIndex, StashUntracked };
const char* groupName(FileGroup g);

struct FileRow {
    FileGroup group = FileGroup::Commit;
    std::string path;
    std::string oldPath;
    core::ChangeKind kind = core::ChangeKind::Modified;
    bool intentToAdd = false;
    bool firstClass = false;
    int sides = 0;           // first-class conflicts: number of sides
    std::string conflict;
    std::string key() const { return std::string(groupName(group)) + ":" + path; }
};

class ChangesPanel {
public:
    // Engine diff slots used by this panel (the Diff panel uses DiffPanel::kSlot).
    static constexpr int kSlotFiles = 0;
    static constexpr int kSlotStashIndex = 2;
    static constexpr int kSlotStashUntracked = 3;
    static constexpr int kSlotCopyPatch = 4;
    static constexpr int kSlotSavePatch = 5;

    explicit ChangesPanel(Session& session);

    void onSelection(const Selection& sel);
    void onStatus(const core::StatusPtr& status);
    void onDiff(const core::DiffEvent& event);
    void draw(bool* open);

    // F6 / Shift+F6: move the current file.
    void moveCurrent(int direction);
    const FileRow* current() const;
    const std::vector<FileRow>& rows() const { return m_rows; }
    bool compareWithHead() const { return m_compareHead; }
    const std::set<std::string>& selectedKeys() const { return m_selected; }
    bool scanning() const { return m_scanning; }
    bool everScanned() const { return m_everScanned; }

private:
    void requestFiles();
    void rebuildFromStatus();
    void setCurrent(const std::string& key, bool notify = true);
    void drawGroup(FileGroup group, const char* title, int count);
    void drawFile(const FileRow& row, int flatIndex);
    void drawFileMenu(const FileRow& row);
    // Double-click: new files open in the editor, others in the diff tool against the parent.
    void openFile(const FileRow& row);
    void dropTarget(FileGroup group);
    // The rows an action on `row` applies to: the selection when `row` is selected.
    std::vector<const FileRow*> actionRows(const FileRow& row) const;
    void toggleStaging(const std::vector<const FileRow*>& rows);
    std::vector<const FileRow*> visibleRows() const;
    core::DiffQuery patchQuery(const FileRow& row) const;

    Session& m_session;
    Selection m_selection;
    std::vector<FileRow> m_rows;
    std::set<std::string> m_selected;
    std::string m_current;
    std::string m_anchor;
    std::string m_filter;
    bool m_compareHead = false;
    bool m_loading = false;
    bool m_scanning = false;
    bool m_everScanned = false; // a partial ("scanning…") status was shown
    std::vector<FileRow> m_stashParts[3];
    std::string m_savePatchPath;
};

} // namespace ggui
