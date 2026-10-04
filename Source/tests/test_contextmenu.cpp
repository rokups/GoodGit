// "Add "Open in GoodGit" to file manager menus" (Settings > General): the Dolphin, Nemo and Nautilus
// files (platform/ContextMenu.hpp).
#include "shell/App.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <fstream>
#include <sstream>

namespace ggtest {

namespace {

using ggui::ContextMenuSupport;

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

} // namespace

GG_TEST("contextmenu", "write, read and remove round trip in an injected data home")
{
    const std::string data = s.path("xdg-data").string();
    const std::string exe = "/opt/good git/bin/ggui";
    const auto files = ggui::contextMenuFiles(data, exe);
    GG_REQUIRE(files.size() == 3);
    GG_CHECK(fs::path(files[0].path) == fs::path(data) / "kio" / "servicemenus" / "goodgit-open.desktop");
    GG_CHECK(fs::path(files[1].path) == fs::path(data) / "nemo" / "actions" / "goodgit-open.nemo_action");
    GG_CHECK(fs::path(files[2].path) == fs::path(data) / "nautilus" / "scripts" / "Open in GoodGit");
    GG_CHECK(!ggui::readContextMenu(data, exe).present);
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, exe, true), "");
    for (const auto& f : files) {
        GG_CHECK(fs::is_regular_file(f.path));
        GG_CHECK(!fs::exists(f.path + ".tmp"));
        const std::string text = slurp(f.path);
        GG_CHECK(text.find("Managed by GoodGit") != std::string::npos);
        GG_CHECK(text.find("\"/opt/good git/bin/ggui\"") != std::string::npos);
        GG_CHECK_STR_EQ(text, f.content);
    }
    const std::string desktop = slurp(files[0].path);
    GG_CHECK(desktop.find("MimeType=inode/directory;\n") != std::string::npos);
    GG_CHECK(desktop.find("Actions=openInGoodGit;\n") != std::string::npos);
    GG_CHECK(desktop.find("X-KDE-ServiceTypes=KonqPopupMenu/Plugin\n") != std::string::npos);
    GG_CHECK(desktop.find("Exec=\"/opt/good git/bin/ggui\" %f\n") != std::string::npos);
    // Nemo passes a folder with spaces as one argument.
    GG_CHECK(slurp(files[1].path).find("\nQuote=double\n") != std::string::npos);
    // The script makes a relative folder absolute, so a name like -wip is not an option.
    const std::string script = slurp(files[2].path);
    GG_CHECK(script.rfind("#!/bin/sh\n", 0) == 0);
    GG_CHECK(script.find("case \"$d\" in /*) ;; *) d=\"$PWD/$d\";; esac\n") != std::string::npos);
    GG_CHECK(script.find("exec \"/opt/good git/bin/ggui\" \"$d\"\n") != std::string::npos);
#ifndef _WIN32
    GG_CHECK((fs::status(files[0].path).permissions() & fs::perms::owner_exec) != fs::perms::none);
    GG_CHECK((fs::status(files[2].path).permissions() & fs::perms::owner_exec) != fs::perms::none);
    GG_CHECK((fs::status(files[1].path).permissions() & fs::perms::owner_exec) == fs::perms::none);
#endif
    auto st = ggui::readContextMenu(data, exe);
    GG_CHECK(st.present && st.enabled);
    // Writing again is fine (rewrite).
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, exe, true), "");
    GG_CHECK(ggui::readContextMenu(data, exe).enabled);
    // Removing deletes the three files and leaves the directories.
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, exe, false), "");
    for (const auto& f : files) {
        GG_CHECK(!fs::exists(f.path));
        GG_CHECK(fs::is_directory(fs::path(f.path).parent_path()));
    }
    st = ggui::readContextMenu(data, exe);
    GG_CHECK(!st.present && !st.enabled);
    // Removing when absent is not an error.
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, exe, false), "");
}

GG_TEST("contextmenu", "files for another executable or a missing file are reported and rewritten")
{
    const std::string data = s.path("xdg-data").string();
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/old/ggui", true), "");
    auto st = ggui::readContextMenu(data, "/new/ggui");
    GG_CHECK(st.present && !st.enabled);
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/new/ggui", true), "");
    GG_CHECK(ggui::readContextMenu(data, "/new/ggui").enabled);
    // One file missing: present, not enabled.
    const auto files = ggui::contextMenuFiles(data, "/new/ggui");
    fs::remove(files[1].path);
    st = ggui::readContextMenu(data, "/new/ggui");
    GG_CHECK(st.present && !st.enabled);
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/new/ggui", true), "");
    GG_CHECK(ggui::readContextMenu(data, "/new/ggui").enabled);
    // A hand-edited file is not the expected content either.
    std::ofstream(files[0].path) << "# edited\n";
    st = ggui::readContextMenu(data, "/new/ggui");
    GG_CHECK(st.present && !st.enabled);
}

GG_TEST("contextmenu", "removing tries every file and reports the first failure")
{
    const std::string data = s.path("xdg-data").string();
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/ggui", true), "");
    const auto files = ggui::contextMenuFiles(data, "/opt/ggui");
    // The first target becomes a non-empty directory, which fs::remove cannot delete.
    fs::remove(files[0].path);
    fs::create_directories(fs::path(files[0].path) / "keep");
    const std::string err = ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/ggui", false);
    GG_CHECK(!err.empty());
    GG_CHECK(err.find(files[0].path) != std::string::npos);
    GG_CHECK(fs::exists(files[0].path));
    GG_CHECK(!fs::exists(files[1].path));
    GG_CHECK(!fs::exists(files[2].path));
}

