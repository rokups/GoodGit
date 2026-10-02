#include "shell/Settings.hpp"

#include "util/Env.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <fstream>
#include <sstream>

namespace ggui {

namespace fs = std::filesystem;

// ---- AsyncIo -----------------------------------------------------------------------------------

AsyncIo::AsyncIo() { m_thread = std::thread([this] { loop(); }); }

AsyncIo::~AsyncIo()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void AsyncIo::post(std::function<std::function<void()>()> work)
{
    {
        std::lock_guard lock(m_mutex);
        m_jobs.push_back(std::move(work));
    }
    m_cv.notify_all();
}

void AsyncIo::pump()
{
    std::deque<std::function<void()>> done;
    {
        std::lock_guard lock(m_mutex);
        done.swap(m_done);
    }
    for (auto& fn : done)
        if (fn)
            fn();
}

bool AsyncIo::idle() const
{
    std::lock_guard lock(m_mutex);
    return m_jobs.empty() && m_done.empty() && m_running == 0;
}

void AsyncIo::flush()
{
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] { return m_jobs.empty() && m_running == 0; });
}

void AsyncIo::loop()
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_cv.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
        if (m_jobs.empty() && m_stop)
            return;
        auto job = std::move(m_jobs.front());
        m_jobs.pop_front();
        ++m_running;
        lock.unlock();
        std::function<void()> cont;
        try {
            cont = job();
        } catch (const std::exception& e) {
            spdlog::error("settings I/O failed: {}", e.what());
        }
        lock.lock();
        --m_running;
        if (cont)
            m_done.push_back(std::move(cont));
        m_cv.notify_all();
    }
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

std::string readAll(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeAtomically(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
    }
    fs::rename(tmp, p, ec);
    if (ec)
        spdlog::warn("cannot write {}: {}", p.string(), ec.message());
}

} // namespace

nlohmann::json toJson(const SettingsData& d)
{
    nlohmann::json j;
    j["version"] = 1;
    j["uiScale"] = d.uiScale;
    j["theme"] = d.theme == Theme::Light ? "light" : "dark";
    j["recent"] = d.recent;
    j["recentOrder"] = d.recentOrder == RecentOrder::Alphabetical ? "alphabetical" : "recent";
    j["panels"] = d.panels;
    j["nothingStaged"] = d.nothingStaged == NothingStaged::StageAll ? "stage-all"
        : d.nothingStaged == NothingStaged::StageSelected                 ? "stage-selected"
                                                                          : "ask";
    j["expandStagesOnCheckout"] = d.expandStagesOnCheckout;
    j["expandConflictStages"] = d.expandConflictStages;
    return j;
}

SettingsData fromJson(const nlohmann::json& j)
{
    SettingsData d;
    if (!j.is_object())
        return d;
    d.uiScale = std::clamp(j.value("uiScale", 1.0f), 0.5f, 3.0f);
    d.theme = j.value("theme", std::string("dark")) == "light" ? Theme::Light : Theme::Dark;
    if (j.contains("recent") && j["recent"].is_array())
        for (const auto& r : j["recent"])
            if (r.is_string())
                d.recent.push_back(r.get<std::string>());
    d.recent = uniqueRepoPaths(d.recent);
    d.recentOrder = j.value("recentOrder", std::string("recent")) == "alphabetical" ? RecentOrder::Alphabetical
                                                                                    : RecentOrder::MostRecent;
    if (j.contains("panels") && j["panels"].is_object())
        for (auto it = j["panels"].begin(); it != j["panels"].end(); ++it)
            if (it.value().is_boolean())
                d.panels[it.key()] = it.value().get<bool>();
    // The view state and the window placement now live in imgui.ini (applied after this); they are
    // still read here for a settings.json written by an earlier version.
    if (j.contains("diff") && j["diff"].is_object()) {
        d.diffSideBySide = j["diff"].value("sideBySide", false);
        d.diffContext = std::clamp(j["diff"].value("context", 3), 0, 100);
        d.diffWhitespace = std::clamp(j["diff"].value("whitespace", 0), 0, 2);
    }
    d.historyShowStashes = j.value("historyShowStashes", true);
    const std::string ns = j.value("nothingStaged", std::string("ask"));
    d.nothingStaged = ns == "stage-all" ? NothingStaged::StageAll
        : ns == "stage-selected"        ? NothingStaged::StageSelected
                                        : NothingStaged::Ask;
    d.expandStagesOnCheckout = j.value("expandStagesOnCheckout", false);
    d.expandConflictStages = j.value("expandConflictStages", false);
    if (j.contains("window") && j["window"].is_object()) {
        const auto& w = j["window"];
        d.windowX = w.value("x", -1);
        d.windowY = w.value("y", -1);
        d.windowW = w.value("w", 0);
        d.windowH = w.value("h", 0);
        d.windowMaximized = w.value("maximized", false);
    }
    return d;
}

