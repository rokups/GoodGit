#include "platform/SelfUpdater.hpp"

#include "libgg/Launch.hpp"

#include <cerrno>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace ggui::update {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kMaxReleaseJson = 4ull * 1024 * 1024;

#ifdef _WIN32
const char* const kExeName = "ggui.exe";
#else
const char* const kExeName = "ggui";
#endif

fs::path workDirOf(const fs::path& installDir)
{
    return installDir / ".ggui-download";
}

void removeAll(const fs::path& path)
{
    std::error_code ec;
    fs::remove_all(path, ec);
}

} // namespace

SelfUpdater::Config SelfUpdater::defaultConfig()
{
    Config config;
    config.exe = gg::selfExecutable();
    config.installDir = config.exe.parent_path();
    config.buildInfo = buildInfo();
    config.detect = true;
    return config;
}

SelfUpdater::SelfUpdater(Config config) : m_config(std::move(config)), m_fetch(m_config.fetch)
{
    if (m_config.buildInfo.commit.empty()) {
        m_state = State::Off;
        m_offReason = "This build does not come from a release package.";
        m_probeDone = true;
        return;
    }
    m_probeThread = std::thread([this] { probe(); });
}

SelfUpdater::~SelfUpdater()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_cancel.cancel();
    }
    m_cv.notify_all();
    if (m_probeThread.joinable()) {
        m_probeThread.join();
    }
    if (m_job.joinable()) {
        m_job.join();
    }
}

void SelfUpdater::probe()
{
    std::string reason;
    FetchFn fetch = m_config.fetch;
    try {
        const fs::path exe = m_config.exe.empty() ? m_config.installDir / kExeName : m_config.exe;
        reason = installProblem(exe);
        if (reason.empty()) {
            FetchBackend backend = m_config.backend;
            if (m_config.detect) {
                backend = detectFetch();
                fetch = makeFetch(backend);
            } else if (!fetch && backend != FetchBackend::None) {
                fetch = makeFetch(backend);
            }
            if (!fetch || (m_config.detect && backend == FetchBackend::None)) {
                fetch = nullptr;
#ifdef _WIN32
                reason = "Self-update needs WinHTTP, which is not available.";
#else
                reason = "Self-update needs curl, wget or libcurl on Linux. None of them works here.";
#endif
            }
        }
        if (reason.empty()) {
            std::error_code ec;
            const fs::path stage = stageDir(m_config.installDir);
            if (fs::exists(stage, ec) && !fs::is_directory(stage, ec)) {
                reason = "The update folder is blocked by a file: " + stage.string();
            }
        }
    } catch (const std::exception& e) {
        reason = std::string("The update check failed: ") + e.what();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fetch = fetch;
        m_offReason = reason;
        if (!reason.empty()) {
            m_state = State::Off;
        }
        m_probeDone = true;
    }
    m_cv.notify_all();
}

SelfUpdater::State SelfUpdater::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool SelfUpdater::probing() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_probeDone;
}

std::string SelfUpdater::offReason() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state == State::Off ? m_offReason : std::string();
}

std::string SelfUpdater::error() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state == State::Failed ? m_error : std::string();
}

std::optional<SelfUpdater::ReleaseInfo> SelfUpdater::release() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_release;
}

SelfUpdater::Progress SelfUpdater::progress() const
{
    fs::path zip;
    Progress result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        result.total = m_total.load();
        if (m_state == State::Ready) {
            result.done = result.total;
            return result;
        }
        if (m_state != State::Downloading) {
            return result;
        }
        zip = m_zip;
    }
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(zip, ec);
    if (!ec) {
        result.done = size;
    }
    return result;
}

bool SelfUpdater::restartPending() const
{
    return state() == State::Ready;
}

std::optional<fs::path> SelfUpdater::readyScript() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != State::Ready) {
        return std::nullopt;
    }
    return m_script;
}

fs::path SelfUpdater::stagePath() const
{
    return stageDir(m_config.installDir);
}

void SelfUpdater::joinJob()
{
    if (m_job.joinable()) {
        m_job.join();
    }
}

bool SelfUpdater::check()
{
    gg::CancelToken token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Idle && m_state != State::UpToDate && m_state != State::Available &&
            m_state != State::Failed) {
            return false;
        }
        m_state = State::Checking;
        m_error.clear();
        m_release.reset();
        m_asset.reset();
        m_total = 0;
        m_cancel = token;
    }
    joinJob(); // the earlier job set its final state, so it ends at once
    m_job = std::thread([this, token] { runCheck(token); });
    return true;
}

bool SelfUpdater::download()
{
    gg::CancelToken token;
    Asset asset;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Available || !m_asset) {
            return false;
        }
        asset = *m_asset;
        m_state = State::Downloading;
        m_error.clear();
        m_cancel = token;
        m_total = asset.size;
        m_zip = workDirOf(m_config.installDir) / "update.zip";
    }
    joinJob();
    m_job = std::thread([this, token, asset] { runDownload(token, asset); });
    return true;
}

