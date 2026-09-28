// Changes panel, Change information panel and patches (§4.4, §4.11; P1-16, P1-17).
#include "panels/BlamePanel.hpp"
#include "panels/ChangesPanel.hpp"
#include "panels/DiffPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <algorithm>

namespace ggtest {

namespace {

using ggui::FileGroup;

std::string fileRef(Scenario& s, const char* group, const std::string& path)
{
    std::string ref = s.child("//Changes", "##files");
    if (group)
        ref += std::string("/") + group;
    return ref + "/" + Scenario::escapeRef(path) + "/###file_" + Scenario::escapeRef(path);
}

std::vector<std::string> paths(Scenario& s, FileGroup g)
{
    std::vector<std::string> out;
    for (const auto& r : s.session()->changes().rows())
        if (r.group == g)
            out.push_back(r.path);
    return out;
}

const ggui::FileRow* rowFor(Scenario& s, const std::string& path)
{
    for (const auto& r : s.session()->changes().rows())
        if (r.path == path)
            return &r;
    return nullptr;
}

void selectCommit(Scenario& s, const std::string& hex)
{
    s.ctx->ItemClick(("//History/**/###row_" + hex).c_str());
    s.waitUntil([&] { return !s.session()->changes().rows().empty(); });
}

} // namespace

GG_TEST("changes", "working tree groups: staged, unstaged, untracked, conflicted", "CHG-GROUPS",
    "CHG-STATUS-ICONS", "CHG-RENAMES", "CONF-NATIVE-STAGES")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->changes().rows().empty(); }));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::Staged) == (V{"a.txt", "c.txt", "e.txt"}));
    GG_CHECK(paths(s, FileGroup::Unstaged) == (V{"b.txt", "c.txt", "n.txt"}));
    GG_CHECK(paths(s, FileGroup::Untracked) == (V{"u.txt"}));
    // Same grouping as git status --porcelain=v2.
    const std::string porcelain = s.statusPorcelain(repo);
    GG_CHECK(porcelain.find("1 M. ") != std::string::npos);
    GG_CHECK(porcelain.find("? u.txt") != std::string::npos);
    const auto* renamed = rowFor(s, "e.txt");
    GG_REQUIRE(renamed != nullptr);
    GG_CHECK(renamed->kind == ggui::core::ChangeKind::Renamed);
    GG_CHECK_STR_EQ(renamed->oldPath, "d.txt");
    const auto* ita = rowFor(s, "n.txt");
    GG_REQUIRE(ita != nullptr);
    GG_CHECK(ita->intentToAdd);
    GG_CHECK(s.itemText(fileRef(s, "Staged", "e.txt").c_str()).rfind("R  d.txt", 0) == 0);
    GG_CHECK(s.itemText(fileRef(s, "Untracked", "u.txt").c_str()).rfind("?  u.txt", 0) == 0);
    GG_CHECK(s.itemExists((s.child("//Changes", "##files") + "/Staged/###group").c_str()));

    const fs::path merge = s.fixture(Recipe::MidMerge);
    GG_REQUIRE(s.openRepository(merge));
    GG_REQUIRE(s.waitUntil([&] { return !paths(s, FileGroup::Conflicted).empty(); }));
    const auto* conflict = rowFor(s, "f.txt");
    GG_REQUIRE(conflict != nullptr);
    GG_CHECK_STR_EQ(conflict->conflict, "both modified");
    GG_CHECK(s.session()->status()->conflicted[0].stage1 && s.session()->status()->conflicted[0].stage2
        && s.session()->status()->conflicted[0].stage3);
    GG_CHECK(s.itemText(fileRef(s, "Conflicted", "f.txt").c_str()).rfind("U  f.txt", 0) == 0);
}

