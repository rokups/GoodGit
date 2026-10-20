#include "shell/Settings.hpp"

#include "util/Env.hpp"

#include <libgg/Files.hpp>
#include <libgg/SequenceEditorLink.hpp>
#include <libgg/Thread.hpp>

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
        const bool hasCont = static_cast<bool>(cont);
        if (hasCont)
            m_done.push_back(std::move(cont));
        m_cv.notify_all();
        if (hasCont) {
            lock.unlock();
            gg::wakeUi(); // pump() runs the continuation on the UI thread
            lock.lock();
        }
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

// Copies `file` to "<file>.bak" and replaces an older backup. Returns true when the copy is made.
bool backupFile(const fs::path& file)
{
    std::error_code ec;
    fs::path bak = file;
    bak += ".bak";
    fs::copy_file(file, bak, fs::copy_options::overwrite_existing, ec);
    if (ec)
        spdlog::warn("cannot copy {} to {}: {}", file.string(), bak.string(), ec.message());
    return !ec;
}

void writeAtomically(const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    bool written = false;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        f.flush();
        written = bool(f);
    }
    if (!written) {
        spdlog::warn("cannot write {}", tmp.string());
        fs::remove(tmp, ec); // not left behind
        return;
    }
    // replaceFile gives up after a short time. On Windows a scanner can hold a file that was just written for
    // longer than that: there the write goes on for about 2 seconds. This is the I/O thread, not the UI's: the
    // jobs queued behind this one (and the exit, which waits for the thread) are late by that much at most.
#ifdef _WIN32
    constexpr int kRounds = 10;
#else
    constexpr int kRounds = 1;
#endif
    for (int round = 0; round < kRounds; ++round) {
        ec = gg::replaceFile(tmp, p);
        if (!ec)
            return;
    }
    spdlog::warn("cannot write {}: {}", p.string(), ec.message());
    fs::remove(tmp, ec); // not left behind
}

// "repoState": {key: {"hiddenRefs": [names]}}. Entries without a hidden ref are not written.
nlohmann::json repoStateToJson(const std::map<std::string, RepoState>& states)
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, state] : states)
        if (!key.empty() && !state.hiddenRefs.empty())
            j[key] = {{"hiddenRefs", state.hiddenRefs}};
    return j;
}

// Reads what repoStateToJson writes; what is not an object, an array or a string is skipped.
std::map<std::string, RepoState> repoStateFromJson(const nlohmann::json& j)
{
    std::map<std::string, RepoState> states;
    const auto it = j.is_object() ? j.find("repoState") : j.end();
    if (it == j.end() || !it->is_object())
        return states;
    for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (entry.key().empty() || !entry.value().is_object())
            continue;
        const auto refs = entry.value().find("hiddenRefs");
        if (refs == entry.value().end() || !refs->is_array())
            continue;
        std::set<std::string> names;
        for (const auto& name : *refs)
            if (name.is_string() && !name.get<std::string>().empty())
                names.insert(name.get<std::string>());
        if (!names.empty())
            states[entry.key()].hiddenRefs.assign(names.begin(), names.end());
    }
    return states;
}

} // namespace

nlohmann::json toJson(const SettingsData& d)
{
    nlohmann::json j;
    j["version"] = 1;
    j["uiScale"] = d.uiScale;
    j["theme"] = d.theme == Theme::Light ? "light" : "dark";
    j["recent"] = d.recent;
    j["repositories"] = nlohmann::json::array();
    for (const auto& e : d.repositories) {
        nlohmann::json item;
        item["path"] = e.path;
        if (!e.alias.empty())
            item["alias"] = e.alias;
        j["repositories"].push_back(std::move(item));
    }
    j["recentOrder"] = d.recentOrder == RecentOrder::Alphabetical ? "alphabetical" : "recent";
    j["panels"] = d.panels;
    j["nothingStaged"] = d.nothingStaged == NothingStaged::StageAll ? "stage-all"
        : d.nothingStaged == NothingStaged::StageSelected                 ? "stage-selected"
                                                                          : "ask";
    j["expandStagesOnCheckout"] = d.expandStagesOnCheckout;
    j["expandConflictStages"] = d.expandConflictStages;
    j["submodulesAutoUpdate"] = d.submodulesAutoUpdate;
    j["submodulesAutoInit"] = d.submodulesAutoInit;
    j["historyBadgePrefix"] = d.historyBadgePrefix;
    j["historyBadgeSuffix"] = d.historyBadgeSuffix;
    j["historyGraphInDescription"] = d.historyGraphInDescription;
    j["repoState"] = repoStateToJson(d.repoState);
    return j;
}

