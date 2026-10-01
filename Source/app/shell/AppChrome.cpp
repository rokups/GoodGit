// Main menu, toolbar and global shortcuts (product spec §4.1; docs/spec/ui-spec.md §1).
#include "panels/HistoryPanel.hpp"
#include "panels/InfoPanel.hpp"
#include "platform/Platform.hpp"
#include "panels/CommitMenu.hpp"
#include "panels/RebasePanel.hpp"
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "shell/Widgets.hpp"
#include "util/Ui.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <algorithm>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace ggui {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNewDetachedOnly =
    "New needs a commit that is HEAD's branch tip or has exactly one branch; here it can only be detached.";

// Commit that toolbar/menu commit actions apply to: the selected commit, else HEAD.
core::Oid targetCommit(Session& s)
{
    if (s.selection().kind == SelKind::Commit)
        return s.selection().id;
    return s.snapshot()->head;
}

bool headSelected(Session& s)
{
    return s.selection().kind == SelKind::Commit && s.selection().id == s.snapshot()->head;
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

// Native (index) conflicts left in the working tree.
bool nativeConflicts(Session& s)
{
    const auto st = s.status();
    return st && std::any_of(st->conflicted.begin(), st->conflicted.end(), [](const auto& e) { return !e.firstClass; });
}

// Icon for a dockable panel's View-menu visibility toggle.
const char* panelIcon(const char* name)
{
    if (name == panel::History)
        return ICON_MS_HISTORY;
    if (name == panel::Changes)
        return ICON_MS_CHECKLIST;
    if (name == panel::Info)
        return ICON_MS_INFO;
    if (name == panel::Diff)
        return ICON_MS_DIFFERENCE;
    if (name == panel::Blame)
        return ICON_MS_PERSON_SEARCH;
    if (name == panel::Branches)
        return ICON_MS_FORK_RIGHT;
    if (name == panel::Tags)
        return ICON_MS_SELL;
    if (name == panel::Worktrees)
        return ICON_MS_FOLDER_COPY; // one checkout per folder
    if (name == panel::Remotes)
        return ICON_MS_CLOUD;
    if (name == panel::Stashes)
        return ICON_MS_INVENTORY_2;
    if (name == panel::Reflog)
        return ICON_MS_MANAGE_HISTORY;
    if (name == panel::Operations)
        return ICON_MS_BUILD;
    return ICON_MS_VISIBILITY;
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
    else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global) && free && !s.newCommitBranch(targetCommit(s)).empty())
        s.newCommitOn(targetCommit(s), false);
    else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global) && free)
        s.actions().undo(false);
    else if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global) && free)
        s.actions().undo(true);
}

