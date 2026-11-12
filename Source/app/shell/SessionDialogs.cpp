// Dialogs shared by panels, menus and the toolbar (docs/spec/ui-spec.md §9).
#include "panels/ChangesPanel.hpp"
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RepoTree.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Session.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>

#include <algorithm>
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
    // What is typed is never lost when Amend is toggled. Unticking Amend keeps an edited text.
    // It restores the commit draft only when the text is empty or still HEAD's message.
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
            // An edited amend text stays in the field. An untouched one (empty, or HEAD's
            // message as filled in) gives the field back to the commit draft.
            const bool edited = !message->text.empty() && !(texts->headLoaded && message->text == texts->head);
            if (!edited)
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
        const size_t slash = b->upstreamRemote.size();
        for (size_t i = 0; i < remotes.size(); ++i)
            if (remotes[i] == b->upstreamRemote)
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

void Session::showFetchMoreDialog(const std::string& remoteName)
{
    const auto remotes = remoteNames(m_snapshot);
    if (remotes.empty()) {
        m_app.notify(App::Notice::Warning, "Fetch", "This repository has no remotes. Add one in the Remotes panel.");
        return;
    }
    Form f;
    f.title = "Fetch more";
    Field remote{Field::Combo, "remote", "Remote"};
    remote.options = remotes;
    auto pick = std::find(remotes.begin(), remotes.end(), remoteName);
    if (pick == remotes.end())
        pick = std::find(remotes.begin(), remotes.end(), "origin");
    if (pick == remotes.end())
        pick = remotes.begin();
    remote.choice = static_cast<int>(pick - remotes.begin());
    f.add(remote);
    f.add(Field{Field::CheckList, "branches", "Branches"});
    f.add(Field{Field::CheckList, "tags", "Tags"});
    // What the dialog has shown so far: the remote it asked about and what it already filled in.
    struct State {
        int last = -1;
        bool branchesDone = false;
        bool tagsDone = false;
    };
    auto state = std::make_shared<State>();
    f.onFrame = [this, remotes, state](Form& form) {
        Field* remoteField = nullptr;
        Field* branches = nullptr;
        Field* tags = nullptr;
        for (auto& fld : form.fields) {
            if (fld.id == "remote")
                remoteField = &fld;
            else if (fld.id == "branches")
                branches = &fld;
            else if (fld.id == "tags")
                tags = &fld;
        }
        if (!remoteField || !branches || !tags || remoteField->choice < 0
            || static_cast<size_t>(remoteField->choice) >= remotes.size())
            return;
        const std::string name = remotes[static_cast<size_t>(remoteField->choice)];
        if (remoteField->choice != state->last) {
            state->last = remoteField->choice;
            state->branchesDone = false;
            state->tagsDone = false;
            branches->setOptions({});
            tags->setOptions({});
            branches->hint = "Checking...";
            tags->hint = "Checking...";
            requestRemoteBranches(name);
            m_remoteTags.erase(name);
            markRemoteTagsStale();
            requestRemoteTagsIfStale();
        }
        const std::string nothing = "Nothing new on " + name + ".";
        if (!state->branchesDone) {
            if (const RemoteBranches* rb = remoteBranches(name)) {
                state->branchesDone = true;
                std::vector<std::string> names;
                if (rb->ok) {
                    for (const auto& n : rb->names) {
                        const std::string full = name + "/" + n;
                        const bool known = std::any_of(m_snapshot->remoteBranches.begin(), m_snapshot->remoteBranches.end(),
                            [&](const core::RemoteBranchInfo& r) { return r.remote == name && r.name == full; });
                        if (!known)
                            names.push_back(n);
                    }
                }
                branches->hint = rb->ok ? nothing : rb->error;
                branches->setOptions(std::move(names));
            }
        }
        if (!state->tagsDone) {
            const auto it = m_remoteTags.find(name);
            if (it != m_remoteTags.end()) {
                state->tagsDone = true;
                std::vector<std::string> names;
                if (it->second.ok) {
                    for (const auto& t : it->second.tags) {
                        const bool known = std::any_of(m_snapshot->tags.begin(), m_snapshot->tags.end(),
                            [&](const core::TagInfo& l) { return l.name == t; });
                        if (!known)
                            names.push_back(t);
                    }
                }
                tags->hint = it->second.ok ? nothing : it->second.error;
                tags->setOptions(std::move(names));
            }
        }
    };
    f.buttons.push_back({"Fetch",
        [this, remotes](Form& form) {
            m_actions->fetchRefs(remotes[static_cast<size_t>(form.choice("remote"))], form.checkedOptions("branches"),
                form.checkedOptions("tags"));
        },
        [](const Form& form) { return !form.checkedOptions("branches").empty() || !form.checkedOptions("tags").empty(); }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showForgetDialog(const std::string& remoteName)
{
    const auto remotes = remoteNames(m_snapshot);
    if (remotes.empty()) {
        m_app.notify(App::Notice::Warning, "Forget", "This repository has no remotes. Add one in the Remotes panel.");
        return;
    }
    Form f;
    f.title = "Forget";
    Field remote{Field::Combo, "remote", "Remote"};
    remote.options = remotes;
    auto pick = std::find(remotes.begin(), remotes.end(), remoteName);
    if (pick == remotes.end())
        pick = std::find(remotes.begin(), remotes.end(), "origin");
    if (pick == remotes.end())
        pick = remotes.begin();
    remote.choice = static_cast<int>(pick - remotes.begin());
    f.add(remote);
    f.add(Field{Field::CheckList, "branches", "Branches"});
    f.add(Field{Field::CheckList, "tags", "Tags"});
    // What the dialog has shown so far: the remote it asked about and whether it filled in the tags.
    struct State {
        int last = -1;
        bool tagsDone = false;
    };
    auto state = std::make_shared<State>();
    f.onFrame = [this, remotes, state](Form& form) {
        Field* remoteField = nullptr;
        Field* branches = nullptr;
        Field* tags = nullptr;
        for (auto& fld : form.fields) {
            if (fld.id == "remote")
                remoteField = &fld;
            else if (fld.id == "branches")
                branches = &fld;
            else if (fld.id == "tags")
                tags = &fld;
        }
        if (!remoteField || !branches || !tags || remoteField->choice < 0
            || static_cast<size_t>(remoteField->choice) >= remotes.size())
            return;
        const std::string name = remotes[static_cast<size_t>(remoteField->choice)];
        const std::string nothing = "Nothing fetched from " + name + ".";
        if (remoteField->choice != state->last) {
            state->last = remoteField->choice;
            state->tagsDone = false;
            // The branches come from the snapshot, so they are ready at once.
            std::vector<std::string> names;
            const std::string prefix = name + "/";
            for (const auto& r : m_snapshot->remoteBranches) {
                if (r.remote != name || r.name.compare(0, prefix.size(), prefix) != 0)
                    continue;
                std::string n = r.name.substr(prefix.size());
                if (n != "HEAD")
                    names.push_back(std::move(n));
            }
            branches->hint = names.empty() ? nothing : std::string();
            branches->setOptions(std::move(names));
            tags->setOptions({});
            tags->hint = "Checking...";
            m_remoteTags.erase(name);
            markRemoteTagsStale();
            requestRemoteTagsIfStale();
        }
        if (!state->tagsDone) {
            const auto it = m_remoteTags.find(name);
            if (it != m_remoteTags.end()) {
                state->tagsDone = true;
                std::vector<std::string> names;
                if (it->second.ok) {
                    for (const auto& l : m_snapshot->tags) {
                        if (it->second.tags.count(l.name) != 0)
                            names.push_back(l.name);
                    }
                }
                tags->hint = it->second.ok ? nothing : it->second.error;
                tags->setOptions(std::move(names));
            }
        }
    };
    f.buttons.push_back({"Forget",
        [this, remotes](Form& form) {
            m_actions->forgetRefs(remotes[static_cast<size_t>(form.choice("remote"))], form.checkedOptions("branches"),
                form.checkedOptions("tags"));
        },
        [](const Form& form) { return !form.checkedOptions("branches").empty() || !form.checkedOptions("tags").empty(); }});
    f.buttons.back().danger = true;
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
    std::vector<StagedDiscard> staged, std::vector<SubmoduleDiscard> submodules)
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
    for (const auto& m : submodules) {
        if (m.staged && (m.kind == core::ChangeKind::Added || m.kind == core::ChangeKind::Copied)) {
            list += "  Submodule " + m.path + ": remove it from the index and delete its working tree\n";
            continue;
        }
        list += "  Submodule " + m.path + ": reset to the commit in " + (m.staged ? "HEAD" : "the index")
            + " and discard changes to tracked files inside it" + (m.clean ? ", delete untracked files inside it" : "") + "\n";
    }
    f.message = "Discard the changes of these files? This cannot be undone.\n\n" + list;
    f.buttons.push_back({"Discard", [this, tracked, untracked, staged, submodules](Form&) {
                             m_actions->discard(tracked, untracked, staged, submodules);
                         }});
    f.buttons.back().danger = true;
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showRemoveSubmoduleDialog(const std::string& path)
{
    Form f;
    f.title = "Remove submodule";
    f.message = "Remove the submodule " + path + "? Its working tree is deleted, including changes that are not committed "
        "(git submodule deinit -f discards them, also when the option below is off). "
        "The removal of the entry in .gitmodules and of the submodule is staged.";
    f.add(Field{Field::Check, "data", "Also delete the repository data in .git/modules/<name>"});
    f.buttons.push_back({"Remove", [this, path](Form& form) { m_actions->submoduleRemove(path, form.checked("data")); }});
    f.buttons.back().danger = true;
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showAddSubmoduleDialog()
{
    Form f;
    f.title = "Add submodule";
    f.add(Field{Field::Text, "url", "URL"});
    f.add(Field{Field::Text, "path", "Path (relative to the working directory, no \"..\")"});
    f.add(Field{Field::Text, "branch", "Branch (optional)"});
    f.buttons.push_back({"Add", [this](Form& form) {
                             m_actions->submoduleAdd(
                                 gg::trim(form.text("url")), gg::trim(form.text("path")), gg::trim(form.text("branch")));
                         },
        [](const Form& form) { return nonEmpty(form, "url") && Actions::submodulePathProblem(form.text("path")).empty(); }});
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
                                 ctx.submodules = core::SubmoduleSync::ForceUpdate;
                             });
                         }});
    f.buttons.back().danger = true;
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
    std::vector<std::string> paths;
    for (const auto& r : m_app.settings().data().repositories) {
        paths.push_back(r.path);
        if (r.path == repoPath)
            current = r.alias;
    }
    auto self = std::find(paths.begin(), paths.end(), repoPath);
    if (self == paths.end())
        self = paths.insert(paths.end(), repoPath);
    // The deduplicated name of the repository: its group and its base name are the defaults.
    const RecentName name = uniqueRecentNames(paths)[static_cast<size_t>(self - paths.begin())];
    const std::string defaultGroup = deduplicationGroup(name.prefix);
    std::string group = defaultGroup;
    std::string alias = name.base;
    if (!normalizeAlias(current).empty()) {
        const AliasParts parts = splitStoredAlias(current);
        group = parts.group;
        alias = parts.name;
    }
    Form f;
    f.title = "Set alias";
    f.add(Field{Field::Info, "alias_path", "", repoPath});
    f.add(Field{Field::Text, "group", "Group path", group});
    // The Alias field has the focus: a typed name and Enter set the alias.
    Field aliasField{Field::Text, "alias", "Alias", alias};
    aliasField.focus = true;
    f.add(std::move(aliasField));
    f.add(Field{Field::Info, "alias_note", "",
        "Use \"/\" in the group path for subgroups. An empty group path is the top level. Leave both empty to remove the alias."});
    // The application, not the session: another repository can open while the dialog is up.
    f.buttons.push_back({"Set", [&app = m_app, repoPath, current, defaultGroup, base = name.base](Form& form) {
        app.settings().setAlias(repoPath, aliasFromDialog(form.text("group"), form.text("alias"), current, defaultGroup, base));
    }});
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

