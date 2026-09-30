// Worktree management (§4.7 Worktrees, §4.1 open in a new window): Add…, Remove…,
// Lock/Unlock, Prune, Repair, Open here, Open in new window, and the per-worktree journal.
#include "panels/SidePanels.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/Worktrees.hpp>

#include <optional>

namespace ggtest {

namespace {

using gg::worktrees::Entry;

std::string wtRow(const std::string& name) { return "//Worktrees/worktree_" + name + "/###row"; }

// The worktree registered at `path`, as plain git lists it.
std::optional<Entry> registered(Scenario& s, const fs::path& repo, const fs::path& path)
{
    const auto r = s.gitMayFail(repo, {"worktree", "list", "--porcelain", "-z"});
    for (const auto& e : gg::worktrees::parse(r.out))
        if (gg::worktrees::samePath(e.path, path))
            return e;
    return std::nullopt;
}

// The worktree as the Worktrees panel shows it (by name).
const ggui::core::WorktreeInfo* shown(Scenario& s, const std::string& name)
{
    for (const auto& w : s.session()->snapshot()->worktrees)
        if (w.name == name)
            return &w;
    return nullptr;
}

bool refExists(Scenario& s, const fs::path& repo, const std::string& ref)
{
    return s.gitMayFail(repo, {"rev-parse", "--verify", "-q", ref}).ok();
}

bool disabled(Scenario& s, const char* ref) { return (s.ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) != 0; }

bool toastShown(Scenario& s, const std::string& title, const std::string& text)
{
    for (const auto& t : s.app.toasts())
        if (t.title == title && t.message.find(text) != std::string::npos)
            return true;
    return false;
}

void undoKey(Scenario& s) { s.ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z); }
void redoKey(Scenario& s) { s.ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Y); }

// Fills and submits the Add worktree dialog (already open).
void addDialog(Scenario& s, const fs::path& path, const char* mode, const std::string& branchOrStart)
{
    s.dialogText("Add worktree", "path", path.string());
    if (mode) {
        s.comboSelect("//Add worktree/Check out##checkout", mode);
        s.ctx->Yield(2);
    }
    if (!mode || std::string(mode) == "A new branch")
        s.dialogText("Add worktree", "branch", branchOrStart);
    else if (std::string(mode) == "An existing branch")
        s.comboSelect("//Add worktree/Branch##existing", branchOrStart.c_str());
    else
        s.dialogText("Add worktree", "start", branchOrStart);
}

} // namespace

