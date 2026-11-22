// Table tests for the pure parts of two panels: refPills (History pills) and the Rebase preview graph layout (GG-98).
#include "panels/HistoryPanel.hpp"
#include "panels/RebasePanel.hpp"
#include "tests/Harness.hpp"

#include <string>
#include <vector>

namespace ggtest {

namespace core = ggui::core;

namespace {

using core::RefBadge;
using core::RefKind;

RefBadge local(const std::string& n) { return {RefKind::LocalBranch, n, false, {}}; }
RefBadge remote(const std::string& r, const std::string& n) { return {RefKind::RemoteBranch, r + "/" + n, false, r}; }
RefBadge other(RefKind k, const std::string& n) { return {k, n, false, {}}; }

// Joined remote names of a pill, e.g. "origin,upstream".
std::string remotesOf(const ggui::RefPill& p)
{
    std::string out;
    for (const auto& r : p.remotes)
        out += (out.empty() ? "" : ",") + r.remote;
    return out;
}

core::RebasePreview::Row prow(const std::string& id, std::vector<std::string> parents, bool conflict = false)
{
    core::RebasePreview::Row r;
    r.id = id;
    r.parents = std::move(parents);
    if (conflict)
        r.conflicts.emplace_back("f.txt", 2);
    return r;
}

// One graph row as text: "lane[!]/color pN from>to@fromPos-toPos ..." with the lines in stored order.
std::string show(const core::HistoryRow& g)
{
    std::string out = std::to_string(g.lane) + (g.conflicted ? "!" : "") + "/" + std::to_string(g.color) + " p"
        + std::to_string(g.parents.size());
    for (const auto& l : g.lines)
        out += " " + std::to_string(l.fromLane) + ">" + std::to_string(l.toLane) + "@" + std::to_string(l.fromPos) + "-"
            + std::to_string(l.toPos);
    return out;
}

} // namespace

GG_TEST("refpills", "refPills groups, orders and filters the refs of a row")
{
    core::Snapshot snap;
    snap.branches.push_back({"dual", {}, "upstream/dual", "upstream"});
    snap.branches.push_back({"main", {}, "origin/main", "origin"});

    struct Row {
        const char* what;
        std::vector<RefBadge> refs;
        bool hideLocalMain; // the filter hides the local branch "main"
        const char* expect; // per pill: shortName|hasLocal|target name|remotes, joined by ";"
    };
    const std::vector<Row> rows{
        {"no refs", {}, false, ""},
        {"HEAD, tag and stash keep order",
            {other(RefKind::Head, "HEAD"), other(RefKind::Tag, "v1"), other(RefKind::Stash, "s0")}, false,
            "HEAD|0|HEAD|;v1|0|v1|;s0|0|s0|"},
        {"local branch", {local("main")}, false, "main|1|main|"},
        {"local and remote of one name share a pill", {local("main"), remote("origin", "main")}, false,
            "main|1|main|origin"},
        {"remote of another name is its own pill", {local("main"), remote("origin", "dev")}, false,
            "main|1|main|;dev|0|origin/dev|origin"},
        {"remote with a slash in the branch name", {remote("origin", "feat/x")}, false,
            "feat/x|0|origin/feat/x|origin"},
        {"two remotes with a local keep the local target",
            {local("main"), remote("origin", "main"), remote("upstream", "main")}, false, "main|1|main|origin,upstream"},
        {"two remotes, no local: the tracked remote is the target",
            {remote("origin", "dual"), remote("upstream", "dual")}, false, "dual|0|upstream/dual|origin,upstream"},
        {"two remotes, branch unknown: the first remote is the target", {remote("a", "zz"), remote("b", "zz")}, false,
            "zz|0|a/zz|a,b"},
        {"hidden local leaves a remote-only pill", {local("main"), remote("origin", "main")}, true,
            "main|0|origin/main|origin"},
        {"a remote without a remote name is a plain pill", {RefBadge{RefKind::RemoteBranch, "odd", false, {}}}, false,
            "odd|0|odd|"},
        {"mixed order is kept",
            {other(RefKind::Head, "HEAD"), local("main"), other(RefKind::Tag, "v1"), remote("origin", "main")}, false,
            "HEAD|0|HEAD|;main|1|main|origin;v1|0|v1|"},
    };
    for (const auto& row : rows) {
        const auto pills = ggui::refPills(
            row.refs,
            [&](const RefBadge& r) { return !(row.hideLocalMain && r.kind == RefKind::LocalBranch && r.name == "main"); },
            snap);
        std::string got;
        for (const auto& p : pills)
            got += (got.empty() ? "" : ";") + p.shortName + "|" + (p.hasLocal ? "1" : "0") + "|" + p.target.name + "|"
                + remotesOf(p);
        if (got != row.expect)
            ctx->LogError("refPills row '%s': got '%s', want '%s'", row.what, got.c_str(), row.expect);
        GG_CHECK(got == row.expect);
    }
    // A long list stays whole and in order; a filter that refuses all leaves nothing.
    std::vector<RefBadge> many;
    for (int i = 0; i < 200; ++i)
        many.push_back(other(RefKind::Tag, "t" + std::to_string(i)));
    const auto pills = ggui::refPills(many, [](const RefBadge&) { return true; }, snap);
    GG_CHECK_EQ(pills.size(), size_t(200));
    GG_CHECK_STR_EQ(pills[199].shortName, "t199");
    GG_CHECK(ggui::refPills(many, [](const RefBadge&) { return false; }, snap).empty());
}

GG_TEST("refpills", "layoutPreviewGraph lays out lanes for linear, merge, root and outside parents")
{
    struct Case {
        const char* what;
        const char* onto;
        std::vector<core::RebasePreview::Row> rows; // oldest first, as the preview has them
        int lanes;
        std::vector<const char*> expect;            // newest first, then the base
    };
    const std::vector<Case> cases{
        {"no rows: only the base", "b", {}, 1, {"0/0 p0"}},
        {"linear list", "b", {prow("A", {"b"}), prow("B", {"A"})}, 1,
            {"0/0 p1 0>0@1-2", "0/0 p1 0>0@0-1 0>0@1-2", "0/0 p0 0>0@0-1"}},
        {"conflicted row", "b", {prow("A", {"b"}, true)}, 1, {"0!/0 p1 0>0@1-2", "0/0 p0 0>0@0-1"}},
        {"root: no base commit", "", {prow("A", {})}, 1, {"0/0 p0", "0/0 p0"}},
        {"a parent outside the result gets no edge", "b", {prow("A", {"b", "far"})}, 1,
            {"0/0 p2 0>0@1-2", "0/0 p0 0>0@0-1"}},
        {"merge opens a lane and the base reuses it", "b", {prow("A", {"b"}), prow("B", {"b"}), prow("M", {"A", "B"})}, 2,
            {"0/0 p2 0>0@1-2 0>1@1-2", "1/1 p1 0>0@0-2 1>1@0-1 1>1@1-2", "0/0 p1 0>0@0-1 1>1@0-2 0>1@1-2",
                "1/1 p0 1>1@0-1"}},
        {"two forks from the base join on one lane", "b", {prow("X", {"b"}), prow("Y", {"b"})}, 2,
            {"0/0 p1 0>0@1-2", "1/1 p1 0>0@0-2 1>0@1-2", "0/0 p0 0>0@0-1"}},
    };
    for (const auto& c : cases) {
        core::RebasePreview preview;
        preview.onto = c.onto;
        preview.rows = c.rows;
        std::vector<core::HistoryRow> graph{core::HistoryRow{}}; // stale rows are dropped
        int lanes = 7;
        ggui::detail::layoutPreviewGraph(preview, graph, lanes);
        std::string got, want;
        for (const auto& g : graph)
            got += show(g) + " | ";
        for (const auto* w : c.expect)
            want += std::string(w) + " | ";
        if (got != want || lanes != c.lanes)
            ctx->LogError("graph '%s': got '%s' (%d lanes), want '%s' (%d)", c.what, got.c_str(), lanes, want.c_str(),
                c.lanes);
        GG_CHECK(got == want);
        GG_CHECK_EQ(lanes, c.lanes);
    }
}

} // namespace ggtest
