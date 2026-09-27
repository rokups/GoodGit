// One worker thread with its own git_repository* (handles never cross threads, §3.1).
#pragma once

#include <libgg/Cancel.hpp>
#include <libgg/Git2.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ggui::core {

class Worker {
public:
    struct Task {
        std::uint64_t id = 0;
        int slot = 0;                 // > 0: latest task per slot wins
        gg::CancelToken token;
        std::function<void(Worker&, const gg::CancelToken&)> fn;
    };

    // `onDropped` is called (on the posting thread or the worker) for tasks that never ran.
    Worker(std::string name, std::filesystem::path repoPath, std::function<void(std::uint64_t)> onDropped);
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void post(Task task);
    void cancel(std::uint64_t id);
    void cancelAll();
    bool busy() const;
    void stop();

    // Worker thread only: the repository handle, opened on first use.
    git_repository* repo();
    // Worker thread only: drop the handle (e.g. after the repository moved).
    void resetRepo();
    const std::filesystem::path& repoPath() const { return m_path; }

private:
    void loop();

    std::string m_name;
    std::filesystem::path m_path;
    std::function<void(std::uint64_t)> m_onDropped;
    gg::git2::Repository m_repo;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    std::uint64_t m_runningId = 0;
    int m_runningSlot = 0;
    gg::CancelToken m_runningToken;
    bool m_stop = false;
    std::thread m_thread;
};

} // namespace ggui::core