GG_TEST("worktrees", "add: a new branch at a start point, locked, in a path with spaces; a detached commit; from Branches; undo and redo")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "other", "HEAD~2"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // The panel's + button; the path is prefilled next to the main worktree. Cancel changes nothing.
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    GG_CHECK(gg::worktrees::samePath(s.app.dialogs().current()->text("path"), s.root() / "linear-worktree"));
    GG_CHECK(!s.itemExists("//Add worktree/##reason")); // shown only with Lock
    s.dialogButton("Add worktree", "Cancel");
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK_EQ(gg::worktrees::parse(s.gitMayFail(repo, {"worktree", "list", "--porcelain", "-z"}).out).size(), 1u);

    // A new branch at HEAD~1, locked with a reason, in a directory whose name has spaces.
    const fs::path spaced = s.root() / "new work tree";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, spaced, nullptr, "feature");
    s.dialogText("Add worktree", "start", "HEAD~1");
    s.dialogCheck("Add worktree", "lock", "Lock the new worktree (--lock)");
    s.dialogText("Add worktree", "reason", "on a usb stick");
    s.dialogButton("Add worktree", "Add");
    GG_REQUIRE(s.waitUntil([&] { return registered(s, repo, spaced).has_value(); }));
    s.settle();
    s.track(spaced);
    const std::string start = s.revParse(repo, "HEAD~1");
    auto e = registered(s, repo, spaced);
    GG_CHECK_STR_EQ(e->branch, "refs/heads/feature");
    GG_CHECK(e->locked);
    GG_CHECK_STR_EQ(e->lockReason, "on a usb stick");
    GG_CHECK_STR_EQ(s.revParse(repo, "feature"), start);
    GG_CHECK(fs::exists(spaced / "f4.txt") && !fs::exists(spaced / "f5.txt"));
    // The panel: git's id for the directory, its branch, the lock and the path.
    GG_REQUIRE(s.waitUntil([&] { return shown(s, "new-work-tree") != nullptr; }));
    GG_CHECK(shown(s, "new-work-tree")->locked && shown(s, "new-work-tree")->branch == "feature");
    GG_CHECK(s.itemExists(wtRow("new-work-tree").c_str()));
    GG_CHECK(gg::worktrees::samePath(shown(s, "new-work-tree")->path, spaced));
    // One journal operation with the worktree it added.
    const auto& ops = s.session()->operations();
    GG_REQUIRE(!ops.empty());
    GG_CHECK_STR_EQ(ops.back().label, "add worktree new work tree");
    GG_CHECK(ops.back().worktrees.size() == 1u && ops.back().worktrees[0].action == "add");

    // Undo removes it again (unlocking it first) and deletes the branch it created.
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, spaced) && !fs::exists(spaced); }));
    s.settle();
    GG_CHECK(!refExists(s, repo, "refs/heads/feature"));
    GG_CHECK(shown(s, "new-work-tree") == nullptr);
    // Redo: the branch first, then the worktree on it, locked with the same reason.
    redoKey(s);
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, spaced).has_value(); }));
    s.settle();
    e = registered(s, repo, spaced);
    GG_CHECK(e && e->branch == "refs/heads/feature" && e->locked && e->lockReason == "on a usb stick");
    GG_CHECK_STR_EQ(s.revParse(repo, "feature"), start);
    GG_CHECK(fs::exists(spaced / "f4.txt"));

    // A detached commit, from the row menu's Add...
    const fs::path detached = s.root() / "detached";
    s.contextMenu(wtRow("main").c_str(), "Add...");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, detached, "A commit (detached HEAD)", "HEAD~3");
    GG_CHECK(!s.itemExists("//Add worktree/##branch"));
    s.dialogButton("Add worktree", "Add");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, detached).has_value(); }));
    s.settle();
    s.track(detached);
    e = registered(s, repo, detached);
    GG_CHECK(e && e->detached && e->branch.empty());
    GG_CHECK_STR_EQ(s.head(detached), s.revParse(repo, "HEAD~3"));
    GG_CHECK(s.waitUntil([&] { return shown(s, "detached") && shown(s, "detached")->branch.empty(); }));

    // Branches ▸ Check out in new worktree...: the dialog comes with that branch chosen.
    s.showPanel("Branches");
    s.contextMenu("//Branches/branch_other/###branch_other", "Check out in new worktree...");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    const ggui::Form* form = s.app.dialogs().current();
    GG_CHECK_EQ(form->choice("checkout"), 1);
    GG_CHECK_STR_EQ(form->field("existing")->options.at(static_cast<size_t>(form->choice("existing"))), "other");
    GG_CHECK(gg::worktrees::samePath(form->text("path"), s.root() / "linear-other"));
    s.dialogButton("Add worktree", "Add");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, s.root() / "linear-other").has_value(); }));
    s.settle();
    s.track(s.root() / "linear-other");
    GG_CHECK_STR_EQ(s.gitOut(s.root() / "linear-other", {"symbolic-ref", "HEAD"}), "refs/heads/other");
    // A branch checked out somewhere cannot be checked out in another worktree from here.
    s.showPanel("Branches");
    ctx->ItemClick("//Branches/branch_main/###branch_main", ImGuiMouseButton_Right);
    GG_CHECK(disabled(s, "//$FOCUSED/Check out in new worktree..."));
    ctx->KeyPress(ImGuiKey_Escape);
}

