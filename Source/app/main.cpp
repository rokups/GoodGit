// ggui entry point: command line, logging, platform, the frame loop and the test engine.
//
//   ggui [PATH]                 open PATH (or the most recent repository)
//   ggui --version | --help     print the version or the options, exit (no window)
//   ggui --smoke                start, render a few frames, exit
//   ggui --headless             no visible window (SDL offscreen driver); also GGUI_HEADLESS=1
// Test builds only (GGUI_ENABLE_IMGUI_TEST_ENGINE; release builds reject these with exit code 2):
//   ggui --test[=FILTER]        run the integration tests in this binary; exit code = result
//   ggui --trace=FILE           with --test: write the per-test results (JSON) to FILE
//   ggui --shard=I/N            with --test: run only shard I of N
//   ggui --list-tests           print the registered tests
//   (Credential prompts go through `git gg askpass`, which asks the running ggui.)

#include "platform/Platform.hpp"
#include "shell/App.hpp"
#include "util/FrameProbe.hpp"
#include "util/Env.hpp"
#include "util/FrameScheduler.hpp"
#include "util/Frames.hpp"
#include "util/Logging.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Launch.hpp>
#include <libgg/SequenceEditorLink.hpp>
#include <libgg/Thread.hpp>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
#include "tests/TestRunner.hpp"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Options {
    bool version = false;
    bool help = false;
    std::string testOnlyOption; // release builds: a test option that was given (rejected)
    bool test = false;
    std::string testFilter;
    bool smoke = false;
    bool headless = false;
    bool listTests = false;
    std::string traceFile;
    int shard = 0;
    int shards = 1;
    std::string repoPath;
};

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

Options parseArgs(int argc, char** argv)
{
    Options o;
    if (const char* h = std::getenv("GGUI_HEADLESS"); h && *h && std::strcmp(h, "0") != 0)
        o.headless = true;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0) {
            o.version = true;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            o.help = true;
        } else if (std::strcmp(a, "--smoke") == 0) {
            o.smoke = true;
        } else if (std::strcmp(a, "--headless") == 0) {
            o.headless = true;
        }
#ifndef GGUI_ENABLE_IMGUI_TEST_ENGINE
        else if (std::strcmp(a, "--test") == 0 || startsWith(a, "--test=") || std::strcmp(a, "--list-tests") == 0
            || startsWith(a, "--shard=") || startsWith(a, "--trace=")) {
            if (o.testOnlyOption.empty())
                o.testOnlyOption = a;
        }
#else
        else if (std::strcmp(a, "--test") == 0) {
            o.test = true;
        } else if (startsWith(a, "--test=")) {
            o.test = true;
            o.testFilter = a + 7;
        } else if (std::strcmp(a, "--list-tests") == 0) {
            o.listTests = true;
        } else if (startsWith(a, "--shard=")) {
            std::sscanf(a + 8, "%d/%d", &o.shard, &o.shards);
        } else if (startsWith(a, "--trace=")) {
            o.traceFile = a + 8;
        }
#endif
        else if (a[0] != '-' && o.repoPath.empty()) {
            o.repoPath = a;
        } else {
            std::fprintf(stderr, "ggui: unknown option %s\n", a);
        }
    }
    return o;
}

// ggui is a GUI-subsystem program on Windows (release builds): without redirected output, print
// to the console it was started from, if any.
void attachConsole()
{
#ifdef _WIN32
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

const char* kUsage =
    "usage: ggui [<path>]\n\n"
    "Opens the Git repository at <path> (default: the most recent one).\n\n"
    "    --version    print the version and exit\n"
    "    --help       print this help and exit\n"
    "    --headless   no visible window (also GGUI_HEADLESS=1)\n"
    "    --smoke      start, render a few frames and exit\n"
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    "    --test[=FILTER] [--shard=I/N] [--trace=FILE]\n"
    "                 run the integration tests in this binary\n"
    "    --list-tests print the registered tests\n"
#endif
    ;

} // namespace

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    const Options options = parseArgs(argc, argv);
    if (options.version || options.help || !options.testOnlyOption.empty())
        attachConsole();
    if (!options.testOnlyOption.empty()) {
        std::fprintf(stderr, "ggui: unknown option %s (this build has no test engine)\n", options.testOnlyOption.c_str());
        return 2;
    }
    if (options.version) {
        std::printf("ggui %s\n", GGUI_VERSION);
        return 0;
    }
    if (options.help) {
        std::fputs(kUsage, stdout);
        return 0;
    }

    ggui::initLogging();
    // A repository that another ggui has open: that window is raised, this process ends (no window of its own).
    if (!options.test && !options.repoPath.empty()) {
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(options.repoPath, ec);
        const std::filesystem::path gitDir = gg::seqlink::resolveGitDir(ec ? std::filesystem::path(options.repoPath) : abs.lexically_normal());
        if (!gitDir.empty() && gg::seqlink::raiseOther(gg::seqlink::gitDirKey(gitDir), ggui::processId())) {
            spdlog::info("{} is open in another ggui window", options.repoPath);
            spdlog::shutdown();
            return 0;
        }
    }
    gg::registerUiThread();
    gg::git2::initLibrary();

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    if (options.listTests)
        return ggtest::listTests();
    std::unique_ptr<ggtest::TestRunner> tests;
    if (options.test)
        ggtest::prepareProcessForTests(argv[0]); // isolate HOME, git config, prefs before anything reads them
