// "Add GoodGit to PATH": puts the ggui executable directory (it holds git-gg, for `git gg` in a
// terminal) on the PATH. Linux with systemd: a systemd user environment.d file, taking effect at
// next login. Windows: the per-user PATH value in the registry (HKCU\Environment), taking effect
// in new terminals.
//
// The state is read from disk (or the registry), not stored: on Linux the setting is on iff the
// managed file exists and its PATH entry is this directory; on Windows iff the user PATH lists this
// directory. Every function takes the config home and the availability, so tests can inject both.
#pragma once

#include <string>

namespace ggui {

enum class PathSetupSupport {
    Available,   // Linux with systemd: the environment.d file
    Registry,    // Windows: the per-user PATH in the registry
    NoSystemd,   // Linux without systemd as init
    Unsupported, // neither Linux nor Windows
};

// True when the setting can be changed: Available or Registry.
bool pathSetupUsable(PathSetupSupport support);

struct PathSetupState {
    bool present = false;  // the managed file exists (Windows: same as enabled)
    bool enabled = false;  // ...and its PATH entry is the directory asked about
    std::string otherDir;  // present but not enabled: the directory it points to (Linux only)
};

// What this machine supports: Windows is always Registry. Linux with systemd needs `<root>/run/systemd/system` to be a directory
// (the sd_booted() test); `root` is "" for the real system.
PathSetupSupport pathSetupSupport(const std::string& root = "");
// Why the setting is unavailable, for a tooltip ("" when Available).
std::string pathSetupUnavailableReason(PathSetupSupport support);

// $XDG_CONFIG_HOME, or ~/.config when unset or not absolute ("" if there is no home either).
std::string pathSetupConfigHome();
// <configHome>/environment.d/60-goodgit.conf
std::string pathSetupFile(const std::string& configHome);

// Reads the managed file and compares its PATH entry with `dir`.
PathSetupState readPathSetup(const std::string& configHome, const std::string& dir);
// Same for `support`: Registry reads the Windows user PATH (configHome is not used), anything else
// reads the managed file.
PathSetupState readPathSetup(PathSetupSupport support, const std::string& configHome, const std::string& dir);
// Linux (Available): writes (enable) or deletes (disable) the managed file, atomically. Refuses a
// `dir` environment.d would expand or misparse ($ \ " ' : newline).
// Windows (Registry): adds `dir` to, or removes it from, the user PATH and tells running programs
// the environment changed; the machine PATH is never touched. Refuses a `dir` with ';' or '"'.
// Returns an error message, "" on success.
std::string writePathSetup(
    PathSetupSupport support, const std::string& configHome, const std::string& dir, bool enable);

// Pure editing of a Windows-style PATH value (entries separated by ';'), for the Registry mode. No
// Windows API, so tests run anywhere. Entries compare case-insensitively (ASCII), ignoring spaces
// around them, surrounding double quotes and trailing '\' or '/'; %VAR% is not expanded. Entries
// that do not match `dir`, including empty ones, are kept as they are.
bool pathListContains(const std::string& list, const std::string& dir);
// `list` with `dir` appended unless already listed.
std::string pathListAdd(const std::string& list, const std::string& dir);
// `list` without any entry matching `dir`.
std::string pathListRemove(const std::string& list, const std::string& dir);

} // namespace ggui
