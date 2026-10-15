#include "util/BuildInfo.hpp"

// These two come from the compile definitions of this file only (Source/app/CMakeLists.txt).
#ifndef GGUI_BUILD_COMMIT
#define GGUI_BUILD_COMMIT ""
#endif
#ifndef GGUI_PACKAGE_SYSTEM
#define GGUI_PACKAGE_SYSTEM ""
#endif

namespace ggui {

BuildInfo buildInfo()
{
    return BuildInfo{GGUI_VERSION, GGUI_BUILD_COMMIT, GGUI_PACKAGE_SYSTEM};
}

std::string shortCommit(const std::string& commit)
{
    return commit.substr(0, 7);
}

} // namespace ggui
