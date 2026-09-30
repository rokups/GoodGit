// Side panels: Branches, Tags, Worktrees, Remotes, Reflog (§4.7).
#include "panels/HistoryPanel.hpp"
#include "panels/SidePanels.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include "util/Ui.hpp"

#include <algorithm>

namespace ggtest {

GG_TEST("panels", "branches: filter, current, upstream, reveal, copy")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"branch", "feature/one", "HEAD~1"});
    s.git(repo, {"branch", "other"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Branches");
    const auto* main = s.session()->snapshot()->findBranch("main");
    GG_REQUIRE(main != nullptr);
    GG_CHECK(main->isHead);
    GG_CHECK_STR_EQ(main->upstream, "origin/main");
    GG_CHECK_EQ(main->ahead, 1);
    GG_CHECK_EQ(main->behind, 1);
    GG_CHECK(s.itemText("//Branches/branch_main/###branch_main").find("origin/main") != std::string::npos);
    // Nested names are addressable; filter narrows the list.
    GG_CHECK(s.itemExists("//Branches/branch_feature:one/###branch_feature:one"));
    ctx->ItemInputValue("//Branches/##branch_filter", "feat");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists("//Branches/branch_main/###branch_main"));
    GG_CHECK(s.itemExists("//Branches/branch_feature:one/###branch_feature:one"));
    s.contextMenu("//Branches/branch_feature:one/###branch_feature:one", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "feature/one");
    s.contextMenu("//Branches/branch_feature:one/###branch_feature:one", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "feature/one"); }));
    ctx->ItemInputValue("//Branches/##branch_filter", "");
    // Remote-tracking branches under their remote.
    ctx->Yield(2);
    const std::string remoteGroup = "//Branches/remote_group_origin";
    GG_CHECK(s.itemExists((remoteGroup + "/origin").c_str()));
}

GG_TEST("panels", "tags: filter, visibility, reveal, copy")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"tag", "v1.0", "HEAD~3"});
    s.git(repo, {"tag", "-a", "-m", "Release two", "v2.0", "HEAD~1"});
    s.git(repo, {"branch", "-f", "side", "HEAD~3"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Tags");
    const auto snapshot = s.session()->snapshot(); // keeps tags alive while the UI refreshes
    const auto& tags = snapshot->tags;
    GG_REQUIRE(tags.size() == 2);
    GG_CHECK(!tags[0].annotated && tags[1].annotated);
    GG_CHECK_STR_EQ(tags[1].message, "Release two\n");
    // Labels are the plain names, annotated or not.
    GG_CHECK_STR_EQ(s.itemText("//Tags/tag_v2.0/###tag_v2.0"), "v2.0");
    GG_CHECK_STR_EQ(s.itemText("//Tags/tag_v1.0/###tag_v1.0"), "v1.0");
    ctx->ItemInputValue("//Tags/##tag_filter", "v2");
    ctx->Yield(2);
    GG_CHECK(!s.itemExists("//Tags/tag_v1.0/###tag_v1.0"));
    s.contextMenu("//Tags/tag_v2.0/###tag_v2.0", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "v2.0");
    s.contextMenu("//Tags/tag_v2.0/###tag_v2.0", "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD~1"); }));
    // Visibility: Ctrl-click shows only this tag's history.
    ctx->ItemInputValue("//Tags/##tag_filter", "");
    ctx->Yield(2);
    ctx->KeyDown(ImGuiMod_Ctrl);
    ctx->ItemClick("//Tags/tag_v1.0/###eye");
    ctx->KeyUp(ImGuiMod_Ctrl);
    auto& history = s.session()->history();
    GG_CHECK(s.waitUntil([&] { return !history.loading() && history.rows().size() == 2; }));
    GG_CHECK(!history.refVisible("refs/tags/v2.0"));
    ctx->ItemClick("//Tags/tag_v2.0/###eye");
    GG_CHECK(s.waitUntil([&] { return history.refVisible("refs/tags/v2.0") && history.rows().size() == 4; }));
}

