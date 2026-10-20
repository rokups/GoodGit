// Decides how long the UI loop may sleep before the next frame (GG-73). Pure logic, no SDL.
//
// A frame follows any input at once and for a short active window after it (ImGui hover delay and
// layout settle). Later the loop wakes at a slow rate, so worker results still show up. A worker
// thread, an animation request or an input event ends the sleep early.
#pragma once

#include <chrono>

namespace ggui {

class FrameScheduler {
public:
    struct Input {
        bool eventSeen = false;       // the last event poll handled an input event
        bool woken = false;           // the last event poll handled only a wake event from a worker
        bool anyMouseDown = false;    // a mouse button is down (a drag needs frames)
        bool framesRequested = false; // a widget asked for the next frame (requestFrames)
        bool wantTextInput = false;   // a text field is active (the cursor blinks)
        bool focused = true;
        bool minimized = false; // minimized or hidden
        std::chrono::steady_clock::time_point now;
    };

    static constexpr int kActiveWindowMs = 500; // after an input event: frames without a wait
    static constexpr int kWakeWindowMs = 200;   // after a wake from a worker: frames without a wait
    static constexpr int kIdleWaitMs = 1000;    // nothing to show: a poll for worker data
    static constexpr int kSlowWaitMs = 100;     // an animation or a text cursor in an unfocused window

    // Returns the wait in milliseconds before the next frame. 0 means draw at once.
    int waitMs(const Input& input);

private:
    std::chrono::steady_clock::time_point m_activeUntil{};
};

} // namespace ggui
