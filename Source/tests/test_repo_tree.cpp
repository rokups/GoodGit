// Repo browser tree: grouping by alias, order, and the alias after a drop onto a group (GG-12).
#include "core/Readers.hpp"
#include "shell/RepoTree.hpp"
#include "util/Ui.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

using ggui::RepoEntry;
using ggui::RepoNode;

// The tree as text: groups as "label{...}", leaves as "label", separated by commas.
std::string dump(const std::vector<RepoNode>& nodes)
{
    std::string out;
    for (const auto& n : nodes) {
        out += (out.empty() ? "" : ",") + n.label;
        if (n.isGroup())
            out += "{" + dump(n.children) + "}";
    }
    return out;
}

} // namespace

GG_TEST("shell", "repo tree: an alias nests into groups, a group with one member stays a group")
{
    using ggui::buildRepoTree;
    auto tree = buildRepoTree({{"/r/one", "g/s/name"}});
    GG_REQUIRE(tree.size() == 1);
    GG_CHECK_STR_EQ(dump(tree), "g{s{name}}");
    GG_CHECK(tree[0].isGroup());
    GG_CHECK_STR_EQ(tree[0].group, "g");
    GG_CHECK(tree[0].path.empty());
    GG_REQUIRE(tree[0].children.size() == 1);
    GG_CHECK_STR_EQ(tree[0].children[0].group, "g/s");
    GG_REQUIRE(tree[0].children[0].children.size() == 1);
    const RepoNode& leaf = tree[0].children[0].children[0];
    GG_CHECK(!leaf.isGroup());
    GG_CHECK_STR_EQ(leaf.path, "/r/one");
    GG_CHECK_STR_EQ(leaf.group, "");
    GG_CHECK(leaf.children.empty());
    // Two repositories in one group; a one-segment alias is a top-level leaf.
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/a", "work/a"}, {"/r/b", "work/b"}, {"/r/c", "top"}})), "work{a,b},top");
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/a", " work // a/ "}})), "work{a}"); // normalised alias
}

GG_TEST("shell", "repo tree: no alias gives a leaf with the base name, in the group of its folder prefix")
{
    using ggui::buildRepoTree;
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/app", ""}, {"/r/other", ""}})), "app,other");
    auto tree = buildRepoTree({{"/w/work/app", ""}, {"/h/home/app", ""}, {"/x/solo", ""}});
    GG_CHECK_STR_EQ(dump(tree), "home{app},work{app},solo");
    GG_REQUIRE(tree.size() == 3);
    GG_CHECK_STR_EQ(tree[0].group, "home");
    GG_REQUIRE(tree[0].children.size() == 1);
    GG_CHECK_STR_EQ(tree[0].children[0].path, "/h/home/app");
    GG_CHECK_STR_EQ(tree[1].group, "work");
    GG_REQUIRE(tree[1].children.size() == 1);
    GG_CHECK_STR_EQ(tree[1].children[0].path, "/w/work/app");
    GG_CHECK_STR_EQ(tree[2].path, "/x/solo");
    // Paths of aliased entries count for the names of the others.
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/w/work/app", "x/y"}, {"/h/home/app", ""}})), "home{app},x{y}");
}

GG_TEST("shell", "repo tree: a deduplicated name with a folder prefix makes groups")
{
    using ggui::buildRepoTree;
    auto tree = buildRepoTree({{"/a/work/foo", ""}, {"/b/personal/foo", ""}});
    GG_CHECK_STR_EQ(dump(tree), "personal{foo},work{foo}");
    GG_REQUIRE(tree.size() == 2);
    GG_CHECK(tree[0].isGroup());
    GG_CHECK_STR_EQ(tree[0].group, "personal");
    GG_REQUIRE(tree[0].children.size() == 1);
    const RepoNode& leaf = tree[0].children[0];
    GG_CHECK(!leaf.isGroup());
    GG_CHECK_STR_EQ(leaf.label, "foo");
    GG_CHECK_STR_EQ(leaf.path, "/b/personal/foo");
    GG_CHECK_STR_EQ(leaf.group, "");
    GG_REQUIRE(tree[1].children.size() == 1);
    GG_CHECK_STR_EQ(tree[1].children[0].path, "/a/work/foo");
    // A nested prefix makes nested groups with their full group paths.
    tree = buildRepoTree({{"/a/p/q/foo", ""}, {"/b/r/q/foo", ""}});
    GG_CHECK_STR_EQ(dump(tree), "p{q{foo}},r{q{foo}}");
    GG_REQUIRE(tree.size() == 2 && tree[0].children.size() == 1 && tree[0].children[0].children.size() == 1);
    GG_CHECK_STR_EQ(tree[0].children[0].group, "p/q");
    GG_CHECK_STR_EQ(tree[0].children[0].children[0].path, "/a/p/q/foo");
    GG_CHECK_STR_EQ(tree[1].children[0].group, "r/q");
    GG_CHECK_STR_EQ(tree[1].children[0].children[0].path, "/b/r/q/foo");
    // An alias group and a group from a prefix with one name are one group.
    tree = buildRepoTree({{"/r/bar", "work/bar"}, {"/a/work/foo", ""}, {"/b/home/foo", ""}});
    GG_CHECK_STR_EQ(dump(tree), "home{foo},work{bar,foo}");
    GG_REQUIRE(tree.size() == 2 && tree[1].children.size() == 2);
    GG_CHECK_STR_EQ(tree[1].children[0].path, "/r/bar");
    GG_CHECK_STR_EQ(tree[1].children[1].path, "/a/work/foo");
}

