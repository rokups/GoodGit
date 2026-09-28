#include "shell/App.hpp"

#include "panels/RebasePanel.hpp"
#include "platform/Platform.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "util/Env.hpp"
#include "util/Ui.hpp"

#include <libgg/GitRunner.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <thread>

namespace ggui {

namespace fs = std::filesystem;

namespace {

App* g_app = nullptr;

// ---- imgui.ini: custom handler for the main window placement --------------------------------

void* iniReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return std::strcmp(name, "Main") == 0 ? static_cast<void*>(g_app) : nullptr;
}

void iniReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    if (!entry)
        return;
    auto& d = static_cast<App*>(entry)->settings().data();
    int a = 0, b = 0;
    if (std::sscanf(line, "Pos=%d,%d", &a, &b) == 2) {
        d.windowX = a;
        d.windowY = b;
    } else if (std::sscanf(line, "Size=%d,%d", &a, &b) == 2) {
        d.windowW = a;
        d.windowH = b;
    } else if (std::sscanf(line, "Maximized=%d", &a) == 1) {
        d.windowMaximized = a != 0;
    }
}

void iniWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    // (Registered by the App, which sets g_app first.)
    SDL_Window* w = g_app->platform().window();
    int x = 0, y = 0, width = 0, height = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &width, &height);
    const bool maximized = (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) != 0;
    buf->appendf("[%s][Main]\nPos=%d,%d\nSize=%d,%d\nMaximized=%d\n\n", handler->TypeName, x, y, width, height,
        maximized ? 1 : 0);
}


} // namespace

bool iconButton(const char* icon, const char* id, const char* tooltip, bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    // The "##tb_x" ids become "###tb_x": the ID does not depend on the icon glyph.
    char label[128];
    std::snprintf(label, sizeof(label), "%s#%s", icon, id);
    const bool clicked = ImGui::Button(label);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

App::App(Platform& platform, AppOptions options)
    : m_platform(platform), m_options(std::move(options)), m_settings(m_io)
{
    g_app = this;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // imgui.ini is loaded/saved asynchronously by Settings
    ImGuiSettingsHandler handler;
    handler.TypeName = "GGUIWindow";
    handler.TypeHash = ImHashStr("GGUIWindow");
    handler.ReadOpenFn = iniReadOpen;
    handler.ReadLineFn = iniReadLine;
    handler.WriteAllFn = iniWriteAll;
    ImGui::AddSettingsHandler(&handler);

    theme().loadFonts();
    applyTheme();
    m_welcomePath = m_options.initialPath;
    // ggui answers git's credential prompts through git-gg (GIT_ASKPASS / SSH_ASKPASS, T1).
    fs::path gitGg = fs::path(executableDir()) / "git-gg";
#ifdef _WIN32
    gitGg += ".exe";
#endif
    if (!m_askpass.start(gitGg))
        spdlog::warn("askpass bridge unavailable");
    m_settings.load([this](const std::string& ini) { onSettingsLoaded(ini); });
}

App::~App()
{
    shutdown();
    g_app = nullptr;
}

void App::shutdown()
{
    m_askpass.cancelAll();
    m_clone.cancel();
    if (m_session) {
        m_session->cancelAll();
        m_session.reset();
    }
    m_closing.clear();
    if (m_settings.loaded()) {
        m_settings.save();
        size_t size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&size);
        m_settings.saveIni(std::string(ini, size));
    }
    m_io.flush();
}

bool App::idle() const
{
    if (!m_settings.loaded() || !m_io.idle() || !m_summaries.idle() || m_pickersRunning > 0 || m_pendingOpens > 0)
        return false;
    if (m_clone.state() == core::CloneService::State::Running)
        return false;
    if (m_session && !m_session->idle())
        return false;
    return m_posted.empty();
}

void App::resetForTest()
{
    // Git processes waiting for a credentials answer would never finish: refuse the prompts
    // and stop a clone before joining anything.
    m_askpass.cancelAll();
    m_clone.cancel();
    if (m_session) {
        m_session->cancelAll();
        m_session.reset();
    }
    m_closing.clear();
    m_io.flush();
    m_io.pump();
    m_errorTitle.clear();
    m_errorMessage.clear();
    m_toasts.clear();
    m_showSettings = false;
    m_dialogs.closeAll();
    m_clone.reset();
    m_configLoaded = false;
    m_configEdit.clear();
    m_configSeen.clear();
    m_recentInfo.clear();
    m_recentFilter.clear();
    m_welcomePath.clear();
    m_autoOpenDone = true;
    m_iniApplied = false;
    ImGui::ClearIniSettings();
    ImGui::CloseCurrentPopup();
    m_settings.load([this](const std::string& ini) { onSettingsLoaded(ini); });
    m_io.flush();
    m_io.pump();
}

