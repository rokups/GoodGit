// Self-update in Settings > General > Updates (shell/AppSettings.cpp). The fetch function is a fake and the
// install folder is a temp folder: no test uses the network, and no test runs the update script.
#include "shell/App.hpp"
#include "tests/Harness.hpp"
#include "tests/SelfUpdateHelpers.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace ggtest {

namespace {

using namespace suhelp;
using ggui::update::SelfUpdater;

const std::string kLocalCommit(40, 'a');
const std::string kNewCommit(40, 'b');
const char* const kReleaseUrl = "https://example.test/release";
const std::string kAssetBase = "https://github.com/rokups/GoodGit/releases/download/latest/";

const char* const kTab = "//Settings/##settings_tabs/General/";

std::string ref(const char* item)
{
    return std::string(kTab) + item;
}

// A temp install folder with git-gg in it, a remote ZIP and a fake network that serves them.
struct UpdateFixture {
    TempRoot root;
    fs::path zip;
    std::string releaseJson;

    explicit UpdateFixture(const std::string& releaseCommit)
    {
        std::error_code ec;
        fs::create_directories(root.installDir(), ec);
        std::ofstream(root.installDir() / kGitGg) << "x";
        zip = root.path / "remote.zip";
        writeZip(zip, packageEntries("ggui-1-test", realGitGg()));
        up::Asset asset = assetFor(zip);
        const std::string name = "ggui-1-" + ggui::buildInfo().packageSystem + ".zip";
        releaseJson = R"({"target_commitish":")" + releaseCommit + R"(","assets":[{"name":")" + name +
                      R"(","browser_download_url":")" + kAssetBase + name + R"(","size":)" + std::to_string(asset.size) +
                      R"(,"digest":"sha256:)" + asset.sha256 + R"(","state":"uploaded"}]})";
    }

    SelfUpdater::Config config(const std::string& buildCommit) const
    {
        SelfUpdater::Config config;
        config.installDir = root.installDir();
        const std::string json = releaseJson;
        const fs::path source = zip;
        config.fetch = [json, source](const std::string& url, const fs::path& out, std::uint64_t,
                                      const gg::CancelToken&) -> std::string {
            if (url == kReleaseUrl) {
                std::ofstream(out, std::ios::binary) << json;
                return {};
            }
            std::error_code ec;
            fs::copy_file(source, out, fs::copy_options::overwrite_existing, ec);
            return ec ? "copy failed" : std::string();
        };
        config.buildInfo.version = "1";
        config.buildInfo.commit = buildCommit;
        config.buildInfo.packageSystem = ggui::buildInfo().packageSystem;
        config.releaseUrl = kReleaseUrl;
        return config;
    }
};

bool disabled(Scenario& s, const std::string& item)
{
    return (s.ctx->ItemInfo(item.c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0;
}

void openUpdates(Scenario& s, SelfUpdater::Config config)
{
    s.app.setUpdaterConfig(std::move(config));
    s.app.openSettings();
    s.ctx->Yield(3);
}

} // namespace

GG_TEST("selfupdate/ui", "the Check button is disabled for a build without a commit")
{
    UpdateFixture f(kNewCommit);
    openUpdates(s, f.config(""));
    const std::string check = ref("Check for updates##update_check");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(check.c_str()); }));
    GG_CHECK(disabled(s, check));
    GG_CHECK(!s.itemExists(ref("Download and install##update_download").c_str()));
    GG_CHECK(!s.itemExists(ref("Restart to update##update_restart").c_str()));
    GG_CHECK(s.textShown("//Settings", "This build: local build"));
    // A click on a disabled button changes nothing.
    s.ctx->ItemClick(check.c_str());
    s.ctx->Yield(3);
    GG_CHECK(!s.textShown("//Settings", "Checking"));
    GG_CHECK(!s.app.restartForUpdate());
    GG_CHECK(!s.app.updateScript().has_value());
    s.ctx->WindowClose("//Settings");
}

GG_TEST("selfupdate/ui", "an up to date build says so and offers no download")
{
    UpdateFixture f(kLocalCommit);
    openUpdates(s, f.config(kLocalCommit));
    const std::string check = ref("Check for updates##update_check");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(check.c_str()); }));
    GG_CHECK(!disabled(s, check));
    s.ctx->ItemClick(check.c_str());
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "ggui is up to date."); }));
    GG_CHECK(!s.itemExists(ref("Download and install##update_download").c_str()));
    s.ctx->WindowClose("//Settings");
}

GG_TEST("selfupdate/ui", "check, download and restart prompt")
{
    UpdateFixture f(kNewCommit);
    openUpdates(s, f.config(kLocalCommit));
    const std::string check = ref("Check for updates##update_check");
    const std::string download = ref("Download and install##update_download");
    const std::string restart = ref("Restart to update##update_restart");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(check.c_str()); }));
    GG_CHECK(s.textShown("//Settings", "This build: aaaaaaa"));
    GG_CHECK(!s.itemExists(download.c_str()));
    s.ctx->ItemClick(check.c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(download.c_str()); }));
    GG_CHECK(s.textShown("//Settings", "A newer build is available: bbbbbbb"));
    GG_CHECK(!s.itemExists(restart.c_str()));

    s.ctx->ItemClick(download.c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(restart.c_str()); }));
    GG_CHECK(s.textShown("//Settings", "The update is ready."));
    GG_CHECK(!s.itemExists(download.c_str()));
    GG_CHECK(!s.app.restartForUpdate());
    const auto script = s.app.updateScript();
    GG_REQUIRE(script.has_value());
    GG_CHECK(pathExists(*script));

    // "Later" closes the form and keeps the update staged.
    s.ctx->ItemClick(restart.c_str());
    GG_REQUIRE(s.dialogOpen("Restart ggui"));
    s.dialogButton("Restart ggui", "Later");
    GG_REQUIRE(s.waitUntil([&] { return !s.app.dialogs().anyOpen(); }));
    GG_CHECK(!s.app.restartForUpdate());
    GG_CHECK_EQ(s.app.quitRequests(), 0);
    GG_CHECK(s.itemExists(restart.c_str()));

    // "Restart now" asks main() to start the script after shutdown; the test run does not quit.
    s.ctx->ItemClick(restart.c_str());
    GG_REQUIRE(s.dialogOpen("Restart ggui"));
    s.dialogButton("Restart ggui", "Restart now");
    GG_REQUIRE(s.waitUntil([&] { return s.app.restartForUpdate(); }));
    GG_CHECK_EQ(s.app.quitRequests(), 1);
    s.app.clearQuit();
    s.ctx->WindowClose("//Settings");
}

GG_TEST("selfupdate/ui", "a failed check shows the error and allows a new check")
{
    UpdateFixture f(kNewCommit);
    SelfUpdater::Config config = f.config(kLocalCommit);
    config.fetch = [](const std::string&, const fs::path&, std::uint64_t, const gg::CancelToken&) -> std::string {
        return "no network in the test";
    };
    openUpdates(s, std::move(config));
    const std::string check = ref("Check for updates##update_check");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(check.c_str()); }));
    s.ctx->ItemClick(check.c_str());
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Settings", "no network in the test"); }));
    GG_CHECK(s.itemExists(check.c_str()));
    GG_CHECK(!disabled(s, check));
    s.ctx->WindowClose("//Settings");
}

} // namespace ggtest
