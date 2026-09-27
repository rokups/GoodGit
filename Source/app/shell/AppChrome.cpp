// Main menu, toolbar and global shortcuts (REBUILD_PLAN §4.1; docs/spec/ui-spec.md §1).
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "platform/Platform.hpp"
#include "panels/CommitMenu.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace ggui {

namespace fs = std::filesystem;

namespace {

// Commit that toolbar/menu commit actions apply to: the selected commit, else HEAD.
core::Oid targetCommit(Session& s)
{
    if (s.selection().kind == SelKind::Commit)
        return s.selection().id;
    return s.snapshot() ? s.snapshot()->head : core::Oid{};
}

// Parents for New: the selection plus Ctrl-clicked commits (several = merge commit).
std::vector<core::Oid> newParents(Session& s)
{
    const core::Oid at = targetCommit(s);
    std::vector<core::Oid> parents;
    if (!at.isNull())
        parents.push_back(at);
    if (s.selection().kind == SelKind::Commit)
        for (const auto& e : s.history().extraSelection())
            parents.push_back(e);
    return parents;
}

bool headSelected(Session& s)
{
    return s.selection().kind == SelKind::Commit && s.snapshot() && s.selection().id == s.snapshot()->head;
}

bool textConflictsOnly(Session& s)
{
    const auto st = s.status();
    if (!st)
        return false;
    bool any = false;
    for (const auto& e : st->conflicted) {
        if (e.firstClass)
            continue;
        any = true;
        if (e.binary || !e.stage2 || !e.stage3)
            return false;
    }
    return any;
}

} // namespace

void App::handleShortcuts()
{
    using namespace ImGui;
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, global))
        pickAndOpenRepository();
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, global))
        requestQuit();
    if (!m_session || !m_session->opened())
        return;
    Session& s = *m_session;
    const bool free = s.actions().busy().empty() && !m_dialogs.anyOpen();
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, global))
        post([this] { closeRepository(); });
    else if (Shortcut(ImGuiKey_F5, global))
        s.refresh();
    else if (Shortcut(ImGuiKey_F6, global))
        s.nextChangedFile(+1);
    else if (Shortcut(ImGuiMod_Shift | ImGuiKey_F6, global))
        s.nextChangedFile(-1);
    else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global) && free)
        s.newCommitOn(newParents(s), false); else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global) && free)
        s.actions().undo(false);
    else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global) && free)
        s.actions().undo(true);
}

void App::drawRecentMenu()
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    ImGui::InputTextWithHint("##recent_filter", "Filter", &m_recentFilter);
    const auto& recent = m_settings.data().recent;
    int shown = 0;
    for (size_t i = 0; i < recent.size(); ++i) {
        const std::string& path = recent[i];
        if (!m_recentFilter.empty() && !containsNoCase(path, m_recentFilter))
            continue;
        std::string detail;
        for (const auto& info : m_recentInfo)
            if (info.path == fs::path(path))
                detail = summaryText(info);
        if (ImGui::MenuItem((path + "###recent_menu_" + std::to_string(i)).c_str(), detail.c_str()))
            post([this, path] { openRepository(path); });
        ++shown;
    }
    if (shown == 0)
        ImGui::TextDisabled("No recent repositories");
}

