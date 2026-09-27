// Phase 0 harness self-checks (P0-07 … P0-10) and the UI-thread assertion (P1-01).
#include "shell/App.hpp"
#include "shell/Settings.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

#include <libgg/Git2.hpp>
#include <libgg/Thread.hpp>

namespace ggtest {

GG_TEST("harness", "smoke: welcome screen", "HARNESS-SMOKE", "APP-WELCOME-TAGLINE")
{
    GG_REQUIRE(s.waitIdle());
    ctx->SetRef("Welcome");
    GG_CHECK(ctx->ItemExists("##welcome_path"));
    GG_CHECK(ctx->ItemExists("Open"));
    // The tagline speaks Git, not "changes".
    ImGuiWindow* w = ctx->GetWindowByRef("//Welcome");
    GG_REQUIRE(w != nullptr);
    GG_CHECK(s.itemExists("//##Toolbar/###tb_open"));
}

GG_TEST("harness", "isolation from user config and settings", "HARNESS-ISOLATION")
{
    // HOME, XDG_CONFIG_HOME and GIT_CONFIG_GLOBAL point into this test's directory.
    GG_CHECK_STR_EQ(ggui::getEnv("HOME"), s.home().string());
    GG_CHECK_STR_EQ(ggui::getEnv("GIT_CONFIG_GLOBAL"), (s.home() / ".gitconfig").string());
    GG_CHECK_STR_EQ(ggui::getEnv("GIT_CONFIG_NOSYSTEM"), "1");
    GG_CHECK_STR_EQ(s.gitOut(s.root(), {"config", "--global", "user.email"}), "test@example.com");
    // Nothing from a system config file can leak in either.
    auto sys = s.gitMayFail(s.root(), {"config", "--system", "--list"});
    GG_CHECK(sys.out.empty());
    // ggui's preferences live in this test's directory.
    GG_CHECK_STR_EQ(ggui::Settings::prefDir().string(), (s.root() / "prefs").string());
    s.app.openSettings();
    ctx->Yield(2);
    ctx->SetRef("Settings");
    s.comboSelect("//Settings/##settings_tabs/General/Theme##theme", "Light");
    GG_REQUIRE(s.waitIdle());
    const std::string saved = s.read(s.root() / "prefs", "settings.json");
    GG_CHECK(saved.find("\"light\"") != std::string::npos);
}

GG_TEST("harness", "fixture recipes build and pass fsck", "HARNESS-FIXTURES")
{
    for (Recipe r : allRecipes()) {
        const fs::path repo = s.fixture(r);
        std::string out;
        const bool ok = s.fsck(repo, &out);
        if (!ok)
            ctx->LogError("recipe %s: fsck failed: %s", recipeName(r), out.c_str());
        GG_CHECK(ok);
    }
}

GG_TEST("harness", "assertion helpers", "HARNESS-ASSERT-HELPERS")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    GG_CHECK_EQ(s.head(repo).size(), static_cast<size_t>(40));
    GG_CHECK(!s.refs(repo).empty());
    GG_CHECK(s.statusPorcelain(repo).empty());
    s.write(repo, "new.txt", "x\n");
    GG_CHECK(s.statusPorcelain(repo).find("? new.txt") != std::string::npos);
    GG_CHECK(s.fsck(repo));
    // Seeded randomizer: the same seed gives the same sequence.
    std::mt19937_64 a(s.seed()), b(s.seed());
    GG_CHECK_EQ(a(), b());
    GG_REQUIRE(s.openRepository(repo));
    GG_CHECK(s.itemExists("//History/##hist_filter"));
}

GG_TEST("threading", "UI-thread call to git trips the assertion", "APP-UI-THREAD-ASSERT")
{
    GG_REQUIRE(gg::threadChecksCompiled());
    const auto previous = gg::setUiThreadCheck(gg::UiThreadCheck::Record);
    gg::clearUiThreadViolations();
    bool ran = false;
    // Deliberately run repository work on the UI thread (only a test does this).
    s.app.post([&ran] {
        gg::git(fs::current_path(), {"--version"});
        ran = true;
    });
    GG_REQUIRE(s.waitUntil([&] { return ran; }));
    const auto violations = gg::uiThreadViolations();
    GG_CHECK_EQ(violations.size(), static_cast<size_t>(1));
    // The same call from a worker thread is fine.
    gg::clearUiThreadViolations();
    gg::git(fs::current_path(), {"--version"});
    GG_CHECK(gg::uiThreadViolations().empty());
    gg::setUiThreadCheck(previous);
}

} // namespace ggtest

namespace ggtest {

GG_TEST("harness", "failure output: screenshot, app log, git command log", "HARNESS-FAILURE-OUTPUT")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    s.git(repo, {"status"});
    TestInfo info;
    info.category = "demo";
    info.name = "failure";
    info.file = __FILE__;
    info.line = __LINE__;
    const fs::path out = s.path("artifacts");
    writeFailureOutput(ctx, info, s, out);
    ctx->Yield(3);
    GG_CHECK(fs::exists(out / "app.log"));
    GG_CHECK(s.read(out, "git-commands.log").find("git status") != std::string::npos);
    GG_CHECK(s.read(out, "info.txt").find("seed: " + std::to_string(s.seed())) != std::string::npos);
    const std::string png = s.read(out, "screenshot.png");
    GG_CHECK(png.size() > 1000 && png.compare(1, 3, "PNG") == 0);
}

} // namespace ggtest

namespace ggtest {

GG_TEST("harness", "large fixture", "HARNESS-FIXTURES")
{
    const fs::path repo = s.largeFixture();
    const int commits = std::stoi(s.gitOut(repo, {"rev-list", "--count", "--all"}));
    const auto refs = s.refs(repo);
    const int files = std::stoi(s.gitOut(repo, {"ls-files"}).empty() ? "0" : std::to_string(gg::splitLines(s.gitOut(repo, {"ls-files"})).size()));
    ctx->LogInfo("large fixture: %d commits, %zu refs, %d files", commits, refs.size(), files);
    GG_CHECK(commits >= 100000);
    GG_CHECK(refs.size() >= 5000);
    GG_CHECK(files >= 50000);
}

} // namespace ggtest

namespace ggtest {

GG_TEST("harness", "transport fixtures: git daemon and ssh shim", "HARNESS-FIXTURES")
{
    const fs::path served = s.path("served");
    fs::create_directories(served);
    s.git(served, {"init", "-q", "--bare", "-b", "main", (served / "repo.git").string()});
    const fs::path work = s.fixture(Recipe::Linear);
    const std::string url = s.startGitDaemon(served);
    GG_REQUIRE(!url.empty());
    s.git(work, {"push", "-q", url + "repo.git", "main"});
    GG_CHECK(s.gitOut(served / "repo.git", {"rev-parse", "main"}) == s.head(work));
    const std::string ssh = s.installSshShim();
    const fs::path clone = s.path("ssh-clone");
    s.git(s.root(), {"clone", "-q", ssh + (served / "repo.git").string(), clone.string()});
    GG_CHECK(s.head(clone) == s.head(work));
    s.track(served / "repo.git");
    s.track(clone);
}

} // namespace ggtest