#ifndef _WIN32
GG_TEST("contextmenu", "an executable file without the exec bit is present but not enabled, and enabling repairs it")
{
    const std::string data = s.path("xdg-data").string();
    GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/ggui", true), "");
    const auto files = ggui::contextMenuFiles(data, "/opt/ggui");
    for (const int i : {0, 2}) {
        fs::permissions(files[i].path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::remove);
        const auto st = ggui::readContextMenu(data, "/opt/ggui");
        GG_CHECK(st.present && !st.enabled);
        GG_CHECK_STR_EQ(slurp(files[i].path), files[i].content);
        GG_CHECK_STR_EQ(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/ggui", true), "");
        GG_CHECK(ggui::readContextMenu(data, "/opt/ggui").enabled);
        GG_CHECK((fs::status(files[i].path).permissions() & fs::perms::owner_exec) != fs::perms::none);
    }
}
#endif

GG_TEST("contextmenu", "refuses an executable path a desktop Exec line or the script would misread")
{
    const std::string data = s.path("xdg-data").string();
    for (const char* bad : {"", "/opt/a\"b/ggui", "/opt/it's/ggui", "/opt/a\\b/ggui", "/opt/$HOME/ggui", "/opt/`x`/ggui",
             "/opt/100%/ggui", "/opt/a\nb/ggui", "/opt/a\rb/ggui"}) {
        const std::string err = ggui::writeContextMenu(ContextMenuSupport::Available, data, bad, true);
        GG_CHECK(!err.empty());
        GG_CHECK(!fs::exists(data));
    }
}

GG_TEST("contextmenu", "unavailable off Linux: nothing is written")
{
    const std::string data = s.path("xdg-data").string();
    GG_CHECK(!ggui::contextMenuUnavailableReason(ContextMenuSupport::Unsupported).empty());
    GG_CHECK(ggui::contextMenuUnavailableReason(ContextMenuSupport::Available).empty());
    GG_CHECK(!ggui::writeContextMenu(ContextMenuSupport::Unsupported, data, "/opt/ggui", true).empty());
    GG_CHECK(!ggui::writeContextMenu(ContextMenuSupport::Unsupported, data, "/opt/ggui", false).empty());
    GG_CHECK(!fs::exists(data));
#ifdef __linux__
    GG_CHECK(ggui::contextMenuSupport() == ContextMenuSupport::Available);
#else
    GG_CHECK(ggui::contextMenuSupport() == ContextMenuSupport::Unsupported);
#endif
}

GG_TEST("contextmenu", "data home follows XDG_DATA_HOME, else ~/.local/share")
{
    const std::string xdg = ggui::getEnv("XDG_DATA_HOME");
    const std::string home = ggui::getEnv("HOME");
    ggui::setEnv("XDG_DATA_HOME", "/tmp/x-data");
    GG_CHECK_STR_EQ(ggui::contextMenuDataHome(), "/tmp/x-data");
    ggui::setEnv("XDG_DATA_HOME", "relative/ignored");
    ggui::setEnv("HOME", "/tmp/x-home");
    GG_CHECK_STR_EQ(ggui::contextMenuDataHome(), "/tmp/x-home/.local/share");
    ggui::unsetEnv("XDG_DATA_HOME");
    GG_CHECK_STR_EQ(ggui::contextMenuDataHome(), "/tmp/x-home/.local/share");
    ggui::setEnv("HOME", home);
    if (!xdg.empty())
        ggui::setEnv("XDG_DATA_HOME", xdg);
}

GG_TEST("contextmenu", "Settings toggles the files (Linux only, else the control is disabled)")
{
    const std::string savedXdg = ggui::getEnv("XDG_DATA_HOME");
    const std::string data = s.path("xdg-data").string();
    ggui::setEnv("XDG_DATA_HOME", data);
    s.app.openSettings();
    ctx->Yield(2);
    const char* box = "//Settings/##settings_tabs/General/Add \"Open in GoodGit\" to file manager menus##context_menu";
    if (ggui::contextMenuSupport() != ContextMenuSupport::Available) {
        GG_CHECK(ctx->ItemInfo(box).ItemFlags & ImGuiItemFlags_Disabled);
    } else {
        const std::string exe = ggui::executablePath();
        GG_CHECK(!exe.empty());
        ctx->ItemClick(box);
        ctx->Yield(2);
        GG_CHECK(ggui::readContextMenu(data, exe).enabled);
        for (const auto& f : ggui::contextMenuFiles(data, exe))
            GG_CHECK(fs::is_regular_file(f.path));
        ctx->ItemClick(box);
        ctx->Yield(2);
        GG_CHECK(!ggui::readContextMenu(data, exe).present);
        // Files naming another GoodGit: a warning with Update and Remove.
        const char* update = "//Settings/##settings_tabs/General/Update##context_menu_update";
        const char* remove = "//Settings/##settings_tabs/General/Remove##context_menu_remove";
        GG_CHECK(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/elsewhere/ggui", true).empty());
        ctx->WindowClose("//Settings");
        s.app.openSettings();
        ctx->Yield(2);
        ctx->ItemClick(update);
        ctx->Yield(2);
        GG_CHECK(ggui::readContextMenu(data, exe).enabled);
        GG_CHECK(!ctx->ItemExists(update));
        GG_CHECK(ggui::writeContextMenu(ContextMenuSupport::Available, data, "/opt/elsewhere/ggui", true).empty());
        ctx->WindowClose("//Settings");
        s.app.openSettings();
        ctx->Yield(2);
        ctx->ItemClick(remove);
        ctx->Yield(2);
        GG_CHECK(!ggui::readContextMenu(data, exe).present);
    }
    ctx->WindowClose("//Settings");
    if (savedXdg.empty())
        ggui::unsetEnv("XDG_DATA_HOME");
    else
        ggui::setEnv("XDG_DATA_HOME", savedXdg);
}

} // namespace ggtest
