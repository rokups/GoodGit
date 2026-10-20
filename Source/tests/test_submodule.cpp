// Committing, folding and moving submodule changes (GG-49).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "core/Readers.hpp"
#include "shell/Actions.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>

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

GG_TEST("submodule", "A submodule HEAD moved outside the app refreshes the status")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() != nullptr; }));
    GG_CHECK(s.session()->status()->unstaged.empty());

    // An empty commit changes only the HEAD of the submodule: no file in the worktree changes.
    s.git(repo / "sub", {"commit", "-q", "--allow-empty", "-m", "Empty"});
    GG_CHECK(unstagedEntry(s, "sub", NewCommits) != nullptr);
    // Moving HEAD back to the commit of the gitlink removes the row.
    s.git(repo / "sub", {"reset", "-q", "--hard", "HEAD~1"});
    GG_CHECK(s.waitUntil([&] { return s.session()->status() && s.session()->status()->unstaged.empty(); }));
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

namespace {

// Runs a mutation whose body sets `sync` (GG-57) and waits for its event. `traceFile` collects the git
// commands of the mutation (GIT_TRACE) when it is not empty.
std::optional<ggui::core::MutationFinishedEvent> runSync(
    Scenario& s, ggui::core::SubmoduleSync sync, const fs::path& traceFile = {})
{
    std::optional<ggui::core::MutationFinishedEvent> event;
    s.session()->actions().run("sync test",
        [sync, traceFile](ggui::core::MutationContext& mc) {
            if (!traceFile.empty())
                mc.env.emplace_back("GIT_TRACE", traceFile.string());
            mc.submodules = sync;
        },
        [&](const ggui::core::MutationFinishedEvent& e) { event = e; });
    if (!s.waitUntil([&] { return event.has_value(); }))
        return std::nullopt;
    s.settle();
    return event;
}

// How many trace lines mention a submodule command.
int submoduleCalls(const fs::path& traceFile)
{
    std::ifstream in(traceFile);
    int n = 0;
    for (std::string line; std::getline(in, line);)
        if (line.find("submodule") != std::string::npos)
            ++n;
    return n;
}

} // namespace

GG_TEST("submodule", "A force update after a mutation moves the submodule to the recorded commit")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo / "sub", {"checkout", "-q", "HEAD~1"});
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    GG_REQUIRE(s.head(repo / "sub") != recorded);
    GG_REQUIRE(s.openRepository(repo));
    const auto event = runSync(s, ggui::core::SubmoduleSync::ForceUpdate);
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Ok);
    GG_CHECK(event->message.empty());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), recorded);
}

GG_TEST("submodule", "The update after a mutation is skipped with the setting off or SubmoduleSync::None")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo / "sub", {"checkout", "-q", "HEAD~1"});
    const std::string moved = s.head(repo / "sub");
    GG_REQUIRE(s.openRepository(repo));
    const auto none = runSync(s, ggui::core::SubmoduleSync::None);
    GG_REQUIRE(none.has_value());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), moved);
    s.app.settings().data().submodulesAutoUpdate = false;
    const auto off = runSync(s, ggui::core::SubmoduleSync::ForceUpdate);
    GG_REQUIRE(off.has_value());
    GG_CHECK(off->outcome == ggui::core::Outcome::Ok);
    GG_CHECK_STR_EQ(s.head(repo / "sub"), moved);
}

GG_TEST("submodule", "The update after a mutation runs git only when .gitmodules exists")
{
    const fs::path linear = s.fixture(Recipe::Linear);
    const fs::path trace = s.root() / "trace.txt";
    GG_REQUIRE(s.openRepository(linear));
    GG_REQUIRE(runSync(s, ggui::core::SubmoduleSync::Update, trace).has_value());
    GG_CHECK(submoduleCalls(trace) == 0);
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(runSync(s, ggui::core::SubmoduleSync::Update, trace).has_value());
    GG_CHECK(submoduleCalls(trace) > 0);
}

GG_TEST("submodule", "A failing update after a mutation leaves the outcome Ok and adds a notice")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    // A gitlink to a commit that no repository has: the update fails.
    s.git(repo, {"update-index", "--cacheinfo", "160000,1111111111111111111111111111111111111111,sub"});
    GG_REQUIRE(s.openRepository(repo));
    const auto event = runSync(s, ggui::core::SubmoduleSync::Update);
    GG_REQUIRE(event.has_value());
    GG_CHECK(event->outcome == ggui::core::Outcome::Ok);
    GG_CHECK(event->message.empty());
    GG_CHECK(event->warning.rfind("Submodules were not updated: ", 0) == 0);
    GG_CHECK(event->warning.find('\n') == std::string::npos);
}