GG_TEST("panels", "worktrees: main, locked, stale; copy, reveal, open")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const fs::path opened = s.fakeTool(kFileManager);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    const auto snapshot = s.session()->snapshot(); // keeps wts alive while the UI refreshes
    const auto& wts = snapshot->worktrees;
    GG_REQUIRE(wts.size() == 4);
    GG_CHECK(wts[0].isMain && wts[0].isCurrent);
    const std::string wt1 = repo.filename().string() + "-wt1";
    const std::string wt2 = repo.filename().string() + "-wt2";
    const std::string wt3 = repo.filename().string() + "-wt3";
    auto find = [&](const std::string& name) {
        return std::find_if(wts.begin(), wts.end(), [&](const auto& w) { return w.name == name; });
    };
    GG_REQUIRE(find(wt1) != wts.end() && find(wt2) != wts.end() && find(wt3) != wts.end());
    GG_CHECK_STR_EQ(find(wt1)->branch, "wt1");
    GG_CHECK(find(wt2)->locked);
    GG_CHECK_STR_EQ(find(wt2)->lockReason, "test lock");
    GG_CHECK(find(wt3)->prunable);
    // Same list as git worktree list --porcelain.
    const std::string porcelain = s.gitOut(repo, {"worktree", "list", "--porcelain"});
    GG_CHECK(porcelain.find("locked test lock") != std::string::npos);
    GG_CHECK(porcelain.find("prunable") != std::string::npos);
    const std::string row = "//Worktrees/worktree_" + wt1 + "/###row";
    GG_CHECK(s.itemExists(row.c_str()));
    s.contextMenu(row.c_str(), "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), wt1);
    s.contextMenu(row.c_str(), "Copy path");
    GG_CHECK_STR_EQ(s.clipboard(), (s.root() / wt1).string());
    s.contextMenu(row.c_str(), "Reveal HEAD");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "wt1"); }));
    s.showPanel("Worktrees");
    s.contextMenu(row.c_str(), "Open directory");
    GG_CHECK(s.waitUntil([&] {
        return s.read(opened.parent_path(), opened.filename().string()).find((s.root() / wt1).string()) != std::string::npos;
    }));
}

