// Self-update: the background updater (platform/SelfUpdater.hpp). The fetch function is a fake: no
// test uses the network.
#include "platform/SelfUpdater.hpp"
#include "tests/Harness.hpp"
#include "tests/SelfUpdateHelpers.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ggtest {

namespace {

using namespace suhelp;
using ggui::update::SelfUpdater;
using State = SelfUpdater::State;

const std::string kLocalCommit(40, 'a');
const std::string kNewCommit(40, 'b');
const char* const kReleaseUrl = "https://example.test/release";
const std::string kAssetBase = "https://github.com/rokups/GoodGit/releases/download/latest/";

// What the fake network does. The worker threads and the test thread share it.
struct FakeNet {
    std::string releaseJson;
    std::string releaseError; // not empty: the release request fails with this text
    fs::path zipSource;       // the file that an asset request copies
    bool blockRelease = false;
    bool blockDownload = false;
    std::atomic<bool> started{false}; // a blocking request is running
    std::atomic<bool> finished{false}; // a blocking request returned
    std::atomic<int> releaseCalls{0};
    std::atomic<int> downloadCalls{0};
    std::atomic<std::uint64_t> releaseMax{0};
    std::atomic<std::uint64_t> downloadMax{0};

    // Blocks until the token is cancelled (at most 10 s) and removes `out`.
    std::string block(const fs::path& out, const gg::CancelToken& cancel, bool writeBytes)
    {
        if (writeBytes) {
            std::ofstream(out, std::ios::binary) << std::string(50, 'x');
        }
        started = true;
        for (int i = 0; i < 1000 && !cancel.cancelled(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::error_code ec;
        fs::remove(out, ec);
        finished = true;
        return "cancelled";
    }

    ggui::update::FetchFn fn()
    {
        return [this](const std::string& url, const fs::path& out, std::uint64_t maxBytes,
                      const gg::CancelToken& cancel) -> std::string {
            if (url == kReleaseUrl) {
                ++releaseCalls;
                releaseMax = maxBytes;
                if (blockRelease) {
                    return block(out, cancel, false);
                }
                if (!releaseError.empty()) {
                    return releaseError;
                }
                std::ofstream(out, std::ios::binary) << releaseJson;
                return {};
            }
            ++downloadCalls;
            downloadMax = maxBytes;
            if (blockDownload) {
                return block(out, cancel, true);
            }
            std::error_code ec;
            fs::copy_file(zipSource, out, fs::copy_options::overwrite_existing, ec);
            return ec ? "copy failed" : std::string();
        };
    }
};

// A temp install folder with git-gg in it, a remote ZIP and a fake network.
struct Fixture {
    TempRoot root;
    std::shared_ptr<FakeNet> net = std::make_shared<FakeNet>();
    up::Asset asset;
    std::string assetName;

    explicit Fixture(const std::string& gitGg = "x", const std::string& releaseCommit = kNewCommit)
    {
        std::error_code ec;
        fs::create_directories(root.installDir(), ec);
        std::ofstream(root.installDir() / kGitGg) << "x";
        net->zipSource = root.path / "remote.zip";
        writeZip(net->zipSource, packageEntries("ggui-1-test", gitGg));
        asset = assetFor(net->zipSource);
        assetName = "ggui-1-" + ggui::buildInfo().packageSystem + ".zip";
        asset.name = assetName;
        setRelease(releaseCommit);
    }

    void setRelease(const std::string& commit, const std::string& digest = {}, const std::string& name = {})
    {
        net->releaseJson = R"({"target_commitish":")" + commit + R"(","assets":[{"name":")" +
                           (name.empty() ? assetName : name) + R"(","browser_download_url":")" + kAssetBase +
                           (name.empty() ? assetName : name) + R"(","size":)" + std::to_string(asset.size) +
                           R"(,"digest":"sha256:)" + (digest.empty() ? asset.sha256 : digest) +
                           R"(","state":"uploaded"}]})";
    }

    SelfUpdater::Config config() const
    {
        SelfUpdater::Config config;
        config.installDir = root.installDir();
        config.fetch = net->fn();
        config.buildInfo.version = "1";
        config.buildInfo.commit = kLocalCommit;
        config.buildInfo.packageSystem = ggui::buildInfo().packageSystem;
        config.releaseUrl = kReleaseUrl;
        return config;
    }

    fs::path workDir() const { return root.installDir() / ".ggui-download"; }
};

