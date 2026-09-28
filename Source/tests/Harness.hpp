// Integration test harness (REBUILD_PLAN §8). Tests are Dear ImGui Test Engine tests that run
// inside the real ggui binary, drive the UI like a user and check both the UI and the
// repository on disk.
//
//   GG_TEST("history", "reveal commit", "HIST-REVEAL", "HIST-REVEAL-CANCEL") {
//       auto repo = s.fixture(Recipe::Linear);
//       s.openRepository(repo);
//       ...
//   }
//
// Every test declares the spec IDs it covers (P0-03 catalogue). Each test runs in its own
// temporary directory with an isolated HOME, XDG_CONFIG_HOME, GIT_CONFIG_GLOBAL, preferences
// directory and a PATH that contains the git-gg under test (P0-08).
#pragma once

#include <libgg/GitRunner.hpp>

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <random>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ggui {
class App;
class Session;
}

namespace ggtest {

namespace fs = std::filesystem;

class Scenario;
using TestBody = void (*)(ImGuiTestContext* ctx, Scenario& s);

struct TestInfo {
    std::string category;
    std::string name;
    std::vector<std::string> specs;
    TestBody body = nullptr;
    const char* file = nullptr;
    int line = 0;
};

std::vector<TestInfo>& registry();

// Directory for cached fixtures, resolved before the test environment is isolated.
fs::path fixtureCacheDir();
// Directory for failure output and explicit screenshots (--artifacts, default ./test-artifacts).
fs::path artifactsDir();

class Scenario;
// Writes screenshot.png, app.log, git-commands.log and info.txt for a failing test into `dir`.
void writeFailureOutput(ImGuiTestContext* ctx, const TestInfo& info, const Scenario& s, const fs::path& dir);

struct Registrar {
    Registrar(const char* category, const char* name, std::initializer_list<const char*> specs, TestBody body,
        const char* file, int line);
};

#define GG_TEST_CONCAT2(a, b) a##b
#define GG_TEST_CONCAT(a, b) GG_TEST_CONCAT2(a, b)
#define GG_TEST(category, name, ...)                                                                     \
    static void GG_TEST_CONCAT(ggTestBody_, __LINE__)(ImGuiTestContext * ctx, ::ggtest::Scenario & s);   \
    static const ::ggtest::Registrar GG_TEST_CONCAT(ggTestReg_, __LINE__)(                              \
        category, name, {__VA_ARGS__}, &GG_TEST_CONCAT(ggTestBody_, __LINE__), __FILE__, __LINE__);     \
    static void GG_TEST_CONCAT(ggTestBody_, __LINE__)([[maybe_unused]] ImGuiTestContext * ctx,          \
        [[maybe_unused]] ::ggtest::Scenario & s)

// Check helpers that record a failure in the test engine and continue.
#define GG_CHECK(expr) IM_CHECK_NO_RET(expr)
#define GG_CHECK_EQ(a, b) IM_CHECK_EQ_NO_RET(a, b)
#define GG_CHECK_STR_EQ(a, b) IM_CHECK_STR_EQ_NO_RET(std::string(a).c_str(), std::string(b).c_str())
// Fatal: stop the test.
#define GG_REQUIRE(expr) IM_CHECK(expr)

// Repository fixture recipes (P0-09). All are built with plain git.
enum class Recipe {
    Empty,            // git init, nothing committed (unborn HEAD)
    Linear,           // main with 5 commits, one file per commit
    Merges,           // main with a feature branch merged (and a second unmerged branch)
    ManyRefs,         // 200 branches, 100 tags
    LinkedWorktrees,  // main + two linked worktrees (one locked, one stale)
    WithRemote,       // clone of a local bare repo (file://), upstream set, ahead 1 behind 1
    Bare,             // bare repository with history
    Sha256,           // --object-format=sha256 with history
    Unborn,           // alias of Empty with an untracked file
    MidMerge,         // merge stopped with a conflict
    MidRebase,        // interactive rebase stopped with a conflict
    MidRebaseApply,   // git rebase (apply backend) stopped with a conflict
    MidCherryPick,    // cherry-pick stopped with a conflict
    MidRevert,        // revert stopped with a conflict
    Bisecting,        // bisect in progress
    Submodules,       // superproject with one submodule
    Lfs,              // LFS-like clean/smudge filter (local test filter, no git-lfs needed)
    TextEdgeCases,    // CRLF, no final newline, binary, marker-like text
    Conflicted2,      // commit containing a two-sided first-class conflict
    ConflictedN,      // commit containing a three-sided first-class conflict
    WorkingChanges,   // staged, unstaged, untracked, renamed, intent-to-add changes
    Stashes,          // three stashes incl. index and untracked parts
};
const char* recipeName(Recipe r);
std::vector<Recipe> allRecipes();

// Per-test scenario: temp dirs, git steps, fixtures, UI helpers.
class Scenario {
public:
    Scenario(ImGuiTestContext* ctx, ggui::App& app, fs::path root, std::uint64_t seed);