bool SelfUpdater::cancel()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Checking && m_state != State::Downloading) {
            return false;
        }
        m_cancel.cancel();
    }
    m_cv.notify_all();
    return true;
}

void SelfUpdater::setState(State to)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = to;
}

void SelfUpdater::fail(const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = text;
    m_state = State::Failed;
}

bool SelfUpdater::waitProbe(const gg::CancelToken& cancel)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return m_probeDone || cancel.cancelled() || m_stopping; });
    return m_probeDone && !cancel.cancelled() && m_state != State::Off;
}

fs::path SelfUpdater::makeWorkDir(std::string& error) const
{
    const fs::path dir = workDirOf(m_config.installDir);
    std::error_code ec;
    fs::remove_all(dir, ec);
#ifdef _WIN32
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Cannot create the download folder: " + ec.message();
        return {};
    }
#else
    // mkdir fails when the name exists (even as a link), so the folder is new and ours.
    if (::mkdir(dir.c_str(), 0700) != 0) {
        error = "Cannot create the download folder: " + std::error_code(errno, std::generic_category()).message();
        return {};
    }
#endif
    return dir;
}

void SelfUpdater::runCheck(gg::CancelToken cancel)
{
    fs::path dir;
    try {
        if (!waitProbe(cancel)) {
            if (cancel.cancelled()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_state == State::Checking) {
                    m_state = State::Idle;
                }
            }
            return; // Off: the probe set the state
        }
        FetchFn fetch;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            fetch = m_fetch;
        }
        std::string error;
        dir = makeWorkDir(error);
        if (dir.empty()) {
            fail(error);
            return;
        }
        const fs::path file = dir / "release.json";
        error = fetch(m_config.releaseUrl, file, kMaxReleaseJson, cancel);
        if (cancel.cancelled()) {
            removeAll(dir);
            setState(State::Idle);
            return;
        }
        if (!error.empty()) {
            removeAll(dir);
            fail("Cannot check for updates: " + error);
            return;
        }
        std::string json;
        {
            std::ifstream in(file, std::ios::binary);
            json.assign(std::istreambuf_iterator<char>(in), {});
        }
        removeAll(dir);
        Release release;
        if (!parseRelease(json, release, error)) {
            fail("The release information is not valid: " + error);
            return;
        }
        switch (compare(m_config.buildInfo.commit, release.commit)) {
        case Freshness::UpToDate:
            setState(State::UpToDate);
            return;
        case Freshness::Unknown:
            fail("The release has no valid commit id.");
            return;
        case Freshness::Newer:
            break;
        }
        const Asset* asset = selectAsset(release, m_config.buildInfo.packageSystem);
        if (!asset) {
            fail("The release has no usable package for " + m_config.buildInfo.packageSystem + ".");
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        ReleaseInfo info;
        info.commit = release.commit;
        info.shortCommit = shortCommit(release.commit);
        info.assetName = asset->name;
        info.size = asset->size;
        m_release = info;
        m_asset = *asset;
        m_total = asset->size;
        m_state = State::Available;
    } catch (const std::exception& e) {
        removeAll(dir);
        fail(std::string("The update check failed: ") + e.what());
    } catch (...) {
        removeAll(dir);
        fail("The update check failed.");
    }
}

void SelfUpdater::runDownload(gg::CancelToken cancel, Asset asset)
{
    const fs::path stage = stageDir(m_config.installDir);
    fs::path dir;
    try {
        FetchFn fetch;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            fetch = m_fetch;
        }
        std::string error;
        dir = makeWorkDir(error);
        if (dir.empty()) {
            fail(error);
            return;
        }
        const fs::path zip = dir / "update.zip";
        error = fetch(asset.url, zip, asset.size, cancel);
        if (cancel.cancelled()) {
            removeAll(dir);
            setState(State::Available);
            return;
        }
        if (!error.empty()) {
            removeAll(dir);
            fail("The download failed: " + error);
            return;
        }
        error = stageUpdate(m_config.installDir, zip, asset);
        removeAll(dir);
        if (!error.empty()) {
            fail("Cannot prepare the update: " + error);
            return;
        }
        error = writeScript(stage);
        if (!error.empty()) {
            removeAll(stage);
            fail(error);
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (cancel.cancelled()) {
            removeAll(stage);
            m_state = State::Available;
            return;
        }
        m_script = scriptPath(stage);
        m_state = State::Ready;
    } catch (const std::exception& e) {
        removeAll(dir);
        fail(std::string("The download failed: ") + e.what());
    } catch (...) {
        removeAll(dir);
        fail("The download failed.");
    }
}

} // namespace ggui::update
