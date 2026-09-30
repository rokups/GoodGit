// "Add GoodGit to PATH" (Settings > General): the systemd environment.d file (platform/PathSetup.hpp).
#include "shell/App.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <fstream>
#include <sstream>

namespace ggtest {

namespace {

using ggui::PathSetupSupport;

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

} // namespace

GG_TEST("pathsetup", "write, read and remove round trip in an injected config home")
{
    const std::string home = s.path("xdg").string();
    const std::string dir = "/opt/good git/bin";
    const fs::path file = ggui::pathSetupFile(home);
    GG_CHECK(file == fs::path(home) / "environment.d" / "60-goodgit.conf");
    GG_CHECK(!ggui::readPathSetup(home, dir).present);
    // Creates environment.d.
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, dir, true), "");
    const std::string text = slurp(file);
    GG_CHECK(text.rfind("#", 0) == 0);
    GG_CHECK(text.find("\nPATH=/opt/good git/bin:${PATH}\n") != std::string::npos);
    GG_CHECK(!fs::exists(file.string() + ".tmp"));
    auto st = ggui::readPathSetup(home, dir);
    GG_CHECK(st.present && st.enabled && st.otherDir.empty());
    // Writing again is fine (rewrite).
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, dir, true), "");
    GG_CHECK(ggui::readPathSetup(home, dir).enabled);
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, dir, false), "");
    GG_CHECK(!fs::exists(file));
    st = ggui::readPathSetup(home, dir);
    GG_CHECK(!st.present && !st.enabled);
    // Removing when absent is not an error.
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, dir, false), "");
}

GG_TEST("pathsetup", "a file pointing to another directory is reported and rewritten")
{
    const std::string home = s.path("xdg").string();
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, "/old/bin", true), "");
    auto st = ggui::readPathSetup(home, "/new/bin");
    GG_CHECK(st.present && !st.enabled);
    GG_CHECK_STR_EQ(st.otherDir, "/old/bin");
    // A prefix of the directory is not the directory.
    GG_CHECK(!ggui::readPathSetup(home, "/old").enabled);
    GG_CHECK_STR_EQ(ggui::writePathSetup(PathSetupSupport::Available, home, "/new/bin", true), "");
    st = ggui::readPathSetup(home, "/new/bin");
    GG_CHECK(st.enabled && st.otherDir.empty());
    // A hand-edited file without a PATH line: present, points nowhere.
    fs::create_directories(fs::path(ggui::pathSetupFile(home)).parent_path());
    std::ofstream(ggui::pathSetupFile(home)) << "# nothing\n";
    st = ggui::readPathSetup(home, "/new/bin");
    GG_CHECK(st.present && !st.enabled && st.otherDir.empty());
}

GG_TEST("pathsetup", "refuses a directory environment.d would expand or misparse")
{
    const std::string home = s.path("xdg").string();
    for (const char* bad : {"/opt/$HOME/bin", "/opt/a\\b", "/opt/\"q\"", "/opt/it's", "/opt/a\nb", "/opt/a:b"}) {
        const std::string err = ggui::writePathSetup(PathSetupSupport::Available, home, bad, true);
        GG_CHECK(!err.empty());
        GG_CHECK(!fs::exists(ggui::pathSetupFile(home)));
    }
    GG_CHECK(!fs::exists(fs::path(home) / "environment.d"));
}

GG_TEST("pathsetup", "unavailable without systemd or off Linux: nothing is written")
{
    const std::string home = s.path("xdg").string();
    for (auto support : {PathSetupSupport::NoSystemd, PathSetupSupport::Unsupported}) {
        GG_CHECK(!ggui::pathSetupUnavailableReason(support).empty());
        GG_CHECK(!ggui::writePathSetup(support, home, "/opt/bin", true).empty());
        GG_CHECK(!fs::exists(home));
    }
    GG_CHECK(ggui::pathSetupUnavailableReason(PathSetupSupport::NoSystemd).find("systemd") != std::string::npos);
    // Detection: <root>/run/systemd/system must be a directory.
    const fs::path root = s.path("fakeroot");
    fs::create_directories(root / "run" / "systemd");
    const PathSetupSupport without = ggui::pathSetupSupport(root.string());
#ifdef __linux__
    GG_CHECK(without == PathSetupSupport::NoSystemd);
    std::ofstream(root / "run" / "systemd" / "system") << "not a dir";
    GG_CHECK(ggui::pathSetupSupport(root.string()) == PathSetupSupport::NoSystemd);
    fs::remove(root / "run" / "systemd" / "system");
    fs::create_directories(root / "run" / "systemd" / "system");
    GG_CHECK(ggui::pathSetupSupport(root.string()) == PathSetupSupport::Available);
#else
    GG_CHECK(without == PathSetupSupport::Unsupported);
#endif
}

