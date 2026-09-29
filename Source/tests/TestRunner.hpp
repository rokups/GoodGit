// Runs the registered GG_TEST scenarios through Dear ImGui Test Engine inside ggui.
#pragma once

#include <memory>
#include <string>

struct ImGuiTestEngine;

namespace ggui {
class App;
class Platform;
} // namespace ggui

namespace ggtest {

// Called at startup when --test is given, before anything reads HOME, git config or
// preferences: creates the test root and isolates the process environment.
void prepareProcessForTests(const char* argv0);

// Prints the registered tests (category/name). Returns the process exit code.
int listTests();

class TestRunner {
public:
    TestRunner(ggui::App& app, ggui::Platform& platform);
    ~TestRunner();
    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // Registers all tests and queues those matching `filter` (test engine filter syntax).
    // `shard`/`shards`: run only every shards-th test starting at shard (CI sharding).
    void start(const std::string& filter, const std::string& traceFile, int shard = 0, int shards = 1);
    void postSwap();
    bool finished() const;
    int exitCode() const { return m_exitCode; }
    // Stops the engine coroutine and writes the trace file. Call before ImGui shuts down.
    void stop();

private:
    void writeTrace();

    ggui::App& m_app;
    ggui::Platform& m_platform;
    ImGuiTestEngine* m_engine = nullptr;
    std::string m_traceFile;
    int m_exitCode = 0;
    int m_queued = 0;
    bool m_stopped = false;
};

} // namespace ggtest