GG_TEST("submodule", "A cancel during the update after a mutation keeps the outcome Ok and adds a warning")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo / "sub", {"checkout", "-q", "HEAD~1"});
    const std::string moved = s.head(repo / "sub");
    GG_REQUIRE(s.openRepository(repo));
    std::optional<ggui::core::MutationFinishedEvent> event;
    s.session()->actions().run("cancel test",
        [](ggui::core::MutationContext& mc) {
            mc.git({"branch", "cancel-test"});
            mc.submodules = ggui::core::SubmoduleSync::ForceUpdate;
            mc.token().cancel(); // the update is the next git command
        },
        [&](const ggui::core::MutationFinishedEvent& e) { event = e; });
    GG_REQUIRE(s.waitUntil([&] { return event.has_value(); }));
    s.settle();
    GG_CHECK(event->outcome == ggui::core::Outcome::Ok);
    GG_CHECK_STR_EQ(event->warning, "Submodules were not updated: Cancelled");
    GG_CHECK(s.gitMayFail(repo, {"show-ref", "--verify", "-q", "refs/heads/cancel-test"}).ok()); // no rollback
    GG_CHECK_STR_EQ(s.head(repo / "sub"), moved);
}

namespace {

using ggui::core::Oid;

// The submodule is at `commit` and the parent has no change.
void checkSubmoduleAt(Scenario& s, const fs::path& repo, const std::string& commit)
{
    GG_CHECK_STR_EQ(s.head(repo / "sub"), commit);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), commit);
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
}

// The branch `name` holds a commit that moves the gitlink of `sub` one commit on. The checkout, the
// submodule and the branch `main` stay as they were. Returns the new submodule commit.
std::string branchWithMovedGitlink(Scenario& s, const fs::path& repo, const std::string& name)
{
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    s.git(repo, {"checkout", "-q", "-b", name});
    const std::string moved = moveSubmodule(s, repo, true);
    s.git(repo, {"commit", "-q", "-m", "Move sub"});
    s.git(repo, {"checkout", "-q", "main"});
    s.git(repo / "sub", {"checkout", "-q", recorded});
    return moved;
}

} // namespace

GG_TEST("submodule", "Reset hard before the submodule existed and back brings the submodule to the HEAD commit")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    const std::string before = s.revParse(repo, "HEAD~2");
    s.git(repo / "sub", {"checkout", "-q", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().resetBranch("main", Oid::fromHex(before), ggui::Actions::ResetMode::Hard, true);
    GG_REQUIRE(s.settle());
    GG_CHECK(!fs::exists(repo / ".gitmodules"));
    s.session()->actions().resetBranch("main", Oid::fromHex(s.revParse(repo, "ORIG_HEAD")), ggui::Actions::ResetMode::Hard, true);
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / ".gitmodules"));
    checkSubmoduleAt(s, repo, recorded);
}

GG_TEST("submodule", "Reset hard to a commit with another gitlink moves the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string older = s.gitOut(repo, {"rev-parse", "HEAD~1:sub"});
    const std::string target = s.revParse(repo, "HEAD~1");
    s.write(repo / "sub", "s3.txt", "s3 modified\n"); // a ForceUpdate discards it
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().resetBranch("main", Oid::fromHex(target), ggui::Actions::ResetMode::Hard, true);
    GG_REQUIRE(s.settle());
    checkSubmoduleAt(s, repo, older);
}

GG_TEST("submodule", "Reset mixed leaves the submodule where it is and the row shows new commits")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string current = s.head(repo / "sub");
    const std::string target = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().resetBranch("main", Oid::fromHex(target), ggui::Actions::ResetMode::Mixed);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), current);
    GG_CHECK(unstagedEntry(s, "sub", NewCommits) != nullptr);
}

GG_TEST("submodule", "Switching to a branch with another gitlink moves the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string moved = branchWithMovedGitlink(s, repo, "feature");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().checkout("feature", false);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "feature");
    checkSubmoduleAt(s, repo, moved);
}

