// The pure parser of `git status --porcelain=v2 -z` output: no repository, fixed input strings.
#include "core/StatusPorcelain.hpp"
#include "tests/Harness.hpp"

#include <initializer_list>
#include <stdexcept>

namespace ggtest {

namespace {

using namespace ggui::core;

const std::string kH(40, 'a');
const std::string kZero(40, '0');

// Joins records with the NUL terminator that -z puts after each of them.
std::string nul(std::initializer_list<std::string> records)
{
    std::string out;
    for (const auto& r : records) {
        out += r;
        out += '\0';
    }
    return out;
}

// `1 XY sub mH mI mW hH hI path`
std::string ordinary(const std::string& xy, const std::string& sub, const std::string& path)
{
    return "1 " + xy + " " + sub + " 100644 100644 100644 " + kH + " " + kH + " " + path;
}

// `2 XY sub mH mI mW hH hI Xscore path` (the original path is the next record)
std::string renamed(const std::string& xy, const std::string& score, const std::string& path)
{
    return "2 " + xy + " N... 100644 100644 100644 " + kH + " " + kH + " " + score + " " + path;
}

// `u XY sub m1 m2 m3 mW h1 h2 h3 path`
std::string unmerged(const std::string& xy, const std::string& modes, const std::string& path)
{
    std::string m[3] = {"000000", "000000", "000000"};
    std::string h[3] = {kZero, kZero, kZero};
    for (int i = 0; i < 3; ++i)
        if (modes[i] != '-') {
            m[i] = "100644";
            h[i] = std::string(40, static_cast<char>('1' + i));
        }
    return "u " + xy + " N... " + m[0] + " " + m[1] + " " + m[2] + " 100644 " + h[0] + " " + h[1] + " " + h[2] + " "
        + path;
}

bool throwsMalformed(const std::string& input)
{
    try {
        parsePorcelainV2(input);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

} // namespace

GG_TEST("status_porcelain", "empty input and headers give an empty result")
{
    GG_CHECK(parsePorcelainV2("").status.empty());
    const auto p = parsePorcelainV2(nul({"# branch.oid " + kH, "# branch.head main", "! ignored.o", "x unknown"}));
    GG_CHECK(p.status.empty());
    GG_CHECK(p.conflictOids.empty());
}

GG_TEST("status_porcelain", "staged and unstaged kinds")
{
    const struct {
        const char* letter;
        ChangeKind kind;
    } kinds[] = {{"M", ChangeKind::Modified}, {"A", ChangeKind::Added}, {"D", ChangeKind::Deleted},
        {"T", ChangeKind::TypeChanged}};
    for (const auto& k : kinds) {
        const std::string l = k.letter;
        const auto staged = parsePorcelainV2(nul({ordinary(l + ".", "N...", "f.txt")}));
        GG_CHECK_EQ(staged.status.staged.size(), static_cast<size_t>(1));
        GG_CHECK(staged.status.unstaged.empty());
        if (!staged.status.staged.empty()) {
            GG_CHECK(staged.status.staged[0].kind == k.kind);
            GG_CHECK_STR_EQ(staged.status.staged[0].path, "f.txt");
            GG_CHECK(!staged.status.staged[0].intentToAdd);
        }
        // Unstaged "A" alone is an intent-to-add entry; the other kinds map one to one.
        const auto unstaged = parsePorcelainV2(nul({ordinary("." + l, "N...", "f.txt")}));
        GG_CHECK(unstaged.status.staged.empty());
        GG_CHECK_EQ(unstaged.status.unstaged.size(), static_cast<size_t>(1));
        if (!unstaged.status.unstaged.empty())
            GG_CHECK(unstaged.status.unstaged[0].kind == k.kind);
    }
}

GG_TEST("status_porcelain", "staged and unstaged at once (MM)")
{
    const auto p = parsePorcelainV2(nul({ordinary("MM", "N...", "both.txt")}));
    GG_REQUIRE(p.status.staged.size() == 1 && p.status.unstaged.size() == 1);
    GG_CHECK_STR_EQ(p.status.staged[0].path, "both.txt");
    GG_CHECK_STR_EQ(p.status.unstaged[0].path, "both.txt");
    GG_CHECK(p.status.staged[0].kind == ChangeKind::Modified);
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Modified);
}

GG_TEST("status_porcelain", "intent-to-add is an unstaged addition")
{
    const auto p = parsePorcelainV2(nul({ordinary(".A", "N...", "new.txt")}));
    GG_CHECK(p.status.staged.empty());
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Added);
    GG_CHECK(p.status.unstaged[0].intentToAdd);
    // A staged addition that is also modified is no intent-to-add.
    const auto q = parsePorcelainV2(nul({ordinary("AM", "N...", "new.txt")}));
    GG_REQUIRE(q.status.staged.size() == 1 && q.status.unstaged.size() == 1);
    GG_CHECK(!q.status.staged[0].intentToAdd);
    GG_CHECK(!q.status.unstaged[0].intentToAdd);
}

GG_TEST("status_porcelain", "renames and copies")
{
    auto p = parsePorcelainV2(nul({renamed("R.", "R100", "new name.txt"), "old name.txt"}));
    GG_CHECK(p.status.unstaged.empty());
    GG_REQUIRE(p.status.staged.size() == 1);
    GG_CHECK(p.status.staged[0].kind == ChangeKind::Renamed);
    GG_CHECK_STR_EQ(p.status.staged[0].path, "new name.txt");
    GG_CHECK_STR_EQ(p.status.staged[0].oldPath, "old name.txt");

    // Renamed and modified in the working tree: the unstaged side is an ordinary entry.
    p = parsePorcelainV2(nul({renamed("RM", "R90", "b.txt"), "a.txt"}));
    GG_REQUIRE(p.status.staged.size() == 1 && p.status.unstaged.size() == 1);
    GG_CHECK(p.status.staged[0].kind == ChangeKind::Renamed);
    GG_CHECK_STR_EQ(p.status.staged[0].oldPath, "a.txt");
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Modified);
    GG_CHECK_STR_EQ(p.status.unstaged[0].path, "b.txt");
    GG_CHECK(p.status.unstaged[0].oldPath.empty());