GG_TEST("changes", "commit files, filter, compare with HEAD, header", "CHG-FILES", "CHG-FILTER", "CHG-COMPARE-HEAD", "CHG-HEADER-WT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "f1.txt", "changed\n");
    s.write(repo, "sub/x.txt", "x\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Two files"});
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::Commit) == (V{"f1.txt", "sub/x.txt"}));
    GG_CHECK(s.itemExists(fileRef(s, nullptr, "sub/x.txt").c_str()));
    ctx->ItemInputValue("//Changes/##changes_filter", "sub");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists(fileRef(s, nullptr, "f1.txt").c_str()));
    GG_CHECK(s.itemExists(fileRef(s, nullptr, "sub/x.txt").c_str()));
    ctx->ItemInputValue("//Changes/##changes_filter", "");
    // Compare an older commit with HEAD: everything that differs between them.
    selectCommit(s, s.revParse(repo, "HEAD~3"));
    GG_CHECK(paths(s, FileGroup::Commit) == (V{"f3.txt"}));
    ctx->ItemClick("//Changes/Compare with HEAD##compare_head");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f1.txt", "f4.txt", "f5.txt", "sub/x.txt"}); }));
    // A file of that comparison: its diff is HEAD against the commit, and plain text has no highlighting.
    ctx->ItemClick(fileRef(s, nullptr, "f4.txt").c_str());
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && d->query.kind == ggui::core::DiffKind::Commits && d->query.b.hex() == s.revParse(repo, "HEAD~3");
    }));
    GG_CHECK_STR_EQ(s.session()->diff().languageName(), "None");
    ctx->ItemClick("//Changes/Compare with HEAD##compare_head");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::Commit) == (V{"f3.txt"}); }));
    GG_CHECK(s.itemText("//Changes/###changes_title").rfind(s.gitOut(repo, {"rev-parse", "--short", "HEAD~3"}) + " ", 0) == 0);
    // The working tree: the zero ID before "Working tree"; Compare with HEAD disabled, in both panels.
    ctx->ItemClick("//History/**/###row_wt");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->selection().kind == ggui::SelKind::WorkingTree; }));
    const std::string zeros(s.gitOut(repo, {"rev-parse", "--short", "HEAD"}).size(), '0');
    GG_CHECK_STR_EQ(s.itemText("//Changes/###changes_title"), zeros + " Working tree");
    GG_CHECK(ctx->ItemInfo("//Changes/Compare with HEAD##compare_head").ItemFlags & ImGuiItemFlags_Disabled);
    GG_CHECK(ctx->ItemInfo("//Diff/Compare with HEAD##diff_vs_head").ItemFlags & ImGuiItemFlags_Disabled);
}

GG_TEST("changes", "multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation", "CHG-MULTISELECT-CTRL",
    "CHG-MULTISELECT-SHIFT", "CHG-SELECT-ALL", "CHG-KEY-NAV")
{
    const fs::path repo = s.fixture(Recipe::WorkingChanges);
    GG_REQUIRE(s.openRepository(repo));
    GG_REQUIRE(s.waitUntil([&] { return s.session()->changes().rows().size() == 7; }));
    auto& changes = s.session()->changes();
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(1));
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(fileRef(s, "Unstaged", "b.txt").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(2));
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick(fileRef(s, "Untracked", "u.txt").c_str());
    ctx->KeyUp(ImGuiMod_Shift);
    // b.txt (Unstaged) … u.txt: b, c, n (unstaged), u
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(4));
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(7));
    // Ctrl-click toggles off.
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    ctx->KeyUp(ImGuiMod_Ctrl);
    GG_CHECK_EQ(changes.selectedKeys().size(), static_cast<size_t>(6));
    // Arrow keys move the current file and the Diff panel follows.
    ctx->ItemClick(fileRef(s, "Staged", "a.txt").c_str());
    ctx->KeyPress(ImGuiKey_DownArrow);
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "c.txt");
    GG_CHECK(s.waitUntil([&] { return s.session()->diff().file() && s.session()->diff().file()->path == "c.txt"; }));
    ctx->KeyPress(ImGuiKey_UpArrow);
    GG_REQUIRE(changes.current() != nullptr);
    GG_CHECK_STR_EQ(changes.current()->path, "a.txt");
}