GG_TEST("submodule", "A dirty submodule does not fail the checkout")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string current = s.head(repo / "sub");
    // s3.txt is not in the commit that "back" records, so the update cannot go there with the change.
    s.git(repo, {"branch", "back", "main~1"});
    s.write(repo / "sub", "s3.txt", "s3 modified\n");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().checkout("back", false);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "back"); // the checkout is Ok
    GG_CHECK_STR_EQ(s.head(repo / "sub"), current);
    GG_CHECK_STR_EQ(s.read(repo / "sub", "s3.txt"), "s3 modified\n");
}

GG_TEST("submodule", "Checkout with the update setting off leaves the submodule where it is")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    branchWithMovedGitlink(s, repo, "feature");
    const std::string current = s.head(repo / "sub");
    s.app.settings().data().submodulesAutoUpdate = false;
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().checkout("feature", false);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "feature");
    GG_CHECK_STR_EQ(s.head(repo / "sub"), current);
}

GG_TEST("submodule", "Merge of a branch that moves the gitlink moves the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string moved = branchWithMovedGitlink(s, repo, "feature");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().mergeNative("feature");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo), s.revParse(repo, "feature"));
    checkSubmoduleAt(s, repo, moved);
}

GG_TEST("submodule", "Stash pop that moves the gitlink moves the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string recorded = s.head(repo / "sub");
    const std::string moved = moveSubmodule(s, repo, true);
    // git stash push ignores a changed gitlink: make the stash commit (worktree, parents HEAD and index) by hand.
    const std::string tree = s.gitOut(repo, {"write-tree"});
    const std::string index = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD", "-m", "index"});
    const std::string stash = s.gitOut(repo, {"commit-tree", tree, "-p", "HEAD", "-p", index, "-m", "stash"});
    s.git(repo, {"stash", "store", "-m", "gitlink", stash});
    s.git(repo, {"reset", "-q", "--hard"});
    s.git(repo / "sub", {"checkout", "-q", recorded});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().stashApply(0, true, true);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), moved);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), "M  sub");
}

GG_TEST("submodule", "Pull from a bare origin whose commit moves the gitlink fetches the commit and moves the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path subOrigin = repo.parent_path() / (repo.filename().string() + "-sub");
    const fs::path bare = s.path("origin.git");
    const fs::path other = s.path("other");
    s.git(s.root(), {"clone", "-q", "--bare", repo.string(), bare.string()});
    s.track(bare);
    s.git(repo, {"remote", "add", "origin", "file://" + bare.generic_string()});
    s.git(repo, {"fetch", "-q", "origin"});
    s.git(repo, {"branch", "-q", "--set-upstream-to=origin/main", "main"});
    s.git(s.root(), {"clone", "-q", "file://" + bare.generic_string(), other.string()});
    // A submodule commit that only the submodule's origin has.
    s.commitFile(subOrigin, "s4.txt", "s4\n", "Sub s4");
    const std::string moved = s.head(subOrigin);
    GG_REQUIRE(!s.gitMayFail(repo / "sub", {"cat-file", "-e", moved + "^{commit}"}).ok());
    s.git(other, {"update-index", "--cacheinfo", "160000," + moved + ",sub"});
    s.git(other, {"commit", "-q", "-m", "Move sub"});
    s.git(other, {"push", "-q", "origin", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().pull(ggui::PullMode::Merge);
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo), s.head(other));
    checkSubmoduleAt(s, repo, moved);
}

// ---- Repositories panel actions (GG-60) ----

GG_TEST("submodule", "Initialize populates a submodule that is not populated")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    s.git(repo, {"submodule", "deinit", "-f", "sub"});
    GG_REQUIRE(!fs::exists(repo / "sub" / ".git"));
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleInit("sub");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "sub" / ".git"));
    GG_CHECK_STR_EQ(s.head(repo / "sub"), recorded);
}

GG_TEST("submodule", "Update returns a submodule to the commit of the superproject")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    s.git(repo / "sub", {"checkout", "-q", "HEAD~1"});
    GG_REQUIRE(s.head(repo / "sub") != recorded);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleUpdate("sub");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), recorded);
}

