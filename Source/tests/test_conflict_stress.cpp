// Stress tests for stacked first-class conflicts (REBUILD_PLAN §4.10, docs/spec/conflict-markers.md
// §7): an ancestor edited under a stack whose commits already hold conflicts, so that several
// descendants conflict again, on top of their own conflicts. Two levels: the marker algebra on
// random file contents (thousands of cases), and the rewrite engine on a real repository.
#include "tests/Harness.hpp"

#include <libgg/Conflicts.hpp>
#include <libgg/Git2.hpp>
#include <libgg/Markers.hpp>
#include <libgg/Rewrite.hpp>

#include <git2.h>

#include <algorithm>
#include <map>
#include <random>
#include <sstream>

namespace ggtest {

namespace {

namespace mk = gg::markers;

// Random file contents built from a small vocabulary, so that edits collide often. Lines may end
// in CRLF, the last line may have no newline, and some lines look like conflict markers.
struct Gen {
    std::mt19937_64 rng;
    explicit Gen(std::uint64_t seed) : rng(seed) { }
    int pick(int n) { return static_cast<int>(rng() % static_cast<std::uint64_t>(n)); }
    bool chance(int percent) { return pick(100) < percent; }

    std::string line()
    {
        static const char* words[] = {"alpha", "beta", "gamma", "delta", "x = 1;", "x = 2;", "", "}", "{",
            "=======", "<<<<<<< not a region", ">>>>>>>", "|||||||", "+++++++ side 1", "------- base"};
        const int n = static_cast<int>(std::size(words));
        // Mostly plain words; marker-like lines now and then.
        const int i = chance(10) ? pick(n) : pick(9);
        return words[i];
    }

    std::vector<std::string> lines(int count)
    {
        std::vector<std::string> out;
        for (int i = 0; i < count; ++i)
            out.push_back(line());
        return out;
    }

    static std::string join(const std::vector<std::string>& ls, bool crlf, bool finalNewline)
    {
        std::string s;
        for (size_t i = 0; i < ls.size(); ++i) {
            s += ls[i];
            if (i + 1 < ls.size() || finalNewline)
                s += crlf ? "\r\n" : "\n";
        }
        return s;
    }

