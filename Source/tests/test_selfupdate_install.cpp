// Self-update: install checks, the update scripts, and the result of a finished update.
#include "libgg/Launch.hpp"
#include "platform/SelfUpdate.hpp"
#include "tests/Harness.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdlib>
#else
#include <unistd.h>
#endif

namespace ggtest {

namespace {

namespace fs = std::filesystem;
namespace up = ggui::update;

#ifdef _WIN32
const char* const kGgui = "ggui.exe";
const char* const kGitGg = "git-gg.exe";
#else
const char* const kGgui = "ggui";
const char* const kGitGg = "git-gg";
#endif

// A fresh empty folder, removed (and made writable first) at the end of the test.
struct TempRoot {
    fs::path path;

    TempRoot()
    {
        static int counter = 0;
        std::error_code ec;
        path = fs::temp_directory_path(ec) / ("gg-selfupdate-" + std::to_string(counter++) + "-" +
#ifdef _WIN32
                                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#else
                                                 std::to_string(getpid()));
#endif
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }

    ~TempRoot()
    {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator();
            it.increment(ec)) {
            if (it->is_directory(ec)) {
                fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
            }
        }
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
    }
};

void writeFile(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
}

std::string readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool pathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

// An install folder whose name has a space and non-ASCII characters, with an old and a new set.
fs::path makeInstall(const fs::path& root, bool withNewGitGg, bool withNewGgui = true,
    const fs::path& name = fs::path(u8"inst dir äö ü"))
{
    const fs::path dir = root / name;
    writeFile(dir / kGgui, "old");
    writeFile(dir / kGitGg, "old");
    if (withNewGgui) {
        writeFile(up::stageDir(dir) / "new" / kGgui, "new");
    }
    if (withNewGitGg) {
        writeFile(up::stageDir(dir) / "new" / kGitGg, "new");
    }
    return dir;
}

// Starts the script of this platform in the stage of `dir` without a restart; it waits for `pid`.
bool startUpdateScript(const fs::path& dir, const std::string& pid, gg::DetachedProcess& child)
{
    const fs::path stage = up::stageDir(dir);
    if (!up::writeScript(stage).empty()) {
        return false;
    }
    std::string error;
    return gg::spawnScriptDetached(up::scriptPath(stage), {pid, "nostart"}, child, error);
}

// Runs the script with a pid that does not exist; true when it ended in time.
bool runUpdateScript(const fs::path& dir)
{
    gg::DetachedProcess child;
    if (!startUpdateScript(dir, "2147483647", child)) {
        return false;
    }
    for (int i = 0; i < 400; ++i) {
        if (!gg::stillRunning(child)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    gg::release(child);
    return false;
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

GG_TEST("selfupdate", "installProblem rejects a folder without git-gg")
{
    TempRoot root;
    writeFile(root.path / "ggui", "x");
    GG_CHECK_STR_EQ(up::installProblem(root.path / "ggui"), "This is not an installed ggui.");
    GG_CHECK(!pathExists(up::stageDir(root.path)));
}

GG_TEST("selfupdate", "installProblem rejects another file name")
{
    TempRoot root;
    writeFile(root.path / "git-gg", "x");
    writeFile(root.path / "ggui-renamed", "x");
    GG_CHECK_STR_EQ(up::installProblem(root.path / "ggui-renamed"), "This is not an installed ggui.");
}

GG_TEST("selfupdate", "installProblem accepts a writable install and leaves no stage")
{
    TempRoot root;
    writeFile(root.path / "ggui", "x");
    writeFile(root.path / "git-gg", "x");
    GG_CHECK_STR_EQ(up::installProblem(root.path / "ggui"), "");
    GG_CHECK(!pathExists(up::stageDir(root.path)));
    // An existing stage stays.
    writeFile(up::stageDir(root.path) / "result.txt", "ok");
    GG_CHECK_STR_EQ(up::installProblem(root.path / "ggui"), "");
    GG_CHECK(pathExists(up::stageDir(root.path) / "result.txt"));
    GG_CHECK(!pathExists(up::stageDir(root.path) / "probe"));
}

#ifndef _WIN32
GG_TEST("selfupdate", "installProblem rejects a folder that is not writable")
{
    if (geteuid() == 0) {
        markCurrentTestSkipped("root can write to any folder");
        return;
    }
    TempRoot root;
    writeFile(root.path / "ggui", "x");
    writeFile(root.path / "git-gg", "x");
    std::error_code ec;
    fs::permissions(root.path, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
    GG_CHECK_STR_EQ(up::installProblem(root.path / "ggui"), "The install folder is not writable.");
    fs::permissions(root.path, fs::perms::owner_all, fs::perm_options::replace, ec);
}
#endif

#ifdef __linux__
GG_TEST("selfupdate", "installProblem leaves /usr to the package manager but not /usr/local")
{
    // Strings only: the /usr check must not need the disk.
    GG_CHECK_STR_EQ(up::installProblem("/usr/bin/ggui"), "Installed by the package manager. Update it with apt.");
    GG_CHECK_STR_EQ(up::installProblem("/usr/lib/ggui/ggui"), "Installed by the package manager. Update it with apt.");
    GG_CHECK(up::installProblem("/usr/local/bin/ggui") != "Installed by the package manager. Update it with apt.");
    GG_CHECK(up::installProblem("/usrx/bin/ggui") != "Installed by the package manager. Update it with apt.");
}
#endif

GG_TEST("selfupdate", "the Windows update script is ASCII with CRLF line ends")
{
    const std::string text = up::windowsUpdateScript();
    GG_CHECK(!text.empty());
    bool ascii = true;
    bool crlf = true;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        ascii = ascii && (c == '\r' || c == '\n' || (c >= 0x20 && c < 0x7f));
        if (c == '\n' && (i == 0 || text[i - 1] != '\r'))
            crlf = false;
        if (c == '\r' && (i + 1 >= text.size() || text[i + 1] != '\n'))
            crlf = false;
    }
    GG_CHECK(ascii);
    GG_CHECK(crlf);
    GG_CHECK(text.find("tasklist") != std::string::npos);
    GG_CHECK(text.find("ping.exe\" -n 2 127.0.0.1") != std::string::npos);
    // The tools are called by full path: a GNU find or a foreign tasklist on PATH must not matter.
    GG_CHECK(text.find("\"%SystemRoot%\\System32\\tasklist.exe\"") != std::string::npos);
    GG_CHECK(text.find("\"%SystemRoot%\\System32\\find.exe\"") != std::string::npos);
    GG_CHECK(text.find("timeout /") == std::string::npos); // the timeout command fails without a console
    GG_CHECK(text.find("timeout -") == std::string::npos);
    for (const char* label : {":undo0", ":undo1", ":undo2", ":undo3"})
        GG_CHECK(text.find(std::string("\r\n") + label + "\r\n") != std::string::npos);
    GG_CHECK(text.find("failed: ggui did not exit") != std::string::npos);
    GG_CHECK(startsWith(text, "@echo off\r\nsetlocal EnableExtensions DisableDelayedExpansion\r\n"));
    GG_CHECK(text.find("failed: install git-gg") == std::string::npos); // the step text is set, not echoed
    GG_CHECK(text.find("set \"STEP=install git-gg\"") != std::string::npos);
}

GG_TEST("selfupdate", "the Unix update script is ASCII")
{
    const std::string text = up::unixUpdateScript();
    GG_CHECK(startsWith(text, "#!/bin/sh\n"));
    bool ascii = true;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        ascii = ascii && (c == '\n' || (c >= 0x20 && c < 0x7f));
    }
    GG_CHECK(ascii);
    GG_CHECK(text.find("failed: ggui did not exit") != std::string::npos);
}

GG_TEST("selfupdate", "the update script swaps the files")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
    GG_REQUIRE(runUpdateScript(dir));
    const fs::path stage = up::stageDir(dir);
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "new");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "new");
    GG_CHECK_STR_EQ(readFile(stage / "result.txt").substr(0, 2), "ok");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    GG_CHECK(!pathExists(dir / (std::string(kGitGg) + ".old")));
    GG_CHECK(!pathExists(stage / "new" / kGgui));
}

