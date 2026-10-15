// Background self-updater: checks the latest release, downloads the package and stages it. The
// public functions belong to the UI thread (call them from one thread only). The work runs on
// threads that the class owns: a probe thread and one job thread at a time. The destructor cancels
// the work and joins every thread. No thread is detached. No UI code is here.
//
// Probe: the constructor starts a probe thread. It finds the Off reason (install folder, fetch
// backend). detectFetch() runs on this thread, never on the UI thread. While the probe runs, state()
// is Idle and probing() is true. A check() in this time is accepted and waits for the probe. When the
// probe finds a reason, state() becomes Off. Only the missing build commit is known at once: the
// constructor sets Off for it, without disk access.
//
// Work folder: <installDir>/.ggui-download. It is new for each job and is removed at the end of the
// job. On Linux it is made with mode 0700 (mkdir), so only the user can write in it. On Windows it
// takes the rights of the install folder. It holds the release JSON and the downloaded ZIP. It is
// not inside the stage folder (<installDir>/.ggui-update). The stage folder is made by stageUpdate.
//
// A stage that an old run left is not handled here: stageUpdate removes it before a new staging, and
// takeResult (S9) reads and removes it at start.
#pragma once

#include "platform/Fetch.hpp"
#include "platform/SelfUpdate.hpp"
#include "util/BuildInfo.hpp"

#include "libgg/Cancel.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace ggui::update {

class SelfUpdater {
public:
    enum class State { Off, Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

    struct Config {
        std::filesystem::path installDir; // the folder of ggui and git-gg
        std::filesystem::path exe;        // the ggui file; empty = installDir / "ggui" ("ggui.exe")
        FetchFn fetch;                    // empty = use `backend` (or detect it when `detect` is set)
        FetchBackend backend = FetchBackend::None;
        bool detect = false;              // run detectFetch() on the probe thread and use the result
        BuildInfo buildInfo;
        std::string releaseUrl = "https://api.github.com/repos/rokups/GoodGit/releases/tags/latest";
    };

    // The real values: this exe, its folder, buildInfo(), detectFetch() on the probe thread.
    static Config defaultConfig();

    // The release that check() found newer.
    struct ReleaseInfo {
        std::string commit;      // the full commit id
        std::string shortCommit; // its first 7 characters
        std::string assetName;
        std::uint64_t size = 0;
    };

    struct Progress {
        std::uint64_t done = 0;
        std::uint64_t total = 0;
        double fraction() const { return total == 0 ? 0.0 : (done >= total ? 1.0 : double(done) / double(total)); }
    };

    explicit SelfUpdater(Config config);
    ~SelfUpdater();
    SelfUpdater(const SelfUpdater&) = delete;
    SelfUpdater& operator=(const SelfUpdater&) = delete;

    State state() const;
    bool probing() const;             // true until the probe thread ends
    std::string offReason() const;    // the text for a disabled tooltip; empty unless state() is Off
    std::string error() const;        // the text of the failure; empty unless state() is Failed
    std::optional<ReleaseInfo> release() const; // set in Available, Downloading and Ready
    Progress progress() const;        // in Downloading: the bytes in the file and the asset size

    // Idle, UpToDate, Available or Failed -> Checking. False (no change) in any other state.
    bool check();
    // Available -> Downloading, then Ready (or Failed). False in any other state.
    bool download();
    // Stops the running check (state goes back to Idle) or download (back to Available). The state
    // changes when the worker ends, not in this call. False when nothing runs.
    bool cancel();

    // True in Ready: the staged update waits for a restart.
    bool restartPending() const;
    // In Ready: the update script in the stage folder. The script is written before Ready.
    std::optional<std::filesystem::path> readyScript() const;
    std::filesystem::path stagePath() const;

private:
    void probe();
    void runCheck(gg::CancelToken cancel);
    void runDownload(gg::CancelToken cancel, Asset asset);
    void setState(State state);
    void fail(const std::string& text);
    bool waitProbe(const gg::CancelToken& cancel);
    std::filesystem::path makeWorkDir(std::string& error) const;
    void joinJob();

    const Config m_config;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    State m_state = State::Idle;
    bool m_probeDone = false;
    bool m_stopping = false;
    std::string m_offReason;
    std::string m_error;
    std::optional<ReleaseInfo> m_release;
    std::optional<Asset> m_asset;
    FetchFn m_fetch;
    gg::CancelToken m_cancel;
    std::filesystem::path m_script;
    std::filesystem::path m_zip; // the file that a running download fills

    std::atomic<std::uint64_t> m_total{0};

    std::thread m_probeThread;
    std::thread m_job;
};

} // namespace ggui::update
