// Commit, amend, reword HEAD, hooks during commit (§4.3, §4.4, §4.12 A; C1 default).
#include "panels/ChangesPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    return s.child("//Changes", "##files") + "/" + group + "/" + path + "/###file_" + path;
}

std::string headMessage(Scenario& s, const fs::path& repo) { return s.gitOut(repo, {"log", "-1", "--format=%B"}); }

void writeHook(Scenario& s, const fs::path& repo, const std::string& name, const std::string& body)
{
    const fs::path hook = repo / ".git" / "hooks" / name;
    s.write(repo / ".git" / "hooks", name, "#!/bin/sh\n" + body);
    fs::permissions(hook, fs::perms::owner_all);
}

} // namespace

GG_TEST("commit", "commit the index from the toolbar; hooks run natively", "ACT-COMMIT", "TB-COMMIT", "TB-COMMIT-LABEL",
    "HOOK-USER-NATIVE")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    writeHook(s, repo, "commit-msg", "printf '\\nHook-Trailer: yes\\n' >> \"$1\"\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.itemText("//##Toolbar/###tb_commit").find("Commit") != std::string::npos);
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Commit staged work");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Commit staged work", 0) == 0; }));
    s.settle();
    GG_CHECK(headMessage(s, repo).find("Hook-Trailer: yes") != std::string::npos);
    // Only the index was committed: b.txt is still unstaged, e.txt (rename) is committed.
    GG_CHECK(s.gitOut(repo, {"diff", "--name-only"}).find("b.txt") != std::string::npos);
    GG_CHECK(s.gitOut(repo, {"ls-tree", "--name-only", "HEAD"}).find("e.txt") != std::string::npos);
    // With HEAD selected the button becomes Amend.
    ctx->ItemClick(("//History/**/###row_" + s.head(repo)).c_str());
    ctx->Yield(2);
    GG_CHECK(s.itemText("//##Toolbar/###tb_commit").find("Amend") != std::string::npos);
}

GG_TEST("commit", "nothing staged: stage all tracked or the selected files", "ACT-COMMIT-NOTHING-STAGED-ALL",
    "ACT-COMMIT-NOTHING-STAGED-SELECTED", "MENU-COMMIT-COMMIT", "HIST-WT-CTX-COMMIT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "one changed\n");
    s.write(repo, "f2.txt", "two changed\n");
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    ctx->MenuClick("//##MainMenuBar/Commit/Commit...");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Everything");
    s.comboSelect("//Commit/Nothing is staged##nothing_staged", "Stage all tracked changes and commit (-a)");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Everything", 0) == 0; }));
    s.settle();
    GG_CHECK(s.statusPorcelain(repo).empty());
    // Only the selected file.
    s.write(repo, "f3.txt", "three changed\n");
    s.write(repo, "f4.txt", "four changed\n");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 2; }));
    ctx->ItemClick(fileRef(s, "Unstaged", "f3.txt").c_str());
    s.contextMenu("//History/**/###row_wt", "Commit...");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Only three");
    s.comboSelect("//Commit/Nothing is staged##nothing_staged", "Stage the selected files and commit");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Only three", 0) == 0; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "--name-only", "--format=", "HEAD"}), "f3.txt");
    GG_CHECK(s.gitOut(repo, {"diff", "--name-only"}) == "f4.txt");
}

GG_TEST("commit", "default for nothing staged comes from Settings", "SET-COMMIT-ALL-DEFAULT", "SET-NOTHING-STAGED-LABEL")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "one changed\n");
    GG_REQUIRE(s.openRepository(repo));
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    s.comboSelect("//Settings/##settings_tabs/Git/Commit with nothing staged##nothing_staged", "Stage all tracked changes");
    GG_CHECK(s.app.settings().data().nothingStaged == ggui::NothingStaged::StageAll);
    ctx->WindowClose("//Settings");
    ctx->MenuClick("//##MainMenuBar/Commit/Commit...");
    GG_REQUIRE(s.dialogOpen("Commit"));
    GG_CHECK(!s.itemExists("//Commit/Nothing is staged##nothing_staged"));
    s.dialogText("Commit", "message", "Default stage all");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return s.statusPorcelain(repo).empty(); }));
    s.settle();
    // "Stage the selected files" as the default: only the file selected in Changes is committed.
    s.app.openSettings();
    ctx->Yield(2);
    ctx->ItemClick("//Settings/##settings_tabs/Git");
    s.comboSelect("//Settings/##settings_tabs/Git/Commit with nothing staged##nothing_staged", "Stage the selected files");
    ctx->WindowClose("//Settings");
    s.write(repo, "f2.txt", "two changed\n");
    s.write(repo, "f3.txt", "three changed\n");
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(fileRef(s, "Unstaged", "f3.txt").c_str()); }));
    ctx->ItemClick(fileRef(s, "Unstaged", "f2.txt").c_str());
    ctx->MenuClick("//##MainMenuBar/Commit/Commit...");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Default stage selected");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo) == "Default stage selected"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "--name-only", "--format=", "HEAD"}), "f2.txt");
    // Amend with HEAD selected: the message field starts with HEAD's message.
    ctx->ItemClick(("//History/**/###row_" + s.head(repo)).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/##message"); }));
    ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
    GG_REQUIRE(s.dialogOpen("Amend"));
    GG_CHECK_STR_EQ(s.app.dialogs().current()->text("message"), "Default stage selected\n");
    s.dialogButton("Amend", "Cancel");
}