GG_TEST("worktrees", "add: an existing branch without checkout (Undo refuses until it is clean), a checked-out branch needs force, the filter")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"branch", "side", "HEAD~2"});
    s.git(repo, {"branch", "spare", "HEAD~1"});
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // An existing branch, files not checked out (--no-checkout).
    const fs::path sideWt = s.root() / "side-wt";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, sideWt, "An existing branch", "side");
    GG_CHECK(!s.itemExists("//Add worktree/##start"));
    s.dialogCheck("Add worktree", "no_checkout", "Do not check out the files (--no-checkout)");
    s.dialogButton("Add worktree", "Add");
    GG_REQUIRE(s.waitUntil([&] { return registered(s, repo, sideWt).has_value(); }));
    s.settle();
    s.track(sideWt);
    GG_CHECK_STR_EQ(registered(s, repo, sideWt)->branch, "refs/heads/side");
    GG_CHECK(!fs::exists(sideWt / "f1.txt"));
    // Its files are not checked out, so git sees changes: Undo refuses and changes nothing.
    undoKey(s);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("has uncommitted changes") != std::string::npos);
    s.settle();
    GG_CHECK(registered(s, repo, sideWt).has_value());
    // Checked out now: Undo removes it; the branch it did not create stays.
    s.git(sideWt, {"reset", "-q", "--hard"});
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, sideWt) && !fs::exists(sideWt); }));
    s.settle();
    GG_CHECK_STR_EQ(s.revParse(repo, "side"), s.revParse(repo, "HEAD~2"));

    // main is checked out in the main worktree: git refuses without --force ...
    const fs::path again = s.root() / "main-again";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, again, "An existing branch", "main");
    s.dialogButton("Add worktree", "Add");
    GG_CHECK(s.dismissError());
    s.settle();
    GG_CHECK(!registered(s, repo, again));
    // ... and allows it with Force.
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, again, "An existing branch", "main");
    s.dialogCheck("Add worktree", "force", "Force, even if the branch is checked out elsewhere (--force)");
    s.dialogButton("Add worktree", "Add");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, again).has_value(); }));
    s.settle();
    s.track(again);
    GG_CHECK_STR_EQ(registered(s, repo, again)->branch, "refs/heads/main");

    // The branch list has a filter: typing narrows it, Enter picks the first match.
    const fs::path spareWt = s.root() / "spare-wt";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    s.dialogText("Add worktree", "path", spareWt.string());
    s.comboSelect("//Add worktree/Check out##checkout", "An existing branch");
    ctx->Yield(2);
    ctx->ItemClick("//Add worktree/Branch##existing");
    ctx->Yield(2);
    ctx->KeyCharsAppend("spa");
    ctx->Yield(2);
    GG_CHECK(ctx->ItemExists("//$FOCUSED/spare"));
    GG_CHECK(!ctx->ItemExists("//$FOCUSED/side"));
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(2);
    s.dialogButton("Add worktree", "Add");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, spareWt).has_value(); }));
    s.settle();
    s.track(spareWt);
    GG_CHECK_STR_EQ(registered(s, repo, spareWt)->branch, "refs/heads/spare");
}

