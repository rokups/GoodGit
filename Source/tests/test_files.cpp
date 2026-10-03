// libgg's replaceFile: a temp file renamed over a target (libgg/Files.hpp).
#include "tests/Harness.hpp"

#include <libgg/Files.hpp>

#include <chrono>

namespace ggtest {

GG_TEST("files", "replaceFile replaces an existing file and consumes the temp file")
{
    const fs::path dir = s.root() / "replace";
    s.write(dir, "target.txt", "old\n");
    s.write(dir, "target.txt.tmp", "new\n");
    const std::error_code ec = gg::replaceFile(dir / "target.txt.tmp", dir / "target.txt");
    GG_CHECK(!ec);
    GG_CHECK_STR_EQ(s.read(dir, "target.txt"), "new\n");
    GG_CHECK(!fs::exists(dir / "target.txt.tmp"));
    // A target that does not exist yet is created.
    s.write(dir, "other.tmp", "created\n");
    GG_CHECK(!gg::replaceFile(dir / "other.tmp", dir / "other.txt"));
    GG_CHECK_STR_EQ(s.read(dir, "other.txt"), "created\n");
}

GG_TEST("files", "replaceFile gives up with an error after a short time when the temp file is missing")
{
    const fs::path dir = s.root() / "replace-missing";
    s.write(dir, "target.txt", "kept\n");
    const auto start = std::chrono::steady_clock::now();
    const std::error_code ec = gg::replaceFile(dir / "missing.tmp", dir / "target.txt");
    const auto took = std::chrono::steady_clock::now() - start;
    GG_CHECK(static_cast<bool>(ec));
    GG_CHECK(took < std::chrono::seconds(2));
    GG_CHECK_STR_EQ(s.read(dir, "target.txt"), "kept\n");
}

} // namespace ggtest