    // An edit of a file: some lines replaced, inserted or deleted, mostly in one small area.
    std::vector<std::string> edit(std::vector<std::string> ls)
    {
        const int edits = 1 + pick(3);
        const int centre = ls.empty() ? 0 : pick(static_cast<int>(ls.size()));
        for (int e = 0; e < edits; ++e) {
            const int at = std::clamp(centre + pick(5) - 2, 0, std::max(0, static_cast<int>(ls.size()) - 1));
            switch (pick(3)) {
            case 0:
                if (!ls.empty())
                    ls[static_cast<size_t>(at)] = line() + " " + std::to_string(pick(100));
                break;
            case 1: ls.insert(ls.begin() + at, line() + " new " + std::to_string(pick(100))); break;
            default:
                if (ls.size() > 1)
                    ls.erase(ls.begin() + at);
            }
        }
        return ls;
    }
};

// No region inside a region: no section of any region is itself conflicted.
bool nested(std::string_view text)
{
    for (const auto& r : mk::parse(text).regions) {
        for (const auto& s : r.sides)
            if (mk::isConflicted(s.content))
                return true;
        for (const auto& b : r.bases)
            if (mk::isConflicted(b.content))
                return true;
    }
    return false;
}

std::string shown(std::string_view s)
{
    std::string out;
    for (char c : s)
        out += c == '\r' ? std::string("\\r") : std::string(1, c);
    return out;
}

} // namespace

GG_TEST("conflict-stress", "algebra: a stack with first-class conflicts rebased onto an edited ancestor stays well-formed and never nests",
    "CONF-STACK-REBASE")
{
    Gen g(s.seed());
    mk::WriteOptions writeOptions;
    const char* strict = std::getenv("GG_STRESS_STRICT");
    writeOptions.sameChangeResolves = !(strict && *strict);
    int failures = 0;
    int conflictedSeen = 0;
    int maxSides = 0;
    int exact = 0, sameValueOnly = 0, differentValue = 0;
    for (int round = 0; round < 400; ++round) {
        // A stack: F0 (the ancestor), then F1..Fk, each an edit of the one before.
        const bool crlf = g.chance(25);
        const bool finalNewline = !g.chance(15);
        const int k = 2 + g.pick(5);
        std::vector<std::vector<std::string>> stackLines{g.lines(4 + g.pick(8))};
        for (int i = 1; i <= k; ++i)
            stackLines.push_back(g.edit(stackLines.back()));
        std::vector<std::string> original;
        for (const auto& ls : stackLines)
            original.push_back(Gen::join(ls, crlf, finalNewline));
        // Earlier rounds leave conflicts in the stack: edit the ancestor a first time and keep the
        // rebased (conflicted) stack as the starting point.
        auto rebase = [&](const std::vector<std::string>& from, const std::string& newBase) {
            std::vector<std::string> out{newBase};
            for (size_t i = 1; i < from.size(); ++i)
                out.push_back(mk::mergeFiles(from[i - 1], out.back(), from[i], writeOptions));
            return out;
        };
        std::vector<std::string> start = original;
        if (g.chance(70))
            start = rebase(original, Gen::join(g.edit(stackLines[0]), crlf, finalNewline));
        // Now edit the ancestor (again): several children conflict on top of their conflicts.
        const std::string edited = Gen::join(g.edit(g.edit(stackLines[0])), crlf, finalNewline);
        std::vector<std::string> after;
        try {
            after = rebase(start, edited);
        } catch (const std::exception& e) {
            ctx->LogError("round %d: the rebase threw: %s", round, e.what());
            ++failures;
            continue;
        }
        for (size_t i = 0; i < after.size(); ++i) {
            const auto parsed = mk::parse(after[i]);
            if (parsed.conflicted()) {
                ++conflictedSeen;
                maxSides = std::max(maxSides, parsed.maxSides());
            }
            if (nested(after[i])) {
                ctx->LogError("round %d: commit %zu has nested regions:\n%s", round, i, shown(after[i]).c_str());
                ++failures;
            }
            // What the scanner reports agrees with the parser.
            if ((gg::conflicts::contentSides(after[i]) > 0) != parsed.conflicted()) {
                ctx->LogError("round %d: commit %zu: scanner and parser disagree", round, i);
                ++failures;
            }
        }
        // Undo the edit (the ancestor as it was): how often each commit comes back byte for byte,
        // as the same conflict written differently, or as a different conflict (logged: Git's rule
        // for the same change on both sides alone makes some of these differ).
        const auto back = rebase(after, start[0]);
        for (size_t i = 0; i < back.size(); ++i) {
            if (back[i] == start[i])
                ++exact;
            else if (mk::sameValue(back[i], start[i]))
                ++sameValueOnly;
            else
                ++differentValue;
        }
    }
    ctx->LogInfo("conflicted commits seen: %d, most sides: %d", conflictedSeen, maxSides);
    ctx->LogInfo("round trips: %d exact, %d the same conflict written differently, %d a different conflict", exact,
        sameValueOnly, differentValue);
    GG_CHECK(conflictedSeen > 100); // the cases do exercise stacked conflicts
    GG_CHECK(maxSides >= 3);
    GG_CHECK_EQ(failures, 0);
}

} // namespace ggtest

