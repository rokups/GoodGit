// Cancellation token shared between the requester and the worker doing the job.
#pragma once

#include <atomic>
#include <memory>

namespace gg {

class CancelToken {
public:
    CancelToken() : m_flag(std::make_shared<std::atomic<bool>>(false)) { }

    // A copy of none() has no flag: cancel() on it must not cancel every other none() user.
    void cancel() const
    {
        if (m_flag)
            m_flag->store(true, std::memory_order_relaxed);
    }
    bool cancelled() const { return m_flag && m_flag->load(std::memory_order_relaxed); }

    // A token that is never cancelled.
    static const CancelToken& none()
    {
        static const CancelToken token{nullptr};
        return token;
    }

private:
    explicit CancelToken(std::nullptr_t) { }

    std::shared_ptr<std::atomic<bool>> m_flag;
};

// Thrown by long-running work when its token is cancelled.
struct Cancelled { };

inline void throwIfCancelled(const CancelToken& token)
{
    if (token.cancelled())
        throw Cancelled{};
}

} // namespace gg
