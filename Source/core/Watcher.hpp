// File watcher (efsw) on its own thread, posting debounced "paths changed" notices (§3.1).
#pragma once

#include "core/Engine.hpp"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace efsw {
class FileWatcher;
}

namespace ggui::core {

class Watcher {
public:
    Watcher(std::filesystem::path workdir, std::filesystem::path gitDir, std::filesystem::path commonDir,
        std::function<void(const WatchEvent&)> notify);
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    // Classifies one changed path (absolute) into the event flags. Exposed for the listener.
    void changed(const std::filesystem::path& path);

private:
    class Listener;
    void debounceLoop();

    std::filesystem::path m_workdir;
    std::filesystem::path m_gitDir;
    std::filesystem::path m_commonDir;
    std::function<void(const WatchEvent&)> m_notify;
    std::unique_ptr<Listener> m_listener;
    std::unique_ptr<efsw::FileWatcher> m_watcher;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    WatchEvent m_pending;
    bool m_hasPending = false;
    std::chrono::steady_clock::time_point m_last;
    bool m_stop = false;
    std::thread m_thread;
};

} // namespace ggui::core