GG_TEST("selfupdate", "the update script installs every other file of new/")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
    const fs::path newDir = up::stageDir(dir) / "new";
    writeFile(dir / "extra.dat", "old extra");
    writeFile(newDir / "extra.dat", "new extra");
    writeFile(newDir / "data" / "theme.txt", "theme");
    GG_REQUIRE(runUpdateScript(dir));
    GG_CHECK_STR_EQ(readFile(up::stageDir(dir) / "result.txt").substr(0, 2), "ok");
    GG_CHECK_STR_EQ(readFile(dir / "extra.dat"), "new extra");
    GG_CHECK_STR_EQ(readFile(dir / "data" / "theme.txt"), "theme");
    GG_CHECK(!pathExists(dir / "extra.dat.old"));
}

GG_TEST("selfupdate", "the update script restores the extra files when an install fails")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
    const fs::path newDir = up::stageDir(dir) / "new";
    writeFile(dir / "a.dat", "old a");
    writeFile(newDir / "a.dat", "new a");
    writeFile(newDir / "z" / "c.dat", "new c");
    // A file in the place of the folder z makes the step after a.dat fail.
    writeFile(dir / "z", "keep");
    GG_REQUIRE(runUpdateScript(dir));
    const std::string result = readFile(up::stageDir(dir) / "result.txt");
    GG_CHECK(startsWith(result, "failed:"));
    GG_CHECK_STR_EQ(readFile(dir / "a.dat"), "old a");
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK_STR_EQ(readFile(dir / "z"), "keep");
    GG_CHECK(!pathExists(dir / "a.dat.old"));
}

