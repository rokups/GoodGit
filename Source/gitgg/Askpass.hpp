// git-gg as GIT_ASKPASS / SSH_ASKPASS: forwards the prompt to the running ggui (P2-04).
#pragma once

#include <string>

namespace gitgg {

// Prints the answer on stdout (exit 0) or fails (exit 1) when cancelled / ggui is gone.
int runAskpass(const std::string& prompt);

} // namespace gitgg
