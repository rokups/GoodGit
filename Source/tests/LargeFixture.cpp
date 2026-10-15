// Large-repository fixture for the responsiveness acceptance test (product spec §3.1).
#include "tests/Harness.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <random>

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

// Builds the fixture into `dir` (a fresh directory) and marks it complete.
void generate(Scenario& s, const fs::path& dir)
{
    fs::create_directories(dir);
    s.git(dir, {"init", "-q", "-b", "main"});
    s.git(dir, {"fast-import", "--quiet", "--done"}, buildStream() + "done\n");
    s.git(dir, {"checkout", "-q", "-f", "main"});
    s.git(dir, {"gc", "-q", "--prune=now"});
    std::ofstream(dir / ".git" / "ggui-fixture-complete") << "ok\n";
}

std::string uniqueSuffix()
{
    std::random_device rd;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08x%08x", rd(), rd());
    return buf;
}

// The shared cache repository. It is built in a unique directory next to the cache and renamed
// into place, so a reader never sees a half-built cache. The process that loses the rename race
// deletes its own copy and uses the winner's. A cache with the completion stamp is never deleted.
fs::path buildCache(Scenario& s)
{
    const fs::path cache = fixtureCacheDir() / kVersion;
    const fs::path stamp = cache / ".git" / "ggui-fixture-complete";
    std::error_code ec;
    if (fs::exists(stamp, ec))
        return cache;
    fs::create_directories(cache.parent_path(), ec);
    const auto start = std::chrono::steady_clock::now();
    const fs::path mine = cache.parent_path() / (std::string(kVersion) + ".tmp-" + uniqueSuffix());
    generate(s, mine);
    for (int attempt = 0; attempt < 2; ++attempt) {
        ec.clear();
        fs::rename(mine, cache, ec);
        if (!ec) {
            spdlog::info("large fixture generated in {} s",
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
            return cache;
        }
        if (fs::exists(stamp, ec)) { // another process won
            removeAll(mine);
            return cache;
        }
        // An incomplete directory (left by an older version of this code): move it away atomically,
        // so only one process removes it, then try again.
        const fs::path stale = cache.parent_path() / (std::string(kVersion) + ".stale-" + uniqueSuffix());
        ec.clear();
        fs::rename(cache, stale, ec);
        if (!ec)
            removeAll(stale);
    }
    removeAll(mine);
    s.ctx->LogError("large fixture: cannot move %s to %s", mine.string().c_str(), cache.string().c_str());
    const bool ok = false;
    IM_CHECK_NO_RET(ok);
    return cache;
}

} // namespace

fs::path Scenario::largeFixture()
{
    const fs::path repo = m_root / "large-fixture";
    if (fs::exists(repo / ".git" / "HEAD"))
        return repo;
    const fs::path cache = buildCache(*this);
    // A local clone with its own git directory: git hardlinks the objects when the filesystems
    // match and copies them otherwise. It keeps the branches, tags and HEAD and has no remote.
    // Its git directory differs from the cache's, so the app treats it as another repository and
    // parallel test processes can open it at the same time.
    git(m_root, {"clone", "-q", "--mirror", cache.string(), (repo / ".git").string()});
    git(repo, {"config", "core.bare", "false"});
    git(repo, {"remote", "remove", "origin"});
    git(repo, {"checkout", "-q", "-f", "main"});
    track(repo);
    return repo;
}

} // namespace ggtest