GG_TEST("changes", "file context menu: copy, patch, save patch, blame", "CHG-CTX-COPY-NAME", "CHG-CTX-COPY-REL",
    "CHG-CTX-COPY-ABS", "CHG-CTX-COPY-PATCH", "CHG-CTX-SAVE-PATCH", "CHG-CTX-BLAME", "PATCH-COPY", "PATCH-SAVE",
    "PATCH-SAVE-SELECTION")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.write(repo, "dir/file.txt", "one\n");
    s.write(repo, "f1.txt", "line 1 changed\n");
    s.git(repo, {"add", "."});
    s.git(repo, {"commit", "-q", "-m", "Two"});
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    const std::string ref = fileRef(s, nullptr, "dir/file.txt");
    s.contextMenu(ref.c_str(), "Copy/Name");
    GG_CHECK_STR_EQ(s.clipboard(), "file.txt");
    s.contextMenu(ref.c_str(), "Copy/Relative path");
    GG_CHECK_STR_EQ(s.clipboard(), "dir/file.txt");
    s.contextMenu(ref.c_str(), "Copy/Absolute path");
    GG_CHECK_STR_EQ(s.clipboard(), (repo / "dir" / "file.txt").string());

    s.contextMenu(ref.c_str(), "Patch/Copy");
    const std::string expected = s.git(repo, {"diff", "HEAD~1", "HEAD", "--", "dir/file.txt"}).out;
    GG_CHECK(s.waitUntil([&] { return s.clipboard() == expected; }));
    // The copied patch applies with plain git.
    s.git(repo, {"apply", "--check", "-R"}, s.clipboard());

    const fs::path out = s.path("file.patch");
    ggui::setEnv("GGUI_TEST_PICK_PATH", out.string());
    s.contextMenu(ref.c_str(), "Patch/Save...");
    GG_CHECK(s.waitUntil([&] { return s.read(s.root(), "file.patch") == expected; }));

    // A multi-file selection saves one patch for all selected files.
    ctx->ItemClick(ref.c_str());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    GG_CHECK_EQ(s.session()->changes().selectedKeys().size(), static_cast<size_t>(2));
    const fs::path both = s.path("both.patch");
    ggui::setEnv("GGUI_TEST_PICK_PATH", both.string());
    s.contextMenu(ref.c_str(), "Patch/Save...");
    const std::string expectedBoth = s.git(repo, {"diff", "HEAD~1", "HEAD"}).out;
    GG_CHECK(s.waitUntil([&] { return s.read(s.root(), "both.patch") == expectedBoth; }));
    ggui::unsetEnv("GGUI_TEST_PICK_PATH");

    s.contextMenu(ref.c_str(), "Blame file");
    GG_CHECK(s.waitUntil([&] { return s.session()->blame().blame() && s.session()->blame().blame()->query.path == "dir/file.txt"; }));
}

GG_TEST("changes", "stash contents: working tree, index and untracked parts", "STASH-INSPECT", "DIFF-STASH-PARTS",
    "STASH-PANEL", "STASH-LIST")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    GG_REQUIRE(s.openRepository(repo));
    const auto snapshot = s.session()->snapshot(); // keeps stashes alive while the UI refreshes
    const auto& stashes = snapshot->stashes;
    GG_REQUIRE(stashes.size() == 3);
    GG_CHECK_STR_EQ(stashes[0].message, "On main: with untracked");
    GG_CHECK_STR_EQ(stashes[1].message, "On main: index and worktree");
    GG_CHECK(stashes[0].hasUntracked && !stashes[1].hasUntracked);
    GG_CHECK(stashes[1].hasIndexChanges);
    GG_CHECK_STR_EQ(stashes[1].base.hex(), s.revParse(repo, "stash@{1}^1"));
    s.showPanel("Stashes");
    GG_CHECK(s.itemText("//Stashes/stash_1/###row").rfind("stash@{1} On main: index", 0) == 0);
    ctx->ItemClick("//Stashes/stash_1/###row");
    GG_CHECK(s.waitUntil([&] { return !paths(s, FileGroup::StashIndex).empty() && !paths(s, FileGroup::StashWorktree).empty(); }));
    using V = std::vector<std::string>;
    GG_CHECK(paths(s, FileGroup::StashIndex) == (V{"a.txt"}));
    GG_CHECK(paths(s, FileGroup::StashWorktree) == (V{"b.txt"}));
    ctx->ItemClick("//Stashes/stash_0/###row");
    GG_CHECK(s.waitUntil([&] { return paths(s, FileGroup::StashUntracked) == (V{"new.txt"}); }));
    GG_CHECK(paths(s, FileGroup::StashWorktree) == (V{"b.txt"}));
    // Diff of the untracked part.
    ctx->ItemClick(fileRef(s, "Untracked files", "new.txt").c_str());
    GG_CHECK(s.waitUntil([&] {
        const auto& d = s.session()->diff().diff();
        return d && !d->files.empty() && d->files[0].path() == "new.txt" && d->query.kind == ggui::core::DiffKind::StashUntracked;
    }));
}