// A clean Alt tap (ImGui's own toggle: no other key, text or modifier in between) normally switches the
// focused window to its menu layer, which does nothing for a panel without a menu bar. Then the main menu
// bar takes the focus on its menu layer; leaving it (Escape or Alt again) hands focus back.
void App::handleMenuBarKey()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* bar = ImGui::FindWindowByName("##MainMenuBar");
    if (!bar)
        return;
    const bool popupsClosed = m_menuPopups > 0 && g.OpenPopupStack.Size == 0;
    m_menuPopups = g.OpenPopupStack.Size;
    if (m_menuBarReturn != 0 && g.OpenPopupStack.Size == 0) {
        // Escape (or Alt) out of the bar's menu layer, or Escape out of its last menu (ImGui then focuses another window).
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        bool left = g.NavWindow == bar ? g.NavLayer == ImGuiNavLayer_Main : escape;
        // An item was activated: the menus closed and focus stayed on the bar or the dock host. An action that opened
        // a dialog or focused another window itself keeps that.
        // (The closed popup keeps the nav window for a frame, so this watches for a few frames.)
        if (popupsClosed && !escape)
            m_menuActivated = 3;
        if (m_menuActivated > 0) {
            --m_menuActivated;
            if (m_dialogs.anyOpen() || !g.NavWindow || ((g.NavWindow->Flags & ImGuiWindowFlags_Popup) == 0 && g.NavWindow != bar && !g.NavWindow->DockNodeAsHost))
                m_menuActivated = 0;
            else if (g.NavWindow == bar || g.NavWindow->DockNodeAsHost)
                left = true;
        }
        if (left)
            if (ImGuiWindow* back = ImGui::FindWindowByID(m_menuBarReturn))
                ImGui::FocusWindow(back);
        if (left || (g.NavWindow != bar && m_menuActivated == 0)) {
            m_menuBarReturn = 0;
            m_menuActivated = 0;
        }
        if (left)
            return;
    }
    // Alt+X, Alt+Space, Alt+arrows: ImGui's own cancel misses a key that arrives in the same frame as Alt.
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_ReservedForModCtrl && !m_altOtherKey; ++k)
        if ((k < ImGuiKey_LeftCtrl || k > ImGuiKey_RightSuper) && ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), ImGuiInputFlags_None, ImGuiKeyOwner_Any))
            m_altOtherKey = true;
    const bool released = ImGui::IsKeyReleased(ImGuiKey_LeftAlt) || ImGui::IsKeyReleased(ImGuiKey_RightAlt);
    const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
    const bool clean = !m_altOtherKey;
    if (!altDown)
        m_altOtherKey = false;
    const bool tapped = clean && released && m_navPrev.toggleLayer && m_navPrev.idle && !g.IO.KeyCtrl && !g.IO.KeyShift && !g.IO.KeySuper;
    if (!tapped || !g.NavWindow || g.NavWindow == bar || m_navPrev.layer != ImGuiNavLayer_Main || g.OpenPopupStack.Size > 0 || m_dialogs.anyOpen())
        return;
    // ImGui did nothing (the window has no menu layer) or moved to the dock host's tab bars: the menu bar is what Alt is for.
    const bool unchanged = g.NavLayer == ImGuiNavLayer_Main && g.NavWindow->ID == m_navPrev.window;
    const bool tabBars = g.NavLayer == ImGuiNavLayer_Menu && g.NavWindow->DockNodeAsHost != nullptr;
    if (!unchanged && !tabBars)
        return;
    m_menuBarReturn = m_navPrev.window;
    ImGui::FocusWindow(bar);
    bar->NavLastIds[ImGuiNavLayer_Menu] = 0;
    g.NavLayer = ImGuiNavLayer_Menu; // the first menu
    ImGui::NavInitWindow(bar, true);
    ImGui::SetNavCursorVisibleAfterMove();
}

// Called at the end of a frame: what the next frame's Alt release acts on.
void App::snapshotNavToggle()
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    m_navPrev.toggleLayer = g.NavWindowingToggleLayer;
    m_navPrev.idle = g.ActiveId == 0 || g.ActiveIdAllowOverlap;
    m_navPrev.window = g.NavWindow ? g.NavWindow->ID : 0;
    m_navPrev.layer = g.NavLayer;
}

