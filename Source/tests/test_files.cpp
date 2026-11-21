// libgg's replaceFile: a temp file renamed over a target (libgg/Files.hpp).
#include "tests/Harness.hpp"

#include <libgg/Files.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace ggtest {

GG_TEST("files", "readFile fails for a directory, which opens but cannot be read")
{
    const fs::path dir = s.root() / "readdir";
    fs::create_directories(dir);
    std::string out = "kept";
    GG_CHECK(!gg::readFile(dir, out));
    GG_CHECK_STR_EQ(out, "kept");
    s.write(dir, "f.txt", "text\n");
    GG_CHECK(gg::readFile(dir / "f.txt", out));
    GG_CHECK_STR_EQ(out, "text\n");
}

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
    const double tookMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    GG_CHECK(static_cast<bool>(ec));
    GG_CHECK(tookMs < timeBudgetMs(2000));
    GG_CHECK_STR_EQ(s.read(dir, "target.txt"), "kept\n");
}

namespace {
size_t countFiles(const fs::path& dir)
{
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        n += e.is_regular_file() ? 1 : 0;
    return n;
}
} // namespace

GG_TEST("files", "writeFileAtomic creates a file and replaces an existing one")
{
    const fs::path dir = s.root() / "atomic";
    fs::create_directories(dir);
    GG_CHECK(!gg::writeFileAtomic(dir / "a.txt", "first\n"));
    GG_CHECK_STR_EQ(s.read(dir, "a.txt"), "first\n");
    GG_CHECK(!gg::writeFileAtomic(dir / "a.txt", "second\n"));
    GG_CHECK_STR_EQ(s.read(dir, "a.txt"), "second\n");
    GG_CHECK(countFiles(dir) == 1);
}

GG_TEST("files", "a failed writeFileAtomic keeps the old file and leaves no temp file")
{
    const fs::path dir = s.root() / "atomic-fail";
    s.write(dir, "keep.txt", "old\n");
    GG_CHECK(static_cast<bool>(gg::writeFileAtomic(dir / "missing-dir" / "x.txt", "new\n")));
    GG_CHECK(!fs::exists(dir / "missing-dir"));
    fs::create_directories(dir / "target-dir");
    GG_CHECK(static_cast<bool>(gg::writeFileAtomic(dir / "target-dir", "new\n")));
    GG_CHECK(fs::is_directory(dir / "target-dir"));
    GG_CHECK_STR_EQ(s.read(dir, "keep.txt"), "old\n");
    GG_CHECK(countFiles(dir) == 1);
}

GG_TEST("files", "writeFileAtomic with one attempt fails at once and leaves no temp file")
{
    const fs::path dir = s.root() / "atomic-once";
    s.write(dir, "keep.txt", "old\n");
    fs::create_directories(dir / "target-dir");
    const auto start = std::chrono::steady_clock::now();
    GG_CHECK(static_cast<bool>(gg::writeFileAtomic(dir / "target-dir", "new\n", std::nullopt, 1)));
    GG_CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(150));
    GG_CHECK(countFiles(dir) == 1);
}

GG_TEST("files", "two threads writing one target with writeFileAtomic never mix the content")
{
    const fs::path dir = s.root() / "atomic-race";
    fs::create_directories(dir);
    const std::string a(50000, 'a');
    const std::string b(70000, 'b');
    std::atomic<int> failures{0};
    auto writer = [&](const std::string& text) {
        for (int i = 0; i < 200; ++i)
            if (gg::writeFileAtomic(dir / "t.txt", text))
                ++failures;
    };
    std::thread t1(writer, std::cref(a));
    std::thread t2(writer, std::cref(b));
    t1.join();
    t2.join();
    GG_CHECK(failures == 0);
    const std::string got = s.read(dir, "t.txt");
    GG_CHECK(got == a || got == b);
    GG_CHECK(countFiles(dir) == 1);
}

} // namespace ggtest