    p = parsePorcelainV2(nul({renamed("C.", "C75", "copy.txt"), "orig.txt"}));
    GG_REQUIRE(p.status.staged.size() == 1);
    GG_CHECK(p.status.staged[0].kind == ChangeKind::Copied);
    GG_CHECK_STR_EQ(p.status.staged[0].oldPath, "orig.txt");

    // The record after a rename is its original path, not a record of its own.
    p = parsePorcelainV2(nul({renamed("R.", "R100", "n.txt"), "? looks like a record", ordinary("M.", "N...", "m.txt")}));
    GG_CHECK_EQ(p.status.staged.size(), static_cast<size_t>(2));
    GG_CHECK(p.status.untracked.empty());
    GG_CHECK(throwsMalformed(nul({renamed("R.", "R100", "n.txt")})));
}

GG_TEST("status_porcelain", "unstaged rename gives a deleted old path and an added new path")
{
    // `--renames` pairs a deleted path with an intent-to-add path on the unstaged side.
    auto p = parsePorcelainV2(nul({renamed(".R", "R100", "new.txt"), "old.txt"}));
    GG_CHECK(p.status.staged.empty());
    GG_REQUIRE(p.status.unstaged.size() == 2);
    // The list is sorted by path: new.txt, then old.txt.
    GG_CHECK_STR_EQ(p.status.unstaged[0].path, "new.txt");
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Added);
    GG_CHECK(p.status.unstaged[0].intentToAdd);
    GG_CHECK(p.status.unstaged[0].oldPath.empty());
    GG_CHECK_STR_EQ(p.status.unstaged[1].path, "old.txt");
    GG_CHECK(p.status.unstaged[1].kind == ChangeKind::Deleted);

    // A copy keeps the old path: only the added entry.
    p = parsePorcelainV2(nul({renamed(".C", "C100", "copy.txt"), "orig.txt"}));
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK_STR_EQ(p.status.unstaged[0].path, "copy.txt");
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Added);

    // A staged side keeps its own rename. The record after the pair is still an own record.
    p = parsePorcelainV2(nul({renamed("RR", "R100", "b.txt"), "a.txt", ordinary("M.", "N...", "m.txt")}));
    GG_REQUIRE(p.status.staged.size() == 2);
    GG_CHECK(p.status.unstaged.size() == 2);
}

