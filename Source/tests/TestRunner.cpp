#include "tests/TestRunner.hpp"

#include "platform/Platform.hpp"
#include "shell/App.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"
#include "util/Logging.hpp"

#include <libgg/Git2.hpp>
#include <libgg/GitRunner.hpp>
#include <libgg/Thread.hpp>

#include <SDL3/SDL_filesystem.h>
#include <imgui_te_engine.h>
#include <imgui_te_internal.h>
#include <imgui_te_utils.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>

namespace ggtest {
namespace {

TestRunner* g_runner = nullptr;
ggui::App* g_app = nullptr;
fs::path g_root;       // per-process test root
fs::path g_artifacts;  // failure output
fs::path g_fixtureCache;
std::string g_basePath; // PATH with git-gg first; restored before every test
std::map<const ImGuiTest*, const TestInfo*> g_infoByTest;

std::string sanitize(const std::string& s)
{
    std::string out;
    for (char c : s)
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_');
    return out;
}

void writeGlobalGitConfig(const fs::path& home)
{
    std::ofstream f(home / ".gitconfig", std::ios::binary);
    f << "[user]\n\tname = Test User\n\temail = test@example.com\n"
         "[init]\n\tdefaultBranch = main\n"
         "[core]\n\tautocrlf = false\n\tfsmonitor = false\n"
         "[advice]\n\tdetachedHead = false\n\tskippedCherryPicks = false\n"
         "[commit]\n\tgpgsign = false\n"
         "[tag]\n\tgpgsign = false\n"
         "[protocol \"file\"]\n\tallow = always\n"
         "[gc]\n\tauto = 0\n"
         "[maintenance]\n\tauto = false\n";
}

// Isolates HOME, XDG_CONFIG_HOME, GIT_CONFIG_GLOBAL and the preferences dir under `dir`.
void isolateEnvironment(const fs::path& dir)
{
    const fs::path home = dir / "home";
    fs::create_directories(home / ".config");
    fs::create_directories(dir / "prefs");
    writeGlobalGitConfig(home);
    // Scenarios start without the first-open hooks prompt; hook scenarios turn it on.
    std::ofstream(dir / "prefs" / "settings.json") << "{\"askHooksOnOpen\": false}\n";
    ggui::setEnv("HOME", home.string());
    ggui::setEnv("USERPROFILE", home.string());
    ggui::setEnv("XDG_CONFIG_HOME", (home / ".config").string());
    ggui::setEnv("GIT_CONFIG_GLOBAL", (home / ".gitconfig").string());
    ggui::setEnv("GIT_CONFIG_NOSYSTEM", "1");
    ggui::setEnv("GGUI_PREF_PATH", (dir / "prefs").string());
    // Deterministic dates for fixtures are set per command by the harness, not globally.
    ggui::unsetEnv("GIT_DIR");
    ggui::unsetEnv("GIT_WORK_TREE");
    ggui::unsetEnv("GIT_INDEX_FILE");
    ggui::unsetEnv("GIT_OBJECT_DIRECTORY");
    ggui::unsetEnv("GIT_EDITOR");
    ggui::unsetEnv("GIT_SEQUENCE_EDITOR");
    ggui::setEnv("GIT_EDITOR", "true");
    if (!g_basePath.empty())
        ggui::setEnv("PATH", g_basePath);
    ggui::unsetEnv("GGUI_TEST_PICK_PATH");
    ggui::unsetEnv("GGUI_TEST_PICK_CANCEL");
    ggui::unsetEnv("GIT_SSH_COMMAND");
    ggui::unsetEnv("GG_HOOKS_MODE");
    ggui::unsetEnv("GG_GGUI");
    ggui::setEnv("EDITOR", "true");
    gg::git2::resetConfigSearchPaths();
}

} // namespace

void writeFailureOutput(ImGuiTestContext* ctx, const TestInfo& info, const Scenario& s, const fs::path& out)
{
    std::error_code ec;
    fs::create_directories(out, ec);
    {
        std::ofstream f(out / "app.log");
        for (const auto& line : ggui::recentLogLines())
            f << line << '\n';
    }
    {
        std::ofstream f(out / "git-commands.log");
        for (const auto& e : gg::commandLog()) {
            f << "[" << e.duration.count() << " ms] (" << e.cwd.string() << ") exit=" << e.exitCode << ":";
            for (const auto& a : e.args)
                f << " " << a;
            f << '\n';
            if (!e.err.empty())
                f << "    stderr: " << gg::trim(e.err) << '\n';
        }
    }
    {
        std::ofstream f(out / "info.txt");
        f << "test: " << info.category << "/" << info.name << "\nseed: " << s.seed() << "\nroot: "
          << s.root().string() << "\nsource: " << info.file << ":" << info.line << '\n';
    }
    // Screenshot through the test engine capture (ScreenCaptureFunc reads the offscreen frame).
    ctx->CaptureReset();
    const std::string png = (out / "screenshot.png").string();
    ImStrncpy(ctx->CaptureArgs->InOutputFile, png.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
    // The engine refuses captures once a test is in error; suspend the status around it.
    const ImGuiTestStatus status = ctx->TestOutput->Status;
    const bool abort = ctx->Abort;
    ctx->TestOutput->Status = ImGuiTestStatus_Running;
    ctx->Abort = false;
    ctx->CaptureScreenshot(ImGuiCaptureFlags_HideMouseCursor);
    ctx->TestOutput->Status = status;
    ctx->Abort = abort;
    spdlog::warn("test failed: {}/{} (seed {}), output in {}", info.category, info.name, s.seed(), out.string());
}

namespace {

void runTest(ImGuiTestContext* ctx, const TestInfo& info)
{
    const fs::path dir = g_root / sanitize(info.category + "_" + info.name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    isolateEnvironment(dir);
    gg::clearCommandLog();
    ggui::clearRecentLogLines();
    gg::setSlowGitLatency(std::chrono::milliseconds(0));
    gg::clearUiThreadViolations();

    std::uint64_t seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (const char* s = std::getenv("GGUI_TEST_SEED"); s && *s)
        seed = std::strtoull(s, nullptr, 10);
    spdlog::info("=== test {}/{} seed={}", info.category, info.name, seed);

    g_app->resetForTest();
    ctx->Yield(2);

    Scenario scenario(ctx, *g_app, dir, seed);
    info.body(ctx, scenario);

    // Post-test hook: every repository the test touched must pass git fsck, be in a state
    // plain git understands (§8.3) and hold no private gg metadata (rule 2, §9).
    for (const auto& repo : scenario.tracked()) {
        if (!fs::exists(repo))
            continue;
        std::string output;
        const bool ok = scenario.fsck(repo, &output);
        if (!ok)
            ctx->LogError("git fsck failed for %s:\n%s", repo.string().c_str(), output.c_str());
        IM_CHECK_NO_RET(ok);
        std::string why;
        const bool transparent = scenario.gitTransparent(repo, &why);
        if (!transparent)
            ctx->LogError("git transparency (REBUILD_PLAN §9) broken for %s:%s", repo.string().c_str(), why.c_str());
        IM_CHECK_NO_RET(transparent);
    }
    const auto violations = gg::uiThreadViolations();
    for (const auto& v : violations)
        ctx->LogError("UI-thread violation: %s", v.c_str());

    if (ctx->IsError())
        writeFailureOutput(ctx, info, scenario, g_artifacts / sanitize(info.category + "_" + info.name));

    g_app->resetForTest();
    ctx->Yield(2);
    if (!ctx->IsError() && !std::getenv("GGUI_KEEP_TEST_DIRS"))
        fs::remove_all(dir, ec);
}

bool screenCapture(ImGuiID, int x, int y, int w, int h, unsigned int* pixels, void* user)
{
    return static_cast<ggui::Platform*>(user)->readPixels(x, y, w, h, pixels);
}

void logToSpdlog(ImGuiTestEngine*, ImGuiTestContext*, ImGuiTestVerboseLevel, const char* line, void*)
{
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    spdlog::info("[te] {}", s);
}

} // namespace

std::vector<TestInfo>& registry()
{
    static std::vector<TestInfo> tests;
    return tests;
}

Registrar::Registrar(const char* category, const char* name, std::initializer_list<const char*> specs, TestBody body,
    const char* file, int line)
{
    TestInfo info;
    info.category = category;
    info.name = name;
    for (const char* s : specs)
        info.specs.emplace_back(s);
    info.body = body;
    info.file = file;
    info.line = line;
    registry().push_back(std::move(info));
}

fs::path artifactsDir() { return g_artifacts; }

fs::path fixtureCacheDir() { return g_fixtureCache; }

void prepareProcessForTests(const char* argv0)
{
    (void)argv0;
    // Resolve the fixture cache before HOME is isolated.
    if (const char* c = std::getenv("GGUI_FIXTURE_CACHE"); c && *c)
        g_fixtureCache = c;
    else if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x)
        g_fixtureCache = fs::path(x) / "ggui-fixtures";
    else if (const char* h = std::getenv("HOME"); h && *h)
        g_fixtureCache = fs::path(h) / ".cache" / "ggui-fixtures";
    else
        g_fixtureCache = fs::temp_directory_path() / "ggui-fixtures";
    if (const char* r = std::getenv("GGUI_TEST_ROOT"); r && *r)
        g_root = r;
    else
        g_root = fs::temp_directory_path() / ("ggui-tests-" + std::to_string(ggui::processId()));
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root);
    g_root = fs::canonical(g_root);
    if (const char* a = std::getenv("GGUI_TEST_ARTIFACTS"); a && *a)
        g_artifacts = a;
    else
        g_artifacts = fs::current_path() / "test-artifacts";

    // Process-level isolation until the first test sets its own directories.
    isolateEnvironment(g_root / "process");
    fs::create_directories(g_root / "empty-system-config");
    gg::git2::setSystemConfigPath(g_root / "empty-system-config");

    // The git-gg under test sits next to ggui.
    const std::string exeDir = ggui::executableDir();
    const char* path = std::getenv("PATH");
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    g_basePath = exeDir + sep + (path ? path : "");
    ggui::setEnv("PATH", g_basePath);

    gg::setCommandLogEnabled(true);
    gg::setUiThreadCheck(gg::UiThreadCheck::Abort);
    if (const char* ms = std::getenv("GGUI_SLOW_GIT_MS"); ms && *ms)
        gg::setSlowGitLatency(std::chrono::milliseconds(std::atoi(ms)));
}

int listTests()
{
    for (const auto& t : registry()) {
        std::printf("%s/%s\t", t.category.c_str(), t.name.c_str());
        for (size_t i = 0; i < t.specs.size(); ++i)
            std::printf("%s%s", i ? "," : "", t.specs[i].c_str());
        std::printf("\n");
    }
    return 0;
}

TestRunner::TestRunner(ggui::App& app, ggui::Platform& platform) : m_app(app), m_platform(platform)
{
    g_runner = this;
    g_app = &app;
    m_engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(m_engine);
    io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    io.ConfigNoThrottle = true;
    io.ConfigSavedSettings = false;
    io.ConfigLogToTTY = false;
    io.ConfigLogToFunc = &logToSpdlog;
    io.ConfigCaptureEnabled = true;
    io.ConfigWatchdogWarning = 60.0f;
    io.ConfigWatchdogKillTest = 600.0f;
    io.ScreenCaptureFunc = &screenCapture;
    io.ScreenCaptureUserData = &platform;
    ImGuiTestEngine_Start(m_engine, ImGui::GetCurrentContext());
}

TestRunner::~TestRunner()
{
    if (m_engine)
        ImGuiTestEngine_DestroyContext(m_engine);
    g_runner = nullptr;
    g_app = nullptr;
}

void TestRunner::start(const std::string& filter, const std::string& traceFile, int shard, int shards)
{
    m_traceFile = traceFile;
    std::vector<ImGuiTest*> registered;
    for (const auto& info : registry()) {
        ImGuiTest* t = ImGuiTestEngine_RegisterTest(m_engine, info.category.c_str(), info.name.c_str(), info.file, info.line);
        const TestInfo* ptr = &info;
        t->TestFunc = [ptr](ImGuiTestContext* ctx) { runTest(ctx, *ptr); };
        g_infoByTest[t] = ptr;
        registered.push_back(t);
    }
    if (shards <= 1) {
        ImGuiTestEngine_QueueTests(m_engine, ImGuiTestGroup_Tests, filter.empty() ? nullptr : filter.c_str(),
            ImGuiTestRunFlags_RunFromCommandLine);
    } else {
        // Comma-separated substrings of "category/name" (sharding does its own filtering).
        std::vector<std::string> needles;
        for (size_t start = 0; start <= filter.size();) {
            size_t end = filter.find(',', start);
            if (end == std::string::npos)
                end = filter.size();
            if (end > start)
                needles.push_back(filter.substr(start, end - start));
            start = end + 1;
        }
        for (size_t i = 0; i < registered.size(); ++i) {
            if (static_cast<int>(i % static_cast<size_t>(shards)) != shard)
                continue;
            const std::string full = std::string(registered[i]->Category) + "/" + registered[i]->Name;
            bool match = needles.empty();
            for (const auto& n : needles)
                match = match || full.find(n) != std::string::npos;
            if (match)
                ImGuiTestEngine_QueueTest(m_engine, registered[i], ImGuiTestRunFlags_RunFromCommandLine);
        }
    }
    ImVector<ImGuiTestRunTask> queue;
    ImGuiTestEngine_GetTestQueue(m_engine, &queue);
    m_queued = queue.Size;
    std::fprintf(stderr, "ggui: running %d test(s)\n", m_queued);
    if (m_queued == 0)
        m_exitCode = 2;
}

void TestRunner::postSwap() { ImGuiTestEngine_PostSwap(m_engine); }

bool TestRunner::finished() const { return m_queued == 0 || ImGuiTestEngine_IsTestQueueEmpty(m_engine); }

void TestRunner::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(m_engine, &summary);
    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(m_engine, &tests);
    int failed = 0;
    for (ImGuiTest* t : tests) {
        if (t->Output.Status == ImGuiTestStatus_Error) {
            ++failed;
            std::fprintf(stderr, "FAILED: %s/%s\n", t->Category, t->Name);
        }
    }
    std::fprintf(stderr, "ggui: %d/%d tests passed\n", summary.CountSuccess, summary.CountTested);
    if (m_queued > 0)
        m_exitCode = (summary.CountSuccess == summary.CountTested && failed == 0) ? 0 : 1;
    writeTrace();
    ImGuiTestEngine_Stop(m_engine);
    // Scratch repositories go away with the run (failures were copied to the artifacts
    // directory); GGUI_KEEP_TEST_DIRS=1 keeps them for inspection.
    if (!g_root.empty() && !std::getenv("GGUI_KEEP_TEST_DIRS")) {
        g_app->resetForTest(); // close the last repository first (Windows cannot delete open files)
        std::error_code ec;
        fs::remove_all(g_root, ec);
    }
}

void TestRunner::writeTrace()
{
    if (m_traceFile.empty())
        return;
    nlohmann::json out;
    out["tests"] = nlohmann::json::array();
    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(m_engine, &tests);
    for (ImGuiTest* t : tests) {
        auto it = g_infoByTest.find(t);
        if (it == g_infoByTest.end())
            continue;
        const char* status = "not-run";
        switch (t->Output.Status) {
        case ImGuiTestStatus_Success: status = "success"; break;
        case ImGuiTestStatus_Error: status = "error"; break;
        case ImGuiTestStatus_Queued:
        case ImGuiTestStatus_Running: status = "incomplete"; break;
        default: break;
        }
        out["tests"].push_back({{"category", t->Category}, {"name", t->Name}, {"status", status},
            {"specs", it->second->specs}, {"file", it->second->file}, {"line", it->second->line}});
    }
    std::ofstream f(m_traceFile);
    f << out.dump(2) << '\n';
}

} // namespace ggtest