GG_TEST("panels", "remotes: list and copy")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "backup", "https://example.invalid/backup.git"});
    s.git(repo, {"config", "remote.backup.prune", "true"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    const auto snapshot = s.session()->snapshot(); // keeps remotes alive while the UI refreshes
    const auto& remotes = snapshot->remotes;
    GG_REQUIRE(remotes.size() == 2);
    GG_CHECK_STR_EQ(remotes[0].name, "backup");
    GG_CHECK(remotes[0].pruneOnFetch);
    GG_CHECK(remotes[1].url.rfind("file://", 0) == 0);
    s.contextMenu("//Remotes/remote_origin/###row", "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "origin");
}

GG_TEST("panels", "remotes: name@host label with the host dimmed, tooltip shows the URL once")
{
    using ggui::remoteHost;
    GG_CHECK_STR_EQ(remoteHost("https://github.com/owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("https://user:secret@git.example.org:8443/owner/repo.git"), "git.example.org");
    GG_CHECK_STR_EQ(remoteHost("ssh://git@github.com:22/owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("ssh://host.lan"), "host.lan");
    GG_CHECK_STR_EQ(remoteHost("ssh://git@[::1]:2222/repo"), "[::1]");
    GG_CHECK_STR_EQ(remoteHost("git@github.com:owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("github.com:owner/repo.git"), "github.com");
    GG_CHECK_STR_EQ(remoteHost("file:///srv/git/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("/srv/git/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("../repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost("C:/repos/repo.git"), "");
    GG_CHECK_STR_EQ(remoteHost(""), "");

    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"remote", "add", "backup", "https://user@example.invalid:8443/backup.git"});
    s.git(repo, {"remote", "add", "split", "https://example.invalid/fetch.git"});
    s.git(repo, {"remote", "set-url", "--push", "split", "ssh://git@example.invalid/push.git"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Remotes");
    GG_CHECK(s.idShownDimmed("//Remotes", "backup@example.invalid", 6));
    // A local remote has no host: just the name.
    GG_CHECK(s.textShown("//Remotes", "origin"));
    GG_CHECK(!s.textShown("//Remotes", "origin@"));
    ctx->MouseMove("//Remotes/remote_backup/###row");
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "https://user@example.invalid:8443/backup.git"));
    GG_CHECK(s.drawnText("//##Tooltip_00").size() == 1);
    ctx->MouseMove("//Remotes/remote_split/###row");
    ctx->SleepNoSkip(1.0f, 0.1f);
    GG_CHECK(s.textShown("//##Tooltip_00", "Fetch: https://example.invalid/fetch.git"));
    GG_CHECK(s.textShown("//##Tooltip_00", "Push: ssh://git@example.invalid/push.git"));
}

GG_TEST("panels", "reflog: HEAD, branch, stash; filter; copy; reveal")
{
    const fs::path repo = s.fixture(Recipe::Stashes);
    s.git(repo, {"switch", "-q", "-c", "temp", "HEAD~1"});
    s.git(repo, {"switch", "-q", "main"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Reflog");
    auto& reflog = s.session()->reflog();
    const auto headLines = gg::splitLines(s.gitOut(repo, {"reflog", "--format=%H"}));
    GG_REQUIRE(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->entries.size() == headLines.size(); }));
    GG_CHECK_STR_EQ(reflog.reflog()->entries[0].newId.hex(), headLines[0]);
    GG_CHECK(reflog.reflog()->entries[0].message.find("checkout: moving from temp to main") != std::string::npos);
    // Column headers use Git words (no "Change" column: a row is a ref moving between commits).
    GG_CHECK(s.textShown("//Reflog", "Commits"));
    const std::string table = "//Reflog/##reflog_table";
    s.contextMenu((table + "/r0/###reflog_0").c_str(), "Copy new ID");
    GG_CHECK_STR_EQ(s.clipboard(), s.gitOut(repo, {"rev-parse", "--short", headLines[0]}));
    s.contextMenu((table + "/r0/###reflog_0").c_str(), "Copy old ID");
    GG_CHECK_STR_EQ(s.clipboard(), s.gitOut(repo, {"rev-parse", "--short", "HEAD@{1}"}));
    s.contextMenu((table + "/r0/###reflog_0").c_str(), "Reveal old commit");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "HEAD@{1}"); }));
    s.showPanel("Reflog");
    s.contextMenu((std::string("//Reflog/##reflog_table") + "/r0/###reflog_0").c_str(), "Reveal new commit");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == headLines[0]; }));
    s.showPanel("Reflog");
    // Filter.
    ctx->ItemInputValue("//Reflog/##reflog_filter", "moving from temp");
    ctx->Yield(2);
    GG_CHECK(s.itemExists((std::string("//Reflog/##reflog_table") + "/r0/###reflog_0").c_str()));
    GG_CHECK(!s.itemExists((std::string("//Reflog/##reflog_table") + "/r1/###reflog_1").c_str()));
    ctx->ItemInputValue("//Reflog/##reflog_filter", "");
    // Other reflogs: a branch and the stash.
    s.comboSelect("//Reflog/##reflog_ref", "###ref_refs:stash");
    const auto stashes = gg::splitLines(s.gitOut(repo, {"reflog", "show", "--format=%H", "refs/stash"}));
    GG_CHECK(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->ref == "refs/stash" && reflog.reflog()->entries.size() == stashes.size(); }));
    s.comboSelect("//Reflog/##reflog_ref", "###ref_refs:heads:temp");
    GG_CHECK(s.waitUntil([&] { return reflog.reflog() && reflog.reflog()->ref == "refs/heads/temp" && reflog.reflog()->entries.size() == 1; }));
}