GG_TEST("status_porcelain", "submodule states")
{
    auto p = parsePorcelainV2(nul({ordinary(".M", "SCMU", "sub")}));
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK(p.status.unstaged[0].submodule);
    GG_CHECK_EQ(p.status.unstaged[0].submoduleState, unsigned(NewCommits | ModifiedContent | UntrackedContent));

    p = parsePorcelainV2(nul({ordinary(".M", "S.M.", "sub")}));
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK_EQ(p.status.unstaged[0].submoduleState, unsigned(ModifiedContent));

    // The state belongs to the unstaged entry only; the flag is on both.
    p = parsePorcelainV2(nul({ordinary("MM", "SC..", "sub")}));
    GG_REQUIRE(p.status.staged.size() == 1 && p.status.unstaged.size() == 1);
    GG_CHECK(p.status.staged[0].submodule);
    GG_CHECK_EQ(p.status.staged[0].submoduleState, 0u);
    GG_CHECK_EQ(p.status.unstaged[0].submoduleState, unsigned(NewCommits));

    // A deleted submodule working directory.
    p = parsePorcelainV2(nul({ordinary(".D", "S...", "sub")}));
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK(p.status.unstaged[0].kind == ChangeKind::Deleted);
    GG_CHECK(p.status.unstaged[0].submoduleState & Missing);

    // A plain file keeps no submodule state.
    p = parsePorcelainV2(nul({ordinary(".D", "N...", "gone.txt")}));
    GG_REQUIRE(p.status.unstaged.size() == 1);
    GG_CHECK(!p.status.unstaged[0].submodule);
    GG_CHECK_EQ(p.status.unstaged[0].submoduleState, 0u);
}

GG_TEST("status_porcelain", "conflict records give stages, descriptions and object ids")
{
    const struct {
        const char* xy;
        const char* stages;
        const char* text;
        bool s1, s2, s3;
    } rows[] = {
        {"UU", "123", "both modified", true, true, true},
        {"DU", "1-3", "deleted by us", true, false, true},
        {"UD", "12-", "deleted by them", true, true, false},
        {"AA", "-23", "both added", false, true, true},
        {"AU", "-2-", "added by us", false, true, false},
        {"UA", "--3", "added by them", false, false, true},
        {"DD", "1--", "both deleted", true, false, false},
    };
    for (const auto& r : rows) {
        const auto p = parsePorcelainV2(nul({unmerged(r.xy, r.stages, "c file.txt")}));
        GG_CHECK(p.status.staged.empty() && p.status.unstaged.empty() && p.status.untracked.empty());
        GG_REQUIRE(p.status.conflicted.size() == 1 && p.conflictOids.size() == 1);
        const StatusEntry& e = p.status.conflicted[0];
        GG_CHECK(e.kind == ChangeKind::Conflicted);
        GG_CHECK_STR_EQ(e.path, "c file.txt");
        GG_CHECK_EQ(e.stage1, r.s1);
        GG_CHECK_EQ(e.stage2, r.s2);
        GG_CHECK_EQ(e.stage3, r.s3);
        GG_CHECK_STR_EQ(e.conflictDescription, r.text);
        GG_CHECK_STR_EQ(p.conflictOids[0].h1, r.s1 ? std::string(40, '1') : kZero);
        GG_CHECK_STR_EQ(p.conflictOids[0].h2, r.s2 ? std::string(40, '2') : kZero);
        GG_CHECK_STR_EQ(p.conflictOids[0].h3, r.s3 ? std::string(40, '3') : kZero);
    }
}