GG_TEST("selfupdate", "the update script replaces stale .old files")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
    writeFile(dir / (std::string(kGgui) + ".old"), "stale");
    writeFile(dir / (std::string(kGitGg) + ".old"), "stale");
    GG_REQUIRE(runUpdateScript(dir));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "new");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "new");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
}

GG_TEST("selfupdate", "the update script restores the old files when a step fails")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, false);
    GG_REQUIRE(runUpdateScript(dir));
    const std::string result = readFile(up::stageDir(dir) / "result.txt");
    GG_CHECK(startsWith(result, "failed:"));
    GG_CHECK(result.find("install git-gg") != std::string::npos);
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    GG_CHECK(!pathExists(dir / (std::string(kGitGg) + ".old")));
}

GG_TEST("selfupdate", "the update script restores both old files when install ggui fails")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true, false);
    GG_REQUIRE(runUpdateScript(dir));
    const std::string result = readFile(up::stageDir(dir) / "result.txt");
    GG_CHECK(startsWith(result, "failed: install ggui"));
    GG_CHECK(result.find("undo") == std::string::npos);
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    GG_CHECK(!pathExists(dir / (std::string(kGitGg) + ".old")));
    GG_CHECK(pathExists(up::stageDir(dir) / "new" / kGitGg));
}

GG_TEST("selfupdate", "the update script waits while ggui is running")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
    // A live process stands for ggui. The test reaps it: a zombie still answers kill -0.
    gg::DetachedProcess ggui;
    std::string error;
#ifdef _WIN32
    const char* systemRoot = std::getenv("SystemRoot");
    GG_REQUIRE(systemRoot != nullptr);
    GG_REQUIRE(gg::spawnDetached({(fs::path(systemRoot) / "System32" / "ping.exe").string(), "-n", "4", "127.0.0.1"}, ggui,
        error));
    const std::string pid = std::to_string(static_cast<unsigned long>(GetProcessId(static_cast<HANDLE>(ggui.process))));