    ImGuiTestContext* ctx;
    ggui::App& app;

    const fs::path& root() const { return m_root; }
    fs::path home() const { return m_root / "home"; }
    fs::path path(const std::string& rel) const { return m_root / rel; }

    // ---- git steps ------------------------------------------------------------------------
    // Runs git in `cwd`; a non-zero exit fails the test (and returns the result).
    gg::RunResult git(const fs::path& cwd, std::vector<std::string> args, std::string input = {});
    // Runs git; failure is allowed.
    gg::RunResult gitMayFail(const fs::path& cwd, std::vector<std::string> args, std::string input = {});
    // stdout of a successful git command, trimmed.
    std::string gitOut(const fs::path& cwd, std::vector<std::string> args);
    // `git gg …` through PATH (the git-gg under test).
    gg::RunResult gitgg(const fs::path& cwd, std::vector<std::string> args, std::string input = {});
    // Any program.
    gg::RunResult run(const fs::path& cwd, std::vector<std::string> args, std::string input = {});

    // ---- files ----------------------------------------------------------------------------
    void write(const fs::path& repo, const std::string& rel, const std::string& content);
    std::string read(const fs::path& repo, const std::string& rel);
    void commitFile(const fs::path& repo, const std::string& rel, const std::string& content, const std::string& message);

    // ---- fixtures -------------------------------------------------------------------------
    // Builds `recipe` under root()/<name> (name defaults to the recipe name). Tracked for the
    // post-test fsck.
    fs::path fixture(Recipe recipe, const std::string& name = {});
    // The large read-only fixture (>= 100k commits, >= 5k refs, >= 50k files), generated once
    // with git fast-import into the fixture cache (GGUI_FIXTURE_CACHE, default
    // ~/.cache/ggui-fixtures) and reused by later runs (P0-11). Tests must not modify it.
    fs::path largeFixture();
    // ---- transport fixtures (no network) ---------------------------------------------------
    // Starts `git daemon` on 127.0.0.1 serving `baseDir` (export-all, receive-pack enabled)
    // and returns its git:// base URL. Stopped at the end of the test.
    std::string startGitDaemon(const fs::path& baseDir);
    // Installs an SSH shim as GIT_SSH_COMMAND: "ssh host cmd" runs cmd locally. When
    // `password` is not empty the shim first asks SSH_ASKPASS and fails unless it answers
    // with that password. Returns the ssh:// style URL prefix to use ("ssh://test@localhost").
    std::string installSshShim(const std::string& password = {});
    ~Scenario();
    // Marks a repository for the post-test fsck/plain-git validity check.
    void track(const fs::path& repo);
    const std::vector<fs::path>& tracked() const { return m_tracked; }

    // ---- repository readers (plain git) -----------------------------------------------------
    std::string head(const fs::path& repo);                 // full HEAD id ("" when unborn)
    std::string revParse(const fs::path& repo, const std::string& rev);
    std::vector<std::string> refs(const fs::path& repo);    // "<name> <id>" lines
    std::string statusPorcelain(const fs::path& repo);      // git status --porcelain=v2 -z
    bool fsck(const fs::path& repo, std::string* output = nullptr);
    // Git transparency (rule 2, REBUILD_PLAN §9), checked after every test: no ref under refs/gg/
    // except those the test itself planted (old gg leftovers for the C3 cleanup), and $GIT_COMMON_DIR/gg
    // holds only the journal, disposable caches, the managed-hook runner and journal bookkeeping.
    bool gitTransparent(const fs::path& repo, std::string* why = nullptr);
    // Every file under .git with its bytes, except disposable caches (the "byte-identical" check).
    std::map<std::string, std::string> gitDirBytes(const fs::path& repo);