GG_TEST("commit", "failing pre-commit hook goes to the banner; Skip hooks", "ACT-COMMIT-HOOK-FAIL", "ACT-COMMIT-SKIP-HOOKS",
    "HOOK-FAIL-POPUP", "HOOK-SKIP")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    writeHook(s, repo, "pre-commit", "echo 'pre-commit hook says no' >&2\nexit 1\n");
    const std::string before = s.head(repo);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Blocked");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return s.app.errorMessage().find("pre-commit hook says no") != std::string::npos; }));
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK_STR_EQ(s.head(repo), before);
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Not blocked");
    s.dialogCheck("Commit", "skip_hooks", "Skip hooks (--no-verify)");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Not blocked", 0) == 0; }));
    s.settle();
}

GG_TEST("commit", "amend content and message, message only, Amend into HEAD", "ACT-AMEND", "ACT-AMEND-MESSAGE",
    "MENU-COMMIT-AMEND", "HIST-WT-CTX-AMEND")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f5.txt", "amended content\n");
    s.git(repo, {"add", "f5.txt"});
    const std::string parent = s.revParse(repo, "HEAD~1");
    GG_REQUIRE(s.openRepository(repo));
    // Message only: the staged change stays staged.
    ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogText("Amend", "message", "Reworded only");
    s.dialogCheck("Amend", "message_only", "Change the message only (keep the index out)");
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Reworded only", 0) == 0; }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"diff", "--cached", "--name-only"}), "f5.txt");
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), parent);
    // Amend with the index and a new message.
    ctx->MenuClick("//##MainMenuBar/Commit/Amend...");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogText("Amend", "message", "Amended with content");
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo).rfind("Amended with content", 0) == 0; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", "HEAD:f5.txt"}), "amended content");
    // Amend into HEAD from the Working tree menu keeps the message (empty field).
    s.write(repo, "f4.txt", "more\n");
    s.git(repo, {"add", "f4.txt"});
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && s.session()->status()->staged.size() == 1; }));
    s.contextMenu("//History/**/###row_wt", "Amend into HEAD...");
    GG_REQUIRE(s.dialogOpen("Amend"));
    s.dialogButton("Amend", "Amend");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"show", "HEAD:f4.txt"}) == "more"; }));
    s.settle();
    GG_CHECK(headMessage(s, repo).rfind("Amended with content", 0) == 0);
    GG_CHECK_STR_EQ(s.revParse(repo, "HEAD~1"), parent);
}

GG_TEST("commit", "reword HEAD from Change information (amend mode)", "INFO-SAVE-MESSAGE", "ACT-DESCRIBE-HEAD", "INFO-AMEND-MODE")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    ctx->ItemClick(("//History/**/###row_" + s.head(repo)).c_str());
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists("//Change information/##message"); }));
    s.setText("//Change information/##message", "Better subject\n\nWith a body.");
    ctx->ItemClick("//Change information/###save_message");
    GG_CHECK(s.waitUntil([&] { return headMessage(s, repo) == "Better subject\n\nWith a body."; }));
    s.settle();
    // An older commit cannot be reworded here yet (needs history editing): read-only.
    ctx->ItemClick(("//History/**/###row_" + s.revParse(repo, "HEAD~1")).c_str());
    ctx->Yield(3);
    GG_CHECK(ctx->ItemInfo("//Change information/###save_message").ItemFlags & ImGuiItemFlags_Disabled);
}

} // namespace ggtest
