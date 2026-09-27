// ggui as GIT_ASKPASS / SSH_ASKPASS (REBUILD_PLAN §4.8, T1). See Askpass.cpp for the bridge.
#pragma once

#include <string>

namespace ggui {

// `ggui --askpass PROMPT`: forwards the prompt to the running ggui and prints the answer.
// Returns the process exit code (0 = answered, 1 = cancelled/no ggui).
int runAskpassClient(const std::string& prompt);

} // namespace ggui
