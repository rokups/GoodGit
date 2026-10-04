#include "platform/ContextMenu.hpp"

#include "util/Env.hpp"

#include <libgg/Files.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ggui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kManaged = "# Managed by GoodGit (ggui): Settings > General > Add \"Open in GoodGit\" to file manager "
                                 "menus. Delete this file to undo.\n";

} // namespace

ContextMenuSupport contextMenuSupport()
{
#ifdef __linux__
    return ContextMenuSupport::Available;
#else
    return ContextMenuSupport::Unsupported;
#endif
}

std::string contextMenuUnavailableReason(ContextMenuSupport support)
{
    switch (support) {
    case ContextMenuSupport::Available:
        return {};
    case ContextMenuSupport::Unsupported:
        break;
    }
    return "Adding GoodGit to file manager menus is not supported on this platform.";
}

std::string contextMenuDataHome()
{
    const std::string xdg = getEnv("XDG_DATA_HOME");
    if (!xdg.empty() && xdg.front() == '/')
        return xdg;
    const std::string home = getEnv("HOME");
    return home.empty() ? std::string() : home + "/.local/share";
}

std::vector<ContextMenuFile> contextMenuFiles(const std::string& dataHome, const std::string& exe)
{
    const fs::path base(dataHome);
    std::vector<ContextMenuFile> files;
    files.push_back({(base / "kio" / "servicemenus" / "goodgit-open.desktop").string(),
        std::string(kManaged)
            + "[Desktop Entry]\n"
              "Type=Service\n"
              "MimeType=inode/directory;\n"
              "Actions=openInGoodGit;\n"
              "X-KDE-ServiceTypes=KonqPopupMenu/Plugin\n"
              "X-KDE-Priority=TopLevel\n"
              "\n"
              "[Desktop Action openInGoodGit]\n"
              "Name=Open in GoodGit\n"
              "Icon=ggui\n"
              "Exec=\""
            + exe + "\" %f\n",
        true});
    files.push_back({(base / "nemo" / "actions" / "goodgit-open.nemo_action").string(),
        std::string(kManaged)
            + "[Nemo Action]\n"
              "Name=Open in GoodGit\n"
              "Comment=Open this folder in GoodGit\n"
              "Exec=\""
            + exe
            + "\" %F\n"
              "Quote=double\n"
              "Icon-Name=ggui\n"
              "Selection=s\n"
              "Extensions=dir;\n",
        false});
    files.push_back({(base / "nautilus" / "scripts" / "Open in GoodGit").string(),
        std::string("#!/bin/sh\n") + kManaged
            + "d=\"${1:-$PWD}\"\n"
              "case \"$d\" in /*) ;; *) d=\"$PWD/$d\";; esac\n"
              "exec \""
            + exe + "\" \"$d\"\n",
        true});
    return files;
}

ContextMenuState readContextMenu(const std::string& dataHome, const std::string& exe)
{
    ContextMenuState st;
    if (dataHome.empty())
        return st;
    st.enabled = true;
    for (const ContextMenuFile& f : contextMenuFiles(dataHome, exe)) {
        std::ifstream in(f.path, std::ios::binary);
        if (!in) {
            st.enabled = false;
            continue;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        st.present = true;
        if (buf.str() != f.content)
            st.enabled = false;
#ifndef _WIN32
        std::error_code ec;
        if (f.executable && (fs::status(f.path, ec).permissions() & fs::perms::owner_exec) == fs::perms::none)
            st.enabled = false;
#endif
    }
    return st;
}

std::string writeContextMenu(
    ContextMenuSupport support, const std::string& dataHome, const std::string& exe, bool enable)
{
    if (support != ContextMenuSupport::Available)
        return contextMenuUnavailableReason(support);
    if (dataHome.empty())
        return "Cannot find the user data directory (neither XDG_DATA_HOME nor HOME is set).";
    std::error_code ec;
    if (!enable) {
        // Every file is tried; the first failure is the one reported.
        std::string firstError;
        for (const ContextMenuFile& f : contextMenuFiles(dataHome, exe)) {
            fs::remove(fs::path(f.path), ec);
            if (ec && firstError.empty())
                firstError = "Cannot remove " + f.path + ": " + ec.message();
        }
        return firstError;
    }
    if (exe.empty())
        return "Cannot tell where the GoodGit executable is.";
    if (exe.find_first_of("\"'\\$`%\n\r") != std::string::npos)
        return "The GoodGit executable (" + exe
            + ") has a character (one of \" ' \\ $ ` % or a line break) that a file manager menu entry would "
              "expand or misread, so it was not added to the file manager menus. Move GoodGit to a plainer path.";
    for (const ContextMenuFile& f : contextMenuFiles(dataHome, exe)) {
        const fs::path file = f.path;
        fs::create_directories(file.parent_path(), ec);
        if (ec)
            return "Cannot create " + file.parent_path().string() + ": " + ec.message();
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return "Cannot write " + tmp.string() + ": " + std::strerror(errno);
            out << f.content;
            out.flush();
            if (!out) {
                out.close();
                fs::remove(tmp, ec);
                return "Cannot write " + tmp.string() + ".";
            }
        }
        using fs::perms;
        const perms mode = perms::owner_read | perms::owner_write | perms::group_read | perms::others_read
            | (f.executable ? perms::owner_exec | perms::group_exec | perms::others_exec : perms::none);
        fs::permissions(tmp, mode, ec);
        if (ec) {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return "Cannot set the permissions of " + tmp.string() + ": " + ec.message();
        }
        ec = gg::replaceFile(tmp, file);
        if (ec) {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return "Cannot write " + file.string() + ": " + ec.message();
        }
    }
    return {};
}

} // namespace ggui
