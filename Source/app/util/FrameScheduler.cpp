#include "util/FrameScheduler.hpp"

#include <algorithm>

namespace ggui {

int FrameScheduler::waitMs(const Input& input)
{
    if (input.eventSeen)
        m_activeUntil = input.now + std::chrono::milliseconds(kActiveWindowMs);
    else if (input.woken)
        m_activeUntil = std::max(m_activeUntil, input.now + std::chrono::milliseconds(kWakeWindowMs));
    // A hidden window ignores animation requests. The loop only polls for worker data.
    if (input.minimized)
        return kIdleWaitMs;
    if (input.now < m_activeUntil)
        return 0;
    // A mouse drag changes the screen on each frame, also when the mouse does not move.
    if (input.anyMouseDown && input.focused)
        return 0;
    if (input.framesRequested)
        return input.focused ? 0 : kSlowWaitMs;
    if (input.wantTextInput && input.focused)
        return kSlowWaitMs;
    return kIdleWaitMs;
}

} // namespace ggui
