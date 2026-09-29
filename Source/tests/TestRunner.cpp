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

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#undef Yield // winbase.h's, not ImGuiTestContext::Yield
#endif
#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#include <atomic>
#include <cstring>
#include <exception>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
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
const TestInfo* g_current = nullptr;               // the test whose body is running
std::map<const TestInfo*, std::string> g_skipped; // GG_REQUIRE_GIT: test → reason

std::string sanitize(const std::string& s)
{
    std::string out;
    for (char c : s)
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_');
    return out;
}

// Watchdog: a test that runs longer than GGUI_TEST_TIMEOUT seconds (default 600) is reported with
// what it waits for (running commands, the last log lines) and the run ends, instead of hanging
// until a CI job's time limit with nothing to show.
std::mutex g_watchMutex;
std::string g_watchTest;
std::chrono::steady_clock::time_point g_watchStart;

void watchTest(const std::string& name)
{
    std::lock_guard lock(g_watchMutex);
    g_watchTest = name;
    g_watchStart = std::chrono::steady_clock::now();
}

// Crash report for a test run: which test, what happened and (Windows) where, on stderr, instead
// of a silent exit code.
std::string currentTestName()
{
    std::lock_guard lock(g_watchMutex);
    return g_watchTest;
}

#ifdef _WIN32
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
{
    std::fprintf(stderr, "ggui: CRASH in %s: exception 0x%08lx at %p\n", currentTestName().c_str(),
        info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                              SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
         ++i) {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) ? symbol->Name : "?";
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD column = 0;
        // The module and the offset in it: addr2line/llvm-symbolizer on the uploaded binary.
        const DWORD64 base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        char module[MAX_PATH] = "?";
        if (base)
            GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
        const char* moduleName = std::strrchr(module, '\\') ? std::strrchr(module, '\\') + 1 : module;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &column, &line))
            std::fprintf(stderr, "ggui:   #%d %s (%s:%lu) %s+0x%llx\n", i, name, line.FileName, line.LineNumber, moduleName,
                static_cast<unsigned long long>(frame.AddrPC.Offset - base));
        else
            std::fprintf(stderr, "ggui:   #%d %s %s+0x%llx\n", i, name, moduleName,
                static_cast<unsigned long long>(frame.AddrPC.Offset - base));
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER; // ends the process
}
#endif

void installCrashReport()
{
    std::set_terminate([] {
        std::string what = "no exception";
        if (auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                what = ex.what();
            } catch (...) {
                what = "an exception of unknown type";
            }
        }
        std::fprintf(stderr, "ggui: TERMINATE in %s: %s\n", currentTestName().c_str(), what.c_str());
        std::fflush(stderr);
        std::abort();
    });
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashFilter);
#endif
}

void startWatchdog()
{
    const char* env = std::getenv("GGUI_TEST_TIMEOUT");
    const int limit = env && *env ? std::atoi(env) : 600;
    if (limit <= 0)
        return;
    std::thread([limit] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::string test;
            {
                std::lock_guard lock(g_watchMutex);
                if (g_watchTest.empty()
                    || std::chrono::steady_clock::now() - g_watchStart < std::chrono::seconds(limit))
                    continue;
                test = g_watchTest;
            }
            std::fprintf(stderr, "ggui: TIMEOUT: %s ran longer than %d s\n", test.c_str(), limit);
            for (const auto& c : gg::runningCommands()) {
                std::string line;
                for (const auto& a : c.args)
                    line += " " + a;
                std::fprintf(stderr, "ggui:   running for %lld ms in %s:%s\n", static_cast<long long>(c.duration.count()),
                    c.cwd.string().c_str(), line.c_str());
            }
            const auto lines = ggui::recentLogLines();
            const size_t from = lines.size() > 40 ? lines.size() - 40 : 0;
            for (size_t i = from; i < lines.size(); ++i)
                std::fprintf(stderr, "ggui:   log: %s\n", lines[i].c_str());
            std::fflush(stderr);
            std::_Exit(3);
        }
    }).detach();
}

