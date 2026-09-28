// Settings window (REBUILD_PLAN §4.1 Settings; P1-12, P2-05, P2-29).
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "util/Ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <libgg/GitRunner.hpp>

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

// sequence.editor that makes plain `git rebase -i` open ggui's todo editor (P4-02). git-gg must be
// on PATH for git (like the managed hooks).
constexpr const char* kGguiSequenceEditor = "git gg sequence-editor";
// Where turning the option on keeps a sequence.editor it replaced (same scope), for turning it off.
constexpr const char* kPreviousSequenceEditor = "gg.previousSequenceEditor";

// ggui's value, also when written with a path to git-gg.
bool isGguiSequenceEditor(const std::string& value)
{
    const std::string v = gg::trim(value);
    const std::string tail = " sequence-editor";
    if (v.size() <= tail.size() || v.compare(v.size() - tail.size(), tail.size(), tail) != 0)
        return false;
    const std::string program = gg::trim(v.substr(0, v.size() - tail.size()));
    return program == "git gg" || program.find("git-gg") != std::string::npos;
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
    auto setConfig = [this, &s](const char* flag, std::vector<std::pair<std::string, std::string>> values) {
        const std::string f = flag;
        s.actions().run("set " + values.front().first, [f, values](core::MutationContext& ctx) {
            for (const auto& [k, v] : values) {
                if (v.empty())
                    ctx.gitMayFail({"config", f, "--unset", k});
                else
                    ctx.git({"config", f, k, v});
            }
        }, [this, &s](const core::MutationFinishedEvent& e) {
            if (e.outcome != core::Outcome::Ok) {
                s.app().showError(e.label, e.message);
                m_worktreeConfigWanted.reset(); // the checkbox shows the configuration again
            }
            s.requestConfig();
        }, false, false);
    };
    const bool free = s.actions().busy().empty();
    ImGui::TextUnformatted("Git configuration");
    ImGui::SameLine();
    helpMarker("Each tab edits one scope. A value set in a later tab overrides the earlier ones for this "
               "repository; an empty field inherits (shown as a hint).");
    // Per-worktree settings (extensions.worktreeConfig): the Worktree tab exists only while on;
    // without it `git config --worktree` would write the repository's config.
    const bool configured = value("effective", "extensions.worktreeConfig") == "true";
    if (m_worktreeConfigWanted == configured)
        m_worktreeConfigWanted.reset();
    bool worktreeConfig = m_worktreeConfigWanted.value_or(configured);
    ImGui::BeginDisabled(!free);
    if (ImGui::Checkbox("Worktree settings##worktree_config", &worktreeConfig)) {
        m_worktreeConfigWanted = worktreeConfig;
        setConfig("--local", {{"extensions.worktreeConfig", worktreeConfig ? "true" : ""}});
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Sets extensions.worktreeConfig: each worktree of this repository can then override\n"
                          "settings in its own config (the Worktree tab). Off: one config for all worktrees.");
    if (!ImGui::BeginTabBar("##config_scope"))
        return;
    for (size_t si = 0; si < std::size(kScopes); ++si) {
        const Scope& sc = kScopes[si];
        if (std::string(sc.scope) == "worktree" && !worktreeConfig)
            continue;
        if (!ImGui::BeginTabItem(sc.label))
            continue;
        const bool editable = free;
        // The value this scope would inherit: the nearest lower-precedence scope that sets it, down
        // to the system configuration (read-only here).
        auto inherited = [&](const std::string& key) -> std::pair<std::string, const char*> {
            for (size_t k = si; k-- > 0;)
                if (auto v = value(kScopes[k].scope, key); !v.empty())
                    return {v, kScopes[k].label};
            if (auto v = value("system", key); !v.empty())
                return {v, "System"};
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
        if (!lowerFrom)
            if (int m = pullMethod(value("system", "pull.rebase"), value("system", "pull.ff")); m != 0) {
                lowerMethod = m;
                lowerFrom = "System";
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
        ImGui::Separator();
        // ggui's todo editor for plain `git rebase -i`: sequence.editor at this scope. Turning it on
        // asks before replacing a sequence.editor of the user's own (kept for turning it off);
        // turning it off removes only ggui's value.
        const std::string seqEditor = value(sc.scope, "sequence.editor");
        bool seqOn = isGguiSequenceEditor(seqEditor);
        ImGui::BeginDisabled(!editable);
        if (ImGui::Checkbox("Use ggui's todo editor for git rebase -i##sequence_editor", &seqOn)) {
            const std::string flag = sc.flag;
            if (!seqOn) {
                setConfig(sc.flag, {{"sequence.editor", value(sc.scope, kPreviousSequenceEditor)}, {kPreviousSequenceEditor, ""}});
            } else if (gg::trim(seqEditor).empty()) {
                setConfig(sc.flag, {{"sequence.editor", kGguiSequenceEditor}});
            } else {
                Form f;
                f.title = "Replace sequence.editor";
                f.message = std::string("The ") + sc.label + " configuration already sets sequence.editor to\n\n    " + seqEditor
                    + "\n\nReplace it with ggui's todo editor? Turning the option off puts it back.";
                f.buttons.push_back({"Replace", [this, session = &s, setConfig, flag, seqEditor](Form&) {
                                         if (m_session.get() != session)
                                             return; // the repository closed meanwhile
                                         setConfig(flag.c_str(), {{"sequence.editor", kGguiSequenceEditor},
                                                                     {kPreviousSequenceEditor, seqEditor}});
                                     }});
                f.buttons.push_back({"Cancel", {}});
                m_dialogs.open(std::move(f));
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Sets sequence.editor = %s: plain git rebase -i shows its list in ggui (this window, or a new\n"
                              "one) and goes on with the list you save. Needs git-gg on PATH.",
                kGguiSequenceEditor);
        {
            // What git rebase -i uses in this repository (the highest scope that sets it).
            std::string effective = "git's editor (sequence.editor is not set)";
            for (size_t k = std::size(kScopes); k-- > 0;)
                if (const std::string v = value(kScopes[k].scope, "sequence.editor"); !v.empty()) {
                    effective = (isGguiSequenceEditor(v) ? std::string("ggui's todo editor") : "'" + v + "'") + "  ("
                        + kScopes[k].label + ")";
                    break;
                }
            ImGui::TextDisabled("git rebase -i here uses: %s", effective.c_str());
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
            const char* staged[] = {"Ask in the Commit dialog", "Stage all tracked changes", "Stage the selected files"};
            int ns = static_cast<int>(d.nothingStaged);
            if (ImGui::Combo("Commit with nothing staged##nothing_staged", &ns, staged, 3)) {
                d.nothingStaged = static_cast<NothingStaged>(ns);
                m_settings.save();
            }
            ImGui::SameLine();
            helpMarker("What Commit does when no changes are staged: ask in the Commit dialog, stage every change to "
                       "tracked files first (like git commit -a), or stage the files selected in Changes first.");
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
