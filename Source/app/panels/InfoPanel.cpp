#include "panels/InfoPanel.hpp"
#include "shell/Dialogs.hpp"
#include "panels/HistoryPanel.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <libgg/GitRunner.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ggui {

namespace {

// A message field of the panel: its context menu has "Word wrap", and a sizer below it resizes it by whole
// lines (shared by all the fields); both are kept in the view settings.
void messageField(Session& session, const char* id, std::string* text, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None)
{
    bool wrap = session.app().settings().data().infoWrapMessage;
    if (wrap)
        flags |= ImGuiInputTextFlags_WordWrap;
    int& lines = session.app().settings().data().infoMessageLines;
    const float lineHeight = ImGui::GetTextLineHeight();
    ImGui::InputTextMultiline(id, text, ImVec2(-1, lineHeight * static_cast<float>(lines)), flags);
    if (beginContextMenu((std::string(id) + "_menu").c_str())) {
        if (menuItem(nullptr, "Word wrap", nullptr, &wrap)) {
            session.app().settings().data().infoWrapMessage = wrap;
            Settings::markViewDirty();
        }
        ImGui::EndPopup();
    }

    // The sizer: snug under the field, a drag follows the mouse and snaps to whole lines.
    const std::string sizerId = std::string(id) + "_sizer";
    const ImGuiID stateId = ImGui::GetID(sizerId.c_str());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::InvisibleButton(sizerId.c_str(), ImVec2(-1, std::max(4.0f, std::round(ImGui::GetFontSize() * 0.35f))));
    ImGui::PopItemFlag();
    const bool active = ImGui::IsItemActive();
    const bool hot = active || ImGui::IsItemHovered();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    int next = lines;
    if (ImGui::IsItemActivated())
        storage->SetInt(stateId, lines);
    if (active) {
        const float dragged = static_cast<float>(storage->GetInt(stateId, lines)) * lineHeight + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y;
        next = static_cast<int>(std::round(dragged / lineHeight));
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        next = 6;
        storage->SetInt(stateId, next); // the press that holds on does not drag from the old height
    }
    if (hot)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float y = std::round((lo.y + hi.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, y), ImVec2(hi.x, y),
        ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : hot ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
    next = std::clamp(next, 2, 40);
    if (next != lines) {
        lines = next;
        Settings::markViewDirty();
    }
}

} // namespace

InfoPanel::InfoPanel(Session& session) : m_session(session) { }

void InfoPanel::onSelection(const Selection& sel)
{
    m_historySelection = sel;
    if (!m_override)
        show(sel);
}

void InfoPanel::setOverride(const std::optional<Selection>& sel)
{
    if (sel == m_override)
        return;
    m_override = sel;
    const Selection& shown = m_override ? *m_override : m_historySelection;
    if (!(shown == m_selection))
        show(shown);
}

void InfoPanel::show(const Selection& sel)
{
    m_selection = sel;
    m_details.reset();
    m_message.clear();
    if (sel.kind == SelKind::WorkingTree || sel.kind == SelKind::Index)
        m_session.requestConfig(); // the identity shown as the author; arrives asynchronously
    if (sel.kind == SelKind::Commit || sel.kind == SelKind::Stash)
        m_request = m_session.engine().commitDetails(sel.id);
}

// A parent clicked: History moves to it. While the Blame panel's line is shown instead, that selection ends
// (the override drops when the Blame panel draws, later in the same frame) so the parent is what is shown.
void InfoPanel::revealParent(const core::Oid& id)
{
    if (m_override)
        m_session.clearBlameSelection();
    m_session.revealCommit(id);
}

void InfoPanel::onDetails(const core::CommitDetailsEvent& event)
{
    if (event.request != m_request)
        return;
    m_details = event.details;
    m_message = m_details->message;
}

// The rows a real commit shows, filled with what the commit would be if made now: the configured
// identity (read asynchronously, see onSelection), the current time, HEAD (and MERGE_HEAD) as
// parents and the branch it would advance.
void InfoPanel::drawPendingCommitInfo(const core::StatusResult* status, const core::Snapshot* snap)
{
    const Palette& p = theme().palette();
    if (!ImGui::BeginTable("##info_table", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    auto label = [](const char* text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", text);
        ImGui::TableSetColumnIndex(1);
    };
    auto configValue = [&](const char* key) {
        const auto& cfg = m_session.config();
        const auto scope = cfg.find("effective");
        if (scope == cfg.end())
            return std::string();
        const auto it = scope->second.find(key);
        return it == scope->second.end() ? std::string() : it->second;
    };
    label("Author");
    const std::string name = configValue("user.name");
    const std::string email = configValue("user.email");
    if (m_session.config().empty())
        ImGui::TextDisabled("Reading git configuration...");
    else if (name.empty() && email.empty())
        ImGui::TextDisabled("(user.name and user.email are not set)");
    else
        plainText((name + " <" + email + ">###author").c_str());
    label("Date");
    ImGui::TextUnformatted(core::formatTime(static_cast<std::int64_t>(std::time(nullptr)), true).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(set on commit)");
    label("State");
    ImGui::PushStyleColor(ImGuiCol_Text, p.unpublished);
    ImGui::TextUnformatted("Not published");
    ImGui::PopStyleColor();
    label("Commit");
    ImGui::TextDisabled("(not committed)");
    label("Branch");
    if (!snap)
        ImGui::TextDisabled("...");
    else if (snap->headDetached)
        plainText("(detached HEAD)###branch");
    else
        plainText((snap->headBranch + "###branch").c_str());
    label("Changes");
    if (status) {
        ImGui::Text("%zu staged, %zu unstaged, %zu untracked, %zu conflicted", status->staged.size(),
            status->unstaged.size(), status->untracked.size(), status->conflicted.size());
    }
    label("Parents");
    std::vector<core::Oid> parents;
    if (snap && !snap->head.isNull())
        parents.push_back(snap->head);
    if (snap)
        parents.insert(parents.end(), snap->mergeHeads.begin(), snap->mergeHeads.end());
    if (parents.empty())
        ImGui::TextDisabled("(root commit)");
    for (size_t i = 0; i < parents.size(); ++i) {
        const std::string id = m_session.shortId(parents[i]) + "###parent_" + std::to_string(i);
        if (selectableDimRange(id.c_str(), kIdPrefixLength, kShortIdLength, false, ImGuiSelectableFlags_None,
                ImGui::CalcTextSize(id.c_str(), nullptr, true)))
            revealParent(parents[i]);
        if (const core::HistoryRow* row = m_session.history().row(parents[i])) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            // The parents share the line: each subject gets its part of what is left.
            textElided(row->subject, nullptr, true, ImGui::GetContentRegionAvail().x / static_cast<float>(parents.size() - i));
            ImGui::PopStyleColor();
        }
        if (i + 1 < parents.size())
            ImGui::SameLine();
    }
    ImGui::EndTable();
}

void InfoPanel::draw(bool* open)
{
    if (!ImGui::Begin(panel::Info, open)) {
        ImGui::End();
        return;
    }
    const Palette& p = theme().palette();
    if (m_selection.kind == SelKind::WorkingTree || m_selection.kind == SelKind::Index) {
        const auto status = m_session.status();
        const bool index = m_selection.kind == SelKind::Index;
        messageField(m_session, "##commit_message", &m_commitMessage);
        const bool nothing =
            !status || (index ? status->staged.empty() : status->unstaged.empty() && status->untracked.empty());
        ImGui::BeginDisabled(!m_session.actions().busy().empty() || nothing || gg::trim(m_commitMessage).empty());
        if (ImGui::Button(ICON_MS_CHECK " Commit###info_commit")) {
            Session* session = &m_session;
            std::string* field = &m_commitMessage;
            auto done = [session, field](const core::MutationFinishedEvent& e) {
                if (e.outcome == core::Outcome::Ok)
                    field->clear();
                else if (e.outcome != core::Outcome::Cancelled)
                    session->app().showError(e.label, e.detail.empty() ? e.message : e.detail);
            };
            if (index)
                m_session.actions().commit(m_commitMessage, false, CommitMode::Index, {}, done);
            else
                m_session.actions().commitWorktree(m_commitMessage, done);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            tooltip("%s", index ? "Commit the staged changes (git commit)"
                                : "Commit only the unstaged and untracked changes; the staged changes stay staged");
        if (index && !nothing) {
            const std::string warning = m_session.commitWarningText();
            if (!warning.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, p.warning);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted((std::string(ICON_MS_WARNING " ") + warning).c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
        }
        const auto snap = m_session.snapshot();
        drawPendingCommitInfo(status.get(), snap.get());
        if (snap && snap->state != core::RepoState::None) {
            ImGui::SeparatorText("Message in progress (MERGE_MSG)");
            if (m_mergeMessageSource != snap->mergeMessage) {
                m_mergeMessage = snap->mergeMessage;
                m_mergeMessageSource = snap->mergeMessage;
            }
            messageField(m_session, "##merge_message", &m_mergeMessage);
            ImGui::BeginDisabled(!m_session.actions().busy().empty() || m_mergeMessage == snap->mergeMessage);
            if (button(ICON_MS_SAVE, "Save message##save_merge_message"))
                m_session.actions().saveMergeMessage(m_mergeMessage);
            ImGui::EndDisabled();
        }
        ImGui::End();
        return;
    }
    if (!m_details) {
        if (m_selection.kind != SelKind::None)
            ImGui::TextDisabled("Loading...");
        ImGui::End();
        return;
    }
    const auto& d = *m_details;
    const auto snap = m_session.snapshot();
    const bool isHead = snap && m_selection.kind == SelKind::Commit && d.id == snap->head;
    // Rewording HEAD is an amend (git runs the commit hooks); any other commit is rewritten in
    // memory with its descendants.
    const bool stash = m_selection.kind == SelKind::Stash;
    const bool editable = m_selection.kind == SelKind::Commit || stash;
    messageField(m_session, "##message", &m_message, editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly);
    const bool free = m_session.actions().busy().empty();
    const auto status = m_session.status();
    const bool stagedPresent = status && !status->staged.empty();
    ImGui::BeginDisabled(!editable || !free || m_message == d.message || gg::trim(m_message).empty());
    // HEAD: a red "Amend HEAD" that asks first; anything else is saved straight away.
    if (isHead ? dangerButton(ICON_MS_SAVE, "Amend HEAD###save_message")
               : button(ICON_MS_SAVE, "Save message###save_message")) {
        if (stash) {
            m_session.actions().stashReword(m_selection.stashIndex, d.id, m_message);
        } else if (isHead) {
            Form f;
            f.title = "Amend HEAD";
            f.message = "This rewrites HEAD with the new message.";
            if (stagedPresent)
                f.message += "\n\nStaged changes are not included (tick Amend in Commit... to add them).";
            Session* session = &m_session;
            const std::string message = m_message;
            f.buttons.push_back({"Amend", [session, message](Form&) { session->actions().amend(message, false, true); }});
            f.buttons.push_back({"Cancel", {}});
            m_session.app().dialogs().open(std::move(f));
        } else {
            m_session.actions().reword(d.id, m_message);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        std::string tip = stash  ? "Change the stash message; its content and position stay"
                        : isHead ? "Reword HEAD (git commit --amend --only)"
                                 : "Reword this commit; its descendants are rebased onto it";
        if (isHead && stagedPresent)
            tip += "\nStaged changes are not included (tick Amend in Commit... to add them)";
        tooltip("%s", tip.c_str());
    }

    if (ImGui::BeginTable("##info_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        auto label = [](const char* text) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", text);
            ImGui::TableSetColumnIndex(1);
        };
        label("Author");
        const std::string author = d.authorName + " <" + d.authorEmail + ">";
        plainText((author + "###author").c_str());
        if (beginContextMenu("##author_menu", ImGuiPopupFlags_MouseButtonRight)) {
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy name"))
                ImGui::SetClipboardText(d.authorName.c_str());
            if (menuItem(ICON_MS_CONTENT_COPY, "Copy email"))
                ImGui::SetClipboardText(d.authorEmail.c_str());
            if (menuItem(ICON_MS_EDIT, "Edit author...", nullptr, false, free && m_selection.kind == SelKind::Commit)) {
                Form f;
                f.title = "Edit author";
                f.add(Field{Field::Text, "name", "Name", d.authorName});
                f.add(Field{Field::Text, "email", "Email", d.authorEmail});
                const core::Oid id = d.id;
                Session* session = &m_session;
                f.buttons.push_back({"Save",
                    [session, id](Form& form) {
                        session->actions().editAuthor(id, gg::trim(form.text("name")), gg::trim(form.text("email")));
                    },
                    [](const Form& form) { return !gg::trim(form.text("name")).empty(); }});
                f.buttons.push_back({"Cancel", {}});
                m_session.app().dialogs().open(std::move(f));
            }
            ImGui::EndPopup();
        }
        if (d.committerName != d.authorName || d.committerEmail != d.authorEmail) {
            label("Committer");
            const std::string committer = d.committerName + " <" + d.committerEmail + ">";
            ImGui::TextUnformatted(committer.c_str());
        }
        label("Date");
        ImGui::TextUnformatted(core::formatTime(d.authorTime, true).c_str());
        if (d.committerTime != d.authorTime) {
            ImGui::SameLine();
            ImGui::TextDisabled("(committed %s)", core::formatTime(d.committerTime, true).c_str());
        }
        label("State");
        if (d.published) {
            ImGui::TextUnformatted(ICON_MS_LOCK " Published");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, p.unpublished);
            ImGui::TextUnformatted("Not published");
            ImGui::PopStyleColor();
        }
        label("Commit");
        fullIdText(d.id.hex(), "commit_id_text", true);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MS_CONTENT_COPY "###commit_id"))
            ImGui::SetClipboardText(d.id.shortHex(kShortIdLength).c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            tooltip("Copy the short ID");
        if (const ConflictList* conflicts = m_session.conflictsOf(d.id)) {
            label("Conflicts");
            for (size_t i = 0; i < conflicts->size(); ++i) {
                const auto& [path, sides] = (*conflicts)[i];
                const std::string text = path + " (" + std::to_string(sides) + " sides)###conflict_" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, p.conflict);
                if (selectable(text.c_str()))
                    m_session.blameFile(path, d.id);
                ImGui::PopStyleColor();
            }
        }
        label("Parents");
        if (d.parents.empty())
            ImGui::TextDisabled("(root commit)");
        for (size_t i = 0; i < d.parents.size(); ++i) {
            const std::string id = d.parents[i].shortHex(kShortIdLength) + "###parent_" + std::to_string(i);
            if (selectableDimRange(id.c_str(), kIdPrefixLength, kShortIdLength, false, ImGuiSelectableFlags_None,
                    ImGui::CalcTextSize(id.c_str(), nullptr, true)))
                revealParent(d.parents[i]);
            if (i + 1 < d.parents.size())
                ImGui::SameLine();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace ggui