// A test's directory name: the start of its name and a hash of all of it. Full names run past
// 200 characters, and a repository inside them past Windows' MAX_PATH.
std::string testDirName(const TestInfo& info)
{
    const std::string full = info.category + "_" + info.name;
    std::uint32_t hash = 2166136261u; // FNV-1a
    for (unsigned char c : full)
        hash = (hash ^ c) * 16777619u;
    char suffix[10];
    std::snprintf(suffix, sizeof suffix, "-%08x", hash);
    std::string name = sanitize(full.substr(0, 40));
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    return name + suffix;
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
    // $XDG_CONFIG_HOME/git exists before libgit2 looks: on Windows it keeps only directories that
    // exist when its search paths are set (below).
    fs::create_directories(home / ".config" / "git");
    fs::create_directories(dir / "prefs");
    writeGlobalGitConfig(home);
    // Scenarios start without the first-open hooks prompt; hook scenarios turn it on.
    std::ofstream(dir / "prefs" / "settings.json") << "{\"askHooksOnOpen\": false}\n";
    ggui::setEnv("HOME", home.string());
    ggui::setEnv("USERPROFILE", home.string());
    ggui::setEnv("XDG_CONFIG_HOME", (home / ".config").string());
    ggui::setEnv("GIT_CONFIG_GLOBAL", (home / ".gitconfig").string());
    ggui::setEnv("GIT_CONFIG_NOSYSTEM", "1");
    // git config --system reads the system file despite NOSYSTEM: name one that does not exist.
    ggui::setEnv("GIT_CONFIG_SYSTEM", (home / "no-system-gitconfig").string());
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
    ggui::unsetEnv("GG_DEBUG_PROCESS");
    ggui::unsetEnv("GG_HOOK_SHELL");
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
    const fs::path dir = g_root / testDirName(info);
    std::error_code ec;
    removeAll(dir);
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
    // On stderr too, flushed: a run that dies (a crash, a CI timeout) shows which test it was in.
    static int started = 0;
    std::fprintf(stderr, "ggui: [%d] %s/%s\n", ++started, info.category.c_str(), info.name.c_str());
    std::fflush(stderr);
    watchTest(info.category + "/" + info.name);

    g_app->resetForTest();
    ctx->Yield(2);

    Scenario scenario(ctx, *g_app, dir, seed);
    g_skipped.erase(&info);
    g_current = &info;
    // An exception fails this test (the rest of the run goes on) instead of ending the process.
    try {
        info.body(ctx, scenario);
    } catch (const std::exception& e) {
        ctx->LogError("uncaught exception: %s", e.what());
        IM_CHECK_NO_RET(false);
    }
    g_current = nullptr;

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
            ctx->LogError("git transparency (product spec §9) broken for %s:%s", repo.string().c_str(), why.c_str());
        IM_CHECK_NO_RET(transparent);
    }
    const auto violations = gg::uiThreadViolations();
    for (const auto& v : violations)
        ctx->LogError("UI-thread violation: %s", v.c_str());

    if (ctx->IsError())
        writeFailureOutput(ctx, info, scenario, g_artifacts / testDirName(info));

    g_app->resetForTest();
    ctx->Yield(2);
    if (!ctx->IsError() && !std::getenv("GGUI_KEEP_TEST_DIRS"))
        removeAll(dir);
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

Registrar::Registrar(const char* category, const char* name, TestBody body, const char* file, int line,
    bool manual)
{
    TestInfo info;
    info.manual = manual;
    info.category = category;
    info.name = name;
    info.body = body;
    info.file = file;
    info.line = line;
    registry().push_back(std::move(info));
}

fs::path artifactsDir() { return g_artifacts; }