GG_TEST("worktrees", "remove: with changes (asks, force), locked, missing; the main worktree refused; undo re-creates")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const std::string n1 = repo.filename().string() + "-wt1";
    const std::string n2 = repo.filename().string() + "-wt2";
    const std::string n3 = repo.filename().string() + "-wt3";
    const fs::path wt1 = s.root() / n1;
    const fs::path wt2 = s.root() / n2;
    const fs::path wt3 = s.root() / n3;
    s.track(wt2);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // The main worktree cannot be removed or locked (git refuses both).
    ctx->ItemClick(wtRow("main").c_str(), ImGuiMouseButton_Right);
    GG_CHECK(disabled(s, "//$FOCUSED/Remove..."));
    GG_CHECK(disabled(s, "//$FOCUSED/Lock..."));
    GG_CHECK(disabled(s, "//$FOCUSED/Open here")); // (this window shows it)
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);

    // wt1 has an untracked file: Remove asks before deleting it. Cancel at either step keeps it.
    s.write(wt1, "scratch.txt", "not committed\n");
    s.contextMenu(wtRow(n1).c_str(), "Remove...");
    GG_REQUIRE(s.dialogOpen("Remove worktree"));
    s.dialogButton("Remove worktree", "Cancel");
    s.contextMenu(wtRow(n1).c_str(), "Remove...");
    GG_REQUIRE(s.dialogOpen("Remove worktree"));
    s.dialogButton("Remove worktree", "Remove");
    GG_REQUIRE(s.dialogOpen("Remove worktree with changes"));
    GG_CHECK(s.app.dialogs().current()->message.find("contains modified or untracked files") != std::string::npos);
    s.dialogButton("Remove worktree with changes", "Cancel");
    s.settle();
    GG_CHECK(registered(s, repo, wt1).has_value() && fs::exists(wt1 / "scratch.txt"));
    s.contextMenu(wtRow(n1).c_str(), "Remove...");
    GG_REQUIRE(s.dialogOpen("Remove worktree"));
    s.dialogButton("Remove worktree", "Remove");
    GG_REQUIRE(s.dialogOpen("Remove worktree with changes"));
    s.dialogButton("Remove worktree with changes", "Delete changes and remove");
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, wt1) && !fs::exists(wt1); }));
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/wt1")); // the branch stays
    GG_CHECK(shown(s, n1) == nullptr);
    GG_CHECK_STR_EQ(s.session()->operations().back().label, "remove worktree " + n1 + " with its changes");
    // Undo re-creates it on its branch, without the deleted file.
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, wt1).has_value(); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(wt1, {"symbolic-ref", "HEAD"}), "refs/heads/wt1");
    GG_CHECK(fs::exists(wt1 / "wt1.txt") && !fs::exists(wt1 / "scratch.txt"));
    GG_CHECK(s.waitUntil([&] { return shown(s, n1) != nullptr; }));

    // A locked worktree: the dialog says so; removing unlocks it first. Undo locks it again.
    s.contextMenu(wtRow(n2).c_str(), "Remove...");
    GG_REQUIRE(s.dialogOpen("Remove worktree"));
    GG_CHECK(s.app.dialogs().current()->message.find("It is locked (test lock)") != std::string::npos);
    s.dialogButton("Remove worktree", "Remove");
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, wt2) && !fs::exists(wt2); }));
    s.settle();
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, wt2).has_value(); }));
    s.settle();
    auto e = registered(s, repo, wt2);
    GG_CHECK(e && e->locked && e->lockReason == "test lock" && e->branch == "refs/heads/wt2");

    // A worktree whose directory is gone: Remove drops git's records of it.
    GG_REQUIRE(shown(s, n3) != nullptr);
    GG_CHECK(shown(s, n3)->missing && shown(s, n3)->prunable);
    s.contextMenu(wtRow(n3).c_str(), "Remove...");
    GG_REQUIRE(s.dialogOpen("Remove worktree"));
    GG_CHECK(s.app.dialogs().current()->message.find("gone already") != std::string::npos);
    s.dialogButton("Remove worktree", "Remove");
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, wt3) && !fs::exists(repo / ".git" / "worktrees" / n3); }));
    s.settle();
    GG_CHECK(s.waitUntil([&] { return shown(s, n3) == nullptr; }));
}