namespace ggtest {

GG_TEST("conflict-stress", "engine: ancestors edited under a stack with first-class conflicts; every rewrite applies and stays well-formed",
    "CONF-STACK-REWRITE-ROBUST")
{
    Gen g(s.seed() ^ 0x5eed);
    const fs::path repo = s.fixture(Recipe::Empty, "stack");
    s.track(repo);
    // Three files (LF, CRLF, no final newline) changed in the same few places by every commit.
    struct File {
        std::string name;
        bool crlf;
        bool finalNewline;
    };
    const std::vector<File> files{{"a.txt", false, true}, {"b.txt", true, true}, {"c.txt", false, false}};
    std::map<std::string, std::vector<std::string>> lines;
    for (const auto& f : files) {
        lines[f.name] = g.lines(8);
        s.write(repo, f.name, Gen::join(lines[f.name], f.crlf, f.finalNewline));
    }
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "base"});
    for (int c = 1; c <= 7; ++c) {
        for (const auto& f : files)
            if (g.chance(70)) {
                lines[f.name] = g.edit(lines[f.name]);
                s.write(repo, f.name, Gen::join(lines[f.name], f.crlf, f.finalNewline));
            }
        s.git(repo, {"add", "."});
        s.git(repo, {"commit", "-q", "--allow-empty", "-m", "c" + std::to_string(c)});
    }
    auto stack = [&] {
        std::vector<std::string> ids;
        for (const auto& l : gg::splitLines(s.gitOut(repo, {"rev-list", "--reverse", "main"})))
            if (!l.empty())
                ids.push_back(l);
        return ids;
    };
    int conflictedCommits = 0;
    int maxSides = 0;
    bool rawEdits = false; // conflicted text edited raw: marker-like leftovers may look nested
    for (int round = 0; round < 25; ++round) {
        const auto ids = stack();
        const size_t at = static_cast<size_t>(g.pick(static_cast<int>(ids.size()) - 1)); // not the tip
        const File& f = files[static_cast<size_t>(g.pick(static_cast<int>(files.size())))];
        // The file as that commit has it, edited like a user would: a conflicted file mostly after
        // taking a side, now and then as raw text (which can leave marker-like lines behind).
        std::string current = s.gitMayFail(repo, {"show", ids[at] + ":" + f.name}).out;
        const bool raw = gg::markers::isConflicted(current) && g.chance(20);
        if (gg::markers::isConflicted(current) && !raw)
            current = gg::markers::takeSide(current, g.pick(gg::markers::parse(current).maxSides()));
        rawEdits = rawEdits || raw;
        std::vector<std::string> ls;
        for (const auto& l : gg::splitLines(current))
            ls.push_back(!l.empty() && l.back() == '\r' ? l.substr(0, l.size() - 1) : l);
        const std::string edited = Gen::join(g.edit(ls), f.crlf, f.finalNewline);

        gg::rewrite::Plan plan;
        std::string computeError;
        {
            gg::git2::Repository r = gg::git2::openRepository(repo);
            plan = gg::rewrite::replayPlan(r.get(), {ids[at]});
        }
        bool found = false;
        for (auto& step : plan.steps)
            if (step.source == ids[at]) {
                step.setFiles.push_back({f.name, edited});
                found = true;
            }
        GG_REQUIRE(found);
        gg::rewrite::Rewriter rewriter(repo);
        gg::rewrite::Result result;
        try {
            result = rewriter.compute(plan);
        } catch (const std::exception& e) {
            ctx->LogError("round %d: compute threw: %s", round, e.what());
            GG_CHECK(false);
            break;
        }
        if (!result.ok || !result.unresolved.empty())
            ctx->LogError("round %d: compute: ok=%d, %zu non-text conflicts, %s", round, result.ok ? 1 : 0,
                result.unresolved.size(), result.error.c_str());
        GG_REQUIRE(result.ok && result.unresolved.empty());
        std::string error;
        const bool applied = rewriter.apply(plan, result, error);
        if (!applied)
            ctx->LogError("round %d: apply: %s", round, error.c_str());
        GG_REQUIRE(applied);
        // Every commit of the stack: well-formed regions only, never nested; the working tree
        // follows HEAD; the repository passes fsck.
        for (const auto& id : stack())
            for (const auto& file : files) {
                const auto r = s.gitMayFail(repo, {"show", id + ":" + file.name});
                if (!r.ok())
                    continue;
                const auto parsed = gg::markers::parse(r.out);
                if (parsed.conflicted()) {
                    ++conflictedCommits;
                    maxSides = std::max(maxSides, parsed.maxSides());
                }
                if (!rawEdits && nested(r.out))
                    ctx->LogInfo("round %d: %s:%s nested:\n%s", round, id.substr(0, 10).c_str(), file.name.c_str(), shown(r.out).c_str());
                GG_CHECK(rawEdits || !nested(r.out));
                GG_CHECK((gg::conflicts::contentSides(r.out) > 0) == parsed.conflicted());
            }
        GG_CHECK(s.statusPorcelain(repo).empty());
        GG_CHECK(s.fsck(repo));
    }
    ctx->LogInfo("conflicted files across the stack: %d, most sides: %d", conflictedCommits, maxSides);
    GG_CHECK(conflictedCommits > 20); // the rounds did stack conflicts
}

} // namespace ggtest
