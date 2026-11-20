// A picker thread posts its result through this gate. The gate outlives the App.
// After close() the post does nothing, so a late dialog cannot reach a destroyed App.
#pragma once

#include <functional>
#include <mutex>

namespace ggui {

class PickerGate {
public:
    // Runs fn under the lock if the gate is open. Returns false if the gate is closed.
    bool post(const std::function<void()>& fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed)
            return false;
        fn();
        return true;
    }

    // Waits for a running post, then blocks all later posts.
    void close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }

private:
    std::mutex m_mutex;
    bool m_closed = false;
};

} // namespace ggui