GG_TEST("panels", "details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation")
{
    const fs::path repo = s.fixture(Recipe::WithRemote);
    s.git(repo, {"tag", "-a", "-m", "An annotated tag", "ann"});
    const fs::path locked = s.root() / "locked-wt";
    s.git(repo, {"worktree", "add", "-q", "--detach", locked.string()});
    s.git(repo, {"worktree", "lock", "--reason", "on a USB stick", locked.string()});
    GG_REQUIRE(s.openRepository(repo));
    auto hover = [&](const std::string& ref) {
        ctx->MouseMove(ref.c_str());
        ctx->SleepNoSkip(1.0f, 0.1f);
    };

    // A remote-tracking branch: its menu, and its visibility in History.
    s.showPanel("Branches");
    const std::string rbranch = "//Branches/remote_group_origin/origin/rbranch_origin:main/###rbranch_origin:main";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(rbranch.c_str()); }));
    s.contextMenu(rbranch.c_str(), "Copy name");
    GG_CHECK_STR_EQ(s.clipboard(), "origin/main");
    s.contextMenu(rbranch.c_str(), "Reveal");
    GG_CHECK(s.waitUntil([&] { return s.session()->selection().id.hex() == s.revParse(repo, "origin/main"); }));
    auto& history = s.session()->history();
    const std::string rbranchEye = "//Branches/remote_group_origin/origin/rbranch_origin:main/###eye";
    ctx->ItemClick(rbranchEye.c_str());
    GG_CHECK(!history.refVisible("refs/remotes/origin/main"));
    ctx->ItemClick(rbranchEye.c_str());
    GG_CHECK(history.refVisible("refs/remotes/origin/main"));
    // Tooltips: an annotated tag's message, a remote's URLs, a locked worktree's reason.
    s.showPanel("Tags");
    hover("//Tags/tag_ann/###tag_ann");
    s.showPanel("Remotes");
    hover("//Remotes/remote_origin/###row");
    s.showPanel("Worktrees");
    const std::string wtRow = "//Worktrees/worktree_" + locked.filename().string() + "/###row";
    GG_REQUIRE(s.waitUntil([&] { return s.itemExists(wtRow.c_str()); }));
    hover(wtRow);
    ctx->ItemClick(wtRow.c_str(), ImGuiMouseButton_Right);
    GG_CHECK(s.itemExists("//$FOCUSED/Unlock"));
    ctx->KeyPress(ImGuiKey_Escape);
    // The reflog filtered by a commit ID.
    s.showPanel("Reflog");
    const std::string head = s.head(repo);
    ctx->ItemInputValue("//Reflog/##reflog_filter", head.substr(0, 12).c_str());
    ctx->Yield(2);
    GG_CHECK(s.itemExists("//Reflog/##reflog_table/r0/###reflog_0"));
    ctx->ItemInputValue("//Reflog/##reflog_filter", "");
    // Stash changes from the Stashes panel.
    s.write(repo, "dirty.txt", "dirty\n");
    GG_REQUIRE(s.waitUntil([&] { return s.session()->status() && !s.session()->status()->untracked.empty(); }));
    s.showPanel("Stashes");
    ctx->ItemClick("//Stashes/Stash changes...##stash_changes");
    GG_REQUIRE(s.dialogOpen("Stash changes"));
    s.dialogText("Stash changes", "message", "from the Stashes panel");
    s.dialogCheck("Stash changes", "untracked", "Include untracked files");
    s.dialogButton("Stash changes", "Stash");
    GG_CHECK(s.waitUntil([&] { return s.gitOut(repo, {"stash", "list"}).find("from the Stashes panel") != std::string::npos; }));
    s.settle();
    GG_CHECK(!fs::exists(repo / "dirty.txt"));
    GG_CHECK(s.waitUntil([&] { return s.itemExists("//Stashes/stash_0/###row"); }));
    s.write(repo, "dirty.txt", "dirty\n");

    // Operations: a commit its pre-commit hook refuses is listed as failed; the buttons undo and redo.
    s.write(repo / ".git" / "hooks", "pre-commit", "#!/bin/sh\necho no >&2\nexit 1\n");
    fs::permissions(repo / ".git" / "hooks" / "pre-commit", fs::perms::owner_all);
    s.git(repo, {"add", "dirty.txt"});
    ctx->ItemClick("//##Toolbar/###tb_commit");
    GG_REQUIRE(s.dialogOpen("Commit"));
    s.dialogText("Commit", "message", "Refused");
    s.dialogButton("Commit", "Commit");
    GG_CHECK(s.dismissError());
    fs::remove(repo / ".git" / "hooks" / "pre-commit");
    s.showPanel("Operations");
    GG_CHECK(s.waitUntil([&] { return s.textShown("//Operations", "(failed)"); }));
    // Ctrl+N needs a commit a branch can advance from: HEAD's (origin/main, selected above, has no local branch).
    s.session()->selectCommit(ggui::core::Oid::fromHex(s.head(repo)));
    ctx->Yield(2);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_N);
    GG_REQUIRE(s.waitUntil([&] { return !s.session()->operations().empty() && s.session()->operations().back().label == "new commit"; }));
    s.settle();
    const std::string made = s.head(repo);
    // A copy: the operations list is reloaded while the UI runs.
    const std::string opId = s.session()->operations().back().id;
    const std::string row = s.child("//Operations", "##ops_table") + "/**/op_" + opId + "/###row";
    hover(row);
    s.contextMenu(row.c_str(), "Copy operation ID");
    GG_CHECK_STR_EQ(s.clipboard(), opId);
    ctx->ItemClick("//Operations/###ops_undo");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) != made; }));
    s.settle();
    ctx->ItemClick("//Operations/###ops_redo");
    GG_CHECK(s.waitUntil([&] { return s.head(repo) == made; }));
    s.settle();
    s.git(repo, {"worktree", "unlock", locked.string()});
    s.git(repo, {"worktree", "remove", "--force", locked.string()});
}

} // namespace ggtest