void App::onSettingsLoaded(const std::string& iniText)
{
    spdlog::info("settings loaded from {}: scale={:.2f} theme={} recent={}", Settings::prefDir().string(),
        m_settings.data().uiScale, m_settings.data().theme == Theme::Light ? "light" : "dark",
        m_settings.data().recent.size());
    applyTheme();
    if (!iniText.empty())
        ImGui::LoadIniSettingsFromMemory(iniText.c_str(), iniText.size());
    m_layoutPending = iniText.find("[Docking]") == std::string::npos;
    m_iniApplied = true;
    const auto& d = m_settings.data();
    if (!m_platform.headless() && d.windowW > 200 && d.windowH > 200) {
        SDL_SetWindowSize(m_platform.window(), d.windowW, d.windowH);
        if (d.windowX >= 0 && d.windowY >= 0)
            SDL_SetWindowPosition(m_platform.window(), d.windowX, d.windowY);
        if (d.windowMaximized)
            SDL_MaximizeWindow(m_platform.window());
    }
    std::vector<fs::path> paths(d.recent.begin(), d.recent.end());
    m_summaries.request(paths);
    if (!m_autoOpenDone && m_options.autoOpen && !m_options.initialPath.empty()) {
        m_autoOpenDone = true;
        openRepository(m_options.initialPath);
    }
}

void App::applyTheme()
{
    const auto& d = m_settings.data();
    theme().apply(d.theme, d.uiScale);
}

void App::pumpSummaries()
{
    auto results = m_summaries.poll();
    if (results.empty())
        return;
    m_recentInfo = std::move(results);
    // Auto-open the most recent repository that still exists (§4.1).
    if (!m_autoOpenDone && m_options.autoOpen && m_options.initialPath.empty() && !m_session) {
        m_autoOpenDone = true;
        for (const auto& r : m_recentInfo)
            if (r.exists) {
                openRepository(r.path);
                break;
            }
    }
}

void App::openRepository(const fs::path& path)
{
    if (path.empty())
        return;
    // G2: git must be present and new enough; checked (off the UI thread) before every open.
    // The open is pending until the check ends either way (Retry starts a new open).
    ++m_pendingOpens;
    checkGit(
        [this, path](bool ok) {
            --m_pendingOpens;
            if (ok)
                openNow(path);
        },
        [this, path] { openRepository(path); });
}

void App::openNow(const fs::path& path)
{
    if (m_session) {
        m_session->cancelAll();
        m_closing.push_back(std::move(m_session));
    }
    clearError();
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec)
        abs = path;
    abs = abs.lexically_normal();
    std::string s = abs.string();
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    spdlog::info("opening {}", s);
    m_session = std::make_unique<Session>(*this, fs::path(s));
}

void App::checkGit(std::function<void(bool ok)> done, std::function<void()> retry)
{
    m_git.running = true;
    m_io.post([this, done, retry]() -> std::function<void()> {
        gg::RunRequest r;
        r.args = {"git", "--version"};
        r.gitEnvironment = false;
        const gg::RunResult res = gg::run(r);
        GitCheck result;
        result.done = true;
        result.found = !res.startFailed && res.exitCode == 0;
        if (result.found) {
            // "git version 2.55.0" (vendor suffixes such as ".windows.1" are ignored).
            const std::string text = gg::trim(res.out);
            const auto pos = text.find_first_of("0123456789");
            result.version = pos == std::string::npos ? text : text.substr(pos);
            int major = 0, minor = 0;
            std::sscanf(result.version.c_str(), "%d.%d", &major, &minor);
            result.supported = major > 2 || (major == 2 && minor >= 36);
        }
        return [this, result, done, retry] {
            m_git = result;
            const bool ok = result.found && result.supported;
            done(ok);
            if (!ok) {
                Form f;
                f.title = "Git required";
                f.message = result.found ? "ggui needs git 2.36 or newer. Found git " + result.version + "."
                                         : "ggui needs git (2.36 or newer), but git was not found on PATH.";
                f.message += "\n\nInstall or update git, then choose Retry.";
                f.buttons.push_back({"Retry", [retry](Form&) { retry(); }});
                f.buttons.push_back({"Quit", [this](Form&) { requestQuit(); }});
                m_dialogs.open(std::move(f));
            }
        };
    });
}

void App::closeRepository()
{
    if (!m_session)
        return;
    m_session->cancelAll();
    m_closing.push_back(std::move(m_session));
    std::vector<fs::path> paths(m_settings.data().recent.begin(), m_settings.data().recent.end());
    m_summaries.request(paths);
}

