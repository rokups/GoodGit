// Worktree dialogs (docs/spec/ui-spec.md §8 Worktrees, §9; REBUILD_PLAN §4.7; P4-03).
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/Session.hpp"

#include <libgg/GitRunner.hpp>

#include <algorithm>

namespace ggui {

namespace fs = std::filesystem;

namespace {

std::string describeHead(const core::WorktreeInfo& w)
{
    if (!w.branch.empty())
        return "branch " + w.branch;
    return w.head.isNull() ? std::string("no commit") : "detached at " + w.head.shortHex(10);
}

// A free directory next to the main worktree: <main>-<suffix>, <main>-<suffix>-2, …
std::string defaultWorktreePath(const core::Snapshot& snap, std::string suffix)
{
    fs::path main = snap.worktrees.empty() ? snap.workdir : snap.worktrees.front().path;
    main = main.lexically_normal();
    if (!main.has_filename())
        main = main.parent_path();
    std::string name = main.filename().string();
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".git") == 0)
        name.resize(name.size() - 4);
    std::replace(suffix.begin(), suffix.end(), '/', '-');
    const fs::path base = main.parent_path() / (name + "-" + suffix);
    fs::path candidate = base;
    std::error_code ec;
    for (int i = 2; i < 100 && fs::exists(candidate, ec); ++i)
        candidate = fs::path(base.string() + "-" + std::to_string(i));
    return candidate.string();
}

} // namespace

