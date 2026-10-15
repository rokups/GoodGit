// Self-update: the build identity (util/BuildInfo.hpp).
#include "tests/Harness.hpp"
#include "util/BuildInfo.hpp"

namespace ggtest {

GG_TEST("selfupdate", "build info has a version and a package system")
{
    const ggui::BuildInfo info = ggui::buildInfo();
    GG_CHECK(!info.version.empty());
    GG_CHECK(!info.packageSystem.empty());
#if defined(__linux__)
    GG_CHECK(info.packageSystem.rfind("linux-", 0) == 0);
#endif
}

GG_TEST("selfupdate", "shortCommit keeps the first 7 characters")
{
    GG_CHECK_STR_EQ(ggui::shortCommit("0123456789abcdef"), "0123456");
    GG_CHECK_STR_EQ(ggui::shortCommit("abc"), "abc");
    GG_CHECK_STR_EQ(ggui::shortCommit(""), "");
}

} // namespace ggtest
