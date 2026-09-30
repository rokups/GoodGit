#include "Watcher.hpp"

#include <efsw/efsw.hpp>
#include <spdlog/spdlog.h>

namespace ggui::core {

namespace fs = std::filesystem;

namespace {

constexpr auto kDebounce = std::chrono::milliseconds(150);

// Relative path of `p` under `base` with '/' separators, or empty when outside.
std::string relativeUnder(const fs::path& p, const fs::path& base)
{
    if (base.empty())
        return {};
    std::string ps = p.lexically_normal().generic_string();
    std::string bs = base.lexically_normal().generic_string();
    if (!bs.empty() && bs.back() != '/')
        bs.push_back('/');
    if (!ps.empty() && ps.back() != '/' && ps + "/" == bs)
        return ".";
    if (ps.size() < bs.size() || ps.compare(0, bs.size(), bs) != 0)
        return {};
    ps = ps.substr(bs.size());
    return ps.empty() ? std::string(".") : ps;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

} // namespace

class Watcher::Listener final : public efsw::FileWatchListener {
public:
    explicit Listener(Watcher& owner) : m_owner(owner) { }
    void handleFileAction(efsw::WatchID, const std::string& dir, const std::string& filename, efsw::Action,
        const std::string& oldFilename) override
    {
        m_owner.changed(fs::path(dir) / filename);
        if (!oldFilename.empty())
            m_owner.changed(fs::path(dir) / oldFilename);
    }

private:
    Watcher& m_owner;
};

Watcher::Watcher(fs::path workdir, fs::path gitDir, fs::path commonDir, std::function<void(const WatchEvent&)> notify)
    : m_workdir(std::move(workdir)), m_gitDir(std::move(gitDir)), m_commonDir(std::move(commonDir)),
      m_notify(std::move(notify))
{
    m_listener = std::make_unique<Listener>(*this);
    m_watcher = std::make_unique<efsw::FileWatcher>();
    auto add = [&](const fs::path& dir) {
        if (dir.empty())
            return;
        const efsw::WatchID id = m_watcher->addWatch(dir.string(), m_listener.get(), true);
        if (id < 0)
            spdlog::warn("file watcher: cannot watch {}: {}", dir.string(), efsw::Errors::Log::getLastErrorLog());
    };
    add(m_workdir);
    // The git dirs may live outside the worktree (linked worktrees, separate git dir).
    if (relativeUnder(m_commonDir, m_workdir).empty())
        add(m_commonDir);
    if (relativeUnder(m_gitDir, m_workdir).empty() && relativeUnder(m_gitDir, m_commonDir).empty())
        add(m_gitDir);
    m_watcher->watch();
    m_thread = std::thread([this] { debounceLoop(); });
}

Watcher::~Watcher()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    m_watcher.reset();
}

void Watcher::changed(const fs::path& path)
{
    WatchEvent e;
    auto classifyGit = [&](const std::string& rel) {
        if (rel.empty())
            return false;
        if (startsWith(rel, "objects/") || startsWith(rel, "logs/") || startsWith(rel, "hooks/")
            || startsWith(rel, "gg/cache") || (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".lock") == 0))
            return true; // ignored
        if (rel == "gg/journal") {
            e.journal = true;
        } else if (rel == "gg" || startsWith(rel, "gg/")) {
            return true; // ggui's own files (rebase/, cache, reconcile.json, ...) are no repository change
        } else if (rel == "index" || (startsWith(rel, "worktrees/") && rel.find("/index") != std::string::npos)) {
            e.index = true;
        } else if (rel == "config") {
            e.refs = true;
        } else {
            e.refs = true; // HEAD, refs/, packed-refs, MERGE_HEAD, rebase-*, sequencer, BISECT_*
        }
        return true;
    };
    const std::string inGit = relativeUnder(path, m_gitDir);
    const std::string inCommon = relativeUnder(path, m_commonDir);
    spdlog::debug("watch: {} (git dir: '{}', common dir: '{}')", path.string(), inGit, inCommon);
    if (!classifyGit(inGit) && !classifyGit(inCommon)) {
        const std::string inWork = relativeUnder(path, m_workdir);
        if (inWork.empty() || inWork == ".git" || startsWith(inWork, ".git/"))
            return;
        e.worktree = true;
    }
    if (!e.worktree && !e.index && !e.refs && !e.journal)
        return;
    {
        std::lock_guard lock(m_mutex);
        m_pending.worktree |= e.worktree;
        m_pending.index |= e.index;
        m_pending.refs |= e.refs;
        m_pending.journal |= e.journal;
        m_hasPending = true;
        m_last = std::chrono::steady_clock::now();
    }
    m_cv.notify_all();
}

void Watcher::debounceLoop()
{
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        if (!m_hasPending) {
            m_cv.wait(lock, [this] { return m_stop || m_hasPending; });
            continue;
        }
        const auto due = m_last + kDebounce;
        if (std::chrono::steady_clock::now() < due) {
            m_cv.wait_until(lock, due);
            continue;
        }
        WatchEvent e = m_pending;
        m_pending = WatchEvent{};
        m_hasPending = false;
        lock.unlock();
        m_notify(e);
        lock.lock();
    }
}

} // namespace ggui::core