void Session::showAddWorktreeDialog(int mode, const std::string& preset)
{
    Form f;
    f.title = "Add worktree";
    std::vector<std::string> branches;
    int existing = 0;
    for (const auto& b : m_snapshot->branches) {
        if (b.name == preset)
            existing = static_cast<int>(branches.size());
        branches.push_back(b.name);
    }
    const std::string suffix = mode == 1 && !preset.empty() ? preset : mode == 2 ? std::string("detached") : "worktree";
    f.add(Field{Field::Text, "path", "Path", defaultWorktreePath(*m_snapshot, suffix), false, 0, {},
        "absolute, or relative to this worktree"});
    Field how{Field::Combo, "checkout", "Check out"};
    how.options = {"A new branch", "An existing branch", "A commit (detached HEAD)"};
    how.choice = std::clamp(mode, 0, 2);
    f.add(how);
    Field name{Field::Text, "branch", "New branch"};
    name.visible = [](const Form& form) { return form.choice("checkout") == 0; };
    f.add(name);
    Field pick{Field::Combo, "existing", "Branch"};
    pick.options = branches;
    pick.choice = existing;
    pick.filterable = true;
    pick.visible = [](const Form& form) { return form.choice("checkout") == 1; };
    f.add(pick);
    Field start{Field::Text, "start", "Start at", mode == 2 && !preset.empty() ? preset : std::string("HEAD")};
    start.visible = [](const Form& form) { return form.choice("checkout") != 1; };
    f.add(start);
    f.add(Field{Field::Check, "force", "Force, even if the branch is checked out elsewhere (--force)"});
    f.add(Field{Field::Check, "no_checkout", "Do not check out the files (--no-checkout)"});
    f.add(Field{Field::Check, "lock", "Lock the new worktree (--lock)"});
    Field reason{Field::Text, "reason", "Lock reason", "", false, 0, {}, "optional"};
    reason.visible = [](const Form& form) { return form.checked("lock"); };
    f.add(reason);
    f.add(Field{Field::Info, "undo_note", "", "Undo removes the worktree again (and a branch it created) while it has no changes."});
    const fs::path here = m_snapshot->workdir.empty() ? m_snapshot->gitDir : m_snapshot->workdir;
    f.buttons.push_back({"Add",
        [this, here, branches](Form& form) {
            Actions::AddWorktree req;
            fs::path path = fs::path(gg::trim(form.text("path")));
            if (path.is_relative())
                path = here / path;
            req.path = path.lexically_normal().string();
            req.mode = static_cast<Actions::AddWorktree::Mode>(form.choice("checkout"));
            if (req.mode == Actions::AddWorktree::Mode::NewBranch)
                req.branch = gg::trim(form.text("branch"));
            else if (req.mode == Actions::AddWorktree::Mode::ExistingBranch)
                req.branch = branches.at(static_cast<size_t>(form.choice("existing")));
            req.start = gg::trim(form.text("start"));
            req.force = form.checked("force");
            req.checkout = !form.checked("no_checkout");
            req.lock = form.checked("lock");
            req.reason = gg::trim(form.text("reason"));
            m_actions->addWorktree(std::move(req));
        },
        [branches](const Form& form) {
            if (gg::trim(form.text("path")).empty())
                return false;
            switch (form.choice("checkout")) {
            case 0: return !gg::trim(form.text("branch")).empty() && !gg::trim(form.text("start")).empty();
            case 1: return !branches.empty();
            default: return !gg::trim(form.text("start")).empty();
            }
        }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showRemoveWorktreeDialog(const core::WorktreeInfo& w)
{
    Form f;
    f.title = "Remove worktree";
    const std::string path = w.path.string();
    f.message = "Remove the worktree " + w.name + " (" + describeHead(w) + ")?\n" + path;
    if (w.missing)
        f.message += "\n\nIts directory is gone already: this removes git's records of it.";
    else
        f.message += "\n\nIts directory is deleted; the branch and commits stay.";
    if (w.locked)
        f.message += "\n\nIt is locked" + (w.lockReason.empty() ? std::string() : " (" + w.lockReason + ")")
            + ": removing unlocks it first.";
    f.message += "\n\nUndo re-creates it at the same branch or commit (files git ignores do not come back).";
    f.buttons.push_back({"Remove", [this, path](Form&) { m_actions->removeWorktree(path, false); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showLockWorktreeDialog(const core::WorktreeInfo& w)
{
    Form f;
    f.title = "Lock worktree";
    f.message = "Git does not prune, move or remove a locked worktree (for example one on a removable drive).\n"
        + w.path.string();
    f.add(Field{Field::Text, "reason", "Reason", "", false, 0, {}, "optional"});
    const std::string path = w.path.string();
    f.buttons.push_back({"Lock", [this, path](Form& form) { m_actions->lockWorktree(path, gg::trim(form.text("reason"))); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showPruneWorktreesDialog()
{
    // What git would prune first (--dry-run), then ask.
    m_actions->previewPruneWorktrees([this](const core::MutationFinishedEvent& e) {
        if (e.outcome != core::Outcome::Ok) {
            m_app.showError(e.label, e.detail.empty() ? e.message : e.detail);
            return;
        }
        if (e.result.empty()) {
            m_app.notify(App::Notice::Info, "Prune worktrees",
                "Nothing to prune: every worktree's directory exists, or it is locked.");
            return;
        }
        Form f;
        f.title = "Prune worktrees";
        f.message = "git worktree prune removes the records of worktrees whose directories are gone:\n\n" + e.result
            + "\n\nPruning cannot be undone.";
        f.buttons.push_back({"Prune", [this](Form&) { m_actions->pruneWorktrees(); }});
        f.buttons.push_back({"Cancel", {}});
        m_app.dialogs().open(std::move(f));
    });
}

void Session::showRepairWorktreeDialog(const core::WorktreeInfo& w)
{
    Form f;
    f.title = "Repair worktree";
    f.message = "git worktree repair fixes the links between the repository and a worktree that was moved by "
                "hand. If this worktree was moved, enter its new location. Repair cannot be undone.";
    f.add(Field{Field::Text, "path", "Location", w.path.string()});
    const fs::path here = m_snapshot->workdir.empty() ? m_snapshot->gitDir : m_snapshot->workdir;
    f.buttons.push_back({"Repair",
        [this, here](Form& form) {
            fs::path path = fs::path(gg::trim(form.text("path")));
            if (path.is_relative())
                path = here / path;
            m_actions->repairWorktree(path.lexically_normal().string());
        },
        [](const Form& form) { return !gg::trim(form.text("path")).empty(); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

} // namespace ggui