GG_TEST("shell", "repo tree: unusual paths without an alias")
{
    using ggui::buildRepoTree;
    auto tree = buildRepoTree({{"/", ""}});
    GG_REQUIRE(tree.size() == 1);
    GG_CHECK(!tree[0].isGroup());
    GG_CHECK_STR_EQ(tree[0].path, "/");
    // Windows paths with drive letters.
    tree = buildRepoTree({{"C:\\a\\work\\foo", ""}, {"D:\\b\\home\\foo", ""}});
    GG_CHECK_STR_EQ(dump(tree), "home{foo},work{foo}");
    GG_REQUIRE(tree.size() == 2);
    GG_CHECK_STR_EQ(tree[0].group, "home");
    GG_REQUIRE(tree[0].children.size() == 1);
    GG_CHECK_STR_EQ(tree[0].children[0].path, "D:\\b\\home\\foo");
    // A space at the start of a directory name stays in the group label.
    tree = buildRepoTree({{"/a/ work/foo", ""}, {"/b/home/foo", ""}});
    GG_CHECK_STR_EQ(dump(tree), " work{foo},home{foo}");
    GG_REQUIRE(tree.size() == 2);
    GG_CHECK_STR_EQ(tree[0].label, " work");
    GG_CHECK_STR_EQ(tree[0].group, " work");
}

GG_TEST("shell", "repo tree: groups come before leaves, case-insensitive, stable for equal labels")
{
    using ggui::buildRepoTree;
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/1", "zeta"}, {"/r/2", "b/x"}, {"/r/3", "Alpha"}, {"/r/4", "a/x"},
                        {"/r/5", "beta"}})),
        "a{x},b{x},Alpha,beta,zeta");
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/1", "B"}, {"/r/2", "a"}, {"/r/3", "c"}, {"/r/4", "A"}})), "a,A,B,c");
    // Equal labels keep the order of the entries.
    auto tree = buildRepoTree({{"/r/2", "app"}, {"/r/1", "App"}, {"/r/3", "app"}});
    GG_REQUIRE(tree.size() == 3);
    GG_CHECK_STR_EQ(tree[0].path, "/r/2");
    GG_CHECK_STR_EQ(tree[1].path, "/r/1");
    GG_CHECK_STR_EQ(tree[2].path, "/r/3");
    // The order applies at each level.
    GG_CHECK_STR_EQ(dump(buildRepoTree({{"/r/1", "g/z"}, {"/r/2", "g/y/q"}, {"/r/3", "g/A"}})), "g{y{q},A,z}");
}

GG_TEST("shell", "repo tree: equal aliases, a group and a leaf with one label, case-sensitive groups")
{
    using ggui::buildRepoTree;
    auto tree = buildRepoTree({{"/r/1", "g/app"}, {"/r/2", "g/app"}});
    GG_CHECK_STR_EQ(dump(tree), "g{app,app}");
    GG_REQUIRE(tree.size() == 1 && tree[0].children.size() == 2);
    GG_CHECK_STR_EQ(tree[0].children[0].path, "/r/1");
    GG_CHECK_STR_EQ(tree[0].children[1].path, "/r/2");
    // A group and a leaf with the same label are two nodes, the group first.
    tree = buildRepoTree({{"/r/1", "work"}, {"/r/2", "work/app"}});
    GG_CHECK_STR_EQ(dump(tree), "work{app},work");
    GG_REQUIRE(tree.size() == 2);
    GG_CHECK(tree[0].isGroup());
    GG_CHECK(!tree[1].isGroup());
    // Group names are case-sensitive.
    tree = buildRepoTree({{"/r/1", "Work/a"}, {"/r/2", "work/b"}});
    GG_CHECK_STR_EQ(dump(tree), "Work{a},work{b}");
    GG_CHECK(buildRepoTree({}).empty());
}