namespace {

// Non-throwing getters: a key that is missing or has the wrong type gives the default, and no other key is affected.
// A key that is present with the wrong type also sets `*bad` (when `bad` is not null): the file has a value that is lost.
void flagWrongType(bool present, bool* bad)
{
    if (present && bad)
        *bad = true;
}

float jsonFloat(const nlohmann::json& j, const char* key, float def, bool* bad)
{
    const auto it = j.find(key);
    if (it != j.end() && it->is_number())
        return static_cast<float>(it->get<double>());
    flagWrongType(it != j.end(), bad);
    return def;
}

int jsonInt(const nlohmann::json& j, const char* key, int def, bool* bad)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        flagWrongType(it != j.end(), bad);
        return def;
    }
    return static_cast<int>(std::clamp(it->get<double>(), -1.0e9, 1.0e9));
}

bool jsonBool(const nlohmann::json& j, const char* key, bool def, bool* bad)
{
    const auto it = j.find(key);
    if (it != j.end() && it->is_boolean())
        return it->get<bool>();
    flagWrongType(it != j.end(), bad);
    return def;
}

std::string jsonString(const nlohmann::json& j, const char* key, const std::string& def, bool* bad)
{
    const auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    flagWrongType(it != j.end(), bad);
    return def;
}

} // namespace

