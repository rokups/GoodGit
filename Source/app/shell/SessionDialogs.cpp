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

// The commit dialog's warning: one line per affected file (the first few), or "" for none.
std::string Session::commitWarningText() const
{
    constexpr size_t kShown = 4;
    std::string text;
    for (size_t i = 0; i < m_commitWarnings.size() && i < kShown; ++i) {
        const auto& w = m_commitWarnings[i];
        if (i)
            text += "\n";
        if (w.kind == gg::outgoing::StagedWarning::Kind::Conflict) {
            text += w.path + ": committing a first-class conflict (" + std::to_string(w.sides)
                + "-sided); push refuses it until resolved";
            continue;
        }
        std::string lines;
        for (size_t k = 0; k < w.lines.size(); ++k)
            lines += (k ? ", " : "") + std::to_string(w.lines[k]);
        text += w.path + ": conflict markers left at line " + lines + " (the edit broke a conflict region)";
    }
    if (m_commitWarnings.size() > kShown)
        text += "\nand " + std::to_string(m_commitWarnings.size() - kShown) + " more";
    return text;
}


// Why the commit dialog's Amend box cannot be ticked ("" when it can): git refuses --amend while
// a merge, cherry-pick or revert is waiting for its commit, and an unborn HEAD has nothing to amend.
static std::string amendBlockedReason(const core::Snapshot& snap)
{
    if (snap.headUnborn)
        return "HEAD has no commit yet, so there is nothing to amend";
    switch (snap.state) {
    case core::RepoState::Merging: return "A merge is in progress: finish it (commit or abort) before amending";
    case core::RepoState::CherryPicking: return "A cherry-pick is in progress: finish it (commit or abort) before amending";
    case core::RepoState::Reverting: return "A revert is in progress: finish it (commit or abort) before amending";
    default: return {};
    }
}

