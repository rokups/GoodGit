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

// The unstaged status entry of `path`, once the status holds one (nullptr after the wait fails).
const ggui::core::StatusEntry* unstagedEntry(Scenario& s, const std::string& path, unsigned state)
{
    const ggui::core::StatusEntry* found = nullptr;
    s.waitUntil([&] {
        const auto status = s.session()->status();
        if (!status)
            return false;
        found = nullptr;
        for (const auto& e : status->unstaged)
            if (e.path == path && e.submoduleState == state)
                found = &e;
        return found != nullptr;
    });
    return found;
}

// Selects the `sub` row of `group` in the working tree and discards it with the D key.
void discardSubmodule(Scenario& s, const fs::path& repo, const char* group, const std::string& path = "sub")
{
    auto* ctx = s.ctx;
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//History/**/###row_wt");
    const std::string ref = fileRef(s, group, path);
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(ref.c_str()); }));
    ctx->ItemClick(ref.c_str());
    ctx->KeyPress(ImGuiKey_D);
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Discard");
    GG_CHECK(s.waitUntil([&] { return !s.itemExists(ref.c_str()); }));
    s.settle();
}

// The submodule is at the commit of HEAD and has no change.
void checkSubmoduleReset(Scenario& s, const fs::path& repo, const std::string& path = "sub")
{
    GG_CHECK_STR_EQ(s.head(repo / path), s.gitOut(repo, {"rev-parse", "HEAD:" + path}));
    GG_CHECK(s.gitOut(repo / path, {"status", "--porcelain"}).empty());
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
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

GG_TEST("submodule", "Unstaged submodule rows tell what changed inside the submodule")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() != nullptr; }));
    GG_CHECK(s.session()->status()->unstaged.empty());

    moveSubmodule(s, repo, false);
    const auto* entry = unstagedEntry(s, "sub", NewCommits);
    GG_REQUIRE(entry != nullptr);
    GG_CHECK(entry->submodule);
    GG_CHECK(entry->kind == ChangeKind::Modified);

    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    GG_CHECK(unstagedEntry(s, "sub", NewCommits | ModifiedContent) != nullptr);

    s.write(repo / "sub", "loose.txt", "loose\n");
    GG_CHECK(unstagedEntry(s, "sub", NewCommits | ModifiedContent | UntrackedContent) != nullptr);

    s.git(repo / "sub", {"checkout", "-q", "--", "s3.txt"});
    s.git(repo, {"add", "sub"});
    GG_CHECK(s.waitUntil([&] {
        const auto status = s.session()->status();
        return status && status->staged.size() == 1 && status->staged[0].submodule;
    }));
    const auto* untracked = unstagedEntry(s, "sub", UntrackedContent);
    GG_REQUIRE(untracked != nullptr);
    GG_CHECK(s.session()->status()->staged[0].submoduleState == 0);
}

GG_TEST("submodule", "A modified file inside a submodule shows as modified content")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(unstagedEntry(s, "sub", ModifiedContent) != nullptr);
}

GG_TEST("submodule", "An untracked file inside a submodule shows as untracked content")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.write(repo / "sub", "loose.txt", "loose\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(unstagedEntry(s, "sub", UntrackedContent) != nullptr);
}

GG_TEST("submodule", "A staged submodule row is flagged without a state")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    moveSubmodule(s, repo, true);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->staged.empty(); }));
    const auto& staged = s.session()->status()->staged;
    GG_REQUIRE(staged.size() == 1);
    GG_CHECK_STR_EQ(staged[0].path, "sub");
    GG_CHECK(staged[0].submodule);
    GG_CHECK(staged[0].submoduleState == 0);
}

GG_TEST("submodule", "A change inside a nested submodule shows on the outer submodule row")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::NestedSubmodules);
    const fs::path inner = repo / "sub" / "inner";
    const std::string tracked = s.gitOut(inner, {"ls-files"}).substr(0, s.gitOut(inner, {"ls-files"}).find('\n'));
    s.write(inner, tracked, "changed\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(unstagedEntry(s, "sub", ModifiedContent) != nullptr);
}

GG_TEST("submodule", "Discard resets a submodule with new commits")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    moveSubmodule(s, repo, false);
    discardSubmodule(s, repo, "Unstaged");
    checkSubmoduleReset(s, repo);
}

GG_TEST("submodule", "Discard resets a submodule with modified content")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    discardSubmodule(s, repo, "Unstaged");
    checkSubmoduleReset(s, repo);
    GG_CHECK_STR_EQ(s.read(repo / "sub", "s3.txt"), "s3\n");
}

GG_TEST("submodule", "Discard deletes the untracked files inside a submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.write(repo / "sub", "loose.txt", "loose\n");
    discardSubmodule(s, repo, "Unstaged");
    checkSubmoduleReset(s, repo);
    GG_CHECK(!fs::exists(repo / "sub" / "loose.txt"));
}

GG_TEST("submodule", "Discard resets a submodule with new commits, modified and untracked content")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    moveSubmodule(s, repo, false);
    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    s.write(repo / "sub", "loose.txt", "loose\n");
    discardSubmodule(s, repo, "Unstaged");
    checkSubmoduleReset(s, repo);
}

GG_TEST("submodule", "Discard of a staged submodule change resets the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    moveSubmodule(s, repo, true);
    discardSubmodule(s, repo, "Staged");
    checkSubmoduleReset(s, repo);
}

GG_TEST("submodule", "Discard of a staged submodule deletion brings the submodule back")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo, {"rm", "-f", "-q", "sub"});
    s.git(repo, {"restore", "--staged", "--worktree", ".gitmodules"});
    discardSubmodule(s, repo, "Staged");
    GG_CHECK(fs::exists(repo / "sub" / "s3.txt"));
    checkSubmoduleReset(s, repo);
}

GG_TEST("submodule", "Discard of a staged new submodule removes it")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path origin = repo.parent_path() / (repo.filename().string() + "-sub");
    s.git(repo, {"-c", "protocol.file.allow=always", "submodule", "add", "-q", origin.string(), "sub2"});
    discardSubmodule(s, repo, "Staged", "sub2");
    GG_CHECK(!fs::exists(repo / "sub2"));
    GG_CHECK(s.gitOut(repo, {"ls-files", "sub2"}).empty());
}

GG_TEST("submodule", "Discard returns a nested submodule to its commit")
{
    const fs::path repo = s.fixture(Recipe::NestedSubmodules);
    const fs::path inner = repo / "sub" / "inner";
    s.commitFile(inner, "i3.txt", "i3\n", "Inner 3");
    s.write(inner, "i3.txt", "i3 modified\n");
    discardSubmodule(s, repo, "Unstaged");
    checkSubmoduleReset(s, repo);
    GG_CHECK_STR_EQ(s.head(inner), s.gitOut(repo / "sub", {"rev-parse", "HEAD:inner"}));
    GG_CHECK(s.gitOut(inner, {"status", "--porcelain"}).empty());
}

} // namespace ggtest