GG_TEST("worktrees", "lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const std::string n1 = repo.filename().string() + "-wt1";
    const std::string n2 = repo.filename().string() + "-wt2";
    const std::string n3 = repo.filename().string() + "-wt3";
    const fs::path wt1 = s.root() / n1;
    const fs::path wt2 = s.root() / n2;
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // Lock: Cancel changes nothing; Lock with a reason.
    s.contextMenu(wtRow(n1).c_str(), "Lock...");
    GG_REQUIRE(s.dialogOpen("Lock worktree"));
    s.dialogButton("Lock worktree", "Cancel");
    s.settle();
    GG_CHECK(!registered(s, repo, wt1)->locked);
    s.contextMenu(wtRow(n1).c_str(), "Lock...");
    GG_REQUIRE(s.dialogOpen("Lock worktree"));
    s.dialogText("Lock worktree", "reason", "portable disk");
    s.dialogButton("Lock worktree", "Lock");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, wt1)->locked; }));
    s.settle();
    GG_CHECK_STR_EQ(registered(s, repo, wt1)->lockReason, "portable disk");
    GG_CHECK(s.waitUntil([&] { return shown(s, n1) && shown(s, n1)->locked && shown(s, n1)->lockReason == "portable disk"; }));
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, wt1)->locked; }));
    s.settle();
    redoKey(s);
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, wt1)->locked; }));
    s.settle();
    GG_CHECK_STR_EQ(registered(s, repo, wt1)->lockReason, "portable disk");
    // Unlock (the menu item follows the state); Undo locks it again with its reason.
    s.contextMenu(wtRow(n1).c_str(), "Unlock");
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, wt1)->locked; }));
    s.settle();
    GG_CHECK(s.waitUntil([&] { return shown(s, n1) && !shown(s, n1)->locked; }));
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, wt1)->locked; }));
    s.settle();
    GG_CHECK_STR_EQ(registered(s, repo, wt1)->lockReason, "portable disk");

    // Prune: git's dry run first (wt3's directory is gone, it is not locked). Cancel keeps it.
    const fs::path admin3 = repo / ".git" / "worktrees" / n3;
    s.contextMenu(wtRow("main").c_str(), "Prune...");
    GG_REQUIRE(s.dialogOpen("Prune worktrees"));
    GG_CHECK(s.app.dialogs().current()->message.find("worktrees/" + n3) != std::string::npos);
    GG_CHECK(s.app.dialogs().current()->message.find(n2) == std::string::npos);
    s.dialogButton("Prune worktrees", "Cancel");
    s.settle();
    GG_CHECK(fs::exists(admin3));
    s.contextMenu(wtRow("main").c_str(), "Prune...");
    GG_REQUIRE(s.dialogOpen("Prune worktrees"));
    s.dialogButton("Prune worktrees", "Prune");
    GG_CHECK(s.waitUntil([&] { return !fs::exists(admin3); }));
    s.settle();
    GG_CHECK(toastShown(s, "Prune worktrees", n3));
    GG_CHECK(s.waitUntil([&] { return shown(s, n3) == nullptr; }));
    // Journaled, but there is nothing Undo could restore.
    GG_CHECK_STR_EQ(s.session()->operations().back().label, "prune worktrees");
    GG_CHECK(!s.session()->operations().back().restorable());

    // wt2 (locked) moved by hand into a directory with spaces: missing, but git keeps it.
    const fs::path moved = s.root() / "moved wt2";
    fs::rename(wt2, moved);
    s.track(moved);
    ctx->KeyPress(ImGuiKey_F5);
    GG_CHECK(s.waitUntil([&] { return shown(s, n2) && shown(s, n2)->missing; }));
    GG_CHECK(!shown(s, n2)->prunable);
    GG_CHECK(s.textShown("//Worktrees", "(missing)"));
    s.contextMenu(wtRow("main").c_str(), "Prune...");
    GG_CHECK(s.waitUntil([&] { return toastShown(s, "Prune worktrees", "Nothing to prune"); }));
    GG_CHECK(s.app.dialogs().current() == nullptr);
    s.settle();
    GG_CHECK(fs::exists(repo / ".git" / "worktrees" / n2));
    // Repair with its new location. Cancel first.
    s.contextMenu(wtRow(n2).c_str(), "Repair...");
    GG_REQUIRE(s.dialogOpen("Repair worktree"));
    s.dialogButton("Repair worktree", "Cancel");
    s.contextMenu(wtRow(n2).c_str(), "Repair...");
    GG_REQUIRE(s.dialogOpen("Repair worktree"));
    GG_CHECK(gg::worktrees::samePath(s.app.dialogs().current()->text("path"), wt2));
    s.dialogText("Repair worktree", "path", moved.string());
    s.dialogButton("Repair worktree", "Repair");
    GG_CHECK(s.waitUntil([&] { return registered(s, repo, moved).has_value(); }));
    s.settle();
    GG_CHECK(toastShown(s, "Repair worktree", "gitdir"));
    GG_CHECK(s.waitUntil([&] { return shown(s, n2) && !shown(s, n2)->missing; }));
    GG_CHECK_STR_EQ(s.gitOut(moved, {"symbolic-ref", "HEAD"}), "refs/heads/wt2");
    GG_CHECK(registered(s, repo, moved)->locked);
    GG_CHECK_STR_EQ(s.session()->operations().back().label, "repair worktree " + moved.filename().string());
    GG_CHECK(!s.session()->operations().back().restorable());
}

