// Frame requests. A widget that changes with time and not with input asks for the next frames.
// The UI loop reads the count to decide if it can sleep. All functions run on the UI thread only.
#pragma once

namespace ggui {

// Asks for at least `n` more frames. The pending count is the maximum of the old count and `n`.
void requestFrames(int n);
// Asks for the next frame. The same as requestFrames(1).
void requestAnimation();
// Returns the pending count and sets it to 0.
int takeFrameRequests();

} // namespace ggui
