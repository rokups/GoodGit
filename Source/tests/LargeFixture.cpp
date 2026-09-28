// Large-repository fixture for the responsiveness acceptance test (P0-11, REBUILD_PLAN §3.1).
#include "tests/Harness.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>

namespace ggtest {

namespace {

constexpr int kFiles = 50000;
constexpr int kCommits = 100000;
constexpr int kBranchRefs = 2600;
constexpr int kTagRefs = 2600;
constexpr const char* kVersion = "large-v1";

void appendData(std::string& out, const std::string& data)
{
    out += "data " + std::to_string(data.size()) + "\n" + data + "\n";
}

std::string filePath(int i)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "dir%03d/file%05d.txt", i % 500, i);
    return buf;
}

// Builds the fast-import stream: one big initial commit, a long main line with a side branch
// merged back every 500 commits, then thousands of branch and tag refs.
std::string buildStream()
{
    std::string s;
    s.reserve(64u << 20);
    long long when = 1600000000;
    int mark = 0;
    auto commitHeader = [&](const std::string& ref, const std::string& msg) {
        s += "commit " + ref + "\nmark :" + std::to_string(++mark) + "\n";
        s += "author Big Repo <big@example.com> " + std::to_string(when) + " +0000\n";
        s += "committer Big Repo <big@example.com> " + std::to_string(when) + " +0000\n";
        appendData(s, msg);
        when += 60;
        return mark;
    };
    // Initial commit with all files (inline data).
    int root = commitHeader("refs/heads/main", "Initial import of " + std::to_string(kFiles) + " files");
    for (int i = 0; i < kFiles; ++i) {
        s += "M 100644 inline " + filePath(i) + "\n";
        appendData(s, "file " + std::to_string(i) + "\n");
    }
    s += "\n";
    int mainTip = root;
    std::vector<int> marks;
    marks.reserve(kCommits);
    int made = 1;
    int n = 0;
    while (made < kCommits) {
        if (n % 500 == 499) {
            // Side branch of 20 commits, merged back.
            const int base = mainTip;
            int sideTip = base;
            for (int k = 0; k < 20 && made < kCommits; ++k, ++made) {
                const int c = commitHeader("refs/heads/side", "Side change " + std::to_string(made));
                s += "from :" + std::to_string(sideTip) + "\n";
                s += "M 100644 inline " + filePath((made * 7919) % kFiles) + "\n";
                appendData(s, "side " + std::to_string(made) + "\n");
                s += "\n";
                sideTip = c;
                marks.push_back(c);
            }
            const int m = commitHeader("refs/heads/main", "Merge side branch " + std::to_string(n));
            s += "from :" + std::to_string(mainTip) + "\nmerge :" + std::to_string(sideTip) + "\n\n";
            mainTip = m;
            marks.push_back(m);
            ++made;
        } else {
            const int c = commitHeader("refs/heads/main", "Change " + std::to_string(made));
            s += "from :" + std::to_string(mainTip) + "\n";
            s += "M 100644 inline " + filePath((made * 104729) % kFiles) + "\n";
            appendData(s, "change " + std::to_string(made) + "\n");
            s += "\n";
            mainTip = c;
            marks.push_back(c);
            ++made;
        }
        ++n;
    }
    for (int i = 0; i < kBranchRefs; ++i)
        s += "reset refs/heads/b/" + std::to_string(i) + "\nfrom :" + std::to_string(marks[(static_cast<size_t>(i) * 37) % marks.size()]) + "\n\n";
    for (int i = 0; i < kTagRefs; ++i)
        s += "reset refs/tags/t/" + std::to_string(i) + "\nfrom :" + std::to_string(marks[(static_cast<size_t>(i) * 53) % marks.size()]) + "\n\n";
    s += "reset refs/heads/side\nfrom 0000000000000000000000000000000000000000\n\n";
    return s;
}

} // namespace

fs::path Scenario::largeFixture()
{
    const fs::path repo = fixtureCacheDir() / kVersion;
    const fs::path stamp = repo / ".git" / "ggui-fixture-complete";
    if (fs::exists(stamp))
        return repo;
    std::error_code ec;
    removeAll(repo);
    fs::create_directories(repo);
    const auto start = std::chrono::steady_clock::now();
    git(repo, {"init", "-q", "-b", "main"});
    git(repo, {"fast-import", "--quiet", "--done"}, buildStream() + "done\n");
    git(repo, {"checkout", "-q", "-f", "main"});
    git(repo, {"gc", "-q", "--prune=now"});
    std::ofstream(stamp) << "ok\n";
    spdlog::info("large fixture generated in {} s",
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
    return repo;
}

} // namespace ggtest
