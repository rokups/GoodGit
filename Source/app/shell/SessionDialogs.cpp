// Dialogs shared by panels, menus and the toolbar (docs/spec/ui-spec.md §9).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Session.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>

#include <cctype>

namespace ggui {

namespace {

bool nonEmpty(const Form& f, const char* id) { return !gg::trim(f.text(id)).empty(); }

std::vector<std::string> remoteNames(const core::SnapshotPtr& snap)
{
    std::vector<std::string> names;
    for (const auto& r : snap->remotes)
        names.push_back(r.name);
    return names;
}

} // namespace

void Session::showCommitDialog(bool amend)
{
    Form f;
    f.title = amend ? "Amend" : "Commit";
    const bool nothingStaged = !amend && m_status && m_status->staged.empty();
    Field msg;
    msg.kind = Field::Multiline;
    msg.id = "message";
    msg.label = amend ? "Message (leave empty to keep the current message)" : "Message";
    if (amend && m_info->details() && m_info->details()->id == m_snapshot->head)
        msg.text = m_info->details()->message;
    // The message waiting in MERGE_MSG (a merge, revert or cherry-pick), as git commit would
    // take it: without git's comment lines and trailing blank lines.
    if (!amend && !m_snapshot->mergeMessage.empty()) {
        for (const auto& line : gg::splitLines(m_snapshot->mergeMessage))
            if (line.rfind('#', 0) != 0)
                msg.text += line + "\n";
        while (!msg.text.empty() && std::isspace(static_cast<unsigned char>(msg.text.back())))
            msg.text.pop_back();
    }
    f.add(msg);
    f.add(Field{Field::Check, "skip_hooks", "Skip hooks (--no-verify)"});
    if (amend)
        f.add(Field{Field::Check, "message_only", "Change the message only (keep the index out)"});
    const NothingStaged pref = m_app.settings().data().nothingStaged;
    if (nothingStaged && pref == NothingStaged::Ask) {
        Field mode;
        mode.kind = Field::Combo;
        mode.id = "nothing_staged";
        mode.label = "Nothing is staged";
        mode.options = {"Stage all tracked changes and commit (-a)", "Stage the selected files and commit"};
        f.add(mode);
    }
    const std::vector<std::string> selected = selectedPaths();
    f.buttons.push_back({amend ? "Amend" : "Commit",
        [this, amend, nothingStaged, pref, selected](Form& form) {
            const std::string message = form.text("message");
            const bool noVerify = form.checked("skip_hooks");
            if (amend) {
                m_actions->amend(message, noVerify, form.checked("message_only"));
                return;
            }
            CommitMode mode = CommitMode::Index;
            if (nothingStaged) {
                const int choice = pref == NothingStaged::Ask ? form.choice("nothing_staged")
                    : pref == NothingStaged::StageSelected    ? 1
                                                              : 0;
                mode = choice == 1 ? CommitMode::StageSelected : CommitMode::StageAllTracked;
            }
            m_actions->commit(message, noVerify, mode, selected);
        },
        [amend](const Form& form) { return amend || nonEmpty(form, "message"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showStashDialog(std::vector<std::string> paths)
{
    Form f;
    f.title = "Stash changes";
    f.add(Field{Field::Text, "message", "Message", "", false, 0, {}, "optional"});
    f.add(Field{Field::Check, "keep_index", "Keep the index (--keep-index)"});
    f.add(Field{Field::Check, "untracked", "Include untracked files"});
    f.add(Field{Field::Check, "staged_only", "Staged changes only (--staged)"});
    Field only{Field::Check, "selected_only", "Selected files only"};
    if (paths.empty())
        paths = selectedPaths();
    if (!paths.empty())
        f.add(only);
    f.buttons.push_back({"Stash", [this, paths](Form& form) {
                             m_actions->stashPush(form.text("message"), form.checked("keep_index"), form.checked("untracked"),
                                 form.checked("staged_only"), form.checked("selected_only") ? paths : std::vector<std::string>{});
                         }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showPushToDialog(const std::string& branch)
{
    const std::string local = branch.empty() ? m_snapshot->headBranch : branch;
    const auto remotes = remoteNames(m_snapshot);
    if (remotes.empty()) {
        m_app.notify(App::Notice::Warning, "Push", "This repository has no remotes. Add one in the Remotes panel.");
        return;
    }
    Form f;
    f.title = "Push to";
    Field remote{Field::Combo, "remote", "Remote"};
    remote.options = remotes;
    const auto* b = m_snapshot->findBranch(local);
    std::string target = local;
    if (b && !b->upstream.empty()) {
        const auto slash = b->upstream.find('/');
        for (size_t i = 0; i < remotes.size(); ++i)
            if (remotes[i] == b->upstream.substr(0, slash))
                remote.choice = static_cast<int>(i);
        target = b->upstream.substr(slash + 1);
    }
    f.add(remote);
    f.add(Field{Field::Info, "local", "", "Local branch: " + local});
    f.add(Field{Field::Text, "branch", "Remote branch", target});
    Field upstream{Field::Check, "set_upstream", "Set as upstream (--set-upstream)"};
    upstream.checked = !b || b->upstream.empty();
    f.add(upstream);
    f.add(Field{Field::Check, "force", "Force with lease"});
    f.buttons.push_back({"Push",
        [this, local, remotes](Form& form) {
            m_actions->push(remotes[static_cast<size_t>(form.choice("remote"))], local, gg::trim(form.text("branch")),
                form.checked("set_upstream"), form.checked("force"));
        },
        [](const Form& form) { return nonEmpty(form, "branch"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showCreateBranchDialog(const std::string& at, const std::string& name)
{
    Form f;
    f.title = "Create branch";
    f.add(Field{Field::Text, "name", "Name", name});
    f.add(commitField(*this, "at", "At (branch, tag or commit)", at));
    Field checkout{Field::Check, "checkout", "Check out after creating"};
    checkout.checked = true;
    f.add(checkout);
    f.buttons.push_back({"Create",
        [this](Form& form) { m_actions->createBranch(gg::trim(form.text("name")), gg::trim(form.text("at")), form.checked("checkout")); },
        [](const Form& form) { return nonEmpty(form, "name") && nonEmpty(form, "at"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showBranchFromCommitDialog(const std::string& commit) { showCreateBranchDialog(commit); }

void Session::showCreateTagDialog(const std::string& at)
{
    Form f;
    f.title = "Create tag";
    f.add(Field{Field::Text, "name", "Name"});
    f.add(commitField(*this, "at", "At (branch, tag or commit)", at));
    f.add(Field{Field::Check, "annotated", "Annotated (with a message)"});
    f.add(Field{Field::Multiline, "message", "Message (annotated tags)"});
    f.buttons.push_back({"Create",
        [this](Form& form) {
            std::string message = form.checked("annotated") ? form.text("message") : std::string();
            if (form.checked("annotated") && gg::trim(message).empty())
                message = gg::trim(form.text("name"));
            m_actions->createTag(gg::trim(form.text("name")), gg::trim(form.text("at")), message);
        },
        [](const Form& form) { return nonEmpty(form, "name") && nonEmpty(form, "at"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showAddRemoteDialog()
{
    Form f;
    f.title = "Add remote";
    f.add(Field{Field::Text, "name", "Name", remoteNames(m_snapshot).empty() ? "origin" : ""});
    f.add(Field{Field::Text, "url", "URL"});
    f.buttons.push_back({"Add", [this](Form& form) { m_actions->addRemote(gg::trim(form.text("name")), gg::trim(form.text("url"))); },
        [](const Form& form) { return nonEmpty(form, "name") && nonEmpty(form, "url"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showEditRemoteDialog(const std::string& remote)
{
    std::string url;
    for (const auto& r : m_snapshot->remotes)
        if (r.name == remote)
            url = r.url;
    Form f;
    f.title = "Edit remote URL";
    f.add(Field{Field::Text, "url", "URL of " + remote, url});
    f.buttons.push_back({"Save", [this, remote](Form& form) { m_actions->setRemoteUrl(remote, gg::trim(form.text("url"))); },
        [](const Form& form) { return nonEmpty(form, "url"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDiscardDialog(std::vector<std::string> tracked, std::vector<std::string> untracked,
    std::vector<StagedDiscard> staged)
{
    Form f;
    f.title = "Discard changes";
    std::string list;
    for (const auto& p : tracked)
        list += "  " + p + "\n";
    for (const auto& p : untracked)
        list += "  " + p + " (untracked: deleted)\n";
    for (const auto& s : staged) {
        if (!s.remove)
            list += "  " + s.path + " (staged)\n";
        else if (!s.oldPath.empty())
            list += "  " + s.path + " (staged: deleted, " + s.oldPath + " restored)\n";
        else
            list += "  " + s.path + " (staged: deleted)\n";
    }
    f.message = "Discard the changes of these files? This cannot be undone.\n\n" + list;
    f.buttons.push_back({"Discard", [this, tracked, untracked, staged](Form&) { m_actions->discard(tracked, untracked, staged); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDiscardAllDialog()
{
    if (!m_status)
        return;
    std::vector<std::string> tracked, untracked;
    for (const auto& e : m_status->unstaged)
        if (!e.intentToAdd)
            tracked.push_back(e.path);
    for (const auto& e : m_status->staged)
        tracked.push_back(e.path);
    for (const auto& e : m_status->untracked)
        untracked.push_back(e.path);
    std::sort(tracked.begin(), tracked.end());
    tracked.erase(std::unique(tracked.begin(), tracked.end()), tracked.end());
    Form f;
    f.title = "Discard changes";
    f.message = "Discard all local changes (staged and unstaged) of tracked files? This cannot be undone.";
    Field untrackedField{Field::Check, "untracked", "Also delete untracked files (" + std::to_string(untracked.size()) + ")"};
    f.add(untrackedField);
    f.buttons.push_back({"Discard", [this, untracked](Form& form) {
                             m_actions->run("discard all changes", [untracked, all = form.checked("untracked")](core::MutationContext& ctx) {
                                 ctx.git({"reset", "-q", "--hard"});
                                 if (all && !untracked.empty())
                                     ctx.git(withPaths({"clean", "-f", "-q"}, untracked));
                                 ctx.worktreeFollowsIndex = true;
                             });
                         }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showApplyPatchDialog()
{
    Form f;
    f.title = "Apply patch";
    Field source{Field::Combo, "source", "Source"};
    source.options = {"Clipboard", "File"};
    f.add(source);
    f.add(Field{Field::Text, "file", "Patch file (for File)"});
    Field target{Field::Combo, "target", "Apply to"};
    target.options = {"Working tree", "Index (git apply --cached)"};
    f.add(target);
    f.buttons.push_back({"Apply", [this](Form& form) {
                             const bool cached = form.choice("target") == 1;
                             if (form.choice("source") == 0) {
                                 const char* text = ImGui::GetClipboardText();
                                 m_actions->applyPatch("apply patch", text ? text : "", cached, false);
                                 return;
                             }
                             const std::string file = gg::trim(form.text("file"));
                             m_actions->run("apply patch " + file, [file, cached](core::MutationContext& ctx) {
                                 std::vector<std::string> args{"apply", "--whitespace=nowarn"};
                                 if (cached)
                                     args.emplace_back("--cached");
                                 args.push_back(file);
                                 ctx.git(args);
                             });
                         }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showRenameBranchDialog(const std::string& branch)
{
    Form f;
    f.title = "Rename branch";
    f.add(Field{Field::Text, "name", "New name for " + branch, branch});
    f.buttons.push_back({"Rename", [this, branch](Form& form) { m_actions->renameBranch(branch, gg::trim(form.text("name"))); },
        [branch](const Form& form) { return nonEmpty(form, "name") && gg::trim(form.text("name")) != branch; }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDeleteBranchDialog(const std::string& branch, int mode)
{
    std::vector<std::string> remotes;
    if (mode > 0) {
        const auto* b = m_snapshot->findBranch(branch);
        for (const auto& r : m_snapshot->remoteBranches) {
            const std::string name = r.name.substr(r.remote.size() + 1);
            if (name == branch && (mode == 2 || (b && b->upstream == r.name)))
                remotes.push_back(r.remote);
        }
    }
    Form f;
    f.title = "Delete branch";
    std::string where = mode == 0 ? "the local branch" : mode == 1 ? "the branch on its remote" : "the local branch and every remote branch";
    f.message = "Delete " + where + " '" + branch + "'?";
    if (mode > 0 && remotes.empty())
        f.message += "\n\nNo remote branch with that name was found.";
    {
        // The commit that goes away: the local branch's tip, or (remote only) its upstream's.
        const auto* b = m_snapshot->findBranch(branch);
        const bool upstreamOk = b && !b->upstream.empty() && !b->upstreamGone;
        f.add(commitInfo(*this, mode == 1 ? "Remote branch" : "Branch", mode == 1 && upstreamOk ? b->upstream : branch));
        // `git branch -d` refuses unless the tip is merged into its upstream (else HEAD).
        if (mode != 1)
            f.add(commitInfo(*this, "Must be merged into (unless -D)", upstreamOk ? b->upstream : std::string("HEAD")));
    }
    if (mode != 1)
        f.add(Field{Field::Check, "force", "Delete even if not merged (-D)"});
    f.buttons.push_back({"Delete", [this, branch, remotes, mode](Form& form) {
                             m_actions->deleteBranch(branch, form.checked("force"), remotes, mode != 1);
                         }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDeleteRemoteBranchDialog(const std::string& remoteBranch)
{
    std::string remote;
    for (const auto& r : m_snapshot->remoteBranches)
        if (r.name == remoteBranch)
            remote = r.remote;
    if (remote.empty())
        return;
    const std::string branch = remoteBranch.substr(remote.size() + 1);
    Form f;
    f.title = "Delete branch";
    f.message = "Delete the branch '" + branch + "' on the remote '" + remote + "'?";
    f.add(commitInfo(*this, "Remote branch", remoteBranch));
    f.buttons.push_back({"Delete", [this, branch, remote](Form&) { m_actions->deleteBranch(branch, false, {remote}, false); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showMoveBranchDialog(const std::string& branch, const std::string& to)
{
    std::string elsewhere;
    if (const auto* b = m_snapshot->findBranch(branch))
        elsewhere = b->worktree;
    Form f;
    f.title = "Move branch";
    f.message = "Point the branch at another commit.";
    if (!elsewhere.empty())
        f.message += "\n\nWarning: '" + branch + "' is checked out in worktree '" + elsewhere
            + "'. Its working tree and index will not follow the branch.";
    f.add(commitInfo(*this, "Branch", branch));
    f.add(commitField(*this, "to", "Move to (branch, tag or commit)", to));
    f.buttons.push_back({"Move", [this, branch](Form& form) { m_actions->moveBranch(branch, gg::trim(form.text("to"))); },
        [](const Form& form) { return nonEmpty(form, "to"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showSetUpstreamDialog(const std::string& branch)
{
    Form f;
    f.title = "Set upstream";
    Field up{Field::Combo, "upstream", "Upstream of " + branch};
    up.filterable = true;
    for (const auto& r : m_snapshot->remoteBranches) {
        up.options.push_back(r.name);
        if (r.name.substr(r.remote.size() + 1) == branch)
            up.choice = static_cast<int>(up.options.size() - 1);
    }
    if (up.options.empty()) {
        m_app.notify(App::Notice::Warning, "Set upstream", "There are no remote-tracking branches: fetch first.");
        return;
    }
    const auto options = up.options;
    f.add(up);
    f.buttons.push_back({"Set", [this, branch, options](Form& form) {
                             m_actions->setUpstream(branch, options[static_cast<size_t>(form.choice("upstream"))]);
                         }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showBranchFromStashDialog(int index)
{
    Form f;
    f.title = "Branch from stash";
    f.add(Field{Field::Text, "name", "New branch for stash@{" + std::to_string(index) + "}"});
    f.buttons.push_back({"Create", [this, index](Form& form) { m_actions->stashBranch(index, gg::trim(form.text("name"))); },
        [](const Form& form) { return nonEmpty(form, "name"); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDropStashDialog(int index)
{
    Form f;
    f.title = "Drop stash";
    f.message = "Drop stash@{" + std::to_string(index) + "}? (Undo can bring it back.)";
    f.buttons.push_back({"Drop", [this, index](Form&) { m_actions->stashDrop(index); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showClearStashesDialog()
{
    Form f;
    f.title = "Clear stashes";
    f.message = "Drop all " + std::to_string(m_snapshot->stashes.size()) + " stashes?";
    f.buttons.push_back({"Clear all", [this](Form&) { m_actions->stashClear(); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDeleteFilesDialog(std::vector<std::string> paths)
{
    Form f;
    f.title = "Delete files";
    std::string list;
    for (const auto& p : paths)
        list += "  " + p + "\n";
    f.message = "Delete these files from the working tree?\n\n" + list;
    f.buttons.push_back({"Delete", [this, paths](Form&) { m_actions->deleteFiles(paths); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

std::string Session::newCommitBranch(const core::Oid& at) const
{
    if (m_snapshot->headUnborn)
        return at.isNull() ? m_snapshot->headBranch : std::string();
    const core::Oid where = at.isNull() ? m_snapshot->head : at;
    if (where == m_snapshot->head && !m_snapshot->headDetached && !m_snapshot->headBranch.empty())
        return m_snapshot->headBranch;
    const core::BranchInfo* only = nullptr;
    int count = 0;
    for (const auto& b : m_snapshot->branches)
        if (b.target == where) {
            only = &b;
            ++count;
        }
    return count == 1 && only->worktree.empty() ? only->name : std::string();
}

void Session::newCommitOn(const core::Oid& parent, bool detach)
{
    std::vector<std::string> specs;
    if (!parent.isNull())
        specs.push_back(parent.hex());
    const std::string branch = detach ? std::string() : newCommitBranch(parent);
    const bool unborn = m_snapshot->headUnborn;
    m_actions->newCommit(specs, detach || (branch.empty() && !unborn), {}, branch);
}

void Session::pushCurrent()
{
    if (m_snapshot->headDetached)
        return;
    const auto* b = m_snapshot->currentBranch();
    if (!b || b->upstream.empty()) {
        showPushToDialog(m_snapshot->headBranch);
        return;
    }
    const auto slash = b->upstream.find('/');
    m_actions->push(b->upstream.substr(0, slash), b->name, b->upstream.substr(slash + 1), false, false);
}

void Session::popStash()
{
    if (!m_snapshot->stashes.empty())
        m_actions->stashApply(0, true, false);
}

void Session::maybePromptHooks()
{
    if (m_hooksPromptChecked || !m_hooksStatus || m_hooksStatus->installed)
        return;
    m_hooksPromptChecked = true;
    auto& settings = m_app.settings();
    const std::string key = m_path.string();
    const HooksAnswer answer = settings.repo(key).hooks;
    if (!settings.data().askHooksOnOpen || answer == HooksAnswer::Never || answer == HooksAnswer::Installed)
        return;
    Form f;
    f.title = "Install ggui hooks?";
    f.message = "Install ggui hooks (Undo for all git operations, block pushing conflicts)?\n\n"
                "They chain to any hooks you already have and can be removed at any time "
                "(Settings > Hooks, or git gg hooks uninstall).";
    f.buttons.push_back({"Install", [this, key](Form&) {
                             m_app.settings().repo(key).hooks = HooksAnswer::Installed;
                             m_app.settings().save();
                             m_actions->installHooks([this](const core::MutationFinishedEvent& e) {
                                 if (e.outcome != core::Outcome::Ok)
                                     m_app.showError("Install hooks", e.message);
                                 requestHooksStatus();
                                 m_engine->readOperations();
                             });
                         }});
    f.buttons.push_back({"Not now", [this, key](Form&) {
                             m_app.settings().repo(key).hooks = HooksAnswer::NotNow;
                             m_app.settings().save();
                         }});
    f.buttons.push_back({"Never", [this, key](Form&) {
                             m_app.settings().repo(key).hooks = HooksAnswer::Never;
                             m_app.settings().save();
                         }});
    m_app.dialogs().open(std::move(f));
}

void Session::maybePromptOldGgRefs()
{
    if (m_ggRefsPromptChecked || m_snapshot->oldGgRefs.empty())
        return;
    m_ggRefsPromptChecked = true;
    const std::string key = m_path.string();
    if (m_app.settings().repo(key).ignoreOldGgRefs)
        return;
    const auto refs = m_snapshot->oldGgRefs;
    // Commits kept alive only by refs/gg/* (read with libgit2 on a worker).
    m_actions->run("inspect old gg data",
        [refs](core::MutationContext& ctx) {
            git_revwalk* raw = nullptr;
            gg::git2::check(git_revwalk_new(&raw, ctx.repo()), "git_revwalk_new");
            gg::git2::Revwalk walk(raw);
            for (const auto& r : refs) {
                git_oid oid;
                if (git_reference_name_to_id(&oid, ctx.repo(), r.c_str()) == 0) {
                    git_object* obj = nullptr;
                    if (git_object_lookup(&obj, ctx.repo(), &oid, GIT_OBJECT_COMMIT) == 0) {
                        git_revwalk_push(walk.get(), &oid);
                        git_object_free(obj);
                    }
                }
                git_error_clear();
            }
            git_revwalk_hide_glob(walk.get(), "refs/heads/*");
            git_revwalk_hide_glob(walk.get(), "refs/tags/*");
            git_revwalk_hide_glob(walk.get(), "refs/remotes/*");
            git_revwalk_hide_glob(walk.get(), "refs/stash");
            git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL);
            // Only tips matter: keeping a tip keeps its ancestors.
            std::vector<std::string> ids;
            git_oid oid;
            std::set<std::string> covered;
            while (git_revwalk_next(&oid, walk.get()) == 0) {
                const std::string hex = gg::git2::toHex(oid);
                gg::git2::Commit c = gg::git2::lookupCommit(ctx.repo(), oid);
                if (!covered.count(hex)) {
                    const char* s = git_commit_summary(c.get());
                    ctx.result += hex + " " + (s ? s : "") + "\n";
                }
                covered.insert(hex);
                for (unsigned i = 0; i < git_commit_parentcount(c.get()); ++i)
                    covered.insert(gg::git2::toHex(*git_commit_parent_id(c.get(), i)));
            }
            git_error_clear();
        },
        [this, refs, key](const core::MutationFinishedEvent& e) {
            Form f;
            f.title = "Old gg data found";
            f.message = std::to_string(refs.size())
                + " refs under refs/gg/ were left by the old gg tool. ggui does not use them.\n"
                  "Clean them up (this can be undone). Commits only they keep alive are listed below:";
            std::vector<std::string> ids;
            for (const auto& line : gg::splitLines(e.result)) {
                const std::string id = line.substr(0, line.find(' '));
                ids.push_back(id);
                const std::string shortId = id.substr(0, 10);
                Field keep{Field::Check, "keep_" + shortId, "Keep " + shortId + " " + line.substr(line.find(' ') + 1)};
                keep.checked = true;
                f.add(keep);
                // A branch name of the user's choice, or the backup name.
                f.add(Field{Field::Text, "branch_" + shortId, "Branch for " + shortId, "gg-backup/" + shortId});
            }
            f.buttons.push_back({"Clean up", [this, ids](Form& form) {
                                     std::vector<std::pair<std::string, std::string>> keep;
                                     for (const auto& id : ids) {
                                         const std::string shortId = id.substr(0, 10);
                                         const std::string branch = gg::trim(form.text("branch_" + shortId));
                                         if (form.checked("keep_" + shortId))
                                             keep.emplace_back(branch.empty() ? "gg-backup/" + shortId : branch, id);
                                     }
                                     m_actions->cleanUpOldGgRefs(keep);
                                 }});
            f.buttons.push_back({"Ignore", [this, key](Form&) {
                                     m_app.settings().repo(key).ignoreOldGgRefs = true;
                                     m_app.settings().save();
                                 }});
            f.buttons.push_back({"Not now", {}});
            m_app.dialogs().open(std::move(f));
        },
        false, false);
}

} // namespace ggui