GG_TEST("shell", "repo tree: aliasWithGroup moves the name under a group")
{
    using ggui::aliasWithGroup;
    GG_CHECK_STR_EQ(aliasWithGroup("app", "app", "work"), "work/app");
    GG_CHECK_STR_EQ(aliasWithGroup("old/app", "app", "work"), "work/app");
    GG_CHECK_STR_EQ(aliasWithGroup("app", "app", "work/web"), "work/web/app");
    GG_CHECK_STR_EQ(aliasWithGroup("work/web/app", "app", ""), "app"); // to the top level
    GG_CHECK_STR_EQ(aliasWithGroup(" a//b/ ", "b", "g"), "g/b");
    GG_CHECK_STR_EQ(aliasWithGroup("", "app", "work"), "work/app");
    GG_CHECK_STR_EQ(aliasWithGroup("", "work/app", "g/h"), "g/h/app"); // only the last segment
    GG_CHECK_STR_EQ(aliasWithGroup("", "app", ""), "");
    GG_CHECK_STR_EQ(aliasWithGroup("", "app", " / "), "");            // the group is normalised
    GG_CHECK_STR_EQ(aliasWithGroup("app", "app", " g // h/ "), "g/h/app");
    GG_CHECK_STR_EQ(aliasWithGroup("", "/", "work"), "");             // no name: no alias
    GG_CHECK_STR_EQ(aliasWithGroup("", "app/", "work"), "work/app");
    GG_CHECK_STR_EQ(aliasWithGroup("work/web/app", "app", "work/web"), "work/web/app"); // the own group
}

GG_TEST("shell", "repo tree: splitStoredAlias splits at the last slash")
{
    using ggui::splitStoredAlias;
    GG_CHECK_STR_EQ(splitStoredAlias("app").group, "");
    GG_CHECK_STR_EQ(splitStoredAlias("app").name, "app");
    GG_CHECK_STR_EQ(splitStoredAlias("work/app").group, "work");
    GG_CHECK_STR_EQ(splitStoredAlias("work/app").name, "app");
    GG_CHECK_STR_EQ(splitStoredAlias("a/b/c/app").group, "a/b/c");
    GG_CHECK_STR_EQ(splitStoredAlias("a/b/c/app").name, "app");
    GG_CHECK_STR_EQ(splitStoredAlias("").group, "");
    GG_CHECK_STR_EQ(splitStoredAlias("").name, "");
}

GG_TEST("shell", "repo tree: aliasFromDialog gives the alias to store")
{
    using ggui::aliasFromDialog;
    GG_CHECK_STR_EQ(aliasFromDialog("", "name", "", "", "base"), "name");                // no group
    GG_CHECK_STR_EQ(aliasFromDialog("a/b", "name", "", "", "base"), "a/b/name");         // a nested group
    GG_CHECK_STR_EQ(aliasFromDialog("", "", "old/x", "", "base"), "");                   // both empty removes
    GG_CHECK_STR_EQ(aliasFromDialog(" / ", "  ", "", "p", "base"), "");                  // also after normalising
    GG_CHECK_STR_EQ(aliasFromDialog("grp", "", "", "", "base"), "grp/base");             // an empty alias takes the base name
    GG_CHECK_STR_EQ(aliasFromDialog("grp", " ", "", "", ""), "");                        // no base name: nothing
    GG_CHECK_STR_EQ(aliasFromDialog(" g // h ", " n ", "", "", "base"), "g/h/n");        // spaces around segments
    GG_CHECK_STR_EQ(aliasFromDialog("", "x/y", "", "", "base"), "x/y");                  // "/" in the alias adds groups
    // Unchanged defaults with no stored alias: nothing, the repository follows its deduplicated name.
    GG_CHECK_STR_EQ(aliasFromDialog("p/q", "base", "", "p/q", "base"), "");
    GG_CHECK_STR_EQ(aliasFromDialog("", "base", "", "", "base"), "");
    // An empty group on a repository in a prefix group stores the base name.
    GG_CHECK_STR_EQ(aliasFromDialog("", "base", "", "p", "base"), "base");
    // A prefix group that differs from its normalised form only by spaces at an end is the same group.
    GG_CHECK_STR_EQ(aliasFromDialog(" work", "base", "", " work", "base"), "");
    GG_CHECK_STR_EQ(aliasFromDialog("work", "base", "", " work", "base"), "");
    // Unchanged values with a stored alias keep it, also when they equal the defaults.
    GG_CHECK_STR_EQ(aliasFromDialog("g", "n", "g/n", "p", "base"), "g/n");
    GG_CHECK_STR_EQ(aliasFromDialog("p", "base", "p/base", "p", "base"), "p/base");
    GG_CHECK_STR_EQ(aliasFromDialog("", "base", "base", "", "base"), "base");
}