Session::MergedState Session::branchMerged(core::RequestId request) const
{
    const auto it = m_branchMerged.find(request);
    return it == m_branchMerged.end() ? MergedState::Unknown : it->second;
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
    // The commit that goes away: the local branch's tip, or (remote only) its upstream's.
    const auto* b = m_snapshot->findBranch(branch);
    const bool upstreamOk = b && !b->upstream.empty() && !b->upstreamGone;
    f.add(commitInfo(*this, mode == 1 ? "Remote branch" : "Branch", mode == 1 && upstreamOk ? b->upstream : branch));
    core::RequestId request = 0;
    if (mode != 1) {
        // `git branch -d` refuses unless the tip is merged into its upstream (else HEAD). A remote branch
        // that this operation deletes first is gone by then: git checks against HEAD.
        // A local upstream has its full name (refs/heads/...), a remote-tracking one has none.
        const std::string localPrefix = "refs/heads/";
        bool useUpstream = upstreamOk;
        for (const auto& r : remotes)
            if (upstreamOk && b->upstream == r + "/" + branch)
                useUpstream = false;
        std::string target = "HEAD";
        std::string shown = "HEAD";
        if (useUpstream) {
            target = b->upstream.rfind("refs/", 0) == 0 ? b->upstream : "refs/remotes/" + b->upstream;
            shown = b->upstream.rfind(localPrefix, 0) == 0 ? b->upstream.substr(localPrefix.size()) : b->upstream;
        }
        // The engine reads the answer off the UI thread; the dialog shows it when it arrives.
        request = m_engine->readBranchMerged(branch, target);
        m_branchMerged.emplace(request, MergedState::Unknown);
        Field mergedInto = commitInfo(*this, "Merged into", shown);
        mergedInto.visible = [this, request](const Form&) { return branchMerged(request) == MergedState::Merged; };
        f.add(std::move(mergedInto));
        Field warning;
        warning.kind = Field::Warning;
        warning.id = "unmerged_warning";
        warning.live = [this, request, branch, shown] {
            return branchMerged(request) == MergedState::NotMerged
                ? "The branch '" + branch + "' is not merged into " + shown + ". The delete operation can lose its commits."
                : std::string();
        };
        f.add(std::move(warning));
    }
    // Not merged: `git branch -d` would refuse, so the action uses -D; the warning above says so. When
    // git could not tell, it decides (-d) and reports its own error.
    FormButton del{"Delete", [this, branch, remotes, mode, request](Form&) {
                       m_actions->deleteBranch(branch, mode != 1 && branchMerged(request) == MergedState::NotMerged, remotes, mode != 1);
                   }};
    del.danger = true;
    // Whether the branch is merged decides how it is deleted: wait for the answer.
    if (mode != 1)
        del.enabled = [this, request](const Form&) { return branchMerged(request) != MergedState::Unknown; };
    f.buttons.push_back(std::move(del));
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDeleteBranchesDialog(std::vector<std::string> branches, int mode)
{
    // The current branch cannot go: git refuses to delete the branch that HEAD names.
    std::string skipped;
    if (mode != 1 && !m_snapshot->headBranch.empty()) {
        const auto head = std::find(branches.begin(), branches.end(), m_snapshot->headBranch);
        if (head != branches.end()) {
            skipped = *head;
            branches.erase(head);
        }
    }
    if (branches.empty())
        return;
    // The remotes that hold a branch of this name, as the single dialog finds them.
    auto remotesOf = [this, mode](const std::string& branch) {
        std::vector<std::string> remotes;
        if (mode > 0) {
            const auto* b = m_snapshot->findBranch(branch);
            for (const auto& r : m_snapshot->remoteBranches) {
                const std::string name = r.name.substr(r.remote.size() + 1);
                if (name == branch && (mode == 2 || (b && b->upstream == r.name)))
                    remotes.push_back(r.remote);
            }
        }
        return remotes;
    };
    Form f;
    f.title = "Delete branches";
    const std::string count = std::to_string(branches.size());
    const std::string noun = branches.size() == 1 ? " branch" : " branches";
    f.message = mode == 0 ? "Delete " + count + " local" + noun + "?"
        : mode == 1       ? "Delete " + count + noun + " on their remotes?"
                          : "Delete " + count + noun + " and every remote branch with the same name?";
    if (!skipped.empty())
        f.add(Field{Field::Info, "head_note", "", "The current branch '" + skipped + "' is not in the list."});
    Field list{Field::CheckList, "branches", "Branches"};
    list.setOptions(branches);
    list.checks.assign(branches.size(), 1);
    f.add(std::move(list));
    // The merged check of each branch. Its target follows the rule of the single dialog.
    auto requests = std::make_shared<std::map<std::string, core::RequestId>>();
    auto shown = std::make_shared<std::map<std::string, std::string>>();
    if (mode != 1) {
        const std::string localPrefix = "refs/heads/";
        for (const auto& branch : branches) {
            const auto* b = m_snapshot->findBranch(branch);
            const bool upstreamOk = b && !b->upstream.empty() && !b->upstreamGone;
            bool useUpstream = upstreamOk;
            for (const auto& r : remotesOf(branch))
                if (upstreamOk && b->upstream == r + "/" + branch)
                    useUpstream = false;
            std::string target = "HEAD";
            std::string name = "HEAD";
            if (useUpstream) {
                target = b->upstream.rfind("refs/", 0) == 0 ? b->upstream : "refs/remotes/" + b->upstream;
                name = b->upstream.rfind(localPrefix, 0) == 0 ? b->upstream.substr(localPrefix.size()) : b->upstream;
            }
            shown->emplace(branch, std::move(name));
            const core::RequestId request = m_engine->readBranchMerged(branch, target);
            m_branchMerged.emplace(request, MergedState::Unknown);
            requests->emplace(branch, request);
        }
    }
    // The warning follows the checked branches: the form updates its text on every frame.
    auto warningText = std::make_shared<std::string>();
    f.onFrame = [this, requests, shown, warningText](Form& form) {
        std::string names;
        std::set<std::string> targets;
        for (const auto& branch : form.checkedOptions("branches")) {
            const auto it = requests->find(branch);
            if (it == requests->end() || branchMerged(it->second) != MergedState::NotMerged)
                continue;
            names += (names.empty() ? "" : ", ") + branch;
            targets.insert((*shown)[branch]);
        }
        // Each branch has its own target: name it when all are the same.
        const std::string target = targets.size() == 1 ? *targets.begin() : "their upstream or HEAD";
        *warningText = names.empty() ? std::string()
                                     : "Not merged into " + target + ": " + names + ". The delete operation can lose their commits.";
    };
    if (mode != 1) {
        Field warning;
        warning.kind = Field::Warning;
        warning.id = "unmerged_warning";
        warning.live = [warningText] { return *warningText; };
        f.add(std::move(warning));
    }
    // Not merged: `git branch -d` would refuse, so the action uses -D; the warning says so.
    FormButton del{"Delete", [this, mode, requests, remotesOf](Form& form) {
                       std::vector<BranchDelete> items;
                       for (const auto& branch : form.checkedOptions("branches")) {
                           const auto it = requests->find(branch);
                           const bool unmerged = it != requests->end() && branchMerged(it->second) == MergedState::NotMerged;
                           items.push_back({branch, mode != 1 && unmerged, remotesOf(branch), mode != 1});
                       }
                       m_actions->deleteBranches(std::move(items));
                   }};
    del.danger = true;
    // The merged state decides how each branch goes: wait for every answer.
    del.enabled = [this, mode, requests](const Form& form) {
        const auto checked = form.checkedOptions("branches");
        if (checked.empty())
            return false;
        if (mode == 1)
            return true;
        for (const auto& branch : checked) {
            const auto it = requests->find(branch);
            if (it != requests->end() && branchMerged(it->second) == MergedState::Unknown)
                return false;
        }
        return true;
    };
    f.buttons.push_back(std::move(del));
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
    FormButton del{"Delete", [this, branch, remote](Form&) { m_actions->deleteBranch(branch, false, {remote}, false); }};
    del.danger = true;
    f.buttons.push_back(std::move(del));
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

// A check list of `names`, all checked, in the field `id`.
static Field checkedList(const std::string& id, const std::string& label, const std::vector<std::string>& names)
{
    Field list{Field::CheckList, id, label};
    list.setOptions(names);
    list.checks.assign(names.size(), 1);
    return list;
}

void Session::showDeleteRemoteBranchesDialog(std::vector<std::string> remoteBranches)
{
    // The remote of each branch comes from the snapshot, as in the single dialog. A name without a remote is left out.
    std::map<std::string, std::string> remoteOf;
    std::vector<std::string> names;
    for (const auto& name : remoteBranches)
        for (const auto& r : m_snapshot->remoteBranches)
            if (r.name == name && remoteOf.emplace(name, r.remote).second)
                names.push_back(name);
    if (names.empty())
        return;
    Form f;
    f.title = "Delete branches";
    f.message = "Delete these branches on their remotes?";
    f.add(checkedList("branches", "Branches", names));
    FormButton del{"Delete", [this, remoteOf](Form& form) {
                       std::vector<BranchDelete> items;
                       for (const auto& name : form.checkedOptions("branches")) {
                           const std::string& remote = remoteOf.at(name);
                           items.push_back({name.substr(remote.size() + 1), false, {remote}, false});
                       }
                       m_actions->deleteBranches(std::move(items));
                   },
        [](const Form& form) { return !form.checkedOptions("branches").empty(); }};
    del.danger = true;
    f.buttons.push_back(std::move(del));
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showForgetRefsDialog(std::vector<std::string> remoteBranches)
{
    if (remoteBranches.empty())
        return;
    Form f;
    f.title = "Forget";
    f.message = "Remove these remote-tracking branches from this repository? The remote keeps them. Fetch more... brings them back.";
    f.add(checkedList("branches", "Branches", remoteBranches));
    FormButton forget{"Forget", [this](Form& form) {
                          std::vector<std::string> refs;
                          for (const auto& name : form.checkedOptions("branches"))
                              refs.push_back("refs/remotes/" + name);
                          m_actions->forgetRefs(std::move(refs));
                      },
        [](const Form& form) { return !form.checkedOptions("branches").empty(); }};
    forget.danger = true;
    f.buttons.push_back(std::move(forget));
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showDeleteTagsDialog(std::vector<std::string> tags, const std::string& remote)
{
    if (tags.empty())
        return;
    Form f;
    f.title = "Delete tags";
    f.message = remote.empty() ? "Delete these tags?" : "Delete these tags on '" + remote + "'?";
    f.add(checkedList("tags", "Tags", tags));
    FormButton del{"Delete", [this, remote](Form& form) {
                       if (remote.empty())
                           m_actions->deleteTags(form.checkedOptions("tags"));
                       else
                           m_actions->deleteRemoteTags(remote, form.checkedOptions("tags"));
                   },
        [](const Form& form) { return !form.checkedOptions("tags").empty(); }};
    del.danger = true;
    f.buttons.push_back(std::move(del));
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

void Session::showRenameStashDialog(int index)
{
    if (!m_snapshot)
        return;
    for (const auto& st : m_snapshot->stashes) {
        if (st.index != index)
            continue;
        const core::Oid commit = st.commit;
        const std::string current = st.message;
        Form f;
        f.title = "Rename stash";
        f.add(Field{Field::Text, "name", "New name for stash@{" + std::to_string(index) + "}", current});
        f.buttons.push_back({"Rename",
            [this, index, commit](Form& form) { m_actions->stashRename(index, commit, gg::trim(form.text("name"))); },
            [current](const Form& form) { return nonEmpty(form, "name") && gg::trim(form.text("name")) != current; }});
        f.buttons.push_back({"Cancel", {}});
        m_app.dialogs().open(std::move(f));
        return;
    }
}

void Session::showDropStashDialog(int index)
{
    Form f;
    f.title = "Drop stash";
    f.message = "Drop stash@{" + std::to_string(index) + "}? (Undo can bring it back.)";
    f.buttons.push_back({"Drop", [this, index](Form&) { m_actions->stashDrop(index); }});
    f.buttons.back().danger = true;
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::showClearStashesDialog()
{
    Form f;
    f.title = "Clear stashes";
    f.message = "Drop all " + std::to_string(m_snapshot->stashes.size()) + " stashes?";
    f.buttons.push_back({"Clear all", [this](Form&) { m_actions->stashClear(); }});
    f.buttons.back().danger = true;
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
    f.buttons.back().danger = true;
    f.buttons.push_back({"Cancel", {}});
    m_app.dialogs().open(std::move(f));
}

void Session::newCommitOn(const core::Oid& parent)
{
    std::vector<std::string> specs;
    if (!parent.isNull())
        specs.push_back(parent.hex());
    // An unborn HEAD has no commit to detach from: the new root commit starts its branch.
    m_actions->newCommit(specs, !m_snapshot->headUnborn, {}, {});
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
    const size_t slash = b->upstreamRemote.size();
    m_actions->push(b->upstreamRemote, b->name, b->upstream.substr(slash + 1), false, false);
}

void Session::popStash()
{
    if (!m_snapshot->stashes.empty())
        m_actions->stashApply(0, true, false);
}

} // namespace ggui