GG_TEST("worktrees", "open here switches this window; open in new window starts a detached ggui on the worktree; a missing program is an error")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const std::string n1 = repo.filename().string() + "-wt1";
    const std::string n3 = repo.filename().string() + "-wt3";
    const fs::path wt1 = s.root() / n1;
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // Open in new window: GG_GGUI names the program. The stand-in logs its arguments and whether
    // it runs in a session of its own (detached from this ggui; Windows has no sessions to check).
    const fs::path log = s.root() / "ggui-stub.log";
#ifdef _WIN32
    const size_t stubLines = 1;
    s.fakeTool("ggui-stub");
#else
    const size_t stubLines = 2;
    s.fakeTool("ggui-stub", "sid=$(cut -d' ' -f6 /proc/$$/stat)\n[ \"$sid\" = \"$$\" ] && echo detached >> '"
            + log.string() + "'\n");
#endif
    ggui::setEnv("GG_GGUI", s.toolPath("ggui-stub").string());
    s.contextMenu(wtRow(n1).c_str(), "Open in new window");
    GG_CHECK(s.waitUntil([&] {
        const auto lines = gg::splitLines(s.read(s.root(), "ggui-stub.log"));
        return lines.size() == stubLines && gg::worktrees::samePath(lines[0], wt1)
            && (stubLines == 1 || lines[1] == "detached");
    }));
    GG_CHECK(s.waitUntil([&] { return toastShown(s, "Open in new window", n1); }));
    // A missing worktree cannot be opened.
    ctx->ItemClick(wtRow(n3).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(disabled(s, "//$FOCUSED/Open in new window"));
    GG_CHECK(disabled(s, "//$FOCUSED/Open here"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    // A program that does not exist: an error, nothing started.
    ggui::setEnv("GG_GGUI", (s.root() / "no-such-ggui").string());
    s.contextMenu(wtRow("main").c_str(), "Open in new window");
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("not found") != std::string::npos);
    GG_CHECK_EQ(gg::splitLines(s.read(s.root(), "ggui-stub.log")).size(), stubLines);

    // Open here: this window shows wt1 now (the checks below need its session).
    s.contextMenu(wtRow(n1).c_str(), "Open here");
    GG_REQUIRE(s.waitUntil([&] {
        auto* session = s.session();
        return session && session->opened() && gg::worktrees::samePath(session->path(), wt1);
    }));
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->worktreeId, n1);
    s.showPanel("Worktrees");
    GG_CHECK(s.waitUntil([&] { return shown(s, n1) && shown(s, n1)->isCurrent; }));
    // The worktree this window shows cannot be removed or opened here again.
    ctx->ItemClick(wtRow(n1).c_str(), ImGuiMouseButton_Right);
    GG_CHECK(disabled(s, "//$FOCUSED/Remove..."));
    GG_CHECK(disabled(s, "//$FOCUSED/Open here"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    // And back to the main worktree.
    s.contextMenu(wtRow("main").c_str(), "Open here");
    GG_REQUIRE(s.waitUntil([&] {
        auto* session = s.session();
        return session && session->opened() && gg::worktrees::samePath(session->path(), repo);
    }));
    s.settle();
    GG_CHECK_STR_EQ(s.session()->snapshot()->worktreeId, "main");
}

GG_TEST("worktrees", "per-worktree journal: a worktree change is undone only from the worktree that made it; refused when it moved on or has changes; git gg undo and redo agree")
{
    const fs::path repo = s.fixture(Recipe::LinkedWorktrees);
    const std::string n1 = repo.filename().string() + "-wt1";
    const fs::path wt1 = s.root() / n1;
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    // From the main worktree: a detached worktree (no shared ref changes).
    const fs::path scratch = s.root() / "scratch";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, scratch, "A commit (detached HEAD)", "HEAD");
    s.dialogButton("Add worktree", "Add");
    GG_REQUIRE(s.waitUntil([&] { return registered(s, repo, scratch).has_value(); }));
    s.settle();
    s.track(scratch);
    const std::string at = s.head(scratch);
    // In wt1's window it is not wt1's to undo.
    s.contextMenu(wtRow(n1).c_str(), "Open here");
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->opened() && gg::worktrees::samePath(s.session()->path(), wt1); }));
    s.settle();
    undoKey(s);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("Nothing to undo") != std::string::npos);
    s.settle();
    GG_CHECK(registered(s, repo, scratch).has_value());
    const auto fromWt1 = s.gitgg(wt1, {"undo"});
    GG_CHECK(!fromWt1.ok());
    GG_CHECK(registered(s, repo, scratch).has_value());

    // Back in the main worktree's window.
    s.showPanel("Worktrees");
    s.contextMenu(wtRow("main").c_str(), "Open here");
    GG_REQUIRE(s.waitUntil([&] { return s.session() && s.session()->opened() && gg::worktrees::samePath(s.session()->path(), repo); }));
    s.settle();
    // A commit made in it since: Undo would lose it, so it refuses.
    s.commitFile(scratch, "s.txt", "s\n", "In scratch");
    undoKey(s);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("has moved on") != std::string::npos);
    s.settle();
    GG_CHECK(registered(s, repo, scratch).has_value());
    // Back at its commit but with an untracked file: refused too.
    s.git(scratch, {"reset", "-q", "--hard", at});
    s.write(scratch, "untracked.txt", "x\n");
    undoKey(s);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("untracked files") != std::string::npos);
    s.settle();
    GG_CHECK(registered(s, repo, scratch).has_value());
    // Clean: Undo removes it.
    fs::remove(scratch / "untracked.txt");
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, scratch) && !fs::exists(scratch); }));
    s.settle();
    // git gg redo in a terminal of the main worktree brings it back, at the same commit.
    const auto redo = s.gitgg(repo, {"redo"});
    GG_CHECK(redo.ok());
    GG_CHECK(registered(s, repo, scratch).has_value());
    GG_CHECK_STR_EQ(s.head(scratch), at);
    GG_CHECK(s.waitUntil([&] { return shown(s, "scratch") != nullptr; }));
}

