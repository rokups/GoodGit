#include "util/BuildInfo.hpp"

// Generated on each build by cmake/BuildIdentity.cmake.
extern const char* const ggui_build_commit;
extern const long long ggui_build_time;

// This one comes from the compile definitions of this file only (Source/app/CMakeLists.txt).
#ifndef GGUI_PACKAGE_SYSTEM
#define GGUI_PACKAGE_SYSTEM ""
#endif

namespace ggui {

BuildInfo buildInfo()
{
    return BuildInfo{GGUI_VERSION, ggui_build_commit, GGUI_PACKAGE_SYSTEM, ggui_build_time};
}

std::string shortCommit(const std::string& commit)
{
    return commit.substr(0, 7);
}

} // namespace ggui