GG_TEST("submodule", "Update to remote branch moves a submodule to the new commit of its origin")
{
    using namespace ggui::core;
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path subOrigin = repo.parent_path() / (repo.filename().string() + "-sub");
    const std::string recorded = s.gitOut(repo, {"rev-parse", "HEAD:sub"});
    s.commitFile(subOrigin, "s4.txt", "s4\n", "Sub s4");
    const std::string moved = s.head(subOrigin);
    GG_REQUIRE(moved != recorded);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleUpdateRemote("sub");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), moved);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"rev-parse", "HEAD:sub"}), recorded);
    GG_CHECK(unstagedEntry(s, "sub", NewCommits) != nullptr);
}

GG_TEST("submodule", "Sync URL follows the URL of .gitmodules")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path subOrigin = repo.parent_path() / (repo.filename().string() + "-sub");
    const fs::path moved = s.path("sub-moved");
    s.git(s.root(), {"clone", "-q", subOrigin.string(), moved.string()});
    s.track(moved);
    s.git(repo, {"config", "-f", ".gitmodules", "submodule.sub.url", moved.string()});
    GG_REQUIRE(s.gitOut(repo / "sub", {"remote", "get-url", "origin"}) != moved.string());
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleSyncUrl("sub");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.gitOut(repo / "sub", {"remote", "get-url", "origin"}), moved.string());
    GG_CHECK_STR_EQ(s.head(repo / "sub"), s.gitOut(repo, {"rev-parse", "HEAD:sub"}));
}

// ---- Remove and Add (GG-60) ----

namespace {

// The lines of `git status --porcelain` of the repository, one string.
std::string porcelain(Scenario& s, const fs::path& repo) { return s.gitOut(repo, {"status", "--porcelain"}); }

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

GG_TEST("submodule", "Remove keeps the data in .git/modules and stages the deletion")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(fs::exists(repo / ".git" / "modules" / "sub"));
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove("sub", false);
    GG_REQUIRE(s.settle());
    GG_CHECK(!fs::exists(repo / "sub"));
    GG_CHECK(fs::exists(repo / ".git" / "modules" / "sub"));
    const std::string status = porcelain(s, repo);
    GG_CHECK(contains(status, "M  .gitmodules"));
    GG_CHECK(contains(status, "D  sub"));
}

GG_TEST("submodule", "Remove with the option deletes the data in .git/modules")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove("sub", true);
    GG_REQUIRE(s.settle());
    GG_CHECK(!fs::exists(repo / "sub"));
    GG_CHECK(!fs::exists(repo / ".git" / "modules" / "sub"));
    const std::string status = porcelain(s, repo);
    GG_CHECK(contains(status, "M  .gitmodules"));
    GG_CHECK(contains(status, "D  sub"));
}

GG_TEST("submodule", "Remove dialog with the option deletes the data in .git/modules")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showRemoveSubmoduleDialog("sub");
    GG_REQUIRE(s.dialogOpen("Remove submodule"));
    s.dialogCheck("Remove submodule", "data", "Also delete the repository data in .git\\/modules\\/<name>"); // "/" separates a reference
    s.dialogButton("Remove submodule", "Remove");
    GG_REQUIRE(s.settle());
    GG_CHECK(!fs::exists(repo / "sub"));
    GG_CHECK(!fs::exists(repo / ".git" / "modules" / "sub"));
    GG_CHECK(contains(porcelain(s, repo), "D  sub"));
}

GG_TEST("submodule", "Remove with the option is refused when the submodule has an unpushed commit")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo / "sub", {"checkout", "-q", "-B", "work"});
    s.commitFile(repo / "sub", "local.txt", "local\n", "Local only");
    const std::string before = porcelain(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove("sub", true);
    GG_REQUIRE(s.dialogOpen("remove submodule sub"));
    s.dialogButton("remove submodule sub", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "sub" / "local.txt"));
    GG_CHECK(fs::exists(repo / ".git" / "modules" / "sub"));
    GG_CHECK_STR_EQ(porcelain(s, repo), before);
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
}

GG_TEST("submodule", "Remove with the option is refused when only a detached HEAD has the commit")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    s.git(repo / "sub", {"checkout", "-q", "--detach"});
    s.commitFile(repo / "sub", "detached.txt", "detached\n", "Detached only");
    const std::string before = porcelain(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove("sub", true);
    GG_REQUIRE(s.dialogOpen("remove submodule sub"));
    s.dialogButton("remove submodule sub", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "sub" / "detached.txt"));
    GG_CHECK(fs::exists(repo / ".git" / "modules" / "sub"));
    GG_CHECK_STR_EQ(porcelain(s, repo), before);
}