GG_TEST("summary", "readSummary reports why a path is not a usable repository (GG-70)")
{
    using Health = ggui::core::RepoSummary::Health;
    const fs::path repo = s.fixture(Recipe::Linear);
    const fs::path file = s.root() / "plain-file";
    {
        std::ofstream(file) << "text\n";
    }
    const fs::path plainDir = s.root() / "plain-dir";
    fs::create_directories(plainDir);

    const auto ok = ggui::core::readSummary(repo);
    GG_CHECK(ok.health == Health::Ok);
    GG_CHECK(ok.exists);
    GG_CHECK(!ggui::summaryWarns(ok));

    // Other kinds of repository are Ok too: bare, linked worktree, a .git file and (not on Windows) a symlink.
    const fs::path bare = s.fixture(Recipe::Bare);
    GG_CHECK(ggui::core::readSummary(bare).health == Health::Ok);
    const fs::path linked = s.root() / "linked-worktree";
    s.git(repo, {"worktree", "add", "-q", "--detach", linked.string()});
    GG_CHECK(fs::is_regular_file(linked / ".git"));
    GG_CHECK(ggui::core::readSummary(linked).health == Health::Ok);
    const fs::path gitfile = s.root() / "gitfile-repo";
    s.git(s.root(), {"init", "-q", "--separate-git-dir", (s.root() / "gitfile-repo.git").string(), gitfile.string()});
    GG_CHECK(fs::is_regular_file(gitfile / ".git"));
    GG_CHECK(ggui::core::readSummary(gitfile).health == Health::Ok);
#ifndef _WIN32
    const fs::path link = s.root() / "link-to-repo";
    std::error_code linkError;
    fs::create_directory_symlink(repo, link, linkError);
    GG_REQUIRE(!linkError);
    GG_CHECK(ggui::core::readSummary(link).health == Health::Ok);
#endif

    const auto missing = ggui::core::readSummary(s.root() / "no-such-dir");
    GG_CHECK(missing.health == Health::Missing);
    GG_CHECK(!missing.exists);
    const auto underFile = ggui::core::readSummary(file / "inner");
    GG_CHECK(underFile.health == Health::Missing);

    const auto notDir = ggui::core::readSummary(file);
    GG_CHECK(notDir.health == Health::NotRepo);
    GG_CHECK(!notDir.exists);
    const auto notRepo = ggui::core::readSummary(plainDir);
    GG_CHECK(notRepo.health == Health::NotRepo);
    GG_CHECK(!notRepo.exists);

    GG_CHECK_STR_EQ(ggui::summaryText(missing), "missing");
    GG_CHECK_STR_EQ(ggui::summaryText(notRepo), "not a repository");
    ggui::core::RepoSummary unreadable;
    unreadable.health = Health::Unreadable;
    GG_CHECK_STR_EQ(ggui::summaryText(unreadable), "unreadable");
    GG_CHECK(ggui::summaryWarns(missing));
    GG_CHECK(ggui::summaryWarns(notRepo));
    GG_CHECK(ggui::summaryWarns(unreadable));
    GG_CHECK(!ggui::summaryWarningText(unreadable).empty());
    GG_CHECK(ggui::summaryWarningText(ok).empty());
    GG_CHECK(ggui::summaryWarnPrefix(&ok).empty());
    GG_CHECK(ggui::summaryWarnPrefix(nullptr).empty());
    GG_CHECK(!ggui::summaryWarnPrefix(&missing).empty());
    GG_CHECK(ggui::summaryText(ok).find("main") != std::string::npos);
}

} // namespace ggtest