// Waits (steps of 5 ms, at most 10 s) until `done` is true.
template <typename Pred>
bool waitUntil(Pred done)
{
    for (int i = 0; i < 2000; ++i) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

bool waitState(const SelfUpdater& updater, State state)
{
    return waitUntil([&] { return updater.state() == state; });
}

bool waitProbeEnd(const SelfUpdater& updater)
{
    return waitUntil([&] { return !updater.probing(); });
}

// Runs a check to the end and returns the final state.
State checkToEnd(SelfUpdater& updater)
{
    if (!updater.check()) {
        return updater.state();
    }
    waitUntil([&] { return updater.state() != State::Checking; });
    return updater.state();
}

} // namespace

GG_TEST("selfupdate", "updater is off without a build commit and refuses every action")
{
    Fixture f;
    SelfUpdater::Config config = f.config();
    config.buildInfo.commit.clear();
    SelfUpdater updater(config);
    GG_CHECK(updater.state() == State::Off);
    GG_CHECK_STR_EQ(updater.offReason(), "This build does not come from a release package.");
    GG_CHECK(!updater.check());
    GG_CHECK(!updater.download());
    GG_CHECK(!updater.cancel());
    GG_CHECK(!updater.restartPending());
    GG_CHECK(!updater.readyScript().has_value());
    GG_CHECK(updater.state() == State::Off);
    GG_CHECK_EQ(f.net->releaseCalls.load(), 0);
}

GG_TEST("selfupdate", "updater is off for an install folder that is not an install")
{
    Fixture f;
    std::error_code ec;
    fs::remove(f.root.installDir() / kGitGg, ec);
    SelfUpdater updater(f.config());
    GG_CHECK(waitState(updater, State::Off));
    GG_CHECK_STR_EQ(updater.offReason(), "This is not an installed ggui.");
    GG_CHECK(!updater.probing());
    GG_CHECK(!updater.check());
    GG_CHECK_EQ(f.net->releaseCalls.load(), 0);
}