// ---- imgui.ini: view state ---------------------------------------------------------------------

namespace {

// One value of the view state stored in imgui.ini as [GGUIView][section] key=value. The section and
// key names are the on-disk schema. Rows of one section must be adjacent (a section is written as
// one block); exactly one of `b` / `i` is set; an int is clamped to [lo, hi] when read.
struct ViewSetting {
    const char* section;
    const char* key;
    bool SettingsData::* b;
    int SettingsData::* i;
    int lo, hi;
};

const ViewSetting kViewSettings[] = {
    {"History", "Stashes", &SettingsData::historyShowStashes, nullptr, 0, 1},
    {"History", "ConflictedOnly", &SettingsData::historyConflictedOnly, nullptr, 0, 1},
    {"Diff", "SideBySide", &SettingsData::diffSideBySide, nullptr, 0, 1},
    {"Diff", "Whitespace", nullptr, &SettingsData::diffWhitespace, 0, 2},
    {"Diff", "Context", nullptr, &SettingsData::diffContext, 0, 100},
    {"Rebase", "NewestFirst", &SettingsData::rebaseNewestFirst, nullptr, 0, 1},
};

void* viewReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
{
    for (const auto& v : kViewSettings)
        if (std::strcmp(v.section, name) == 0)
            return handler->UserData ? const_cast<char*>(v.section) : nullptr;
    return nullptr;
}

void viewReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line)
{
    if (!entry || !handler->UserData)
        return;
    auto& d = *static_cast<SettingsData*>(handler->UserData);
    const char* section = static_cast<const char*>(entry);
    const char* eq = std::strchr(line, '=');
    if (!eq)
        return;
    int value = 0;
    if (std::sscanf(eq + 1, "%d", &value) != 1)
        return;
    const std::string key(line, eq);
    for (const auto& v : kViewSettings) {
        if (std::strcmp(v.section, section) != 0 || key != v.key)
            continue;
        if (v.b)
            d.*(v.b) = value != 0;
        else
            d.*(v.i) = std::clamp(value, v.lo, v.hi);
        return;
    }
}

void viewWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    const auto& d = *static_cast<const SettingsData*>(handler->UserData);
    const char* section = nullptr;
    for (const auto& v : kViewSettings) {
        if (!section || std::strcmp(section, v.section) != 0) {
            if (section)
                buf->append("\n");
            buf->appendf("[%s][%s]\n", handler->TypeName, v.section);
            section = v.section;
        }
        buf->appendf("%s=%d\n", v.key, v.b ? (d.*(v.b) ? 1 : 0) : d.*(v.i));
    }
    if (section)
        buf->append("\n");
}

} // namespace

// ---- Settings ----------------------------------------------------------------------------------

Settings::Settings(AsyncIo& io) : m_io(io) { }

void Settings::registerIniHandler()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "GGUIView";
    handler.TypeHash = ImHashStr("GGUIView");
    handler.ReadOpenFn = viewReadOpen;
    handler.ReadLineFn = viewReadLine;
    handler.WriteAllFn = viewWriteAll;
    handler.UserData = &m_data;
    ImGui::AddSettingsHandler(&handler);
}

void Settings::markViewDirty() { ImGui::MarkIniSettingsDirty(); }

fs::path Settings::prefDir()
{
    const std::string over = getEnv("GGUI_PREF_PATH");
    if (!over.empty())
        return fs::path(over);
    char* p = SDL_GetPrefPath("gg", "ggui");
    fs::path dir = p ? fs::path(p) : fs::current_path();
    SDL_free(p);
    return dir;
}

