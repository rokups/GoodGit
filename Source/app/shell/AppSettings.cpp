// Settings window (REBUILD_PLAN §4.1 Settings; P1-12, P2-05, P2-29).
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

namespace ggui {

namespace {

struct ScopeKey {
    const char* scope;   // "user", "repository", "worktree"
    const char* flag;    // git config flag
    const char* label;
};

constexpr ScopeKey kEditorScopes[] = {{"user", "--global", "User"}, {"repository", "--local", "Repository"},
    {"worktree", "--worktree", "Worktree"}};

} // namespace

void App::drawSettingsWindow()
{
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 38, ImGui::GetFontSize() * 26), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &m_showSettings, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    auto& d = m_settings.data();
    Session* s = (m_session && m_session->opened()) ? m_session.get() : nullptr;
    if (ImGui::BeginTabBar("##settings_tabs")) {
        const ImGuiTabItemFlags first = m_settingsFreshOpen ? ImGuiTabItemFlags_SetSelected : 0;
        m_settingsFreshOpen = false;
        if (ImGui::BeginTabItem("General", nullptr, first)) {
            int percent = static_cast<int>(d.uiScale * 100.0f + 0.5f);
            if (ImGui::SliderInt("UI scale##scale", &percent, 50, 300, "%d%%", ImGuiSliderFlags_AlwaysClamp))
                d.uiScale = static_cast<float>(percent) / 100.0f;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                applyTheme();
                m_settings.save();
            }
            const char* themes[] = {"Dark", "Light"};
            int t = d.theme == Theme::Light ? 1 : 0;
            if (ImGui::Combo("Theme##theme", &t, themes, 2)) {
                d.theme = t == 1 ? Theme::Light : Theme::Dark;
                applyTheme();
                m_settings.save();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Git")) {
            const char* staged[] = {"Ask", "Stage all tracked changes", "Stage the selected files"};
            int ns = static_cast<int>(d.nothingStaged);
            if (ImGui::Combo("When nothing is staged##nothing_staged", &ns, staged, 3)) {
                d.nothingStaged = static_cast<NothingStaged>(ns);
                m_settings.save();
            }
            if (ImGui::Checkbox("Expand to index stages on checkout##expand_stages", &d.expandConflictStages))
                m_settings.save();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Two-sided first-class conflicts also get index stages 1-3 (git mergetool works on them);\n"
                                  "ggui collapses them again before switching away.");
            ImGui::Separator();
            if (!s) {
                ImGui::TextDisabled("Open a repository to edit its git configuration.");
            } else {
                if (!m_configLoaded) {
                    s->requestConfig();
                    m_configLoaded = true;
                }
                const auto& cfg = s->config();
                if (cfg.empty()) {
                    // Nothing to edit before the values are known (typed text would be replaced).
                    ImGui::TextDisabled("Reading git configuration...");
                } else {
                auto value = [&](const std::string& scope, const std::string& key) {
                    auto it = cfg.find(scope);
                    if (it == cfg.end())
                        return std::string();
                    auto k = it->second.find(key);
                    return k == it->second.end() ? std::string() : k->second;
                };
                auto editRow = [&](const char* scope, const char* flag, const char* key, const char* label, const char* id) {
                    const std::string mapKey = std::string(scope) + "|" + key;
                    if (!m_configEdit.count(mapKey))
                        m_configEdit[mapKey] = value(scope, key); // once; then it is the user's text
                    ImGui::PushID(id);
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
                    ImGui::InputText(label, &m_configEdit[mapKey]);
                    ImGui::SameLine();
                    const bool free = s->actions().busy().empty();
                    ImGui::BeginDisabled(!free);
                    if (ImGui::SmallButton("Save")) {
                        const std::string v = m_configEdit[mapKey];
                        const std::string k = key;
                        const std::string f = flag;
                        s->actions().run("set " + k, [f, k, v](core::MutationContext& ctx) {
                            if (v.empty())
                                ctx.gitMayFail({"config", f, "--unset", k});
                            else
                                ctx.git({"config", f, k, v});
                        }, [s](const core::MutationFinishedEvent& e) {
                            if (e.outcome != core::Outcome::Ok)
                                s->app().showError(e.label, e.message);
                            s->requestConfig();
                        }, false, false);
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                };
                ImGui::TextUnformatted("core.editor");
                for (const auto& sc : kEditorScopes)
                    editRow(sc.scope, sc.flag, "core.editor", sc.label, (std::string("editor_") + sc.scope).c_str());
                ImGui::Separator();
                editRow("repository", "--local", "merge.tool", "merge.tool", "merge_tool");
                editRow("repository", "--local", "diff.tool", "diff.tool", "diff_tool");
                const char* rebase[] = {"(not set)", "false", "true", "merges"};
                const std::string current = value("repository", "pull.rebase");
                int choice = 0;
                for (int i = 1; i < 4; ++i)
                    if (current == rebase[i])
                        choice = i;
                if (ImGui::Combo("pull.rebase##pull_rebase", &choice, rebase, 4)) {
                    const std::string v = choice == 0 ? std::string() : rebase[choice];
                    s->actions().run("set pull.rebase", [v](core::MutationContext& ctx) {
                        if (v.empty())
                            ctx.gitMayFail({"config", "--local", "--unset", "pull.rebase"});
                        else
                            ctx.git({"config", "--local", "pull.rebase", v});
                    }, [s](const core::MutationFinishedEvent&) { s->requestConfig(); }, false, false);
                }
                ImGui::TextDisabled("Effective pull.rebase: %s", value("effective", "pull.rebase").empty()
                        ? "(git default: merge)" : value("effective", "pull.rebase").c_str());
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hooks")) {
            ImGui::Checkbox("Ask to install the ggui hooks when opening a repository##ask_hooks", &d.askHooksOnOpen);
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_settings.save();
            ImGui::Separator();
            if (!s) {
                ImGui::TextDisabled("Open a repository to manage its hooks.");
            } else {
                const auto& st = s->hooksStatus();
                if (!st) {
                    ImGui::TextDisabled("Reading hook status...");
                } else {
                    ImGui::Text("Status: %s", st->installed ? (st->mode == gg::hooks::Mode::Config ? "installed (config-defined hooks)"
                                                                                                  : "installed (wrapper scripts)")
                            : st->partial ? "partially installed"
                                          : "not installed");
                    if (!st->gitGgFound)
                        ImGui::TextDisabled("git-gg is not on PATH: the hooks do nothing for plain git.");
                }
                ImGui::TextWrapped("With the hooks, Undo and the Operations panel also cover plain git commands, and "
                                   "plain git push refuses commits with first-class conflicts. Existing hooks keep running.");
                const bool free = s->actions().busy().empty();
                ImGui::BeginDisabled(!free);
                if (ImGui::Button("Install hooks##install_hooks"))
                    s->actions().installHooks([s](const core::MutationFinishedEvent& e) {
                        if (e.outcome != core::Outcome::Ok)
                            s->app().showError("Install hooks", e.message);
                        s->requestHooksStatus();
                        s->engine().readOperations();
                    });
                ImGui::SameLine();
                if (ImGui::Button("Remove hooks##remove_hooks"))
                    s->actions().uninstallHooks([s](const core::MutationFinishedEvent& e) {
                        if (e.outcome != core::Outcome::Ok)
                            s->app().showError("Remove hooks", e.message);
                        s->requestHooksStatus();
                        s->engine().readOperations();
                    });
                ImGui::EndDisabled();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace ggui
