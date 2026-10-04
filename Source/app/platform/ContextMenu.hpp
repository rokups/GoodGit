// "Add \"Open in GoodGit\" to file manager menus": for the current user, an "Open in GoodGit" item on
// folders in Dolphin (a KIO service menu), Nemo (an action) and Nautilus (a script, under Scripts).
// Linux only. A `MimeType=inode/directory` application entry is deliberately not installed: it could
// become the default folder handler. Thunar is not covered.
//
// The state is read from disk, not stored: the setting is on iff every managed file exists with
// exactly the content this GoodGit would write and the executable ones keep the owner-exec bit.
// Every function takes the data home and the availability, so tests can inject both.
#pragma once

#include <string>
#include <vector>

namespace ggui {

enum class ContextMenuSupport {
    Available,
    Unsupported, // not Linux
};

struct ContextMenuFile {
    std::string path;
    std::string content;
    bool executable = false; // mode 0755 instead of 0644
};

struct ContextMenuState {
    bool present = false; // at least one managed file exists
    // every managed file exists with the content for the executable asked about, and the
    // executable ones keep the owner-exec bit
    bool enabled = false;
};

// What this machine supports.
ContextMenuSupport contextMenuSupport();
// Why the setting is unavailable, for a tooltip ("" when Available).
std::string contextMenuUnavailableReason(ContextMenuSupport support);

// $XDG_DATA_HOME, or ~/.local/share when unset or not absolute ("" if there is no home either).
std::string contextMenuDataHome();
// The managed files under `dataHome`, each running the executable `exe` (a full path) with the folder:
// <dataHome>/kio/servicemenus/goodgit-open.desktop (Dolphin), <dataHome>/nemo/actions/goodgit-open.nemo_action
// (Nemo) and <dataHome>/nautilus/scripts/Open in GoodGit (Nautilus).
std::vector<ContextMenuFile> contextMenuFiles(const std::string& dataHome, const std::string& exe);

// Reads the managed files and compares them with what `exe` would get.
ContextMenuState readContextMenu(const std::string& dataHome, const std::string& exe);
// Writes (enable) or deletes (disable) the managed files, each atomically. Returns an error message,
// "" on success. Disabling removes only the files, never directories. Refuses an `exe` that would
// need escaping in a desktop Exec line or the shell script (" ' \ $ ` % or a line break).
std::string writeContextMenu(
    ContextMenuSupport support, const std::string& dataHome, const std::string& exe, bool enable);

} // namespace ggui
