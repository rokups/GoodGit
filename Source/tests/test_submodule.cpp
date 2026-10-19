// Committing, folding and moving submodule changes (GG-49).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "core/Readers.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + path + "/###file_" + path;
}

// The submodule gets a new commit, so the parent sees a changed gitlink. Returns the new submodule HEAD.
std::string moveSubmodule(Scenario& s, const fs::path& repo, bool stage, const std::string& name = "s4.txt")
{
    s.commitFile(repo / "sub", name, name + "\n", "Sub " + name);
    const std::string id = s.head(repo / "sub");
    if (stage)
        s.git(repo, {"add", "sub"});
    return id;
}

bool headChanged(Scenario& s, const fs::path& repo, const std::string& before)
{
    const bool ok = s.waitUntil([&] { return s.head(repo) != before; });
    s.settle();
    return ok;
}

void commitWorkingTree(Scenario& s, const fs::path& repo, const std::string& message)
{
    auto* ctx = s.ctx;
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/##commit_message"); }));
    s.setText("//Change information/##commit_message", message);
    ctx->Yield(2);
    ctx->ItemClick("//Change information/###info_commit");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"log", "-1", "--format=%s"}) == message; }));
    s.settle();
}

} // namespace

GG_TEST("submodule", "Working tree commit takes an unstaged submodule change")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string sub = moveSubmodule(s, repo, false);
    commitWorkingTree(s, repo, "Take unstaged submodule");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), sub);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-tree", "HEAD", "sub"}).substr(0, 6), "160000");
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
}

GG_TEST("submodule", "Working tree commit takes an unstaged submodule change with new files inside the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string sub = moveSubmodule(s, repo, false);
    s.write(repo / "sub", "loose.txt", "loose\n");
    commitWorkingTree(s, repo, "Take dirty submodule");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), sub);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-tree", "HEAD", "sub"}).substr(0, 6), "160000");
}

GG_TEST("submodule", "Working tree commit with a modified file inside the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string old = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    s.write(repo, "readme.txt", "super changed\n");
    commitWorkingTree(s, repo, "Dirty submodule");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), old);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "HEAD:readme.txt"}), "super changed");
}

GG_TEST("submodule", "Working tree commit with a moved and modified submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string sub = moveSubmodule(s, repo, false);
    s.write(repo / "sub", "s4.txt", "s4 modified\n");
    commitWorkingTree(s, repo, "Moved and dirty submodule");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), sub);
}

GG_TEST("submodule", "Working tree commit takes an unstaged submodule change next to a staged file")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string sub = moveSubmodule(s, repo, false);
    s.write(repo, "readme.txt", "super changed\n");
    s.git(repo, {"add", "readme.txt"});
    commitWorkingTree(s, repo, "Submodule only");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), sub);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "HEAD:readme.txt"}), "super");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), "M  readme.txt");
}

GG_TEST("submodule", "Dropping an unstaged or staged submodule row on a commit folds it in")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    for (bool stage : {false, true}) {
        const std::string sub = moveSubmodule(s, repo, stage, stage ? "s5.txt" : "s4.txt");
        const std::string before = s.head(repo);
        ctx->ItemClick("//History/**/###row_wt");
        const std::string ref = fileRef(s, stage ? "Staged" : "Unstaged", "sub");
        GG_REQUIRE(s.waitUntil([&] { return s.itemExists(ref.c_str()); }));
        ctx->ItemDragAndDrop(ref.c_str(), rowRef(before).c_str());
        ctx->Yield(2);
        GG_CHECK(headChanged(s, repo, before));
        GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), sub);
        GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-tree", "HEAD", "sub"}).substr(0, 6), "160000");
        GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
    }
}

GG_TEST("submodule", "Moving a committed submodule row to the parent commit")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string older = s.gitOut(repo, {"rev-parse", "HEAD~1:sub"});
    const std::string sub = moveSubmodule(s, repo, true);
    s.write(repo, "x.txt", "x\n");
    s.git(repo, {"add", "x.txt"});
    s.git(repo, {"commit", "-q", "-m", "Sub and x"});
    const std::string tip = s.head(repo);
    const std::string tree = s.revParse(repo, "HEAD^{tree}");
    GG_REQUIRE(s.openRepository(repo));
    s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(tip)) != nullptr; });
    ctx->ItemClick(rowRef(tip).c_str());
    const std::string ref = s.child("//Changes", "##files") + "/sub/###file_sub";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(ref.c_str()); }));
    s.contextMenu(ref.c_str(), "Move to parent");
    GG_CHECK(headChanged(s, repo, tip));
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD^{tree}"), tree);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD~1:sub"}), sub);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD~2:sub"}), older);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "--name-only", "--format=", "HEAD"}), "x.txt");
}

GG_TEST("submodule", "Submodule list of the snapshot and the summary")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->snapshot()->submodules.empty(); }));
    const auto& list = s.session()->snapshot()->submodules;
    GG_REQUIRE(list.size() == 1);
    GG_CHECK_STR_EQ(list[0].name, "sub");
    GG_CHECK(list[0].initialized);
    GG_CHECK_STR_EQ(list[0].branch, "main");
    GG_CHECK_STR_EQ(list[0].head.hex(), s.head(repo / "sub"));
    GG_CHECK_STR_EQ(list[0].path.string(), (repo / "sub").lexically_normal().string());
    auto summary = ggui::core::readSummary(repo);
    GG_REQUIRE(summary.submodules.size() == 1);
    GG_CHECK_STR_EQ(summary.submodules[0].name, "sub");
    GG_CHECK(summary.submodules[0].initialized);
    GG_CHECK_STR_EQ(summary.submodules[0].branch, "main");
    s.git(repo, {"submodule", "deinit", "-f", "sub"});
    summary = ggui::core::readSummary(repo);
    GG_REQUIRE(summary.submodules.size() == 1);
    GG_CHECK(!summary.submodules[0].initialized);
}

GG_TEST("submodule", "Nested submodules nest in the list")
{
    const fs::path repo = s.fixture(Recipe::NestedSubmodules);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->snapshot()->submodules.empty(); }));
    const auto& list = s.session()->snapshot()->submodules;
    GG_REQUIRE(list.size() == 1);
    GG_CHECK_STR_EQ(list[0].name, "sub");
    GG_CHECK(list[0].initialized);
    GG_REQUIRE(list[0].children.size() == 1);
    GG_CHECK_STR_EQ(list[0].children[0].name, "inner");
    GG_CHECK(list[0].children[0].initialized);
    GG_CHECK(list[0].children[0].children.empty());
}

} // namespace ggtest