GG_TEST("status_porcelain", "untracked path with a space")
{
    const auto p = parsePorcelainV2(nul({"? dir/my file.txt"}));
    GG_REQUIRE(p.status.untracked.size() == 1);
    GG_CHECK_STR_EQ(p.status.untracked[0].path, "dir/my file.txt");
    GG_CHECK(p.status.untracked[0].kind == ChangeKind::Untracked);
    // Paths with spaces in the other record types.
    const auto q = parsePorcelainV2(nul({ordinary("M.", "N...", "a b  c.txt")}));
    GG_REQUIRE(q.status.staged.size() == 1);
    GG_CHECK_STR_EQ(q.status.staged[0].path, "a b  c.txt");
}

GG_TEST("status_porcelain", "malformed records throw")
{
    GG_CHECK(throwsMalformed(nul({"1 M."})));
    GG_CHECK(throwsMalformed(nul({"1 M. N... 100644 100644 100644 " + kH + " " + kH})));
    GG_CHECK(throwsMalformed(nul({"2 R. N... 100644 100644 100644 " + kH + " " + kH + " R100"})));
    GG_CHECK(throwsMalformed(nul({"u UU N... 000000 100644 100644 100644"})));
    GG_CHECK(throwsMalformed(nul({"?"})));
    // The message names the record type and the raw record.
    try {
        parsePorcelainV2(nul({"1 M."}));
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        GG_CHECK(what.find("'1'") != std::string::npos);
        GG_CHECK(what.find("1 M.") != std::string::npos);
    }
}

GG_TEST("status_porcelain", "output is sorted by path")
{
    const auto p = parsePorcelainV2(nul({ordinary("M.", "N...", "z.txt"), ordinary("A.", "N...", "a.txt"),
        ordinary(".M", "N...", "y.txt"), ordinary(".M", "N...", "b.txt"), "? u2", "? u1",
        unmerged("UU", "123", "k"), unmerged("AA", "-23", "c"), unmerged("DD", "1--", "m")}));
    GG_REQUIRE(p.status.staged.size() == 2 && p.status.unstaged.size() == 2 && p.status.untracked.size() == 2);
    GG_CHECK_STR_EQ(p.status.staged[0].path, "a.txt");
    GG_CHECK_STR_EQ(p.status.staged[1].path, "z.txt");
    GG_CHECK_STR_EQ(p.status.unstaged[0].path, "b.txt");
    GG_CHECK_STR_EQ(p.status.unstaged[1].path, "y.txt");
    GG_CHECK_STR_EQ(p.status.untracked[0].path, "u1");
    GG_CHECK_STR_EQ(p.status.untracked[1].path, "u2");
    GG_REQUIRE(p.status.conflicted.size() == 3 && p.conflictOids.size() == 3);
    GG_CHECK_STR_EQ(p.status.conflicted[0].path, "c");
    GG_CHECK_STR_EQ(p.status.conflicted[1].path, "k");
    GG_CHECK_STR_EQ(p.status.conflicted[2].path, "m");
    // The object ids follow their entries: "c" is AA (no base), "k" is UU, "m" is DD (base only).
    GG_CHECK_STR_EQ(p.conflictOids[0].h1, kZero);
    GG_CHECK_STR_EQ(p.conflictOids[1].h1, std::string(40, '1'));
    GG_CHECK_STR_EQ(p.conflictOids[1].h3, std::string(40, '3'));
    GG_CHECK_STR_EQ(p.conflictOids[2].h2, kZero);
}

GG_TEST("status_porcelain", "parses real git output")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "untracked file.txt", "x\n");
    s.write(repo, "added.txt", "y\n");
    s.gitOut(repo, {"add", "added.txt"});
    const auto p = parsePorcelainV2(s.statusPorcelain(repo));
    GG_REQUIRE(p.status.untracked.size() == 1);
    GG_CHECK_STR_EQ(p.status.untracked[0].path, "untracked file.txt");
    GG_REQUIRE(p.status.staged.size() == 1);
    GG_CHECK_STR_EQ(p.status.staged[0].path, "added.txt");
    GG_CHECK(p.status.staged[0].kind == ChangeKind::Added);
}

} // namespace ggtest
