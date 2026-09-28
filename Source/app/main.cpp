// ggui entry point: command line, logging, platform, the frame loop and the test engine.
//
//   ggui [PATH]                 open PATH (or the most recent repository)
//   ggui --test[=FILTER]        run the integration tests in this binary; exit code = result
//   ggui --smoke                start, render a few frames, exit
//   ggui --headless             no visible window (SDL offscreen driver); also GGUI_HEADLESS=1
//   ggui --trace=FILE           with --test: write the spec-ID traceability data to FILE
//   ggui --shard=I/N            with --test: run only shard I of N
//   ggui --list-tests           print the registered tests and their spec IDs
//   (Credential prompts go through `git gg askpass`, which asks the running ggui.)

#include "platform/Platform.hpp"
#include "shell/App.hpp"
#include "util/FrameProbe.hpp"
#include "util/Logging.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Thread.hpp>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
#include "tests/TestRunner.hpp"
#endif

namespace {

struct Options {
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
        if (std::strcmp(a, "--test") == 0) {
            o.test = true;
        } else if (startsWith(a, "--test=")) {
            o.test = true;
            o.testFilter = a + 7;
        } else if (std::strcmp(a, "--smoke") == 0) {
            o.smoke = true;
        } else if (std::strcmp(a, "--headless") == 0) {
            o.headless = true;
        } else if (std::strcmp(a, "--list-tests") == 0) {
            o.listTests = true;
        } else if (startsWith(a, "--shard=")) {
            std::sscanf(a + 8, "%d/%d", &o.shard, &o.shards);
        } else if (startsWith(a, "--trace=")) {
            o.traceFile = a + 8;
        } else if (a[0] != '-' && o.repoPath.empty()) {
            o.repoPath = a;
        } else {
            std::fprintf(stderr, "ggui: unknown option %s\n", a);
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    const Options options = parseArgs(argc, argv);

    ggui::initLogging();
    gg::registerUiThread();
    gg::git2::initLibrary();

#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    if (options.listTests)
        return ggtest::listTests();
    std::unique_ptr<ggtest::TestRunner> tests;
    if (options.test)
        ggtest::prepareProcessForTests(argv[0]); // isolate HOME, git config, prefs before anything reads them
#else
    if (options.test || options.listTests) {
        std::fprintf(stderr, "ggui: this build has no test engine\n");
        return 2;
    }
#endif

    ggui::Platform platform;
    ggui::PlatformOptions popts;
    popts.headless = options.headless;
    popts.vsync = !options.test;
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
    while (true) {
        platform.pollEvents();
        if (platform.quitRequested() && !app->confirmQuit())
            platform.cancelQuit();
        if (platform.quitRequested())
            break;
        platform.beginFrame();
        const auto frameStart = std::chrono::steady_clock::now();
        app->frame();
        platform.endFrame();
        ggui::frameProbe().record(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
        if (tests) {
            tests->postSwap();
            if (tests->finished())
                break;
        }
#endif
        ++frames;
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
    app.reset();
    platform.shutdown();
#ifdef GGUI_ENABLE_IMGUI_TEST_ENGINE
    tests.reset(); // destroys the test engine context after ImGui's
#endif
    spdlog::shutdown();
    return exitCode;
}