    // ---- UI helpers -----------------------------------------------------------------------
    // Yields frames until `pred` is true or `seconds` elapse. Returns pred().
    bool waitUntil(const std::function<bool()>& pred, float seconds = 20.0f);
    // Waits until the app has no pending engine work and the event queue is drained.
    bool waitIdle(float seconds = 30.0f);
    // Opens a repository through the Welcome screen's path field (keyboard path).
    [[nodiscard]] bool openRepository(const fs::path& repo);
    bool itemExists(const char* ref);
    // Reference to child window `child` inside window `parent` ("//Diff", "##diff_body"),
    // usable as a prefix: s.child("//Diff", "##diff_body") + "/###line_4".
    std::string child(const char* parent, const char* child);
    // ---- dialogs (Forms) ------------------------------------------------------------------
    // Waits until the dialog with this title is shown.
    bool dialogOpen(const char* title, float seconds = 10.0f);
    // Types into a text field without pressing Enter (Enter would submit the dialog).
    void setText(const std::string& ref, const std::string& text);
    void dialogText(const char* title, const char* field, const std::string& text);
    void dialogCheck(const char* title, const char* field, const char* label, bool on = true);
    void dialogButton(const char* title, const char* button);
    // Waits for the running mutation(s) to finish and the UI to settle.
    bool settle(float seconds = 60.0f);
    // Saves the current frame as <artifacts>/screens/<name>.png (for looking at the UI).
    void screenshot(const std::string& name);
    // Clicks the merge row's expand toggle (merges start collapsed) and waits for the side history.
    bool expandMerge(const std::string& mergeHex);
    // Text drawn by a window in the last frame, one entry per baseline (glyphs mapped back from
    // the draw list; icons appear as "<icon>"). Finds plain text that has no item ID.
    struct DrawnGlyph {
        float baseline;
        float x;
        unsigned codepoint;
        ImU32 col;
    };
    static std::vector<DrawnGlyph> drawnGlyphs(ImGuiWindow* window);
    std::vector<std::string> drawnText(const char* windowRef);
    bool textShown(const char* windowRef, const std::string& text);
    // A full commit ID is drawn with its first `shortLen` characters in the text colour and the
    // rest dimmed (UF-14).
    // Whether a filled shape (hover/selection highlight, button frame) is drawn behind an item.
    bool itemDrawsBackground(const char* ref);
    bool idShownDimmed(const char* windowRef, const std::string& hex, size_t shortLen);
    // Waits for an error popup and closes it with OK; false if none appeared.
    bool dismissError(float seconds = 20.0f);
    // Brings a docked panel's tab to the front (e.g. "Tags" behind "Branches").
    void showPanel(const char* name);
    // Visible text of an item's label (before "##"), from the test engine (max 31 bytes).
    std::string itemText(const char* ref);
    ggui::Session* session();
    // Escapes '/' for use inside a test-engine reference path.
    static std::string escapeRef(const std::string& text);
    // Runs another ggui process (the binary under test) with extra environment.
    gg::RunResult runGgui(std::vector<std::string> args,
        std::vector<std::pair<std::string, std::optional<std::string>>> env = {});
    // Puts an executable script named `name` first on PATH for this test; returns its log file
    // (each invocation appends its arguments, one per line).
    fs::path fakeTool(const std::string& name, const std::string& body = {});
    // Opens the combo `combo` (any ref, "**/" wildcards allowed) and clicks `item` in it.
    void comboSelect(const char* combo, const char* item);
    // Opens the context menu of `ref` and clicks `path` in it ("Copy/ID" for submenus).
    void contextMenu(const char* ref, const char* path);
    std::string itemLabel(const char* ref);
    // Text of the clipboard.
    std::string clipboard();

    // ---- randomness -----------------------------------------------------------------------
    std::uint64_t seed() const { return m_seed; }
    std::mt19937_64& rng() { return m_rng; }

private:
    fs::path m_root;
    std::vector<fs::path> m_tracked;
    std::uint64_t m_seed;
    std::mt19937_64 m_rng;
    int m_commitCounter = 0;
    std::set<std::string> m_plantedGgRefs; // refs/gg/* names the test passed to git itself
    std::vector<fs::path> m_daemonPidFiles;
};

} // namespace ggtest