#else
    // The stub lives until the test creates the release file: no timing decides how long it runs.
    // The guard kills it when a failed check ends the test early.
    const fs::path release = root.path / "release-ggui-stub";
    const StubGuard guard(root.path / "stub.pid", {release.filename().string()});
    GG_REQUIRE(gg::spawnDetached({"/bin/sh", "-c",
                   "i=0; echo $$ > '" + (root.path / "stub.pid").string() + "'; while [ ! -e '" + release.string() +
                       "' ] && [ $((i += 1)) -lt 1200 ]; do sleep 0.05; done"},
        ggui, error));
    const std::string pid = std::to_string(ggui.pid);
#endif
    gg::DetachedProcess script;
    GG_REQUIRE(startUpdateScript(dir, pid, script));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const fs::path stage = up::stageDir(dir);
    GG_CHECK(gg::stillRunning(ggui));
    GG_CHECK(gg::stillRunning(script));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK(!pathExists(stage / "result.txt"));
#ifndef _WIN32
    std::ofstream(release) << "go\n"; // ggui ends now
#endif
    bool ended = false;
    for (int i = 0; i < 240 && !ended; ++i) {
        // An ended process with an open handle can still be listed: close the handle at once.
        if (!gg::stillRunning(ggui)) {
            gg::release(ggui);
        }
        ended = !gg::stillRunning(script);
        if (!ended) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    GG_REQUIRE(ended);
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "new");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "new");
    GG_CHECK(startsWith(readFile(stage / "result.txt"), "ok"));
    gg::release(ggui);
}

GG_TEST("selfupdate", "the update script keeps the old files when the first rename fails")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true);
#ifdef _WIN32
    // An open file cannot be renamed: the first step, the backup of ggui, fails.
    std::ifstream lock(dir / kGgui, std::ios::binary);
    GG_REQUIRE(lock.is_open());
#else
    if (geteuid() == 0) {
        markCurrentTestSkipped("root can rename in any folder");
        return;
    }
    // The stage inside stays writable for the result: only the install folder itself is locked.
    std::error_code ec;
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
#endif
    const bool ended = runUpdateScript(dir);
#ifdef _WIN32
    lock.close();
#else
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    GG_REQUIRE(ended);
    const fs::path stage = up::stageDir(dir);
    GG_CHECK(startsWith(readFile(stage / "result.txt"), "failed: backup ggui"));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    GG_CHECK(!pathExists(dir / (std::string(kGitGg) + ".old")));
    GG_CHECK_STR_EQ(readFile(stage / "new" / kGgui), "new");
    GG_CHECK_STR_EQ(readFile(stage / "new" / kGitGg), "new");
}

// The product refuses '%' and '"' in the script path; it accepts space, & ^ ! ' , ; = and non-ASCII.
GG_TEST("selfupdate", "the update script runs in an install folder with a space and cmd metacharacters")
{
    for (const char* name : {"a b", "a&b", "a^b", "a!b", "a'b,c;d=e", "a&b^c!d e"}) {
        TempRoot root;
        const fs::path dir = makeInstall(root.path, true, true, fs::path(name));
        GG_REQUIRE(runUpdateScript(dir));
        GG_CHECK_STR_EQ(readFile(up::stageDir(dir) / "result.txt").substr(0, 2), "ok");
        GG_CHECK_STR_EQ(readFile(dir / kGgui), "new");
        GG_CHECK_STR_EQ(readFile(dir / kGitGg), "new");
        GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    }
}

GG_TEST("selfupdate", "the update script restores the old files in an install folder with metacharacters")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, false, true, fs::path("a&b^c!d e"));
    GG_REQUIRE(runUpdateScript(dir));
    GG_CHECK(startsWith(readFile(up::stageDir(dir) / "result.txt"), "failed: install git-gg"));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
}