void App::pickFolder(const std::string& title, std::function<void(std::string)> done)
{
    // Test hook (§8.1): the native dialog cannot be driven by the test engine; a scenario
    // provides the answer through GGUI_TEST_PICK_PATH instead.
    const std::string testPick = getEnv("GGUI_TEST_PICK_PATH");
    if (!testPick.empty() || getEnv("GGUI_TEST_PICK_CANCEL") == "1") {
        post([done, testPick] { done(testPick); });
        return;
    }
    // COVERAGE_EXCL_START: native picker integration (needs a desktop session).
    ++m_pickersRunning;
    std::thread([this, title, done] {
        std::string result;
        if (NFD_Init() == NFD_OKAY) {
            nfdu8char_t* out = nullptr;
            nfdpickfolderu8args_t args{};
            if (NFD_PickFolderU8_With(&out, &args) == NFD_OKAY && out) {
                result = out;
                NFD_FreePathU8(out);
            }
            NFD_Quit();
        }
        (void)title;
        m_io.post([this, done, result]() -> std::function<void()> {
            return [this, done, result] {
                --m_pickersRunning;
                done(result);
            };
        });
    }).detach();
    // COVERAGE_EXCL_STOP
}

void App::pickSaveFile(const std::string& title, const std::string& defaultName, std::function<void(std::string)> done)
{
    const std::string testPick = getEnv("GGUI_TEST_PICK_PATH");
    if (!testPick.empty() || getEnv("GGUI_TEST_PICK_CANCEL") == "1") {
        post([done, testPick] { done(testPick); });
        return;
    }
    // COVERAGE_EXCL_START: native picker integration (needs a desktop session).
    ++m_pickersRunning;
    std::thread([this, title, defaultName, done] {
        std::string result;
        if (NFD_Init() == NFD_OKAY) {
            nfdu8char_t* out = nullptr;
            nfdsavedialogu8args_t args{};
            args.defaultName = defaultName.c_str();
            if (NFD_SaveDialogU8_With(&out, &args) == NFD_OKAY && out) {
                result = out;
                NFD_FreePathU8(out);
            }
            NFD_Quit();
        }
        (void)title;
        m_io.post([this, done, result]() -> std::function<void()> {
            return [this, done, result] {
                --m_pickersRunning;
                done(result);
            };
        });
    }).detach();
    // COVERAGE_EXCL_STOP
}

void App::pickAndOpenRepository()
{
    pickFolder("Open repository", [this](const std::string& path) {
        if (!path.empty())
            openRepository(path);
    });
}

void App::showError(const std::string& title, const std::string& message)
{
    m_errorTitle = title;
    m_errorMessage = message;
    spdlog::warn("{}: {}", title, message);
    Form f;
    f.title = title.empty() ? std::string("Error") : title;
    f.message = message;
    f.icon = ICON_MS_ERROR;
    f.buttons.push_back({"OK", {}});
    f.buttons.push_back({"Copy message", [message](Form&) { ImGui::SetClipboardText(message.c_str()); }});
    m_dialogs.open(std::move(f));
}

void App::notify(Notice level, const std::string& title, const std::string& message)
{
    if (level == Notice::Warning) {
        m_errorTitle = title;
        m_errorMessage = message;
        spdlog::warn("{}: {}", title, message);
    } else {
        spdlog::info("{}: {}", title, message);
    }
    // The same notice again replaces the older one instead of stacking up.
    std::erase_if(m_toasts, [&](const Toast& t) { return t.title == title && t.message == message; });
    m_toasts.push_back(Toast{m_nextToast++, level, title, message, 0.0f});
    if (m_toasts.size() > 5)
        m_toasts.erase(m_toasts.begin());
}

void App::clearError()
{
    m_errorTitle.clear();
    m_errorMessage.clear();
    m_toasts.clear();
}

void App::resetLayout() { m_layoutPending = true; }

void App::frame()
{
    m_io.pump();
    auto posted = std::move(m_posted);
    m_posted.clear();
    for (auto& fn : posted)
        fn();
    pumpSummaries();
    // Sessions being closed are destroyed once their engines have no running work.
    std::erase_if(m_closing, [](const std::unique_ptr<Session>& s) { return s->idle(); });

    if (m_session) {
        m_session->pump();
        if (m_session->failed()) {
            m_closing.push_back(std::move(m_session));
        } else if (m_session->opened() && (m_settings.data().recent.empty()
                       || m_settings.data().recent.front() != m_session->path().string())) {
            m_settings.addRecent(m_session->path().string());
        }
    }

    pumpAskpass();
    pumpClone();
    handleShortcuts();
    drawMenuBar();
    drawToolbar();
    if (m_session && m_session->opened())
        drawDockHost();
    else
        drawWelcome();
    if (m_showSettings)
        drawSettingsWindow();
    m_dialogs.draw();
    drawToasts();
    saveIniIfNeeded();
    updateTitle();
}