SettingsData fromJson(const nlohmann::json& j, bool* skipped)
{
    SettingsData d;
    if (!j.is_object())
        return d;
    const auto skip = [skipped] {
        if (skipped)
            *skipped = true;
    };
    d.uiScale = std::clamp(jsonFloat(j, "uiScale", 1.0f, skipped), static_cast<float>(kUiScaleMinPercent) / 100.0f,
        static_cast<float>(kUiScaleMaxPercent) / 100.0f);
    d.theme = jsonString(j, "theme", "dark", skipped) == "light" ? Theme::Light : Theme::Dark;
    d.historyBadgePrefix = std::clamp(jsonInt(j, "historyBadgePrefix", 12, skipped), 1, 100);
    d.historyBadgeSuffix = std::clamp(jsonInt(j, "historyBadgeSuffix", 12, skipped), 1, 100);
    if (j.contains("recent") && j["recent"].is_array()) {
        for (const auto& r : j["recent"]) {
            if (r.is_string())
                d.recent.push_back(r.get<std::string>());
            else
                skip();
        }
    } else if (j.contains("recent")) {
        skip();
    }
    d.recent = uniqueRepoPaths(d.recent);
    if (j.contains("repositories") && j["repositories"].is_array()) {
        std::set<std::string> seen;
        for (const auto& item : j["repositories"]) {
            const auto pathIt = item.is_object() ? item.find("path") : item.end();
            if (!item.is_object() || pathIt == item.end() || !pathIt->is_string()) {
                skip();
                continue;
            }
            std::string path = normalizeRepoPath(pathIt->get<std::string>());
            if (path.empty() || !seen.insert(path).second)
                continue;
            const auto aliasIt = item.find("alias");
            std::string alias = aliasIt != item.end() && aliasIt->is_string() ? normalizeAlias(aliasIt->get<std::string>())
                                                                              : std::string();
            d.repositories.push_back({std::move(path), std::move(alias)});
        }
    } else if (!j.contains("repositories")) {
        // A settings file from before the permanent list: it starts as the recent list.
        for (const auto& path : d.recent)
            d.repositories.push_back({path, std::string()});
    } else {
        skip(); // "repositories" has the wrong type: its content is not readable
    }
    d.recentOrder = jsonString(j, "recentOrder", "recent", skipped) == "alphabetical" ? RecentOrder::Alphabetical
                                                                              : RecentOrder::MostRecent;
    if (j.contains("panels") && j["panels"].is_object()) {
        for (auto it = j["panels"].begin(); it != j["panels"].end(); ++it) {
            if (it.value().is_boolean())
                d.panels[it.key()] = it.value().get<bool>();
            else
                skip();
        }
    } else if (j.contains("panels")) {
        skip();
    }
    // The view state and the window placement now live in imgui.ini (applied after this); they are
    // still read here for a settings.json written by an earlier version.
    if (j.contains("diff") && j["diff"].is_object()) {
        const auto& diff = j["diff"];
        d.diffSideBySide = jsonBool(diff, "sideBySide", false, skipped);
        d.diffContext = std::clamp(jsonInt(diff, "context", 3, skipped), 0, 100);
        d.diffWhitespace = std::clamp(jsonInt(diff, "whitespace", 0, skipped), 0, 2);
    } else if (j.contains("diff")) {
        skip();
    }
    d.historyShowStashes = jsonBool(j, "historyShowStashes", true, skipped);
    const std::string ns = jsonString(j, "nothingStaged", "ask", skipped);
    d.nothingStaged = ns == "stage-all" ? NothingStaged::StageAll
        : ns == "stage-selected"        ? NothingStaged::StageSelected
                                        : NothingStaged::Ask;
    d.expandStagesOnCheckout = jsonBool(j, "expandStagesOnCheckout", false, skipped);
    d.expandConflictStages = jsonBool(j, "expandConflictStages", false, skipped);
    d.submodulesAutoUpdate = jsonBool(j, "submodulesAutoUpdate", true, skipped);
    d.submodulesAutoInit = jsonBool(j, "submodulesAutoInit", false, skipped);
    d.historyGraphInDescription = jsonBool(j, "historyGraphInDescription", false, skipped);
    if (j.contains("repoState") && !j["repoState"].is_object())
        skip();
    d.repoState = repoStateFromJson(j);
    if (j.contains("window") && j["window"].is_object()) {
        const auto& w = j["window"];
        d.windowX = jsonInt(w, "x", -1, skipped);
        d.windowY = jsonInt(w, "y", -1, skipped);
        d.windowW = jsonInt(w, "w", 0, skipped);
        d.windowH = jsonInt(w, "h", 0, skipped);
        d.windowMaximized = jsonBool(w, "maximized", false, skipped);
    } else if (j.contains("window")) {
        skip();
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
    {"Branches", "HideNew", &SettingsData::branchesHideNew, nullptr, 0, 1},
    {"Tags", "HideNew", &SettingsData::tagsHideNew, nullptr, 0, 1},
    {"Diff", "SideBySide", &SettingsData::diffSideBySide, nullptr, 0, 1},
    {"Diff", "Whitespace", nullptr, &SettingsData::diffWhitespace, 0, 2},
    {"Diff", "Context", nullptr, &SettingsData::diffContext, 0, 100},
    {"Info", "WrapMessage", &SettingsData::infoWrapMessage, nullptr, 0, 1},
    {"Info", "MessageLines", nullptr, &SettingsData::infoMessageLines, 2, 40},
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
    fs::path dir;
    if (p) {
        dir = fs::path(p);
    } else {
        std::error_code ec;
        dir = fs::current_path(ec);
        if (ec || dir.empty())
            dir = ".";
    }
    SDL_free(p);
    return dir;
}

void Settings::load(std::function<void(const std::string& iniText)> onLoaded)
{
    m_dir = prefDir();
    m_loaded = false;
    m_backupFailed = false;
    m_pendingRepoKeys.clear();
    const fs::path dir = m_dir;
    m_io.post([this, dir, onLoaded = std::move(onLoaded)]() -> std::function<void()> {
        SettingsData data;
        const fs::path file = dir / "settings.json";
        const std::string text = readAll(file);
        bool backupFailed = false;
        bool damaged = false;
        if (text.empty()) {
            // A file that exists and has a size, but cannot be read, is damaged: an empty or missing file is "no file".
            std::error_code ec;
            const auto size = fs::is_regular_file(file, ec) && !ec ? fs::file_size(file, ec) : std::uintmax_t{0};
            damaged = !ec && size != 0;
        } else {
            // A file that is not readable as a whole, or that has entries which fromJson skips, is copied to
            // settings.json.bak now: a later save() writes the file again, and the skipped data would be lost.
            try {
                const nlohmann::json j = nlohmann::json::parse(text);
                if (j.is_object())
                    data = fromJson(j, &damaged);
                else
                    damaged = true;
            } catch (const std::exception& e) {
                spdlog::warn("ignoring unreadable settings.json: {}", e.what());
                data = SettingsData{};
                damaged = true;
            }
        }
        if (damaged) {
            backupFailed = !backupFile(file);
            data.loadWarning = backupFailed
                ? "Some settings were not readable. The backup of the old file failed. Settings are not saved."
                : "Some settings were not readable. A backup of the old file is saved as settings.json.bak.";
        }
        std::string ini = readAll(dir / "imgui.ini");
        return [this, data = std::move(data), ini = std::move(ini), backupFailed, onLoaded]() {
            m_data = data;
            m_backupFailed = backupFailed;
            m_loaded = true;
            if (onLoaded)
                onLoaded(ini);
        };
    });
}

void Settings::save()
{
    if (m_backupFailed) {
        // The file is damaged and its backup failed: a write would destroy the only copy of the old data.
        spdlog::warn("settings.json is not saved: it was unreadable and its backup failed");
        return;
    }
    nlohmann::json j = toJson(m_data);
    std::map<std::string, RepoState> own;
    for (const auto& key : m_pendingRepoKeys)
        if (const auto it = m_data.repoState.find(key); it != m_data.repoState.end())
            own.emplace(key, it->second);
    std::set<std::string> ownKeys;
    ownKeys.swap(m_pendingRepoKeys);
    for (const auto& key : ownKeys)
        ++m_savingRepoKeys[key];
    const fs::path file = m_dir / "settings.json";
    m_io.post([this, file, j = std::move(j), own = std::move(own), ownKeys = std::move(ownKeys)]() mutable
              -> std::function<void()> {
        // Another ggui process (another worktree, another repository) may have stored repository state since
        // this one read the file: the file's state is kept, except for the repositories this save changes.
        std::map<std::string, RepoState> merged;
        bool fromFile = false;
        if (const std::string old = readAll(file); !old.empty()) {
            try {
                merged = repoStateFromJson(nlohmann::json::parse(old));
                fromFile = true;
            } catch (const std::exception&) {
            }
        }
        if (!fromFile)
            merged = repoStateFromJson(j); // no readable file: this process's state
        for (const auto& key : ownKeys)
            merged.erase(key);
        for (auto& [key, state] : own)
            merged[key] = std::move(state);
        j["repoState"] = repoStateToJson(merged);
        writeAtomically(file, j.dump(2));
        // Memory takes the file's state, except for repositories changed after this save was posted (pending, or
        // in a later save that is not finished: this file has an older value for them).
        return [this, merged = std::move(merged), ownKeys = std::move(ownKeys)]() {
            for (const auto& key : ownKeys)
                if (const auto it = m_savingRepoKeys.find(key); it != m_savingRepoKeys.end() && --it->second <= 0)
                    m_savingRepoKeys.erase(it);
            const auto keep = [this](const std::string& key) {
                return m_pendingRepoKeys.count(key) || m_savingRepoKeys.count(key);
            };
            auto& states = m_data.repoState;
            for (auto it = states.begin(); it != states.end();)
                it = !keep(it->first) && !merged.count(it->first) ? states.erase(it) : std::next(it);
            for (const auto& [key, state] : merged)
                if (!keep(key))
                    states[key] = state;
        };
    });
}

std::set<std::string> Settings::hiddenRefs(const std::string& key) const
{
    const auto it = m_data.repoState.find(key);
    if (it == m_data.repoState.end())
        return {};
    return {it->second.hiddenRefs.begin(), it->second.hiddenRefs.end()};
}

void Settings::setHiddenRefs(const std::string& key, const std::set<std::string>& names)
{
    if (key.empty())
        return;
    std::vector<std::string> sorted(names.begin(), names.end());
    sorted.erase(std::remove(sorted.begin(), sorted.end(), std::string()), sorted.end());
    const auto it = m_data.repoState.find(key);
    const std::vector<std::string> before = it != m_data.repoState.end() ? it->second.hiddenRefs : std::vector<std::string>{};
    if (sorted == before)
        return;
    if (sorted.empty())
        m_data.repoState.erase(key);
    else
        m_data.repoState[key].hiddenRefs = std::move(sorted);
    m_pendingRepoKeys.insert(key);
    save();
}

std::string repoStateKey(const fs::path& commonDir)
{
    if (commonDir.empty())
        return {};
    return gg::seqlink::gitDirKey(commonDir);
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

std::string normalizeAlias(const std::string& alias)
{
    const auto trim = [](const std::string& s) {
        const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        size_t b = 0, e = s.size();
        while (b < e && space(s[b]))
            ++b;
        while (e > b && space(s[e - 1]))
            --e;
        return s.substr(b, e - b);
    };
    std::string out;
    size_t start = 0;
    const std::string text = trim(alias);
    while (start <= text.size()) {
        size_t slash = text.find('/', start);
        if (slash == std::string::npos)
            slash = text.size();
        const std::string segment = trim(text.substr(start, slash - start));
        if (!segment.empty())
            out += (out.empty() ? "" : "/") + segment;
        start = slash + 1;
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
    std::vector<std::string> keys(paths.size());
    for (size_t i = 0; i < keys.size(); ++i)
        keys[i] = lower(names[i].text());
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return keys[a] < keys[b]; });
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
    save();
}

void Settings::forgetRecent(const std::string& path)
{
    std::erase(m_data.recent, path);
    std::erase(m_data.recent, normalizeRepoPath(path));
    save();
}

void Settings::addRepository(const std::string& rawPath)
{
    const std::string path = normalizeRepoPath(rawPath);
    if (path.empty())
        return;
    auto& r = m_data.repositories;
    if (std::any_of(r.begin(), r.end(), [&](const RepoEntry& e) { return e.path == path; }))
        return;
    r.push_back({path, std::string()});
    save();
}

void Settings::removeRepository(const std::string& path)
{
    const std::string normalized = normalizeRepoPath(path);
    std::erase_if(m_data.repositories, [&](const RepoEntry& e) { return e.path == path || e.path == normalized; });
    save();
}

void Settings::setAlias(const std::string& path, const std::string& alias)
{
    const std::string normalized = normalizeRepoPath(path);
    for (auto& e : m_data.repositories) {
        if (e.path != path && e.path != normalized)
            continue;
        e.alias = normalizeAlias(alias);
        save();
        return;
    }
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