GG_TEST("submodule", "Remove is refused when the name in .gitmodules leaves .git/modules")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string url = s.gitOut(repo, {"config", "-f", ".gitmodules", "submodule.sub.url"});
    s.write(repo, ".gitmodules", "[submodule \"../outside\"]\n\tpath = sub\n\turl = " + url + "\n");
    fs::create_directories(repo / ".git" / "outside");
    s.write(repo / ".git" / "outside", "keep.txt", "keep\n");
    const std::string before = porcelain(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove("sub", true);
    GG_REQUIRE(s.dialogOpen("remove submodule sub"));
    s.dialogButton("remove submodule sub", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / ".git" / "outside" / "keep.txt"));
    GG_CHECK(fs::exists(repo / "sub" / ".git"));
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    GG_CHECK_STR_EQ(porcelain(s, repo), before);
}

GG_TEST("submodule", "Remove is refused for the path .")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const std::string before = porcelain(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleRemove(".", true);
    GG_REQUIRE(s.dialogOpen("remove submodule ."));
    s.dialogButton("remove submodule .", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / ".git" / "modules" / "sub"));
    GG_CHECK(fs::exists(repo / "sub" / ".git"));
    GG_CHECK(fs::exists(repo / "readme.txt"));
    GG_CHECK_STR_EQ(porcelain(s, repo), before);
}

GG_TEST("submodule", "Add stages .gitmodules and the gitlink and populates the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path other = s.fixture(Recipe::Linear, "other");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleAdd(other.string(), "libs/other", "");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "libs" / "other" / ".git"));
    GG_CHECK_STR_EQ(s.head(repo / "libs" / "other"), s.head(other));
    const std::string status = porcelain(s, repo);
    GG_CHECK(contains(status, "M  .gitmodules"));
    GG_CHECK(contains(status, "A  libs/other"));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-files", "-s", "libs/other"}).substr(0, 6), "160000");
}

GG_TEST("submodule", "Add with a branch follows that branch")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path other = s.fixture(Recipe::Linear, "other");
    s.git(other, {"checkout", "-q", "-b", "feature"});
    s.commitFile(other, "feature.txt", "feature\n", "Feature");
    s.git(other, {"checkout", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.session()->actions().submoduleAdd(other.string(), "other", "feature");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "other" / "feature.txt"));
    GG_CHECK_STR_EQ(s.gitOut(repo, {"config", "-f", ".gitmodules", "submodule.other.branch"}), "feature");
    GG_CHECK(contains(porcelain(s, repo), "A  other"));
}

GG_TEST("submodule", "Add dialog adds the submodule")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path other = s.fixture(Recipe::Linear, "other");
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showAddSubmoduleDialog();
    GG_REQUIRE(s.dialogOpen("Add submodule"));
    s.dialogText("Add submodule", "url", other.string());
    s.dialogText("Add submodule", "path", "other");
    s.dialogButton("Add submodule", "Add");
    GG_REQUIRE(s.settle());
    GG_CHECK(fs::exists(repo / "other" / ".git"));
    GG_CHECK(contains(porcelain(s, repo), "A  other"));
}

GG_TEST("submodule", "Add is refused for a path that is absolute or has ..")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    const fs::path other = s.fixture(Recipe::Linear, "other");
    GG_REQUIRE(s.openRepository(repo));
    const std::string before = porcelain(s, repo);
    GG_CHECK(!ggui::Actions::submodulePathProblem("").empty());
    GG_CHECK(!ggui::Actions::submodulePathProblem("/abs/path").empty());
    GG_CHECK(!ggui::Actions::submodulePathProblem("a/../b").empty());
    GG_CHECK(!ggui::Actions::submodulePathProblem("..\\b").empty());
    GG_CHECK(ggui::Actions::submodulePathProblem("a/b").empty());
    // The title of the error dialog has no "/" here: it separates a reference.
    s.session()->actions().submoduleAdd(other.string(), "..", "");
    GG_REQUIRE(s.dialogOpen("add submodule .."));
    s.dialogButton("add submodule ..", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(porcelain(s, repo), before);
}

} // namespace ggtest