void App::updateTitle()
{
    const std::string title = (m_session && m_session->opened()) ? m_session->displayName() + " \xe2\x80\x94 ggui" : "ggui";
    if (title != m_title) {
        m_title = title;
        m_platform.setTitle(title);
    }
}

void App::saveIniIfNeeded()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantSaveIniSettings && m_iniApplied) {
        size_t size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&size);
        m_settings.saveIni(std::string(ini, size));
        io.WantSaveIniSettings = false;
    }
}





void App::drawToasts()
{
    constexpr float kLifetime[] = {6.0f, 12.0f}; // info, warning
    const Palette& p = theme().palette();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float margin = ImGui::GetFontSize();
    float bottom = vp->WorkPos.y + vp->WorkSize.y - margin;
    std::vector<std::uint64_t> closed;
    // Newest at the bottom, older ones stacked above.
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        Toast& t = *it;
        const float life = kLifetime[static_cast<int>(t.level)];
        const float alpha = std::clamp((life - t.age) / 0.5f, 0.0f, 1.0f);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin, bottom), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.95f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(alpha, 0.05f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetFontSize() * 0.3f);
        const std::string name = "##toast_" + std::to_string(t.id);
        ImGui::Begin(name.c_str(), nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoMove);
        const ImU32 accent = t.level == Notice::Warning ? p.warning : p.lanes[0];
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextUnformatted(t.level == Notice::Warning ? ICON_MS_WARNING : ICON_MS_INFO);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(t.title.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 28);
        ImGui::TextDisabled("%s", t.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MS_CLOSE "###toast_close"))
            closed.push_back(t.id);
        if (ImGui::BeginPopupContextWindow("##toast_menu")) {
            if (ImGui::MenuItem("Copy message"))
                ImGui::SetClipboardText((t.title + ": " + t.message).c_str());
            ImGui::EndPopup();
        }
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        bottom -= ImGui::GetWindowHeight() + margin * 0.5f;
        ImGui::End();
        ImGui::PopStyleVar(2);
        if (!hovered)
            t.age += ImGui::GetIO().DeltaTime;
        if (t.age >= life)
            closed.push_back(t.id);
    }
    std::erase_if(m_toasts, [&](const Toast& t) { return std::find(closed.begin(), closed.end(), t.id) != closed.end(); });
}


void App::drawDockHost()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiWindow* toolbar = ImGui::FindWindowByName("##Toolbar");
    const float top = toolbar ? toolbar->Pos.y + toolbar->Size.y : vp->WorkPos.y;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y - top));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##DockHost", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(3);
    const ImGuiID dockId = ImGui::GetID("##DockSpace");
    if (m_layoutPending || !ImGui::DockBuilderGetNode(dockId)) {
        m_layoutPending = false;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetContentRegionAvail());
        ImGuiID left = 0, center = 0, right = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.18f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.40f, &right, &center);
        ImGuiID leftTop = 0, leftBottom = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, &leftTop, &leftBottom);
        ImGuiID rightTop = 0, rightBottom = 0;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, &rightTop, &rightBottom);
        ImGuiID changes = 0, info = 0;
        ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Up, 0.55f, &changes, &info);
        ImGui::DockBuilderDockWindow(panel::Branches, leftTop);
        ImGui::DockBuilderDockWindow(panel::Tags, leftTop);
        ImGui::DockBuilderDockWindow(panel::Worktrees, leftBottom);
        ImGui::DockBuilderDockWindow(panel::Remotes, leftBottom);
        ImGui::DockBuilderDockWindow(panel::Stashes, leftBottom);
        ImGui::DockBuilderDockWindow(panel::History, center);
        ImGui::DockBuilderDockWindow(panel::Rebase, center);
        ImGui::DockBuilderDockWindow(panel::Changes, changes);
        ImGui::DockBuilderDockWindow(panel::Info, info);
        ImGui::DockBuilderDockWindow(panel::Diff, rightBottom);
        ImGui::DockBuilderDockWindow(panel::Blame, rightBottom);
        ImGui::DockBuilderDockWindow(panel::Reflog, rightBottom);
        ImGui::DockBuilderDockWindow(panel::Operations, rightBottom);
        ImGui::DockBuilderFinish(dockId);
        auto& panels = m_settings.data().panels;
        for (const char* name : panel::All)
            panels[name] = panel::defaultVisible(name);
        m_session->focusPanel(panel::Branches);
        m_session->focusPanel(panel::Worktrees);
        m_session->focusPanel(panel::Diff);
        m_session->focusPanel(panel::History);
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
    m_session->draw();
}


} // namespace ggui
