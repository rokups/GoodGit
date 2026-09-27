#include "shell/Askpass.hpp"

#include <cstdio>

namespace ggui {

int runAskpassClient(const std::string& prompt)
{
    // Implemented in P2-04.
    std::fprintf(stderr, "ggui: askpass not available for: %s\n", prompt.c_str());
    return 1;
}

} // namespace ggui