GG_TEST("info", "change information: message, author, committer, date, ID, parents", "INFO-MESSAGE", "INFO-AUTHOR",
    "INFO-COPY-NAME", "INFO-COPY-EMAIL", "INFO-DATE", "INFO-COMMIT-ID-COPY", "INFO-PARENTS-REVEAL", "INFO-COMMITTER",
    "INFO-AUTHOR-PLAIN", "APP-ID-DIMMED")
{
    const fs::path repo = s.fixture(Recipe::Merges);
    // A commit whose committer differs from its author.
    s.write(repo, "c.txt", "c\n");
    s.git(repo, {"add", "c.txt"});
    gg::RunRequest r;
    r.args = {"git", "-c", "user.name=Committer Person", "-c", "user.email=committer@example.com", "commit", "-q",
        "--author=Author Person <author@example.com>", "-m", "Subject line\n\nBody text."};
    r.cwd = repo;
    GG_REQUIRE(gg::run(r).ok());
    GG_REQUIRE(s.openRepository(repo));
    selectCommit(s, s.head(repo));
    auto& info = s.session()->info();
    GG_REQUIRE(s.waitUntil([&] { return info.details() != nullptr; }));
    GG_CHECK_STR_EQ(info.details()->message, "Subject line\n\nBody text.\n");
    GG_CHECK_STR_EQ(info.details()->authorName, "Author Person");
    GG_CHECK_STR_EQ(info.details()->committerName, "Committer Person");
    GG_CHECK(s.itemExists("//Change information/##message"));
    s.contextMenu("//Change information/**/###author", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "Author Person");
    s.contextMenu("//Change information/**/###author", "Copy email");
    GG_CHECK_STR_EQ(s.clipboard(), "author@example.com");
    // The author line is plain text: no hover or click effect (its context menu stays).
    ctx->ItemClick("//Change information/**/###author");
    ctx->Yield(2);
    GG_CHECK(ImGui::GetActiveID() == 0 && !s.itemDrawsBackground("//Change information/**/###author"));
    // The full ID shows the short prefix normally and the rest dimmed; copy is short, Shift full.
    const std::string shortHead = s.gitOut(repo, {"rev-parse", "--short", "HEAD"});
    ctx->ScrollToItemY("//Change information/**/###commit_id_text");
    GG_CHECK(s.idShownDimmed("//Change information", s.head(repo), shortHead.size()));
    ctx->ItemClick("//Change information/**/###commit_id");
    GG_CHECK_STR_EQ(s.clipboard(), shortHead);
    ctx->KeyDown(ImGuiMod_Shift);
    ctx->ItemClick("//Change information/**/###commit_id");
    ctx->KeyUp(ImGuiMod_Shift);
    GG_CHECK_STR_EQ(s.clipboard(), s.head(repo));
    GG_CHECK(info.details()->authorTime > 0);
    // Parents: the merge has two; clicking one reveals it.
    selectCommit(s, s.revParse(repo, "HEAD~1"));
    GG_REQUIRE(s.waitUntil([&] { return info.details() && info.details()->parents.size() == 2; }));
    // (A clickable item does draw a hover highlight: the check above can fail.)
    ctx->MouseMove("//Change information/**/###parent_1");
    GG_CHECK(s.itemDrawsBackground("//Change information/**/###parent_1"));
    ctx->ItemClick("//Change information/**/###parent_1");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD~1^2"); }));
    // The root commit has no parents.
    selectCommit(s, s.revParse(repo, "HEAD~1^2~2"));
    GG_CHECK(s.waitUntil([&] { return info.details() && info.details()->parents.empty(); }));
}

} // namespace ggtest
