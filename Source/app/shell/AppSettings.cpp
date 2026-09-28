// Settings window (REBUILD_PLAN §4.1 Settings; P1-12, P2-05, P2-29).
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <set>

namespace ggui {

namespace {

// Config scopes, lowest precedence first (a later scope overrides an earlier one).
struct Scope {
    const char* scope; // key in Session::config(): "user", "repository", "worktree"
    const char* flag;  // git config flag
    const char* label; // tab label
};

constexpr Scope kScopes[] = {{"user", "--global", "User"}, {"repository", "--local", "Repository"},
    {"worktree", "--worktree", "Worktree"}};

// Pull method from pull.rebase / pull.ff at one scope (0 = not set there).
constexpr const char* kPullMethods[] = {"(not set)", "Merge", "Rebase", "Rebase, keeping merges", "Fast-forward only"};

int pullMethod(const std::string& rebase, const std::string& ff)
{
    // git's spellings: merges/m, and the true values of a boolean (or interactive/i).
    static const std::set<std::string> kMerges{"merges", "m"};
    static const std::set<std::string> kRebase{"true", "yes", "on", "1", "i", "interactive"};
    if (kMerges.count(rebase))
        return 3;
    if (kRebase.count(rebase))
        return 2;
    if (ff == "only")
        return 4;
    if (!rebase.empty())
        return 1;
    return 0;
}

} // namespace

void App::drawGitConfigSettings(Session& s)
{
    const auto& cfg = s.config();
    auto value = [&](const std::string& scope, const std::string& key) {
        auto it = cfg.find(scope);
        if (it == cfg.end())
            return std::string();
        auto k = it->second.find(key);
        return k == it->second.end() ? std::string() : k->second;
    };
    // Sets (or, for empty values, unsets) keys at one scope in one mutation.
    auto setConfig = [&s](const char* flag, std::vector<std::pair<std::string, std::string>> values) {
        const std::string f = flag;
        s.actions().run("set " + values.front().first, [f, values](core::MutationContext& ctx) {
            for (const auto& [k, v] : values) {
                if (v.empty())
                    ctx.gitMayFail({"config", f, "--unset", k});
                else
                    ctx.git({"config", f, k, v});
            }
        }, [&s](const core::MutationFinishedEvent& e) {
            if (e.outcome != core::Outcome::Ok)
                s.app().showError(e.label, e.message);
            s.requestConfig();
        }, false, false);
    };
    const bool free = s.actions().busy().empty();
    ImGui::TextUnformatted("Git configuration");
    ImGui::SameLine();
    helpMarker("Each tab edits one scope. A value set in a later tab overrides the earlier ones for this "
               "repository; an empty field inherits (shown as a hint).");
    if (!ImGui::BeginTabBar("##config_scope"))
        return;
    for (size_t si = 0; si < std::size(kScopes); ++si) {
        const Scope& sc = kScopes[si];
        if (!ImGui::BeginTabItem(sc.label))
            continue;
        // Without extensions.worktreeConfig, `git config --worktree` writes the repository's config.
        const bool worktreeOff = std::string(sc.scope) == "worktree" && value("effective", "extensions.worktreeConfig") != "true";
        if (worktreeOff) {
            ImGui::TextWrapped("Worktree settings are off in this repository: git keeps one config for all its "
                               "worktrees until extensions.worktreeConfig is set.");
            ImGui::BeginDisabled(!free);
            if (ImGui::Button("Enable worktree settings##enable_worktree_config"))
                setConfig("--local", {{"extensions.worktreeConfig", "true"}});
            ImGui::EndDisabled();
            ImGui::Separator();
        }
        const bool editable = free && !worktreeOff;
        // The value this scope would inherit: the nearest lower-precedence scope that sets it.
        auto inherited = [&](const std::string& key) -> std::pair<std::string, const char*> {
            for (size_t k = si; k-- > 0;)
                if (auto v = value(kScopes[k].scope, key); !v.empty())
                    return {v, kScopes[k].label};
            return {std::string(), nullptr};
        };
        auto inheritButton = [&](const char* id, const std::string& lower, const char* from) {
            ImGui::SameLine();
            ImGui::BeginDisabled(!editable);
            const bool clicked = ImGui::SmallButton((std::string("Inherit##") + id).c_str());
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Clear this override and use %s from %s", lower.c_str(), from);
            return clicked;
        };
        auto textOption = [&](const char* key) {
            const std::string mine = value(sc.scope, key);
            const auto [lower, from] = inherited(key);
            const std::string mapKey = std::string(sc.scope) + "|" + key;
            const std::string label = std::string(key) + "##" + key;
            std::string& text = m_configEdit[mapKey];
            // Follow changes made elsewhere (plain git, another tab) unless the field is being edited.
            if (ImGui::GetActiveID() != ImGui::GetID(label.c_str()) && m_configSeen[mapKey] != mine) {
                text = mine;
                m_configSeen[mapKey] = mine;
            }
            const std::string hint = from ? lower + "  (" + from + ")" : std::string();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
            ImGui::BeginDisabled(!editable);
            ImGui::InputTextWithHint(label.c_str(), hint.c_str(), &text);
            ImGui::EndDisabled();
            if (ImGui::IsItemDeactivatedAfterEdit() && text != mine)
                setConfig(sc.flag, {{key, text}});
            if (!mine.empty() && from && inheritButton(key, lower, from)) {
                text.clear();
                setConfig(sc.flag, {{key, ""}});
            }
        };
        textOption("user.name");
        textOption("user.email");
        ImGui::Separator();
        textOption("core.editor");
        textOption("merge.tool");
        textOption("diff.tool");
        ImGui::Separator();
        // Pull method: pull.rebase (merge / rebase / rebase keeping merges) or pull.ff=only.
        const int method = pullMethod(value(sc.scope, "pull.rebase"), value(sc.scope, "pull.ff"));
        int lowerMethod = 0;
        const char* lowerFrom = nullptr;
        for (size_t k = si; k-- > 0 && !lowerFrom;)
            if (int m = pullMethod(value(kScopes[k].scope, "pull.rebase"), value(kScopes[k].scope, "pull.ff")); m != 0) {
                lowerMethod = m;
                lowerFrom = kScopes[k].label;
            }
        std::string preview = kPullMethods[method];
        if (method == 0)
            preview = lowerFrom ? std::string(kPullMethods[lowerMethod]) + "  (" + lowerFrom + ")" : "Merge  (git default)";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
        ImGui::BeginDisabled(!editable);
        if (ImGui::BeginCombo("Pull method##pull_method", preview.c_str())) {
            for (int m = 0; m < static_cast<int>(std::size(kPullMethods)); ++m) {
                if (!ImGui::Selectable(kPullMethods[m], m == method) || m == method)
                    continue;
                const std::string ff = value(sc.scope, "pull.ff");
                std::vector<std::pair<std::string, std::string>> set;
                const char* rebase[] = {"", "false", "true", "merges", ""};
                set.emplace_back("pull.rebase", rebase[m]);
                if (m == 4 || ff == "only")
                    set.emplace_back("pull.ff", m == 4 ? "only" : "");
                setConfig(sc.flag, std::move(set));
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (method != 0 && lowerFrom && inheritButton("pull_method", kPullMethods[lowerMethod], lowerFrom)) {
            std::vector<std::pair<std::string, std::string>> set{{"pull.rebase", ""}};
            if (value(sc.scope, "pull.ff") == "only")
                set.emplace_back("pull.ff", "");
            setConfig(sc.flag, std::move(set));
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

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
                if (s->config().empty())
                    ImGui::TextDisabled("Reading git configuration..."); // typed text would be replaced
                else
                    drawGitConfigSettings(*s);
            }
            ImGui::EndTabItem();
        } else {
            m_configLoaded = false; // read again when the tab is shown (plain git may have changed it)
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
