#include "Worker.hpp"

#include <spdlog/spdlog.h>

namespace ggui::core {

Worker::Worker(std::string name, std::filesystem::path repoPath, std::function<void(std::uint64_t)> onDropped)
    : m_name(std::move(name)), m_path(std::move(repoPath)), m_onDropped(std::move(onDropped))
{
    m_thread = std::thread([this] { loop(); });
}

Worker::~Worker() { stop(); }

void Worker::stop()
{
    std::deque<Task> dropped;
    {
        std::lock_guard lock(m_mutex);
        if (m_stop)
            return;
        m_stop = true;
        m_runningToken.cancel();
        dropped.swap(m_queue);
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    for (auto& t : dropped)
        if (m_onDropped)
            m_onDropped(t.id);
}

void Worker::post(Task task)
{
    std::vector<std::uint64_t> dropped;
    {
        std::lock_guard lock(m_mutex);
        if (task.slot > 0) {
            for (auto it = m_queue.begin(); it != m_queue.end();) {
                if (it->slot == task.slot) {
                    dropped.push_back(it->id);
                    it = m_queue.erase(it);
                } else {
                    ++it;
                }
            }
            if (m_runningId && m_runningSlot == task.slot)
                m_runningToken.cancel();
        }
        m_queue.push_back(std::move(task));
    }
    m_cv.notify_one();
    for (auto id : dropped)
        if (m_onDropped)
            m_onDropped(id);
}

void Worker::cancel(std::uint64_t id)
{
    bool droppedPending = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_runningId == id)
            m_runningToken.cancel();
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
            if (it->id == id) {
                m_queue.erase(it);
                droppedPending = true;
                break;
            }
        }
    }
    if (droppedPending && m_onDropped)
        m_onDropped(id);
}

void Worker::cancelAll()
{
    std::deque<Task> dropped;
    {
        std::lock_guard lock(m_mutex);
        m_runningToken.cancel();
        dropped.swap(m_queue);
    }
    for (auto& t : dropped)
        if (m_onDropped)
            m_onDropped(t.id);
}

bool Worker::busy() const
{
    std::lock_guard lock(m_mutex);
    return m_runningId != 0 || !m_queue.empty();
}

git_repository* Worker::repo()
{
    if (!m_repo)
        m_repo = gg::git2::openRepository(m_path);
    return m_repo.get();
}

void Worker::resetRepo() { m_repo.reset(); }

void Worker::loop()
{
    for (;;) {
        Task task;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                break;
            task = std::move(m_queue.front());
            m_queue.pop_front();
            m_runningId = task.id;
            m_runningSlot = task.slot;
            m_runningToken = task.token;
        }
        task.fn(*this, task.token);
        {
            std::lock_guard lock(m_mutex);
            m_runningId = 0;
            m_runningSlot = 0;
            m_runningToken = gg::CancelToken();
        }
    }
    m_repo.reset();
}

} // namespace ggui::core