void App::drawRecentMenu()
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    ImGui::InputTextWithHint("##recent_filter", "Filter", &m_recentFilter);
    const auto& recent = m_settings.data().recent;
    const auto names = uniqueRecentNames(recent);
    int shown = 0;
    const std::string currentKey = currentRepoKey();
    std::string forget;
    for (size_t i : recentDisplayOrder(recent, m_settings.data().recentOrder)) {
        const std::string& path = recent[i];
        if (!m_recentFilter.empty() && !containsNoCase(path, m_recentFilter))
            continue;
        std::string detail;
        for (const auto& info : m_recentInfo)
            if (info.path == fs::path(path))
                detail = summaryText(info);
        const std::string label = names[i].text() + "###recent_menu_" + std::to_string(i);
        if (menuItemDimPrefix(ICON_MS_FOLDER, label.c_str(), names[i].prefix.size(), detail.c_str()))
            post([this, path] { openRepository(path); });
        const bool current = path == currentKey;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(current ? "%s" : "%s\nDel removes", path.c_str());
        if (!current && (hoveredDeletePressed() || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))))
            forget = path;
        ++shown;
    }
    if (!forget.empty())
        m_settings.forgetRecent(forget);
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
        if (menuItem(ICON_MS_FOLDER_OPEN, "Open...", "Ctrl+O"))
            pickAndOpenRepository();
        if (menuItem(ICON_MS_CREATE_NEW_FOLDER, "Initialize..."))
            initializeRepository();
        if (menuItem(ICON_MS_CLOUD_DOWNLOAD, "Clone..."))
            showCloneDialog();
        if (beginMenu(ICON_MS_HISTORY, "Recent")) {
            drawRecentMenu();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (menuItem(ICON_MS_OPEN_IN_NEW, "Open working directory", nullptr, false, s && !s->snapshot()->bare))
            openInFileManager(s->snapshot()->workdir);
        if (menuItem(ICON_MS_CONTENT_COPY, "Copy path", nullptr, false, s != nullptr))
            ImGui::SetClipboardText(s->path().string().c_str());
        if (menuItem(ICON_MS_CLOSE, "Close repository", "Ctrl+W", false, s != nullptr))
            post([this] { closeRepository(); });
        if (menuItem(ICON_MS_REFRESH, "Refresh", "F5", false, s != nullptr))
            s->refresh();
        ImGui::Separator();
        if (menuItem(ICON_MS_DOWNLOAD, "Fetch", nullptr, false, free))
            s->actions().fetch("", false, false);
        std::string reason;
        if (menuItem(ICON_MS_ARROW_DOWNWARD, "Pull", nullptr, false, free && s->pullAvailable(&reason)))
            s->actions().pull(PullMode::Config);
        if (menuItem(ICON_MS_UPLOAD, "Push", nullptr, false, free && !s->snapshot()->headDetached))
            s->pushCurrent();
        ImGui::Separator();
        if (menuItem(ICON_MS_SETTINGS, "Settings..."))
            openSettings();
        if (menuItem(ICON_MS_LOGOUT, "Quit", "Ctrl+Q"))
            requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Commit")) {
        const core::Oid at = s ? targetCommit(*s) : core::Oid{};
        const bool attach = s && !s->newCommitBranch(at).empty();
        if (menuItem(ICON_MS_ADD, "New commit", "Ctrl+N", false, free && attach))
            s->newCommitOn(at, false);
        if (!attach && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", kNewDetachedOnly);
        if (menuItem(ICON_MS_ADD_CIRCLE, "New detached commit", nullptr, false, free && !at.isNull()))
            s->newCommitOn(at, true);
        if (menuItem(ICON_MS_CHECK, "Commit...", nullptr, false, free && !s->snapshot()->bare))
            s->showCommitDialog(false);
        if (menuItem(ICON_MS_EDIT_NOTE, "Amend...", nullptr, false, free && !s->snapshot()->headUnborn))
            s->showCommitDialog(true);
        // The selected commit's history editing actions.
        const core::HistoryRow* selected = s && s->selection().kind == SelKind::Commit ? s->history().row(s->selection().id) : nullptr;
        ImGui::Separator();
        if (beginMenu(ICON_MS_LIST_ALT, "Selected commit", selected != nullptr)) {
            drawCommitEditItems(*s, *selected);
            ImGui::EndMenu();
        }
        if (menuItem(ICON_MS_LOW_PRIORITY, "Interactive rebase...", nullptr, false, free && !s->snapshot()->headUnborn))
            showInteractiveRebaseDialog(*s, "HEAD");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (menuItem(ICON_MS_UNDO, "Undo", "Ctrl+Z", false, free))
            s->actions().undo(false);
        if (menuItem(ICON_MS_REDO, "Redo", "Ctrl+Y", false, free))
            s->actions().undo(true);
        ImGui::Separator();
        if (menuItem(ICON_MS_CONTENT_PASTE, "Apply patch...", nullptr, false, free))
            s->showApplyPatchDialog();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        auto& panels = m_settings.data().panels;
        for (const char* name : panel::All) {
            bool visible = panels.count(name) ? panels[name] : panel::defaultVisible(name);
            if (menuItem(panelIcon(name), name, nullptr, &visible, s != nullptr)) {
                panels[name] = visible;
                m_settings.save();
            }
        }
        ImGui::Separator();
        if (menuItem(ICON_MS_NAVIGATE_BEFORE, "Previous changed file", "Shift+F6", false, s != nullptr))
            s->nextChangedFile(-1);
        if (menuItem(ICON_MS_NAVIGATE_NEXT, "Next changed file", "F6", false, s != nullptr))
            s->nextChangedFile(+1);
        ImGui::Separator();
        if (menuItem(ICON_MS_GRID_VIEW, "Reset layout"))
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
    const bool attach = s && !s->newCommitBranch(targetCommit(*s)).empty();
    if (iconButton(ICON_MS_ADD, "##tb_new", tip(attach ? "New commit on the selection (Ctrl+N)" : kNewDetachedOnly), free && attach))
        s->newCommitOn(targetCommit(*s), false);
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
            if (menuItem(ICON_MS_DOWNLOAD, ("Fetch " + r.name).c_str()))
                s->actions().fetch(r.name, false, false);
        ImGui::Separator();
        if (menuItem(ICON_MS_DELETE_SWEEP, "Fetch and prune"))
            s->actions().fetch("", true, false);
        if (menuItem(ICON_MS_SELL, "Fetch tags"))
            s->actions().fetch("", false, true);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    std::string pullReason;
    const bool canPull = s && s->pullAvailable(&pullReason);
    const int incoming = s ? s->incoming() : 0;
    const std::string pullLabel = std::string(ICON_MS_ARROW_DOWNWARD) + " Pull" + (incoming ? " " ICON_MS_ARROW_DOWNWARD_ALT + std::to_string(incoming) : "");
    if (iconButton(pullLabel.c_str(), "##tb_pull", !busy.empty() ? busy.c_str() : canPull ? "Pull from the upstream" : pullReason.c_str(),
            free && canPull))
        s->actions().pull(PullMode::Config);
    ImGui::SameLine(0, 1);
    if (iconButton(ICON_MS_EXPAND_MORE, "##tb_pull_menu", tip("Pull options"), free && canPull))
        ImGui::OpenPopup("##pull_menu");
    if (ImGui::BeginPopup("##pull_menu")) {
        if (menuItem(ICON_MS_MERGE, "Pull (merge)"))
            s->actions().pull(PullMode::Merge);
        if (menuItem(ICON_MS_LOW_PRIORITY, "Pull (rebase)"))
            s->actions().pull(PullMode::Rebase);
        if (menuItem(ICON_MS_FAST_FORWARD, "Pull (fast-forward only)"))
            s->actions().pull(PullMode::FastForwardOnly);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const bool canPush = s && !s->snapshot()->headDetached && !s->snapshot()->headUnborn && hasRemotes;
    const int outgoing = s ? s->outgoing() : 0;
    const std::string pushLabel = std::string(ICON_MS_UPLOAD) + " Push" + (outgoing ? " " ICON_MS_ARROW_UPWARD_ALT + std::to_string(outgoing) : "");
    if (iconButton(pushLabel.c_str(), "##tb_push", tip("Push the current branch"), free && canPush))
        s->pushCurrent();
    ImGui::SameLine(0, 1);
    if (iconButton(ICON_MS_EXPAND_MORE, "##tb_push_menu", tip("Push options"), free && hasRemotes))
        ImGui::OpenPopup("##push_menu");
    if (ImGui::BeginPopup("##push_menu")) {
        if (menuItem(ICON_MS_UPLOAD, "Push to...", nullptr, false, canPush))
            s->showPushToDialog();
        if (menuItem(ICON_MS_WARNING, "Force with lease...", nullptr, false, canPush)) {
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
        if (menuItem(ICON_MS_SELL, "Push tags")) {
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
    if (iconButton(ICON_MS_OUTBOX, "##tb_pop", tip("Pop the latest stash"), free && !s->snapshot()->stashes.empty()))
        s->popStash();
}

void App::drawEditBanner()
{
    // An Edit commit session (§4.3): amending HEAD restacks what comes after it.
    Session& s = *m_session;
    const auto& edit = s.editSession();
    if (!edit)
        return;
    ImGui::SameLine();
    const std::string text = "Editing " + edit->commit.substr(0, s.shortIdLength()) + " of " + edit->branch
        + " \xe2\x80\x94 amend to restack " + std::to_string(edit->descendants) + " descendant"
        + (edit->descendants == 1 ? "" : "s");
    drawBadge((text + "###tb_edit").c_str(), theme().palette().conflictFill);
    const std::string branch = edit->branch;
    ImGui::BeginDisabled(!s.actions().busy().empty());
    ImGui::SameLine();
    if (smallButton(ICON_MS_KEYBOARD_RETURN, ("Return to " + branch + "##tb_edit_return").c_str()))
        s.actions().checkout(branch, false);
    ImGui::SameLine();
    if (smallButton(ICON_MS_CLOSE, "Stop editing##tb_edit_stop"))
        s.stopEditing();
    ImGui::EndDisabled();
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
    drawBadge((badge + "###tb_state").c_str(), theme().palette().conflictFill);
    // While git waits for the todo list (ggui as sequence.editor) the rebase has not started yet.
    const bool free = s.actions().busy().empty() && !editingForGit();
    if (editingForGit() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("git rebase -i waits for the list in the todo editor: Save or Cancel it first");
    ImGui::BeginDisabled(!free);
    const bool bisect = snap.state == core::RepoState::Bisecting;
    ImGui::SameLine();
    if (!bisect && smallButton(ICON_MS_PLAY_ARROW, "Continue##tb_continue"))
        s.actions().continueOperation();
    if (!bisect)
        ImGui::SameLine();
    const bool canSkip = snap.state != core::RepoState::Merging;
    if (canSkip && smallButton(ICON_MS_SKIP_NEXT, "Skip##tb_skip"))
        s.actions().skipOperation();
    if (canSkip)
        ImGui::SameLine();
    if (smallButton(bisect ? ICON_MS_RESTART_ALT : ICON_MS_CANCEL, (bisect ? "Reset##tb_abort" : "Abort##tb_abort")))
        s.actions().abortOperation();
    if (textConflictsOnly(s)) {
        ImGui::SameLine();
        if (smallButton(ICON_MS_CHECK_CIRCLE, "Commit with conflicts##tb_commit_conflicts"))
            s.actions().commitWithConflicts();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Record the text conflicts as first-class conflicts in the commit and finish the %s",
                core::repoStateBadge(snap.state));
    }
    if (snap.rebase) {
        // A stopped interactive rebase (§4.10 native, §4.13): amend, edit the rest, see where it is.
        if (!nativeConflicts(s)) {
            ImGui::SameLine();
            if (smallButton(ICON_MS_EDIT_NOTE, "Amend and continue##tb_amend_continue"))
                s.actions().amendAndContinue();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Amend HEAD with the staged changes (git commit --amend), then continue the rebase");
        }
        ImGui::SameLine();
        if (smallButton(ICON_MS_LIST_ALT, "Edit remaining todo##tb_edit_todo")) {
            RebasePanel::Request r;
            r.remaining = true;
            s.rebase().open(std::move(r));
        }
    }
    ImGui::EndDisabled();
    if (snap.rebase) {
        ImGui::SameLine();
        if (smallButton(ICON_MS_PENDING_ACTIONS, "Progress##tb_rebase_progress"))
            ImGui::OpenPopup("##rebase_progress");
        if (ImGui::BeginPopup("##rebase_progress")) {
            drawRebaseProgress(s, *snap.rebase);
            ImGui::EndPopup();
        }
    }
}

void App::drawRebaseProgress(Session& s, const core::RebaseProgress& rebase)
{
    const Palette& p = theme().palette();
    auto line = [&](const core::RebaseStep& step) {
        std::string text = step.action;
        if (!step.commit.empty())
            text += " " + step.commit.substr(0, s.shortIdLength());
        if (!step.text.empty())
            text += " " + step.text;
        return text;
    };
    const std::string branch = rebase.headName.rfind("refs/heads/", 0) == 0 ? rebase.headName.substr(11) : "detached HEAD";
    const size_t done = rebase.done.empty() ? 0 : rebase.done.size() - 1;
    plainText(("Rebasing " + branch + ": " + std::to_string(done) + " done, " + std::to_string(rebase.remaining.size())
                  + " remaining###rp_title").c_str());
    ImGui::SeparatorText("Done");
    for (size_t i = 0; i < done; ++i)
        ImGui::TextDisabled("%s", line(rebase.done[i]).c_str());
    if (done == 0)
        ImGui::TextDisabled("(nothing yet)");
    ImGui::SeparatorText("Stopped at");
    if (!rebase.done.empty()) {
        const core::RebaseStep& current = rebase.done.back();
        ImGui::PushStyleColor(ImGuiCol_Text, p.conflict);
        plainText((line(current) + "###rp_current").c_str());
        ImGui::PopStyleColor();
        std::string reason;
        if (nativeConflicts(s))
            reason = "Conflicts: resolve them (or Commit with conflicts), then Continue.";
        else if (current.action == "edit")
            reason = "Edit: change the commit (Amend and continue), or Continue as it is.";
        else if (current.action == "break")
            reason = "Break: Continue when you are ready.";
        else if (current.action == "exec")
            reason = "The command failed: fix the problem, then Continue.";
        else
            reason = "Continue to go on (or Skip this commit).";
        plainText((reason + "###rp_reason").c_str());
    }
    ImGui::SeparatorText("Remaining");
    for (size_t i = 0; i < rebase.remaining.size(); ++i)
        plainText((line(rebase.remaining[i]) + "###rp_next_" + std::to_string(i)).c_str());
    if (rebase.remaining.empty())
        ImGui::TextDisabled("(nothing: Continue finishes the rebase)");
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
    // Named for the window navigator (Ctrl+Tab); "###" keeps the ID free of the title.
    ImGui::Begin("Toolbar###Toolbar", nullptr, flags);
    ImGui::PopStyleVar(2);
    const bool open = m_session && m_session->opened();

    drawRepositoryButtons();
    ImGui::SameLine();

    // Repository switcher (open + recent)
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    const auto& recent = m_settings.data().recent;
    const auto names = uniqueRecentNames(recent);
    const std::string currentKey = currentRepoKey();
    std::string current = m_session ? m_session->displayName() : std::string("No repository");
    for (size_t i = 0; i < recent.size(); ++i)
        if (!currentKey.empty() && recent[i] == currentKey)
            current = names[i].text();
    if (ImGui::BeginCombo("##tb_repo", current.c_str())) {
        std::string forget;
        for (size_t i : recentDisplayOrder(recent, m_settings.data().recentOrder)) {
            const std::string& path = recent[i];
            const bool selected = path == currentKey;
            const std::string label = names[i].text() + "###switch_" + std::to_string(i);
            if (selectableDimPrefix(label.c_str(), names[i].prefix.size(), selected, 0, ImVec2(0, 0)) && !selected)
                post([this, path] { openRepository(path); });
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(selected ? "%s" : "%s\nDel removes", path.c_str());
            if (!selected && (hoveredDeletePressed() || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))))
                forget = path;
        }
        if (!forget.empty())
            m_settings.forgetRecent(forget);
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
            if (beginContextMenu("##tb_head_menu")) {
                copyIdMenuItem("Copy ", headText, snap.head.hex());
                ImGui::EndPopup();
            }
        }
        drawStateBadge();
        drawEditBanner();
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
        if (smallButton(ICON_MS_CANCEL, "Cancel##tb_cancel") && m_session)
            m_session->cancelAll();
    }
    ImGui::End();
}

void App::pumpSequenceEditor()
{
    Session* s = (m_session && m_session->opened()) ? m_session.get() : nullptr;
    const std::string gitDir = s ? s->snapshot()->gitDir.string() : std::string();
    m_sequenceEditor.setRepository(gitDir);
    if (m_sequenceOpen != 0 && !m_sequenceEditor.waiting(m_sequenceOpen)) {
        // git-gg went away (the rebase was interrupted in its terminal).
        m_sequenceOpen = 0;
        if (s && s->rebase().editingForGit()) {
            s->rebase().close();
            notify(Notice::Warning, "git rebase -i stopped waiting",
                "git no longer waits for the todo list (interrupted in its terminal); nothing was handed over.");
        }
    }
    const auto request = m_sequenceEditor.pending();
    if (!request)
        return;
    if (!s || request->gitDir != gitDir) {
        m_sequenceEditor.cancel(request->id); // the repository closed meanwhile
        return;
    }
    RebasePanel& panel = s->rebase();
    if (panel.isOpen()) {
        if (m_sequenceNoticed != request->id) {
            m_sequenceNoticed = request->id;
            notify(Notice::Info, "git rebase -i is waiting",
                "git rebase -i waits for the todo editor: Start or Cancel the open todo to see git's list.");
        }
        return;
    }
    const std::uint64_t id = request->id;
    m_sequenceEditor.shown(id);
    m_sequenceOpen = id;
    RebasePanel::Request r;
    RebasePanel::Request::Sequence sequence;
    sequence.context = request->context;
    sequence.remaining = request->remaining;
    sequence.done = [this, id](std::optional<std::string> text) {
        if (text)
            m_sequenceEditor.save(id, *text);
        else
            m_sequenceEditor.cancel(id);
        if (m_sequenceOpen == id)
            m_sequenceOpen = 0;
    };
    r.sequence = std::move(sequence);
    panel.open(std::move(r));
    m_platform.raise();
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
