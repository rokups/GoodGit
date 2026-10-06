// Reset <branch> to here: the commit menu item, its dialog and the Hard confirmation.
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <fstream>

namespace ggtest {

namespace {

std::string rowRef(const std::string& hex) { return "//History/**/###row_" + hex; }

// The Linear repository open in the app, with the history rows loaded.
bool openLinear(Scenario& s, fs::path& repo)
{
    repo = s.fixture(Recipe::Linear);
    if (!s.openRepository(repo))
        return false;
    return s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(s.head(repo))) != nullptr; });
}

// Opens the dialog of the reset item on `commit` and picks the mode (the default is Mixed).
void openReset(Scenario& s, const std::string& commit, const char* mode = nullptr)
{
    s.waitUntil([&] { return s.itemExists(rowRef(commit).c_str()); });
    s.contextMenu(rowRef(commit).c_str(), "###reset_here");
    if (mode && s.dialogOpen("Reset branch"))
        s.comboSelect("//Reset branch/Mode##mode", mode);
}

const char* kSoft = "Soft: keep the index and the working tree";
const char* kHard = "Hard: reset the index and the working tree";

// Waits until the status of the working tree is loaded and `pred` holds for it.
template <class Pred>
bool statusIs(Scenario& s, Pred pred)
{
    return s.waitUntil([&] {
        const auto st = s.session()->status();
        return st && !st->partial && pred(*st);
    });
}

} // namespace

GG_TEST("reset", "soft: the branch moves, the index and the files stay")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string target = s.revParse(repo, "HEAD~2");
    openReset(s, target, kSoft);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"branch", "--show-current"}), "main");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "f4.txt\nf5.txt");
    GG_CHECK(fs::exists(repo / "f4.txt") && fs::exists(repo / "f5.txt"));
    GG_CHECK(s.gitOut(repo, {"diff", "--name-only"}).empty());
}

GG_TEST("reset", "mixed is the default: the index follows, the files stay as changes; Undo")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    const std::string target = s.revParse(repo, "HEAD~2");
    openReset(s, target);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"ls-files", "--others", "--exclude-standard"}), "f4.txt\nf5.txt");
    GG_CHECK(fs::exists(repo / "f5.txt"));
    // One Undo puts the branch back.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == before; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
}

GG_TEST("reset", "hard with a clean tree runs without a second dialog")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    const std::string target = s.revParse(repo, "HEAD~2");
    GG_REQUIRE(statusIs(s, [](const ggui::core::StatusResult& st) { return st.empty(); }));
    openReset(s, target, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_CHECK(!s.dialogOpen("Discard changes", 0.5f));
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
    GG_CHECK(!fs::exists(repo / "f5.txt"));
    // Undo of a hard reset with a clean tree brings the files back.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == before; }));
    s.settle();
    GG_CHECK(fs::exists(repo / "f5.txt"));
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
}

GG_TEST("reset", "hard keeps an untracked file and does not ask")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string target = s.revParse(repo, "HEAD~1");
    s.write(repo, "new.txt", "new\n");
    GG_REQUIRE(statusIs(s, [](const ggui::core::StatusResult& st) { return !st.untracked.empty(); }));
    openReset(s, target, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_CHECK(!s.dialogOpen("Discard changes", 0.5f));
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK(fs::exists(repo / "new.txt"));
}

GG_TEST("reset", "hard with a changed tracked file asks first")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    const std::string target = s.revParse(repo, "HEAD~2");
    s.write(repo, "f1.txt", "changed\n");
    GG_REQUIRE(statusIs(s, [](const ggui::core::StatusResult& st) { return !st.unstaged.empty(); }));
    openReset(s, target, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    // Cancel changes nothing.
    s.dialogButton("Discard changes", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), before);
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "changed\n");
    // Reset hard moves the branch and discards the change.
    openReset(s, target, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Reset hard");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == target; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "line 1\n");
    GG_CHECK(s.gitOut(repo, {"status", "--porcelain"}).empty());
}

GG_TEST("reset", "hard with a staged change asks first")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    s.write(repo, "f1.txt", "staged\n");
    s.git(repo, {"add", "f1.txt"});
    GG_REQUIRE(statusIs(s, [](const ggui::core::StatusResult& st) { return !st.staged.empty(); }));
    openReset(s, s.revParse(repo, "HEAD~2"), kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), before);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "f1.txt");
}