void Settings::load(std::function<void(const std::string& iniText)> onLoaded)
{
    m_dir = prefDir();
    m_loaded = false;
    const fs::path dir = m_dir;
    m_io.post([this, dir, onLoaded = std::move(onLoaded)]() -> std::function<void()> {
        SettingsData data;
        const std::string text = readAll(dir / "settings.json");
        if (!text.empty()) {
            try {
                data = fromJson(nlohmann::json::parse(text));
            } catch (const std::exception& e) {
                spdlog::warn("ignoring unreadable settings.json: {}", e.what());
            }
        }
        std::string ini = readAll(dir / "imgui.ini");
        return [this, data = std::move(data), ini = std::move(ini), onLoaded]() {
            m_data = data;
            m_loaded = true;
            if (onLoaded)
                onLoaded(ini);
        };
    });
}

void Settings::save()
{
    const std::string text = toJson(m_data).dump(2);
    const fs::path file = m_dir / "settings.json";
    m_io.post([file, text]() -> std::function<void()> {
        writeAtomically(file, text);
        return {};
    });
}

void Settings::saveIni(std::string text)
{
    const fs::path file = m_dir / "imgui.ini";
    m_io.post([file, text = std::move(text)]() -> std::function<void()> {
        writeAtomically(file, text);
        return {};
    });
}

std::string normalizeRepoPath(const std::string& path)
{
    if (path.empty())
        return path;
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(fs::path(path), ec), ec);
    if (ec)
        p = fs::path(path).lexically_normal();
    std::string out = p.string();
    // A root keeps its separator: "D:" would be the drive's current directory, not its root.
    const size_t root = std::max<size_t>(p.root_path().string().size(), 1);
    while (out.size() > root && (out.back() == '/' || out.back() == '\\'))
        out.pop_back();
    return out;
}

std::vector<std::string> uniqueRepoPaths(const std::vector<std::string>& paths)
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& raw : paths) {
        std::string p = normalizeRepoPath(raw);
        if (!p.empty() && seen.insert(p).second)
            out.push_back(std::move(p));
    }
    return out;
}

std::vector<size_t> recentDisplayOrder(const std::vector<std::string>& paths, RecentOrder order)
{
    std::vector<size_t> idx(paths.size());
    for (size_t i = 0; i < idx.size(); ++i)
        idx[i] = i;
    if (order != RecentOrder::Alphabetical)
        return idx;
    const auto names = uniqueRecentNames(paths);
    auto lower = [](std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const std::string ba = lower(names[a].base), bb = lower(names[b].base);
        if (ba != bb)
            return ba < bb;
        return lower(names[a].prefix) < lower(names[b].prefix);
    });
    return idx;
}

void Settings::addRecent(const std::string& rawPath)
{
    const std::string path = normalizeRepoPath(rawPath);
    if (path.empty())
        return;
    auto& r = m_data.recent;
    std::erase(r, path);
    r.insert(r.begin(), path);
    if (r.size() > 20)
        r.resize(20);
    save();
}

void Settings::forgetRecent(const std::string& path)
{
    std::erase(m_data.recent, path);
    std::erase(m_data.recent, normalizeRepoPath(path));
    save();
}

std::vector<RecentName> uniqueRecentNames(const std::vector<std::string>& paths)
{
    // Path components, ignoring trailing separators ("/" and "\\" both split).
    std::vector<std::vector<std::string>> parts(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string cur;
        for (char c : paths[i]) {
            if (c == '/' || c == '\\') {
                if (!cur.empty())
                    parts[i].push_back(std::move(cur));
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty())
            parts[i].push_back(std::move(cur));
        if (parts[i].empty())
            parts[i].push_back(paths[i]); // "/" or ""
    }
    auto nameAt = [&](size_t i, size_t depth) {
        std::string n;
        const auto& p = parts[i];
        for (size_t k = p.size() - depth; k < p.size(); ++k)
            n += (k == p.size() - depth ? "" : "/") + p[k];
        return n;
    };
    std::vector<size_t> depth(paths.size(), 1);
    for (bool grew = true; grew;) {
        grew = false;
        std::map<std::string, std::vector<size_t>> groups;
        for (size_t i = 0; i < paths.size(); ++i)
            groups[nameAt(i, depth[i])].push_back(i);
        for (const auto& [name, members] : groups) {
            if (members.size() < 2)
                continue;
            for (size_t i : members)
                if (depth[i] < parts[i].size()) {
                    ++depth[i];
                    grew = true;
                }
        }
    }
    std::vector<RecentName> out(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        out[i].base = parts[i].back();
        const std::string full = nameAt(i, depth[i]);
        out[i].prefix = full.substr(0, full.size() - out[i].base.size());
    }
    return out;
}

} // namespace ggui