void App::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    Session* s = (m_session && m_session->opened()) ? m_session.get() : nullptr;
    const bool free = s && s->actions().busy().empty();
    if (ImGui::BeginMenu("Repository")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            pickAndOpenRepository();
        if (ImGui::MenuItem("Initialize..."))
            initializeRepository();
        if (ImGui::MenuItem("Clone..."))
            showCloneDialog();
        if (ImGui::BeginMenu("Recent")) {
            drawRecentMenu();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open working directory", nullptr, false, s && !s->snapshot()->bare))
            openInFileManager(s->snapshot()->workdir);
        if (ImGui::MenuItem("Copy path", nullptr, false, s != nullptr))
            ImGui::SetClipboardText(s->path().string().c_str());
        if (ImGui::MenuItem("Close repository", "Ctrl+W", false, s != nullptr))
            post([this] { closeRepository(); });
        if (ImGui::MenuItem("Refresh", "F5", false, s != nullptr))
            s->refresh();
        ImGui::Separator();
        if (ImGui::MenuItem("Fetch", nullptr, false, free))
            s->actions().fetch("", false, false);
        std::string reason;
        if (ImGui::MenuItem("Pull", nullptr, false, free && s->pullAvailable(&reason)))
            s->actions().pull(PullMode::Config);
        if (ImGui::MenuItem("Push", nullptr, false, free && !s->snapshot()->headDetached))
            s->pushCurrent();
        ImGui::Separator();
        if (ImGui::MenuItem("Settings..."))
            openSettings();
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Commit")) {
        const core::Oid at = s ? targetCommit(*s) : core::Oid{};
        if (ImGui::MenuItem("New commit", "Ctrl+N", false, free))
            s->newCommitOn(newParents(*s), false);
        if (ImGui::MenuItem("New detached commit", nullptr, false, free && !at.isNull()))
            s->newCommitOn(newParents(*s), true);
        if (ImGui::MenuItem("Commit...", nullptr, false, free && !s->snapshot()->bare))
            s->showCommitDialog(false);
        if (ImGui::MenuItem("Amend...", nullptr, false, free && !s->snapshot()->headUnborn))
            s->showCommitDialog(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Move HEAD to parent", nullptr, false, free && !s->snapshot()->headUnborn))
            s->actions().moveHead(false);
        if (ImGui::MenuItem("Move HEAD to child", nullptr, false, free && !s->headChild().isNull()))
            s->actions().moveHead(true, s->headChild());
        // The selected commit's history editing actions.
        const core::HistoryRow* selected = s->selection().kind == SelKind::Commit ? s->history().row(s->selection().id) : nullptr;
        ImGui::Separator();
        if (ImGui::BeginMenu("Selected commit", selected != nullptr)) {
            drawCommitEditItems(*s, *selected);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Interactive rebase...", nullptr, false, free && !s->snapshot()->headUnborn))
            showInteractiveRebaseDialog(*s, "HEAD");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, free))
            s->actions().undo(false);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, free))
            s->actions().undo(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Apply patch...", nullptr, false, free))
            s->showApplyPatchDialog();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        auto& panels = m_settings.data().panels;
        for (const char* name : panel::All) {
            bool visible = panels.count(name) ? panels[name] : panel::defaultVisible(name);
            if (ImGui::MenuItem(name, nullptr, &visible, s != nullptr)) {
                panels[name] = visible;
                m_settings.save();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Previous changed file", "Shift+F6", false, s != nullptr))
            s->nextChangedFile(-1);
        if (ImGui::MenuItem("Next changed file", "F6", false, s != nullptr))
            s->nextChangedFile(+1);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout"))
            resetLayout();
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawRepositoryButtons()
{
    Session* s = (m_session && m_session->opened()) ? m_session.get() : nullptr;
    const std::string busy = s ? s->actions().busyTooltip() : std::string();
    const bool free = s && busy.empty();
    auto tip = [&](const char* normal) { return busy.empty() ? normal : busy.c_str(); };

    // New / Commit-Amend / Undo / Redo
    if (iconButton(ICON_MS_ADD, "##tb_new", tip("New commit on the selection (Ctrl+N)"), free))
        s->newCommitOn(newParents(*s), false);
    ImGui::SameLine();
    const bool amend = s && headSelected(*s);
    const std::string commitLabel = std::string(ICON_MS_CHECK) + (amend ? " Amend" : " Commit");
    if (iconButton(commitLabel.c_str(), "##tb_commit", tip(amend ? "Amend HEAD" : "Commit the index"),
            free && !s->snapshot()->bare))
        s->showCommitDialog(amend);
    ImGui::SameLine();
    if (iconButton(ICON_MS_UNDO, "##tb_undo", tip("Undo (Ctrl+Z)"), free))
        s->actions().undo(false);
    ImGui::SameLine();
    if (iconButton(ICON_MS_REDO, "##tb_redo", tip("Redo (Ctrl+Y)"), free))
        s->actions().undo(true);
    ImGui::SameLine();
    if (iconButton(ICON_MS_REFRESH, "##tb_refresh", "Refresh (F5)", s != nullptr))
        s->refresh();

    // Fetch / Pull / Push
    ImGui::SameLine();
    const bool hasRemotes = s && !s->snapshot()->remotes.empty();
    if (iconButton(ICON_MS_DOWNLOAD " Fetch", "##tb_fetch", tip("Fetch all remotes"), free && hasRemotes))
        s->actions().fetch("", false, false);
    ImGui::SameLine(0, 1);
    if (iconButton(ICON_MS_EXPAND_MORE, "##tb_fetch_menu", tip("Fetch options"), free && hasRemotes))
        ImGui::OpenPopup("##fetch_menu");
    if (ImGui::BeginPopup("##fetch_menu")) {
        for (const auto& r : s->snapshot()->remotes)
            if (ImGui::MenuItem(("Fetch " + r.name).c_str()))
                s->actions().fetch(r.name, false, false);
        ImGui::Separator();
        if (ImGui::MenuItem("Fetch and prune"))
            s->actions().fetch("", true, false);
        if (ImGui::MenuItem("Fetch tags"))
            s->actions().fetch("", false, true);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    std::string pullReason;
    const bool canPull = s && s->pullAvailable(&pullReason);
    const int incoming = s ? s->incoming() : 0;
    const std::string pullLabel = std::string(ICON_MS_ARROW_DOWNWARD) + " Pull" + (incoming ? " \xe2\x86\x93" + std::to_string(incoming) : "");
    if (iconButton(pullLabel.c_str(), "##tb_pull", !busy.empty() ? busy.c_str() : canPull ? "Pull from the upstream" : pullReason.c_str(),
            free && canPull))
        s->actions().pull(PullMode::Config);
    ImGui::SameLine(0, 1);
    if (iconButton(ICON_MS_EXPAND_MORE, "##tb_pull_menu", tip("Pull options"), free && canPull))
        ImGui::OpenPopup("##pull_menu");
    if (ImGui::BeginPopup("##pull_menu")) {
        if (ImGui::MenuItem("Pull (merge)"))
            s->actions().pull(PullMode::Merge);
        if (ImGui::MenuItem("Pull (rebase)"))
            s->actions().pull(PullMode::Rebase);
        if (ImGui::MenuItem("Pull (fast-forward only)"))
            s->actions().pull(PullMode::FastForwardOnly);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const bool canPush = s && !s->snapshot()->headDetached && !s->snapshot()->headUnborn && hasRemotes;
    const int outgoing = s ? s->outgoing() : 0;
    const std::string pushLabel = std::string(ICON_MS_UPLOAD) + " Push" + (outgoing ? " \xe2\x86\x91" + std::to_string(outgoing) : "");
    if (iconButton(pushLabel.c_str(), "##tb_push", tip("Push the current branch"), free && canPush))
        s->pushCurrent();
    ImGui::SameLine(0, 1);
    if (iconButton(ICON_MS_EXPAND_MORE, "##tb_push_menu", tip("Push options"), free && hasRemotes))
        ImGui::OpenPopup("##push_menu");
    if (ImGui::BeginPopup("##push_menu")) {
        if (ImGui::MenuItem("Push to...", nullptr, false, canPush))
            s->showPushToDialog();
        if (ImGui::MenuItem("Force with lease...", nullptr, false, canPush)) {
            const auto* b = s->snapshot()->currentBranch();
            if (b && !b->upstream.empty()) {
                const auto slash = b->upstream.find('/');
                Form f;
                f.title = "Force push";
                f.message = "Overwrite " + b->upstream + " with " + b->name + " (--force-with-lease)?";
                const std::string remote = b->upstream.substr(0, slash), rb = b->upstream.substr(slash + 1), local = b->name;
                f.buttons.push_back({"Force push", [s, remote, local, rb](Form&) { s->actions().push(remote, local, rb, false, true); }});
                f.buttons.push_back({"Cancel", {}});
                m_dialogs.open(std::move(f));
            } else {
                s->showPushToDialog();
            }
        }
        if (ImGui::MenuItem("Push tags")) {
            const auto* b = s->snapshot()->currentBranch();
            std::string remote = s->snapshot()->remotes.front().name;
            if (b && !b->upstream.empty())
                remote = b->upstream.substr(0, b->upstream.find('/'));
            s->actions().push(remote, "", "", false, false, true);
        }
        ImGui::EndPopup();
    }
    // Stash / Pop
    ImGui::SameLine();
    const bool dirty = s && s->status() && !s->status()->empty();
    if (iconButton(ICON_MS_INVENTORY_2, "##tb_stash", tip("Stash changes..."), free && dirty))
        s->showStashDialog();
    ImGui::SameLine();
    if (iconButton(ICON_MS_UNARCHIVE, "##tb_pop", tip("Pop the latest stash"), free && !s->snapshot()->stashes.empty()))
        s->popStash();
}

void App::drawStateBadge()
{
    Session& s = *m_session;
    const auto& snap = *s.snapshot();
    if (snap.state == core::RepoState::None)
        return;
    ImGui::SameLine();
    std::string badge = core::repoStateBadge(snap.state);
    if (!snap.stateDetail.empty())
        badge += " " + snap.stateDetail;
    drawBadge((badge + "###tb_state").c_str(), theme().palette().conflict);
    const bool free = s.actions().busy().empty();
    ImGui::BeginDisabled(!free);
    const bool bisect = snap.state == core::RepoState::Bisecting;
    ImGui::SameLine();
    if (!bisect && ImGui::SmallButton("Continue##tb_continue"))
        s.actions().continueOperation();
    if (!bisect)
        ImGui::SameLine();
    const bool canSkip = snap.state != core::RepoState::Merging;
    if (canSkip && ImGui::SmallButton("Skip##tb_skip"))
        s.actions().skipOperation();
    if (canSkip)
        ImGui::SameLine();
    if (ImGui::SmallButton((bisect ? "Reset##tb_abort" : "Abort##tb_abort")))
        s.actions().abortOperation();
    if (textConflictsOnly(s)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Commit with conflicts##tb_commit_conflicts"))
            s.actions().commitWithConflicts();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Record the text conflicts as first-class conflicts in the commit and finish the %s",
                core::repoStateBadge(snap.state));
    }
    ImGui::EndDisabled();
}

void App::drawToolbar()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##Toolbar", nullptr, flags);
    ImGui::PopStyleVar(2);
    const bool open = m_session && m_session->opened();

    drawRepositoryButtons();
    ImGui::SameLine();

    // Repository switcher (open + recent)
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    const std::string current = m_session ? m_session->displayName() : std::string("No repository");
    if (ImGui::BeginCombo("##tb_repo", current.c_str())) {
        for (size_t i = 0; i < m_settings.data().recent.size(); ++i) {
            const std::string& path = m_settings.data().recent[i];
            const bool selected = m_session && m_session->path().string() == path;
            if (ImGui::Selectable((path + "###switch_" + std::to_string(i)).c_str(), selected) && !selected)
                post([this, path] { openRepository(path); });
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (iconButton(ICON_MS_FOLDER_OPEN, "##tb_open", "Open the working directory", open && !m_session->snapshot()->bare))
        openInFileManager(m_session->snapshot()->workdir);

    if (open) {
        const auto& snap = *m_session->snapshot();
        ImGui::SameLine();
        const std::string branch = snap.headDetached ? std::string("detached") : snap.headBranch;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_MS_CALL_SPLIT);
        ImGui::SameLine(0, 2);
        plainText((branch + "###tb_branch").c_str());
        ImGui::SameLine();
        const std::string headText = snap.head.isNull() ? std::string("(no commit)") : m_session->shortId(snap.head);
        plainText((headText + "###tb_head").c_str());
        if (!snap.head.isNull()) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
                ImGui::TextUnformatted("HEAD");
                ImGui::SameLine();
                idText(snap.head.hex(), headText.size());
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopupContextItem("##tb_head_menu")) {
                copyIdMenuItem("Copy ID", headText, snap.head.hex());
                ImGui::EndPopup();
            }
        }
        drawStateBadge();
    }

    // Activity spinner + Cancel
    std::vector<core::Activity> activities;
    if (m_session)
        activities = m_session->activities();
    for (auto& s : m_closing)
        if (s->opening())
            for (auto& a : s->activities())
                activities.push_back(a);
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(activities, [&](const core::Activity& a) { return now - a.started < std::chrono::milliseconds(200); });
    if (!activities.empty()) {
        ImGui::SameLine();
        spinner("##tb_activity", ImGui::GetFontSize() * 0.45f);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            for (const auto& a : activities) {
                if (a.percent >= 0)
                    ImGui::Text("%s (%d%%)", a.label.c_str(), a.percent);
                else
                    ImGui::TextUnformatted(a.label.c_str());
            }
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        const auto& first = activities.front();
        if (first.percent >= 0)
            ImGui::Text("%s %d%%", first.label.c_str(), first.percent);
        else
            ImGui::TextUnformatted(first.label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##tb_cancel") && m_session)
            m_session->cancelAll();
    }
    ImGui::End();
}

void App::pumpAskpass()
{
    const auto request = m_askpass.pending();
    if (!request || request->id == m_askpassShown)
        return;
    m_askpassShown = request->id;
    Form f;
    f.title = "Credentials";
    f.message = request->prompt;
    Field answer;
    answer.kind = request->secret ? Field::Password : Field::Text;
    answer.id = "answer";
    f.add(answer);
    const std::uint64_t id = request->id;
    f.buttons.push_back({"OK", [this, id](Form& form) { m_askpass.answer(id, form.text("answer")); }});
    f.buttons.push_back({"Cancel", [this, id](Form&) { m_askpass.cancel(id); }});
    m_dialogs.open(std::move(f));
}

void App::pumpClone()
{
    switch (m_clone.state()) {
    case core::CloneService::State::Done: {
        const fs::path dest = m_clone.destination();
        m_clone.reset();
        openRepository(dest);
        break;
    }
    case core::CloneService::State::Failed:
        showError("Clone failed", m_clone.error());
        m_clone.reset();
        break;
    case core::CloneService::State::Cancelled:
        m_clone.reset();
        break;
    default:
        break;
    }
}

} // namespace ggui
