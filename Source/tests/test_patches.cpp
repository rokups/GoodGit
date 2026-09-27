// Apply patch (§4.11; P2-23).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

namespace {

// A patch changing f1.txt ("line 1" → "patched 1"), made with git itself.
std::string makePatch(Scenario& s, const fs::path& repo)
{
    s.write(repo, "f1.txt", "patched 1\n");
    const std::string patch = s.git(repo, {"diff"}).out;
    s.git(repo, {"checkout", "-q", "--", "f1.txt"});
    return patch;
}

void applyPatch(Scenario& s, const char* source, const char* target, const std::string& file = {})
{
    s.ctx->MenuClick("//##MainMenuBar/Edit/Apply patch...");
    if (!s.dialogOpen("Apply patch"))
        return;
    s.comboSelect("//Apply patch/Source##source", source);
    if (!file.empty())
        s.dialogText("Apply patch", "file", file);
    s.comboSelect("//Apply patch/Apply to##target", target);
    s.dialogButton("Apply patch", "Apply");
}

} // namespace

GG_TEST("patches", "apply from the clipboard or a file, to the working tree or the index", "MENU-EDIT-APPLY-PATCH",
    "PATCH-APPLY-CLIPBOARD", "PATCH-APPLY-FILE", "PATCH-APPLY-INDEX", "PATCH-APPLY-WORKTREE")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string patch = makePatch(s, repo);
    GG_REQUIRE(s.openRepository(repo));
    // Clipboard → working tree.
    ImGui::SetClipboardText(patch.c_str());
    applyPatch(s, "Clipboard", "Working tree");
    GG_CHECK(s.waitUntil([&] { return s.read(repo, "f1.txt") == "patched 1\n"; }));
    s.settle();
    GG_CHECK(s.gitOut(repo, {"diff", "--cached", "--name-only"}).empty());
    s.git(repo, {"checkout", "-q", "--", "f1.txt"});
    // File → index only (the working tree keeps the old content).
    s.write(s.root(), "change.patch", patch);
    applyPatch(s, "File", "Index (git apply --cached)", (s.root() / "change.patch").string());
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"diff", "--cached", "--name-only"}) == "f1.txt"; }));
    s.settle();
    GG_CHECK_STR_EQ(s.read(repo, "f1.txt"), "line 1\n");
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", ":f1.txt"}), "patched 1");
}

GG_TEST("patches", "a patch that does not apply is reported and changes nothing", "PATCH-APPLY-FAIL")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string patch = makePatch(s, repo);
    s.commitFile(repo, "f1.txt", "something else entirely\n", "Diverge");
    GG_REQUIRE(s.openRepository(repo));
    ImGui::SetClipboardText(patch.c_str());
    applyPatch(s, "Clipboard", "Working tree");
    GG_CHECK(s.waitUntil([&] { return s.app.errorMessage().find("patch") != std::string::npos; }));
    GG_CHECK(s.dismissError());
    GG_CHECK(s.statusPorcelain(repo).empty());
}

} // namespace ggtest