GG_TEST("worktrees", "Add is one operation Undo reverts; a plain git worktree add in a terminal does not move the main worktree's HEAD, and Undo leaves its branch alone")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_REQUIRE(s.openRepository(repo));
    s.showPanel("Worktrees");
    const fs::path hooked = s.root() / "hooked";
    ctx->ItemClick("//Worktrees/###add_worktree");
    GG_REQUIRE(s.dialogOpen("Add worktree"));
    addDialog(s, hooked, nullptr, "hooked");
    s.dialogButton("Add worktree", "Add");
    GG_REQUIRE(s.waitUntil([&] { return registered(s, repo, hooked).has_value(); }));
    s.settle();
    s.track(hooked);
    // The new branch joined ggui's operation, and no HEAD change for the main worktree.
    const auto& ops = s.session()->operations();
    GG_REQUIRE(!ops.empty());
    GG_CHECK_STR_EQ(ops.back().label, "add worktree hooked");
    for (const auto& r : ops.back().refs)
        GG_CHECK(r.ref != "HEAD");
    GG_CHECK(ops.back().src == "ggui");
    undoKey(s);
    GG_CHECK(s.waitUntil([&] { return !registered(s, repo, hooked) && !refExists(s, repo, "refs/heads/hooked"); }));
    s.settle();
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "HEAD"}), "refs/heads/main");

    // The same from a terminal: the reconciler sees the new branch; the new worktree's HEAD is not
    // the main worktree's.
    const fs::path term = s.root() / "term";
    s.git(repo, {"worktree", "add", "-q", "-b", "term", term.string()});
    s.track(term);
    GG_REQUIRE(s.waitUntil([&] { return s.read(repo, ".git/gg/journal").find("\"refs/heads/term\"") != std::string::npos; }));
    const std::string journal = s.read(repo, ".git/gg/journal");
    GG_CHECK(journal.find("\"refs/heads/term\"") != std::string::npos);
    GG_CHECK(journal.find("\"HEAD\",\"ref:refs/heads/main\",\"ref:refs/heads/term\"") == std::string::npos);
    // Undo would delete the branch the new worktree has checked out: refused.
    undoKey(s);
    GG_CHECK(s.dismissError());
    GG_CHECK(s.app.errorMessage().find("is checked out in") != std::string::npos);
    s.settle();
    GG_CHECK(refExists(s, repo, "refs/heads/term"));
    GG_CHECK(registered(s, repo, term).has_value());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"symbolic-ref", "HEAD"}), "refs/heads/main");
}

} // namespace ggtest