#ifndef _WIN32
GG_TEST("selfupdate", "updater is off for a read-only install folder")
{
    if (geteuid() == 0) {
        markCurrentTestSkipped("root can write to a read-only folder");
        return;
    }
    Fixture f;
    std::error_code ec;
    fs::permissions(f.root.installDir(), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
    {
        SelfUpdater updater(f.config());
        GG_CHECK(waitState(updater, State::Off));
        GG_CHECK_STR_EQ(updater.offReason(), "The install folder is not writable.");
        GG_CHECK(!updater.check());
    }
    fs::permissions(f.root.installDir(), fs::perms::owner_all, fs::perm_options::replace, ec);
}
#endif

GG_TEST("selfupdate", "updater is off without a fetch backend and names the programs")
{
    Fixture f;
    SelfUpdater::Config config = f.config();
    config.fetch = nullptr;
    config.backend = ggui::update::FetchBackend::None;
    config.detect = false;
    SelfUpdater updater(config);
    GG_CHECK(waitState(updater, State::Off));
#ifndef _WIN32
    GG_CHECK(contains(updater.offReason(), "curl, wget or libcurl"));
#endif
    GG_CHECK(!updater.offReason().empty());
    GG_CHECK(!updater.check());
}

GG_TEST("selfupdate", "updater is off when the stage path is a file")
{
    Fixture f;
    std::ofstream(up::stageDir(f.root.installDir())) << "not a folder";
    SelfUpdater updater(f.config());
    GG_CHECK(waitState(updater, State::Off));
    GG_CHECK(!updater.offReason().empty());
}

GG_TEST("selfupdate", "updater check finds the build up to date")
{
    Fixture f("x", kLocalCommit);
    SelfUpdater updater(f.config());
    GG_CHECK(updater.state() == State::Idle);
    GG_CHECK(checkToEnd(updater) == State::UpToDate);
    GG_CHECK(!updater.release().has_value());
    GG_CHECK_EQ(f.net->releaseCalls.load(), 1);
    GG_CHECK_EQ(f.net->releaseMax.load(), static_cast<std::uint64_t>(4 * 1024 * 1024));
    GG_CHECK(!pathExists(f.workDir()));
    // A new check is allowed from UpToDate.
    GG_CHECK(checkToEnd(updater) == State::UpToDate);
    GG_CHECK_EQ(f.net->releaseCalls.load(), 2);
}

GG_TEST("selfupdate", "updater check finds a newer release and its package")
{
    Fixture f;
    SelfUpdater updater(f.config());
    GG_CHECK(checkToEnd(updater) == State::Available);
    const auto info = updater.release();
    GG_REQUIRE(info.has_value());
    GG_CHECK_STR_EQ(info->commit, kNewCommit);
    GG_CHECK_STR_EQ(info->shortCommit, "bbbbbbb");
    GG_CHECK_STR_EQ(info->assetName, f.assetName);
    GG_CHECK_EQ(info->size, f.asset.size);
    GG_CHECK_EQ(updater.progress().done, static_cast<std::uint64_t>(0));
    GG_CHECK_EQ(updater.progress().total, f.asset.size);
    GG_CHECK(!pathExists(f.workDir()));
    GG_CHECK(updater.error().empty());
    GG_CHECK(updater.offReason().empty());
}

GG_TEST("selfupdate", "updater check fails with the fetch error and then retries")
{
    Fixture f;
    f.net->releaseError = "network is down";
    SelfUpdater updater(f.config());
    GG_CHECK(checkToEnd(updater) == State::Failed);
    GG_CHECK(contains(updater.error(), "network is down"));
    GG_CHECK(!pathExists(f.workDir()));
    f.net->releaseError.clear();
    GG_CHECK(checkToEnd(updater) == State::Available);
    GG_CHECK(updater.error().empty());
}

GG_TEST("selfupdate", "updater check fails on bad JSON, a bad commit and a missing package")
{
    Fixture f;
    SelfUpdater updater(f.config());
    f.net->releaseJson = "{not json";
    GG_CHECK(checkToEnd(updater) == State::Failed);
    GG_CHECK(!updater.error().empty());

    f.setRelease("not-a-commit");
    GG_CHECK(checkToEnd(updater) == State::Failed);
    GG_CHECK(contains(updater.error(), "commit"));

    f.setRelease(kNewCommit, {}, "ggui-1-other-system.zip");
    GG_CHECK(checkToEnd(updater) == State::Failed);
    GG_CHECK(contains(updater.error(), "no usable package"));
    GG_CHECK(contains(updater.error(), ggui::buildInfo().packageSystem));
}

GG_TEST("selfupdate", "updater refuses an action in the wrong state")
{
    Fixture f;
    f.net->blockRelease = true;
    SelfUpdater updater(f.config());
    GG_CHECK(!updater.download());
    GG_CHECK(!updater.cancel());
    GG_CHECK(updater.state() == State::Idle);
    GG_CHECK(updater.check());
    GG_CHECK(updater.state() == State::Checking);
    GG_CHECK(!updater.check());
    GG_CHECK(!updater.download());
    GG_CHECK(waitUntil([&] { return f.net->started.load(); }));
    GG_CHECK(updater.cancel());
    GG_CHECK(waitState(updater, State::Idle));
    GG_CHECK(!pathExists(f.workDir()));
    GG_CHECK(updater.error().empty());
    // Not Available yet, so no download.
    GG_CHECK(!updater.download());
}

GG_TEST("selfupdate", "updater download stages the update and gets ready")
{
    const std::string gitGg = realGitGg();
    if (gitGg.empty()) {
        markCurrentTestSkipped("git-gg is not next to the test binary");
        return;
    }
    Fixture f(gitGg);
    SelfUpdater updater(f.config());
    GG_CHECK(!updater.restartPending());
    GG_CHECK(checkToEnd(updater) == State::Available);
    GG_CHECK(updater.download());
    GG_CHECK(!updater.download());
    GG_CHECK(!updater.check());
    GG_CHECK(waitUntil([&] { return updater.state() != State::Downloading; }));
    GG_CHECK(updater.state() == State::Ready);
    GG_CHECK(updater.error().empty());
    GG_CHECK_EQ(f.net->downloadMax.load(), f.asset.size);
    const fs::path stage = up::stageDir(f.root.installDir());
    GG_CHECK_STR_EQ(readFile(stage / "new" / kGgui), "ggui bytes");
    GG_CHECK(readFile(stage / "new" / kGitGg) == gitGg);
    GG_CHECK(pathExists(up::scriptPath(stage)));
#ifdef _WIN32
    const std::string scriptText = up::windowsUpdateScript();
#else
    const std::string scriptText = up::unixUpdateScript();
#endif
    GG_CHECK_STR_EQ(readFile(up::scriptPath(stage)), scriptText);
    GG_CHECK(updater.restartPending());
    const auto script = updater.readyScript();
    GG_REQUIRE(script.has_value());
    GG_CHECK(*script == up::scriptPath(stage));
    GG_CHECK(updater.release().has_value());
    GG_CHECK_EQ(updater.progress().done, f.asset.size);
    // The downloaded ZIP and its folder are gone.
    GG_CHECK(!pathExists(f.workDir()));
    // Ready allows no new action.
    GG_CHECK(!updater.check());
    GG_CHECK(!updater.download());
    GG_CHECK(!updater.cancel());
}

GG_TEST("selfupdate", "updater download fails on a wrong digest and keeps nothing")
{
    Fixture f;
    f.setRelease(kNewCommit, std::string(64, 'c'));
    SelfUpdater updater(f.config());
    GG_CHECK(checkToEnd(updater) == State::Available);
    GG_CHECK(updater.download());
    GG_CHECK(waitUntil([&] { return updater.state() != State::Downloading; }));
    GG_CHECK(updater.state() == State::Failed);
    GG_CHECK(!updater.error().empty());
    GG_CHECK(!updater.restartPending());
    GG_CHECK(!updater.readyScript().has_value());
    GG_CHECK(!pathExists(up::stageDir(f.root.installDir())));
    GG_CHECK(!pathExists(f.workDir()));
}

GG_TEST("selfupdate", "updater download fails with the fetch error")
{
    Fixture f;
    SelfUpdater updater(f.config());
    GG_CHECK(checkToEnd(updater) == State::Available);
    std::error_code ec;
    fs::remove(f.net->zipSource, ec); // the fake copy fails
    GG_CHECK(updater.download());
    GG_CHECK(waitUntil([&] { return updater.state() != State::Downloading; }));
    GG_CHECK(updater.state() == State::Failed);
    GG_CHECK(contains(updater.error(), "copy failed"));
    GG_CHECK(!pathExists(f.workDir()));
    // A failed download does not block a new check.
    GG_CHECK(checkToEnd(updater) == State::Available);
}

GG_TEST("selfupdate", "updater cancel stops a download, shows progress and returns to Available")
{
    Fixture f;
    SelfUpdater updater(f.config());
    GG_CHECK(checkToEnd(updater) == State::Available);
    f.net->blockDownload = true;
    GG_CHECK(updater.download());
    GG_CHECK(updater.state() == State::Downloading);
    GG_CHECK(waitUntil([&] { return f.net->started.load(); }));
    // The fake wrote 50 bytes of the asset.
    GG_CHECK_EQ(updater.progress().done, static_cast<std::uint64_t>(50));
    GG_CHECK_EQ(updater.progress().total, f.asset.size);
    GG_CHECK(updater.progress().fraction() > 0.0 && updater.progress().fraction() < 1.0);
    GG_CHECK(updater.cancel());
    GG_CHECK(waitState(updater, State::Available));
    GG_CHECK(updater.error().empty());
    GG_CHECK(updater.release().has_value());
    GG_CHECK(!pathExists(f.workDir()));
    GG_CHECK(!pathExists(up::stageDir(f.root.installDir())));
    // The download starts again from Available.
    f.net->blockDownload = false;
    GG_CHECK(updater.download());
    GG_CHECK(waitUntil([&] { return updater.state() != State::Downloading; }));
}

GG_TEST("selfupdate", "updater destructor joins a running check")
{
    Fixture f;
    f.net->blockRelease = true;
    {
        SelfUpdater updater(f.config());
        GG_CHECK(updater.check());
        GG_CHECK(waitUntil([&] { return f.net->started.load(); }));
        GG_CHECK(!f.net->finished.load());
    }
    // The destructor cancelled the request and waited for the worker.
    GG_CHECK(f.net->finished.load());
}

GG_TEST("selfupdate", "updater destructor joins a running download")
{
    Fixture f;
    {
        SelfUpdater updater(f.config());
        GG_CHECK(checkToEnd(updater) == State::Available);
        f.net->blockDownload = true;
        GG_CHECK(updater.download());
        GG_CHECK(waitUntil([&] { return f.net->started.load(); }));
        GG_CHECK(!f.net->finished.load());
    }
    GG_CHECK(f.net->finished.load());
    GG_CHECK(!pathExists(f.workDir()));
}

GG_TEST("selfupdate", "updater destructor is safe right after the constructor")
{
    Fixture f;
    for (int i = 0; i < 20; ++i) {
        SelfUpdater updater(f.config());
        if (i % 2 == 0) {
            updater.check();
        }
    }
    GG_CHECK(!pathExists(f.workDir()));
}

GG_TEST("selfupdate", "updater check waits for the probe and ends in Off when the probe finds a problem")
{
    Fixture f;
    std::error_code ec;
    fs::remove(f.root.installDir() / kGitGg, ec);
    SelfUpdater updater(f.config());
    updater.check(); // may run before or after the probe: the result is the same
    GG_CHECK(waitProbeEnd(updater));
    GG_CHECK(waitState(updater, State::Off));
    GG_CHECK_EQ(f.net->releaseCalls.load(), 0);
}

} // namespace ggtest
