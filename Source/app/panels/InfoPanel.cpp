#include "panels/InfoPanel.hpp"
#include "shell/Dialogs.hpp"

#include "shell/App.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <libgg/GitRunner.hpp>

namespace ggui {

InfoPanel::InfoPanel(Session& session) : m_session(session) { }

void InfoPanel::onSelection(const Selection& sel)
{
    m_selection = sel;
    m_details.reset();
    m_message.clear();
    if (sel.kind == SelKind::Commit || sel.kind == SelKind::Stash)
        m_request = m_session.engine().commitDetails(sel.id);
}

void InfoPanel::onDetails(const core::CommitDetailsEvent& event)
{
    if (event.request != m_request)
        return;
    m_details = event.details;
    m_message = m_details->message;
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
        ImGui::TextUnformatted(m_selection.kind == SelKind::WorkingTree ? "Working tree" : "Index (staged)");
        if (status) {
            ImGui::Text("%zu staged, %zu unstaged, %zu untracked, %zu conflicted", status->staged.size(),
                status->unstaged.size(), status->untracked.size(), status->conflicted.size());
        }
        const bool index = m_selection.kind == SelKind::Index;
        ImGui::InputTextMultiline("##commit_message", &m_commitMessage, ImVec2(-1, ImGui::GetTextLineHeight() * 6));
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
            ImGui::SetTooltip("%s", index ? "Commit the staged changes (git commit)"
                                          : "Commit only the unstaged and untracked changes; the staged changes stay staged");
        const auto snap = m_session.snapshot();
        if (snap && snap->state != core::RepoState::None) {
            ImGui::SeparatorText("Message in progress (MERGE_MSG)");
            if (m_mergeMessageSource != snap->mergeMessage) {
                m_mergeMessage = snap->mergeMessage;
                m_mergeMessageSource = snap->mergeMessage;
            }
            ImGui::InputTextMultiline("##merge_message", &m_mergeMessage, ImVec2(-1, ImGui::GetTextLineHeight() * 6));
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
    const bool editable = m_selection.kind == SelKind::Commit;
    ImGui::InputTextMultiline("##message", &m_message, ImVec2(-1, ImGui::GetTextLineHeight() * 6),
        editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly);
    const bool free = m_session.actions().busy().empty();
    ImGui::BeginDisabled(!editable || !free || m_message == d.message || gg::trim(m_message).empty());
    if (ImGui::Button(ICON_MS_SAVE " Save message###save_message")) {
        if (isHead)
            m_session.actions().amend(m_message, false, true);
        else
            m_session.actions().reword(d.id, m_message);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", isHead ? "Reword HEAD (git commit --amend --only)"
                                       : "Reword this commit; its descendants are rebased onto it");
    if (isHead) {
        const auto status = m_session.status();
        const bool cleanIndex = status && status->staged.empty();
        ImGui::SameLine();
        if (cleanIndex)
            ImGui::TextDisabled("Amend mode: saving rewrites HEAD");
        else
            ImGui::TextDisabled("Staged changes are not included (use Amend... to add them)");
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
        if (ImGui::BeginPopupContextItem("##author_menu", ImGuiPopupFlags_MouseButtonRight)) {
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
        const std::string shortId = m_session.shortId(d.id);
        idText(d.id.hex(), shortId.size(), "commit_id_text");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MS_CONTENT_COPY "###commit_id"))
            copyId(shortId, d.id.hex());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Copy the short ID (%s)", kCopyIdHint);
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
            const std::string id = d.parents[i].shortHex(10) + "###parent_" + std::to_string(i);
            if (selectable(id.c_str(), false, ImGuiSelectableFlags_None, ImGui::CalcTextSize(id.c_str(), nullptr, true)))
                m_session.revealCommit(d.parents[i]);
            if (i + 1 < d.parents.size())
                ImGui::SameLine();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace ggui
