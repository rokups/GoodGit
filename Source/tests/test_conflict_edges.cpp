// First-class conflict round trips and plain-git transparency (§4.10, §8.4 conflicts; P3-14).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <algorithm>
#include <random>

namespace ggtest {

namespace {

using ggui::core::Oid;

// base, then one commit per version of `file` (each changes the same place).
struct Line {
    fs::path path;
    std::vector<std::string> commits; // commits[0] = base
};

Line makeLine(Scenario& s, const std::string& name, const std::string& file, const std::vector<std::string>& versions,
    const std::string& attributes = {})
{
    Line l;
    l.path = s.path(name);
    s.git(s.root(), {"init", "-q", "-b", "main", l.path.string()});
    s.track(l.path);
    if (!attributes.empty()) {
        s.write(l.path, ".gitattributes", attributes);
        s.git(l.path, {"add", ".gitattributes"});
    }
    for (size_t i = 0; i < versions.size(); ++i) {
        s.write(l.path, file, versions[i]);
        s.git(l.path, {"add", "--", file});
        s.git(l.path, {"commit", "-q", "--allow-empty", "-m", i == 0 ? std::string("base") : "v" + std::to_string(i)});
        l.commits.push_back(s.head(l.path));
    }
    return l;
}

std::string blobAt(Scenario& s, const fs::path& repo, const std::string& rev, const std::string& file)
{
    auto r = s.gitMayFail(repo, {"rev-parse", rev + ":" + file});
    return r.ok() ? gg::trim(r.out) : std::string();
}

// Blob ids of `file` along main (oldest first).
std::vector<std::string> blobsAlongMain(Scenario& s, const fs::path& repo, const std::string& file)
{
    std::vector<std::string> out;
    const auto revs = gg::splitLines(s.gitOut(repo, {"rev-list", "--reverse", "main"}));
    for (const auto& r : revs)
        if (!r.empty())
            out.push_back(blobAt(s, repo, r, file));
    return out;
}

std::vector<std::string> subjectsAlongMain(Scenario& s, const fs::path& repo)
{
    std::vector<std::string> out;
    for (const auto& l : gg::splitLines(s.gitOut(repo, {"log", "--reverse", "--format=%s", "main"})))
        if (!l.empty())
            out.push_back(l);
    return out;
}

bool reordered(Scenario& s, const fs::path& repo, const std::string& before)
{
    const bool ok = s.waitUntil([&] { return s.revParse(repo, "main") != before; });
    s.settle();
    return ok;
}

// Moves the commit with subject `subject` right after (or before) the one with `anchor`.
bool move(Scenario& s, const fs::path& repo, const std::string& subject, const std::string& anchor, bool after)
{
    auto idOf = [&](const std::string& subj) {
        return gg::trim(s.gitOut(repo, {"log", "--format=%H", "--grep=^" + subj + "$", "main"}));
    };
    const std::string before = s.revParse(repo, "main");
    s.session()->actions().reorder(Oid::fromHex(idOf(subject)), Oid::fromHex(idOf(anchor)), after, false);
    return reordered(s, repo, before);
}

// v1 and v2 change the same place: moving v2 before v1 conflicts both; moving it back must give
// back every blob byte for byte (and no markers anywhere).
void roundTrip(Scenario& s, const Line& l, const std::string& file)
{
    const auto original = blobsAlongMain(s, l.path, file);
    GG_REQUIRE(s.openRepository(l.path));
    GG_CHECK(move(s, l.path, "v2", "v1", false));
    GG_CHECK(subjectsAlongMain(s, l.path) == (std::vector<std::string>{"base", "v2", "v1"}));
    GG_CHECK_EQ(s.gitgg(l.path, {"conflicts", "main~1"}).exitCode, 1); // v2 on base: conflicted
    GG_CHECK(move(s, l.path, "v2", "v1", true));
    GG_CHECK(subjectsAlongMain(s, l.path) == (std::vector<std::string>{"base", "v1", "v2"}));
    GG_CHECK(blobsAlongMain(s, l.path, file) == original);
    GG_CHECK_EQ(s.gitgg(l.path, {"conflicts", "main"}).exitCode, 0); // resolved everywhere again
    s.ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    s.ctx->Yield(3);
}

} // namespace

GG_TEST("edges", "round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text",
    "CONF-EDGE-NOEOL", "CONF-EDGE-CRLF", "CONF-EDGE-EMPTY", "CONF-EDGE-MARKERLIKE")
{
    roundTrip(s, makeLine(s, "noeol", "f.txt", {"a\nb\nc", "a\nb\nC1", "a\nb\nC2"}), "f.txt");
    roundTrip(s, makeLine(s, "crlf", "f.txt", {"a\r\nb\r\nc\r\n", "a\r\nB1\r\nc\r\n", "a\r\nB2\r\nc\r\n"}), "f.txt");
    roundTrip(s, makeLine(s, "empty", "f.txt", {"a\nb\nc\n", "a\nc\n", "a\nB2\nB2b\nc\n"}), "f.txt");
    roundTrip(s,
        makeLine(s, "markerlike", "f.txt",
            {"a\n<<<<<<< not a marker\nb\n", "a\n<<<<<<< not a marker\nB1\n=======\n", "a\n<<<<<<< not a marker\nB2\n>>>>>>> x\n"}),
        "f.txt");
}

GG_TEST("edges", "N-way: merging two conflicted lines gives three sides; merging again simplifies", "CONF-NWAY-MERGE")
{
    // A and B each hold a two-sided region on the same line (terms x2+x0-x1 and x5+x0-x4).
    const fs::path repo = s.fixture(Recipe::Empty);
    s.commitFile(repo, "f.txt", "x=0\n", "base");
    s.git(repo, {"branch", "b"});
    s.commitFile(repo, "f.txt", "<<<<<<< side 1\nx=2\n||||||| base\nx=1\n=======\nx=0\n>>>>>>> side 2\n", "A conflicted");
    s.git(repo, {"switch", "-q", "b"});
    s.commitFile(repo, "f.txt", "<<<<<<< side 1\nx=5\n||||||| base\nx=4\n=======\nx=0\n>>>>>>> side 2\n", "B conflicted");
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Branches/branch_b/###branch_b"); }));
    const std::string before = s.head(repo);
    s.contextMenu("//Branches/branch_b/###branch_b", "Merge into HEAD...");
    GG_REQUIRE(s.dialogOpen("Merge into HEAD"));
    s.dialogButton("Merge into HEAD", "Merge");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != before; }));
    s.settle();
    const auto r = s.gitgg(repo, {"conflicts", "HEAD"});
    GG_CHECK(r.out.find("f.txt (3 sides)") != std::string::npos);
    const std::string merged = s.gitOut(repo, {"show", "HEAD:f.txt"});
    GG_CHECK(merged.find("gg 3-sided conflict") != std::string::npos);
    GG_CHECK(merged.find("x=2") != std::string::npos && merged.find("x=5") != std::string::npos);
    // Simplification: reverting B's merge side (moving HEAD's tree back to A's) cancels its terms.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == before; }));
    GG_CHECK(s.gitgg(repo, {"conflicts", "HEAD"}).out.find("(2 sides)") != std::string::npos);
}

