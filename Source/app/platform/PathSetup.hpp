// "Add GoodGit to PATH": puts the ggui executable directory (it holds git-gg, which the repository
// hooks need) on the login PATH through a systemd user environment.d file. Linux with systemd only.
//
// The state is read from disk, not stored: the setting is on iff the managed file exists and its
// PATH entry is this directory. Every function takes the config home and the availability, so
// tests can inject both.
#pragma once

#include <string>

namespace ggui {

enum class PathSetupSupport {
    Available,
    NoSystemd,   // Linux without systemd as init
    Unsupported, // not Linux
};

struct PathSetupState {
    bool present = false;  // the managed file exists
    bool enabled = false;  // ...and its PATH entry is the directory asked about
    std::string otherDir;  // present but not enabled: the directory it points to
};

// What this machine supports: Linux with systemd needs `<root>/run/systemd/system` to be a directory
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
// Writes (enable) or deletes (disable) the managed file, atomically. Returns an error message,
// "" on success. Refuses a `dir` environment.d would expand or misparse ($ \ " ' : newline).
std::string writePathSetup(
    PathSetupSupport support, const std::string& configHome, const std::string& dir, bool enable);

} // namespace ggui
