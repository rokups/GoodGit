// Welcome screen, Initialize and Clone (product spec §4.1).
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

#include "shell/Widgets.hpp"

#include <IconsMaterialSymbols.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace ggui {

namespace fs = std::filesystem;

namespace {
const char* kTagline = "A Git client with undo and first-class conflicts";
}

void App::initializeRepository()
{
    pickFolder("Initialize repository", [this](const std::string& path) {
        if (path.empty())
            return;
        ++m_pendingOpens;
        m_io.post([this, path]() -> std::function<void()> {
            std::error_code ec;
            fs::create_directories(path, ec);
            const gg::RunResult r = gg::git(path, {"init", "-q", path});
            return [this, r, path] {
                --m_pendingOpens;
                if (!r.ok()) {
                    showError("Initialize repository", r.message());
                    return;
                }
                openRepository(path);
            };
        });
    });
}

void App::showCloneDialog()
{
    Form f;
    f.title = "Clone repository";
    f.add(Field{Field::Text, "url", "URL", "", false, 0, {}, "https://… or git@host:repo.git or a local path"});
    f.add(Field{Field::Text, "destination", "Destination directory"});
    f.buttons.push_back({"Clone",
        [this](Form& form) {
            const std::string url = gg::trim(form.text("url"));
            fs::path dest = gg::trim(form.text("destination"));
            m_clone.start(url, fs::absolute(dest));
        },
        [](const Form& form) { return !gg::trim(form.text("url")).empty() && !gg::trim(form.text("destination")).empty(); }});
    f.buttons.push_back({"Cancel", {}});
    m_dialogs.open(std::move(f));
}

std::string App::currentRepoKey() const
{
    return m_session && m_session->opened() ? normalizeRepoPath(m_session->path().string()) : std::string();
}

std::string App::recentRowText(size_t i) const
{
    const auto& recent = m_settings.data().recent;
    if (i >= recent.size())
        return {};
    std::string detail;
    for (const auto& info : m_recentInfo)
        if (info.path == fs::path(recent[i]))
            detail = summaryText(info);
    return uniqueRecentNames(recent)[i].text() + (detail.empty() ? std::string() : "  \xe2\x80\x94  " + detail);
}

void App::drawWelcome()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiWindow* toolbar = ImGui::FindWindowByName("Toolbar###Toolbar");
    const float top = toolbar ? toolbar->Pos.y + toolbar->Size.y : vp->WorkPos.y;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y - top));
    ImGui::Begin("Welcome", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 48);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 2));
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.0f);
    ImGui::TextUnformatted("ggui");
    ImGui::PopFont();
    ImGui::TextDisabled("%s", kTagline);
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));

    const bool cloning = m_clone.state() == core::CloneService::State::Running;
    const bool opening = (m_session && m_session->opening()) || m_pendingOpens > 0;
    ImGui::BeginDisabled(opening || cloning);
    if (ImGui::Button(ICON_MS_FOLDER_OPEN " Open repository...###welcome_open"))
        pickAndOpenRepository();
    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_ADD " Initialize repository...###welcome_init"))
        initializeRepository();
    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_DOWNLOAD " Clone repository...###welcome_clone"))
        showCloneDialog();
    ImGui::SetNextItemWidth(width - ImGui::CalcTextSize("Open").x - ImGui::GetStyle().FramePadding.x * 4);
    const bool enter = ImGui::InputTextWithHint("##welcome_path", "Path to a repository", &m_welcomePath,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((button(ICON_MS_FOLDER_OPEN, "Open") || enter) && !m_welcomePath.empty())
        post([this, path = m_welcomePath] { openRepository(path); });
    ImGui::EndDisabled();

    if (opening && m_session) {
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
        spinner("##opening", ImGui::GetFontSize() * 0.45f);
        ImGui::SameLine();
        ImGui::Text("Opening %s...", m_session->path().string().c_str());
        ImGui::SameLine();
        if (button(ICON_MS_CLOSE, "Cancel##open"))
            m_session->cancelOpen();
    }
    if (cloning) {
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
        spinner("##cloning", ImGui::GetFontSize() * 0.45f);
        ImGui::SameLine();
        const int pct = m_clone.percent();
        ImGui::Text("Cloning into %s... %s %s", m_clone.destination().string().c_str(), m_clone.phase().c_str(),
            pct >= 0 ? (std::to_string(pct) + "%").c_str() : "");
        ImGui::SameLine();
        if (button(ICON_MS_CLOSE, "Cancel##clone"))
            m_clone.cancel();
    }

    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));
    ImGui::SeparatorText("Recent repositories");
    // Only the list scrolls: everything above stays put.
    beginList(width);
    const auto& recent = m_settings.data().recent;
    if (recent.empty())
        ImGui::TextDisabled("No recent repositories");
    const auto names = uniqueRecentNames(recent);
    const std::string currentKey = currentRepoKey();
    std::string forget;
    for (size_t i : recentDisplayOrder(recent, m_settings.data().recentOrder)) {
        const std::string& path = recent[i];
        ImGui::PushID(("recent_" + std::to_string(i)).c_str());
        const std::string label = recentRowText(i) + "###row";
        if (selectableDimPrefix(label.c_str(), names[i].prefix.size(), m_recentFocus == static_cast<int>(i),
                ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 0)))
            post([this, path] { openRepository(path); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s\nDel removes", path.c_str());
        if (ImGui::IsItemFocused())
            m_recentFocus = static_cast<int>(i);
        if ((ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) || (path != currentKey && hoveredDeletePressed()))
            forget = path;
        if (beginContextMenu("##recent_menu")) {
            if (menuItem(ICON_MS_REMOVE, "Forget"))
                forget = path;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (!forget.empty()) {
        m_settings.forgetRecent(forget);
        m_recentFocus = -1;
    }
    endList();
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace ggui