GG_TEST("edges", "randomized reorders of N changes to one place always come back exact", "CONF-NO-NESTING")
{
    const Line l = makeLine(s, "random", "f.txt", {"v=0\n", "v=1\n", "v=2\n", "v=3\n", "v=4\n"});
    const auto original = blobsAlongMain(s, l.path, "f.txt");
    GG_REQUIRE(s.openRepository(l.path));
    std::mt19937 rng(static_cast<unsigned>(s.seed()));
    std::vector<std::string> order{"v1", "v2", "v3", "v4"};
    for (int round = 0; round < 4; ++round) {
        std::vector<std::string> target = order;
        std::shuffle(target.begin(), target.end(), rng);
        // Selection sort through moves: put target[i] right after target[i-1] (or first).
        for (size_t i = 0; i < target.size(); ++i) {
            const auto now = subjectsAlongMain(s, l.path);
            const std::string anchor = i == 0 ? now[1] : target[i - 1];
            if (target[i] == anchor)
                continue;
            if (i == 0 && now[1] == target[0])
                continue;
            if (i > 0 && std::find(now.begin(), now.end(), target[i - 1]) + 1 < now.end()
                && *(std::find(now.begin(), now.end(), target[i - 1]) + 1) == target[i])
                continue;
            GG_CHECK(move(s, l.path, target[i], anchor, i != 0));
        }
        const auto now = subjectsAlongMain(s, l.path);
        GG_CHECK(std::vector<std::string>(now.begin() + 1, now.end()) == target);
        // No nested markers, ever: every conflicted file parses into well-formed regions only.
        GG_CHECK(!s.gitMayFail(l.path, {"grep", "-q", "^<<<<<<<.*<<<<<<<", "main"}).ok());
    }
    // Back to v1..v4: the original blobs.
    for (size_t i = 0; i < order.size(); ++i) {
        const auto now = subjectsAlongMain(s, l.path);
        if (now[i + 1] == order[i])
            continue;
        GG_CHECK(move(s, l.path, order[i], i == 0 ? now[1] : order[i - 1], i != 0));
    }
    GG_CHECK(blobsAlongMain(s, l.path, "f.txt") == original);
}

