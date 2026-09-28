#include "shell/Dialogs.hpp"
#include "shell/Theme.hpp"

#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

namespace ggui {

const Field* Form::field(const std::string& id) const
{
    for (const auto& f : fields)
        if (f.id == id)
            return &f;
    return nullptr;
}

std::string Form::text(const std::string& id) const
{
    const Field* f = field(id);
    return f ? f->text : std::string();
}

bool Form::checked(const std::string& id) const
{
    const Field* f = field(id);
    return f && f->checked;
}

int Form::choice(const std::string& id) const
{
    const Field* f = field(id);
    return f ? f->choice : 0;
}

void Dialogs::open(Form form) { m_queue.push_back(std::move(form)); }

void Dialogs::draw()
{
    if (m_queue.empty())
        return;
    Form& form = m_queue.front();
    if (!m_opened) {
        ImGui::OpenPopup(form.title.c_str());
        m_opened = true;
        m_focusFirst = true;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 28, 0), ImVec2(vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.9f));
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal(form.title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Closed by ImGui (should not happen for modals without a close button).
        m_queue.erase(m_queue.begin());
        m_opened = false;
        return;
    }
    if (form.icon) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme().palette().errorBg);
        ImGui::TextUnformatted(form.icon);
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    if (!form.message.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40);
        ImGui::TextUnformatted(form.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    for (size_t i = 0; i < form.revealRows.size(); ++i) {
        const auto& [text, id] = form.revealRows[i];
        ImGui::TextUnformatted(text.c_str());
        if (id.empty())
            continue;
        ImGui::SameLine();
        ImGui::PushID(("reveal_" + std::to_string(i)).c_str());
        if (ImGui::SmallButton("Reveal") && form.onReveal) {
            form.onReveal(id);
            keepOpen = false;
        }
        ImGui::PopID();
    }
    bool enterPressed = false;
    for (size_t i = 0; i < form.fields.size(); ++i) {
        Field& f = form.fields[i];
        if (f.visible && !f.visible(form))
            continue;
        const std::string id = "##" + f.id;
        if (m_focusFirst && (f.kind == Field::Text || f.kind == Field::Password || f.kind == Field::Multiline)) {
            ImGui::SetKeyboardFocusHere();
            m_focusFirst = false;
        }
        switch (f.kind) {
        case Field::Text:
        case Field::Password:
            if (!f.label.empty())
                ImGui::TextUnformatted(f.label.c_str());
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 28);
            if (ImGui::InputTextWithHint(id.c_str(), f.hint.c_str(), &f.text,
                    ImGuiInputTextFlags_EnterReturnsTrue | (f.kind == Field::Password ? ImGuiInputTextFlags_Password : 0)))
                enterPressed = true;
            break;
        case Field::Multiline:
            if (!f.label.empty())
                ImGui::TextUnformatted(f.label.c_str());
            ImGui::InputTextMultiline(id.c_str(), &f.text, ImVec2(ImGui::GetFontSize() * 36, ImGui::GetTextLineHeight() * 8));
            break;
        case Field::Check:
            ImGui::Checkbox((f.label + id).c_str(), &f.checked);
            break;
        case Field::Combo: {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
            const char* preview = f.options.empty() ? "" : f.options[static_cast<size_t>(f.choice)].c_str();
            if (ImGui::BeginCombo((f.label + id).c_str(), preview)) {
                bool pickFirst = false;
                if (f.filterable) {
                    if (ImGui::IsWindowAppearing()) {
                        f.filter.clear();
                        ImGui::SetKeyboardFocusHere();
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    pickFirst = ImGui::InputTextWithHint("##filter", ICON_MS_SEARCH " Filter", &f.filter,
                        ImGuiInputTextFlags_EnterReturnsTrue);
                }
                for (size_t k = 0; k < f.options.size(); ++k) {
                    if (!containsNoCase(f.options[k], f.filter))
                        continue;
                    if (pickFirst) {
                        f.choice = static_cast<int>(k);
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if (ImGui::Selectable(f.options[k].c_str(), static_cast<int>(k) == f.choice))
                        f.choice = static_cast<int>(k);
                }
                ImGui::EndCombo();
            }
            break;
        }
        case Field::Info:
            ImGui::TextDisabled("%s", f.text.c_str());
            break;
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    int clicked = -1;
    for (size_t b = 0; b < form.buttons.size(); ++b) {
        const FormButton& button = form.buttons[b];
        const bool enabled = !button.enabled || button.enabled(form);
        if (b)
            ImGui::SameLine();
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(button.label.c_str()))
            clicked = static_cast<int>(b);
        ImGui::EndDisabled();
        // Enter on a single-line field activates the first (primary) button.
        if (b == 0 && enabled && enterPressed)
            clicked = 0;
    }
    if (clicked < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape))
        clicked = static_cast<int>(form.buttons.size()) - 1; // last button = Cancel
    if (clicked >= 0 || !keepOpen) {
        std::function<void(Form&)> action;
        if (clicked >= 0)
            action = form.buttons[static_cast<size_t>(clicked)].action;
        Form done = std::move(form);
        m_queue.erase(m_queue.begin());
        m_opened = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (action)
            action(done);
        return;
    }
    ImGui::EndPopup();
}

void Dialogs::pushRefused(Session& session, const std::string& detail)
{
    Form f;
    f.title = "Push refused";
    f.message = "These commits contain first-class conflicts and are never pushed. Resolve them first.";
    for (const auto& line : gg::splitLines(detail)) {
        if (line.empty())
            continue;
        if (line[0] == ' ') {
            f.revealRows.emplace_back(line, "");
            continue;
        }
        const auto space = line.find(' ');
        const std::string id = line.substr(0, space);
        f.revealRows.emplace_back(line.substr(0, 10) + line.substr(space == std::string::npos ? line.size() : space), id);
    }
    // File rows are indented text only: drop their (empty) reveal target.
    Session* s = &session;
    f.onReveal = [s](const std::string& id) {
        if (!id.empty())
            s->revealCommit(core::Oid::fromHex(id));
    };
    f.buttons.push_back({"Close", {}});
    open(std::move(f));
}

} // namespace ggui