GG_TEST("reset", "hard asks when an untracked file is in the commit too")
{
    fs::path repo;
    repo = s.fixture(Recipe::Linear);
    // The files of the last two commits are untracked now; the old tip stays on "keep".
    const std::string tip = s.head(repo);
    s.git(repo, {"branch", "keep"});
    s.git(repo, {"reset", "-q", "--mixed", "HEAD~2"});
    s.write(repo, "f5.txt", "mine\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(tip)) != nullptr; }));
    openReset(s, tip, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f5.txt"), "mine\n");
    GG_CHECK(s.head(repo) != tip);
    openReset(s, tip, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Reset hard");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == tip; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f5.txt"), "line 5\n");
}

GG_TEST("reset", "hard asks for an untracked file with a non-ASCII name")
{
    fs::path repo = s.fixture(Recipe::Linear);
    // "uber.txt" with u-umlaut: the name that git quotes unless it prints paths NUL-separated.
    const std::u8string name = u8"\u00fcber.txt";
    {
        std::ofstream out(repo / fs::path(name), std::ios::binary);
        out << "kept\n";
    }
    s.git(repo, {"add", "-A"});
    s.git(repo, {"commit", "-q", "-m", "umlaut"});
    const std::string tip = s.head(repo);
    s.git(repo, {"branch", "keep"});
    s.git(repo, {"reset", "-q", "--mixed", "HEAD~1"});
    {
        std::ofstream out(repo / fs::path(name), std::ios::binary);
        out << "mine\n";
    }
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->history().row(ggui::core::Oid::fromHex(tip)) != nullptr; }));
    openReset(s, tip, kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Cancel");
    s.settle();
    GG_CHECK(s.head(repo) != tip);
    std::ifstream in(repo / fs::path(name), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    GG_CHECK_STR_EQ(text, "mine\n");
}

GG_TEST("reset", "hard asks about a change the status has not shown yet")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    GG_REQUIRE(statusIs(s, [](const ggui::core::StatusResult& st) { return st.empty(); }));
    openReset(s, s.revParse(repo, "HEAD~2"), kHard);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    // The save and the click follow at once: the cached status may still be clean, the action looks at the tree.
    s.write(repo, "f1.txt", "late\n");
    s.dialogButton("Reset branch", "Reset");
    GG_REQUIRE(s.dialogOpen("Discard changes"));
    s.dialogButton("Discard changes", "Cancel");
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), before);
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "late\n");
}

GG_TEST("reset", "HEAD moved while the dialog was open: nothing moves")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string before = s.head(repo);
    s.git(repo, {"branch", "other", "HEAD~1"});
    openReset(s, s.revParse(repo, "HEAD~2"), kSoft);
    GG_REQUIRE(s.dialogOpen("Reset branch"));
    s.git(repo, {"switch", "-q", "other"});
    s.dialogButton("Reset branch", "Reset");
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "main"), before);
    GG_CHECK_STR_EQ(s.revParse(repo, "other"), s.revParse(repo, "main~1"));
}

GG_TEST("reset", "the item is disabled while a merge is in progress")
{
    const fs::path repo = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(repo));
    const std::string head = s.head(repo);
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->state == ggui::core::RepoState::Merging; }));
    s.waitUntil([&] { return s.itemExists(rowRef(head).c_str()); });
    ctx->ItemClick(rowRef(head).c_str(), ImGuiMouseButton_Right);
    const ImGuiTestItemInfo info = ctx->ItemInfo("//$FOCUSED/###reset_here");
    GG_CHECK(info.ItemFlags & ImGuiItemFlags_Disabled);
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("reset", "a detached HEAD disables the item")
{
    fs::path repo;
    GG_REQUIRE(openLinear(s, repo));
    const std::string target = s.revParse(repo, "HEAD~1");
    s.git(repo, {"switch", "-q", "--detach", "HEAD"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->snapshot()->headDetached; }));
    s.waitUntil([&] { return s.itemExists(rowRef(target).c_str()); });
    ctx->ItemClick(rowRef(target).c_str(), ImGuiMouseButton_Right);
    const ImGuiTestItemInfo info = ctx->ItemInfo("//$FOCUSED/###reset_here");
    GG_CHECK(info.ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(std::string(info.DebugLabel).find("Reset to here...") == 0);
    ctx->KeyPress(ImGuiKey_Escape);
}

} // namespace ggtest