GG_TEST("edges", "plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push",
    "CONF-TRANSPARENCY", "CONF-GIT-MERGE-GGUI-REGIONS")
{
    const fs::path repo = s.fixture(Recipe::Conflicted2);
    s.git(repo, {"branch", "other", "HEAD~2"});
    s.git(repo, {"switch", "-q", "other"});
    s.commitFile(repo, "unrelated.txt", "u\n", "Unrelated");
    s.git(repo, {"switch", "-q", "main"});
    auto conflicted = [&](const fs::path& at, const std::string& rev) { return s.gitgg(at, {"conflicts", rev}).exitCode == 1; };
    GG_REQUIRE(conflicted(repo, "main"));
    s.git(repo, {"rebase", "-q", "other"});
    GG_CHECK(conflicted(repo, "HEAD"));
    s.git(repo, {"switch", "-q", "other"});
    s.git(repo, {"cherry-pick", "main~1"});
    GG_CHECK(conflicted(repo, "HEAD"));
    s.git(repo, {"commit", "-q", "--amend", "-m", "Picked and amended"});
    GG_CHECK(conflicted(repo, "HEAD"));
    s.git(repo, {"switch", "-q", "main"});
    s.git(repo, {"merge", "-q", "--no-edit", "other", "-s", "ours"});
    GG_CHECK(conflicted(repo, "HEAD"));
    s.write(repo, "scratch.txt", "s\n");
    s.git(repo, {"stash", "push", "-q", "-u"});
    s.git(repo, {"stash", "pop", "-q"});
    fs::remove(repo / "scratch.txt");
    GG_CHECK(conflicted(repo, "HEAD"));
    s.git(repo, {"gc", "-q", "--prune=now"});
    GG_CHECK(conflicted(repo, "HEAD"));
    const fs::path clone = s.path("transparency-clone");
    s.git(s.root(), {"clone", "-q", repo.string(), clone.string()});
    s.track(clone);
    GG_CHECK(conflicted(clone, "HEAD"));
    const fs::path bare = s.path("transparency-bare.git");
    s.git(s.root(), {"init", "-q", "--bare", bare.string()});
    s.track(bare);
    s.git(repo, {"push", "-q", "--no-verify", bare.string(), "main"});
    GG_CHECK(conflicted(bare, "main"));
    // The UI shows the same.
    GG_REQUIRE(s.openRepository(clone));
    GG_CHECK(s.waitUntil([&] { return s.session()->conflictsOf(Oid::fromHex(s.head(clone))) != nullptr; }));
    ctx->MenuClick("//##MainMenuBar/Repository/Close repository");
    ctx->Yield(3);

    // Plain git merging a file with ggui regions: a change elsewhere in the file merges cleanly
    // and the regions stay a first-class conflict; a change on the region itself is a native
    // conflict, and once that is resolved the ggui region is a first-class conflict again.
    const fs::path m = s.fixture(Recipe::Empty);
    std::string head, tail;
    for (int i = 1; i <= 10; ++i)
        head += "l" + std::to_string(i) + "\n";
    for (int i = 12; i <= 20; ++i)
        tail += "l" + std::to_string(i) + "\n";
    s.commitFile(m, "f.txt", head + "x=0\n" + tail, "base");
    s.git(m, {"branch", "edit-far"});
    s.git(m, {"branch", "edit-near"});
    const std::string region = "<<<<<<< side 1\nx=1\n||||||| base\nx=0\n=======\nx=2\n>>>>>>> side 2\n";
    s.commitFile(m, "f.txt", head + region + tail, "ggui region");
    s.git(m, {"switch", "-q", "edit-far"});
    std::string farHead = head;
    farHead.replace(0, 3, "L1\n");
    s.commitFile(m, "f.txt", farHead + "x=0\n" + tail, "Edit far away");
    s.git(m, {"switch", "-q", "edit-near"});
    s.commitFile(m, "f.txt", head + "x=9\n" + tail, "Edit the same line");
    s.git(m, {"switch", "-q", "main"});
    GG_CHECK(s.gitMayFail(m, {"merge", "-q", "--no-edit", "edit-far"}).ok());
    GG_CHECK(s.read(m, "f.txt") == farHead + region + tail);
    GG_CHECK(conflicted(m, "HEAD"));
    // On the same line: git stops with index stages; ggui shows a native conflict.
    GG_CHECK(!s.gitMayFail(m, {"merge", "-q", "--no-edit", "edit-near"}).ok());
    GG_REQUIRE(s.openRepository(m));
    const std::string row = s.child("//Changes", "##files") + "/Conflicted/f.txt/###file_f.txt";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(row.c_str()); }));
    GG_CHECK(!s.session()->status()->conflicted.empty() && !s.session()->status()->conflicted.front().firstClass);
    s.contextMenu(row.c_str(), "Take ours");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(m, {"ls-files", "-u"}).empty(); }));
    s.settle();
    // Git's conflict is gone; the ggui region is still there, as a first-class conflict.
    GG_CHECK(s.read(m, "f.txt") == farHead + region + tail);
    GG_CHECK(s.waitUntil([&] {
        const auto st = s.session()->status();
        return st && !st->conflicted.empty() && st->conflicted.front().firstClass;
    }));
}

} // namespace ggtest