GG_TEST("selfupdate", "the update script is refused before any file changes in a folder with a percent sign")
{
    TempRoot root;
    const fs::path dir = makeInstall(root.path, true, true, fs::path("a%b"));
    const fs::path stage = up::stageDir(dir);
    GG_REQUIRE(up::writeScript(stage).empty());
    gg::DetachedProcess child;
    std::string error;
    GG_CHECK(!gg::spawnScriptDetached(up::scriptPath(stage), {"2147483647", "nostart"}, child, error));
    GG_CHECK(!error.empty());
    GG_CHECK(!pathExists(stage / "result.txt"));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "old");
    GG_CHECK_STR_EQ(readFile(dir / kGitGg), "old");
    GG_CHECK_STR_EQ(readFile(stage / "new" / kGgui), "new");
    GG_CHECK(!gg::isSafeScriptPath("a\"b"));
    GG_CHECK(gg::isSafeScriptPath("a b&c^d!e"));
}

GG_TEST("selfupdate", "spawnScriptDetached rejects an unsafe argument")
{
    gg::DetachedProcess child;
    std::string error;
    for (const char* bad : {"", "a b", "a\"b", "a&b", "a%b", "a=b", "a,b", "a;b", "\xc3\xa4"}) {
        error.clear();
        GG_CHECK(!gg::spawnScriptDetached("unused", {bad}, child, error));
        GG_CHECK(!error.empty());
    }
}

GG_TEST("selfupdate", "takeResult returns the result and removes the stage and the .old files")
{
    TempRoot root;
    const fs::path dir = root.path;
    writeFile(dir / kGgui, "new");
    writeFile(dir / kGitGg, "new");
    writeFile(dir / (std::string(kGgui) + ".old"), "old");
    writeFile(dir / (std::string(kGitGg) + ".old"), "old");
    writeFile(up::stageDir(dir) / "new" / kGgui, "x");
    writeFile(up::stageDir(dir) / "result.txt", "failed: install ggui \r\n");
    const auto result = up::takeResult(dir);
    GG_REQUIRE(result.has_value());
    GG_CHECK_STR_EQ(*result, "failed: install ggui");
    GG_CHECK(!pathExists(up::stageDir(dir)));
    GG_CHECK(!pathExists(dir / (std::string(kGgui) + ".old")));
    GG_CHECK(!pathExists(dir / (std::string(kGitGg) + ".old")));
    GG_CHECK_STR_EQ(readFile(dir / kGgui), "new");
    GG_CHECK(!up::takeResult(dir).has_value());
}

GG_TEST("selfupdate", "takeResult keeps an .old file whose live file is missing")
{
    TempRoot root;
    const fs::path dir = root.path;
    writeFile(dir / (std::string(kGgui) + ".old"), "old");
    writeFile(up::stageDir(dir) / "result.txt", "ok");
    GG_CHECK(up::takeResult(dir).has_value());
    GG_CHECK(pathExists(dir / (std::string(kGgui) + ".old")));
}

GG_TEST("selfupdate", "takeResult deletes an old stage without a result and keeps a young one")
{
    TempRoot root;
    const fs::path stage = up::stageDir(root.path);
    writeFile(stage / "new" / kGgui, "x");
    writeFile(up::scriptPath(stage), "script");
    // Young: a running script may still own it.
    GG_CHECK(!up::takeResult(root.path).has_value());
    GG_CHECK(pathExists(stage / "new" / kGgui));
    std::error_code ec;
    fs::last_write_time(up::scriptPath(stage), fs::file_time_type::clock::now() - std::chrono::minutes(9), ec);
    GG_CHECK(!up::takeResult(root.path).has_value());
    GG_CHECK(pathExists(stage));
    // Old: nothing runs any more.
    fs::last_write_time(up::scriptPath(stage), fs::file_time_type::clock::now() - std::chrono::minutes(11), ec);
    GG_CHECK(!up::takeResult(root.path).has_value());
    GG_CHECK(!pathExists(stage));
    GG_CHECK(!up::takeResult(root.path).has_value());
}

} // namespace ggtest