void Session::showCommitDialog(const CommitDialogState* restore)
{
    Form f;
    f.title = "Commit";
    const bool nothingStaged = m_status && m_status->staged.empty();
    // What is typed is never lost when Amend is toggled: each mode keeps its own text.
    struct Texts {
        std::string commit; // the commit message, while Amend is ticked
        std::string amend;  // the amend message as edited, while Amend is not ticked
        std::string head;   // HEAD's message, read off the UI thread
        bool headLoaded = false;
        bool filled = false; // `head` went into the field (once, and only into an empty field)
    };
    auto texts = std::make_shared<Texts>();
    Field msg;
    msg.kind = Field::Multiline;
    msg.id = "message";
    msg.label = "Message";
    msg.labelFn = [](const Form& form) {
        return form.checked("amend") ? std::string("Message (leave empty to keep the current message)") : std::string("Message");
    };
    // The message waiting in MERGE_MSG (a merge, revert or cherry-pick), as git commit would
    // take it: without git's comment lines and trailing blank lines.
    if (!m_snapshot->mergeMessage.empty()) {
        for (const auto& line : gg::splitLines(m_snapshot->mergeMessage))
            if (line.rfind('#', 0) != 0)
                msg.text += line + "\n";
        while (!msg.text.empty() && std::isspace(static_cast<unsigned char>(msg.text.back())))
            msg.text.pop_back();
    }
    f.add(msg);
    // HEAD's message (whatever is selected) goes into the field as soon as it is read.
    if (!m_snapshot->headUnborn) {
        if (m_info->details() && m_info->details()->id == m_snapshot->head) {
            texts->head = m_info->details()->message;
            texts->headLoaded = true;
        } else {
            commitMessages({m_snapshot->head}, [texts](const std::vector<std::string>& messages) {
                if (!messages.empty()) {
                    texts->head = messages.front();
                    texts->headLoaded = true;
                }
            });
        }
    }
    auto fillHead = [texts](Form& form) {
        if (!form.checked("amend") || !texts->headLoaded || texts->filled)
            return;
        texts->filled = true;
        for (auto& field : form.fields)
            if (field.id == "message" && field.text.empty())
                field.text = texts->head;
    };
    f.onFrame = fillHead;
    Field amendBox{Field::Check, "amend", "Amend"};
    amendBox.disabledReason = [this](const Form&) { return amendBlockedReason(*m_snapshot); };
    amendBox.onChange = [texts, fillHead](Form& form) {
        Field* message = nullptr;
        for (auto& field : form.fields)
            if (field.id == "message")
                message = &field;
        if (!message)
            return;
        if (form.checked("amend")) {
            texts->commit = message->text;
            message->text = texts->amend;
            fillHead(form);
        } else {
            texts->amend = message->text;
            message->text = texts->commit;
        }
    };
    Field skipHooks{Field::Check, "skip_hooks", "Skip hooks (--no-verify)"};
    Field messageOnly{Field::Check, "message_only", "Change the message only (keep the index out)"};
    messageOnly.visible = [](const Form& form) { return form.checked("amend"); };
    if (restore) {
        // Reopened as it was, Amend ticked with the edited amend text.
        texts->commit = restore->commitText;
        texts->amend = restore->amendText;
        texts->filled = true;
        f.fields.front().text = restore->amendText;
        amendBox.checked = true;
        skipHooks.checked = restore->skipHooks;
        messageOnly.checked = restore->messageOnly;
    }
    f.add(std::move(amendBox));
    // Staged files that are (or would stay) first-class conflicts: a warning only, committing is
    // never blocked. Read off the UI thread; refreshed while the dialog is open (Session::handle).
    m_commitWarnings.clear();
    m_engine->readCommitWarnings();
    Field warning;
    warning.kind = Field::Warning;
    warning.id = "conflict_warning";
    warning.live = [this] { return commitWarningText(); };
    warning.visible = [](const Form& form) { return !(form.checked("amend") && form.checked("message_only")); };
    f.add(std::move(warning));
    f.add(std::move(skipHooks));
    f.add(std::move(messageOnly));
    const NothingStaged pref = m_app.settings().data().nothingStaged;
    if (nothingStaged && pref == NothingStaged::Ask) {
        Field mode;
        mode.kind = Field::Combo;
        mode.id = "nothing_staged";
        mode.label = "Nothing is staged";
        mode.options = {"Stage all tracked changes and commit (-a)", "Stage the selected files and commit"};
        mode.visible = [](const Form& form) { return !form.checked("amend"); };
        f.add(mode);
    }
    const std::vector<std::string> selected = selectedPaths();
    FormButton primary{"Commit",
        [this, nothingStaged, pref, selected, texts](Form& form) {
            const std::string message = form.text("message");
            const bool noVerify = form.checked("skip_hooks");
            if (form.checked("amend")) {
                const bool msgOnly = form.checked("message_only");
                // Declined at the published-history question: the dialog comes back as it was.
                const CommitDialogState state{texts->commit, message, noVerify, msgOnly};
                m_actions->amend(message, noVerify, msgOnly, {}, [this, state] { showCommitDialog(&state); });
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
        [](const Form& form) { return form.checked("amend") || nonEmpty(form, "message"); }};
    primary.labelFn = [](const Form& form) { return std::string(form.checked("amend") ? "Amend" : "Commit"); };
    f.buttons.push_back(std::move(primary));
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

void Session::showSetAliasDialog(const std::string& repoPath)
{
    std::string current;
    for (const auto& r : m_app.settings().data().repositories)
        if (r.path == repoPath)
            current = r.alias;
    Form f;
    f.title = "Set alias";
    f.add(Field{Field::Text, "alias", "Alias for " + repoPath, current});
    f.add(Field{Field::Info, "alias_note", "", "Use \"/\" for groups: group/subgroup/name. Leave empty to remove the alias."});
    // The application, not the session: another repository can open while the dialog is up.
    f.buttons.push_back({"Set", [&app = m_app, repoPath](Form& form) { app.settings().setAlias(repoPath, form.text("alias")); }});
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

} // namespace ggui