#endif

    ggui::Platform platform;
    ggui::PlatformOptions popts;
    popts.headless = options.headless;
    popts.vsync = !options.test;
    // Tests, smoke and headless runs need fast frames: they never sleep in the event wait.
    const bool idleWait = !options.test && !options.smoke && !options.headless;
    popts.idleWait = idleWait;
    std::string error;
    if (!platform.init(popts, error)) {
        spdlog::critical("{}", error);
        std::fprintf(stderr, "ggui: %s\n", error.c_str());
        return 1;
    }

    ggui::AppOptions appOptions;
    appOptions.initialPath = options.repoPath;
    appOptions.autoOpen = !options.test;
    appOptions.argv0 = argv[0];
    auto app = std::make_unique<ggui::App>(platform, appOptions);

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    if (options.test) {
        tests = std::make_unique<ggtest::TestRunner>(*app, platform);
        tests->start(options.testFilter, options.traceFile, options.shard, options.shards);
    }
#endif

    int frames = 0;
    int exitCode = 0;
    int waitMs = 0;
    int pendingFrames = 0; // frames that widgets asked for and the loop did not draw yet
    ggui::FrameScheduler scheduler;
    // GGUI_FRAME_LOG=1: once a second, log the frame rate and the number of waits.
    const std::string frameLogValue = ggui::getEnv("GGUI_FRAME_LOG");
    const bool frameLog = !frameLogValue.empty() && frameLogValue != "0";
    auto logStart = std::chrono::steady_clock::now();
    long long logFrames = ggui::frameProbe().frames;
    int logWaits = 0;
    while (true) {
        platform.pollEvents(waitMs);
        if (waitMs > 0)
            ++logWaits;
        if (platform.quitRequested() && !app->confirmQuit())
            platform.cancelQuit();
        if (platform.quitRequested())
            break;
        platform.beginFrame();
        const auto frameStart = std::chrono::steady_clock::now();
        app->frame();
        const auto appEnd = std::chrono::steady_clock::now();
        platform.endFrame();
        const auto presentEnd = std::chrono::steady_clock::now();
        ggui::frameProbe().record(std::chrono::duration<double, std::milli>(appEnd - frameStart).count(),
            std::chrono::duration<double, std::milli>(presentEnd - appEnd).count());
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
        if (tests) {
            tests->postSwap();
            if (tests->finished())
                break;
        }
#endif
        ++frames;
        // Always take the requests, so the counter does not grow in the modes that never sleep.
        // A request for n frames gives n frames: the counter falls by one in each frame.
        pendingFrames = std::max(pendingFrames - 1, ggui::takeFrameRequests());
        const auto now = std::chrono::steady_clock::now();
        waitMs = 0;
        if (idleWait) {
            ggui::FrameScheduler::Input input;
            input.eventSeen = platform.eventsSeen();
            input.woken = platform.woken();
            input.anyMouseDown = ImGui::IsAnyMouseDown();
            input.framesRequested = pendingFrames > 0;
            input.wantTextInput = ImGui::GetIO().WantTextInput;
            input.focused = platform.focused();
            input.minimized = platform.minimized() || platform.hidden();
            input.now = now;
            waitMs = scheduler.waitMs(input);
        }
        if (frameLog && now - logStart >= std::chrono::seconds(1)) {
            const long long total = ggui::frameProbe().frames;
            spdlog::info("frames/s {} waited {}", total - logFrames, logWaits);
            logStart = now;
            logFrames = total;
            logWaits = 0;
        }
        if (options.smoke && frames >= 10 && app->idle())
            break;
        if (app->quitRequested()) {
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
            if (tests) {
                app->clearQuit(); // the test run decides when to exit
                continue;
            }
#endif
            break;
        }
    }

    app->shutdown();
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    if (tests) {
        tests->stop();
        exitCode = tests->exitCode();
    }
#endif
    // A staged update that the user chose to install: the script waits for this process to end.
    std::filesystem::path updateScript;
    if (!options.test && app->restartForUpdate()) {
        if (const auto script = app->updateScript())
            updateScript = *script;
    }
    app.reset();
    platform.shutdown();
    if (!updateScript.empty()) {
        gg::DetachedProcess child;
        std::string spawnError;
        if (gg::spawnScriptDetached(updateScript, {std::to_string(ggui::processId())}, child, spawnError)) {
            spdlog::info("started the update script {}", updateScript.string());
            gg::release(child);
        } else {
            spdlog::error("cannot start the update script {}: {}", updateScript.string(), spawnError);
            // takeResult reads this file at the next start and tells the user.
            std::ofstream result(updateScript.parent_path() / "result.txt", std::ios::binary | std::ios::trunc);
            result << "failed: cannot start the update script\n";
        }
    }
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    tests.reset(); // destroys the test engine context after ImGui's
#endif
    spdlog::shutdown();
    return exitCode;
}