void removeAll(const fs::path& path)
{
    std::error_code ec;
    fs::remove_all(path, ec);
    if (!ec || !fs::exists(path, ec))
        return;
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow, ec);
    fs::remove_all(path, ec);
}

double timeBudgetMs(double ms)
{
    const char* slack = std::getenv("GGUI_TIMING_SLACK");
    const double factor = slack && *slack ? std::strtod(slack, nullptr) : 1.0;
    return ms * (factor >= 1.0 ? factor : 1.0);
}

void markCurrentTestSkipped(const std::string& reason)
{
    if (g_current)
        g_skipped[g_current] = reason;
}

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
    removeAll(g_root);
    fs::create_directories(g_root);
    g_root = fs::canonical(g_root);
#ifdef _WIN32
    // The long form (TEMP can be an 8.3 name such as RUNNER~1): git and Windows report long ones.
    {
        const std::wstring shortPath = g_root.wstring();
        std::wstring longPath(32768, L'\0');
        const DWORD n = GetLongPathNameW(shortPath.c_str(), longPath.data(), static_cast<DWORD>(longPath.size()));
        if (n > 0 && n < longPath.size()) {
            longPath.resize(n);
            g_root = fs::path(longPath);
        }
    }
#endif
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
        std::printf("%s/%s\n", t.category.c_str(), t.name.c_str());
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
    startWatchdog();
    installCrashReport();
#ifdef _WIN32
    // No dialogs in a test run (nobody clicks them on a CI runner): failed assertions and crashes
    // are reported on stderr and end the run.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        (void)type; // the _Crt* calls are empty macros in release builds
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    std::vector<ImGuiTest*> registered;
    for (const auto& info : registry()) {
        if (info.manual && filter.find(info.category) == std::string::npos)
            continue;
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
    int skipped = 0;
    for (ImGuiTest* t : tests) {
        if (t->Output.Status == ImGuiTestStatus_Error) {
            ++failed;
            std::fprintf(stderr, "FAILED: %s/%s\n", t->Category, t->Name);
        }
        const auto info = g_infoByTest.find(t);
        const auto skip = info == g_infoByTest.end() ? g_skipped.end() : g_skipped.find(info->second);
        if (skip != g_skipped.end() && t->Output.Status == ImGuiTestStatus_Success) {
            ++skipped;
            std::fprintf(stderr, "SKIPPED: %s/%s: %s\n", t->Category, t->Name, skip->second.c_str());
        }
    }
    // A skipped test ends early without errors: the engine counts it as a success.
    std::fprintf(stderr, "ggui: %d/%d tests passed", summary.CountSuccess - skipped, summary.CountTested - skipped);
    if (skipped > 0)
        std::fprintf(stderr, ", %d skipped (git too old)", skipped);
    std::fprintf(stderr, "\n");
    if (m_queued > 0)
        m_exitCode = (summary.CountSuccess == summary.CountTested && failed == 0) ? 0 : 1;
    writeTrace();
    ImGuiTestEngine_Stop(m_engine);
    // Scratch repositories go away with the run (failures were copied to the artifacts
    // directory); GGUI_KEEP_TEST_DIRS=1 keeps them for inspection.
    if (!g_root.empty() && !std::getenv("GGUI_KEEP_TEST_DIRS")) {
        g_app->resetForTest(); // close the last repository first (Windows cannot delete open files)
        std::error_code ec;
        removeAll(g_root);
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
        case ImGuiTestStatus_Success: status = g_skipped.count(it->second) ? "skipped" : "success"; break;
        case ImGuiTestStatus_Error: status = "error"; break;
        case ImGuiTestStatus_Queued:
        case ImGuiTestStatus_Running: status = "incomplete"; break;
        default: break;
        }
        out["tests"].push_back({{"category", t->Category}, {"name", t->Name}, {"status", status},
            {"file", it->second->file}, {"line", it->second->line}});
    }
    std::ofstream f(m_traceFile);
    f << out.dump(2) << '\n';
}

} // namespace ggtest