GG_TEST("pathsetup", "config home follows XDG_CONFIG_HOME, else ~/.config")
{
    const std::string xdg = ggui::getEnv("XDG_CONFIG_HOME");
    const std::string home = ggui::getEnv("HOME");
    ggui::setEnv("XDG_CONFIG_HOME", "/tmp/x-cfg");
    GG_CHECK_STR_EQ(ggui::pathSetupConfigHome(), "/tmp/x-cfg");
    ggui::setEnv("XDG_CONFIG_HOME", "relative/ignored");
    ggui::setEnv("HOME", "/tmp/x-home");
    GG_CHECK_STR_EQ(ggui::pathSetupConfigHome(), "/tmp/x-home/.config");
    ggui::unsetEnv("XDG_CONFIG_HOME");
    GG_CHECK_STR_EQ(ggui::pathSetupConfigHome(), "/tmp/x-home/.config");
    ggui::setEnv("HOME", home);
    if (!xdg.empty())
        ggui::setEnv("XDG_CONFIG_HOME", xdg);
}

GG_TEST("pathsetup", "Settings toggles the file (needs systemd, else the control is disabled)")
{
    const std::string savedXdg = ggui::getEnv("XDG_CONFIG_HOME");
    const std::string home = s.path("xdg").string();
    ggui::setEnv("XDG_CONFIG_HOME", home);
    s.app.openSettings();
    ctx->Yield(2);
    const char* box = "//Settings/##settings_tabs/General/Add GoodGit to PATH##add_to_path";
    if (ggui::pathSetupSupport() != PathSetupSupport::Available) {
        GG_CHECK(ctx->ItemInfo(box).ItemFlags & ImGuiItemFlags_Disabled);
    } else {
        const std::string dir = ggui::executableDir();
        ctx->ItemClick(box);
        ctx->Yield(2);
        GG_CHECK(ggui::readPathSetup(home, dir).enabled);
        ctx->ItemClick(box);
        ctx->Yield(2);
        GG_CHECK(!ggui::readPathSetup(home, dir).present);
        // A file naming another GoodGit: a warning with Point to this GoodGit and Remove.
        const char* repoint = "//Settings/##settings_tabs/General/Point to this GoodGit##path_repoint";
        const char* remove = "//Settings/##settings_tabs/General/Remove the file##path_remove";
        GG_CHECK(ggui::writePathSetup(PathSetupSupport::Available, home, "/opt/elsewhere", true).empty());
        ctx->WindowClose("//Settings");
        s.app.openSettings();
        ctx->Yield(2);
        ctx->ItemClick(repoint);
        ctx->Yield(2);
        GG_CHECK(ggui::readPathSetup(home, dir).enabled);
        GG_CHECK(!ctx->ItemExists(repoint));
        GG_CHECK(ggui::writePathSetup(PathSetupSupport::Available, home, "/opt/elsewhere", true).empty());
        ctx->WindowClose("//Settings");
        s.app.openSettings();
        ctx->Yield(2);
        ctx->ItemClick(remove);
        ctx->Yield(2);
        GG_CHECK(!ggui::readPathSetup(home, dir).present);
    }
    ctx->WindowClose("//Settings");
    if (savedXdg.empty())
        ggui::unsetEnv("XDG_CONFIG_HOME");
    else
        ggui::setEnv("XDG_CONFIG_HOME", savedXdg);
}

} // namespace ggtest
