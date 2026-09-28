// UI actions that had no test of their own (P3-20 UI-action audit, docs/ui-actions.md): dialog
// keys, menu variants, options inside dialogs and actions in less common repository states.
#include "shell/App.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"
#include "util/Env.hpp"

namespace ggtest {

GG_TEST("ui", "Git required: Quit asks the app to quit; the refused open does not block later opens",
    "APP-PROMPT-GIT-OLD")
{
    const fs::path repo = s.fixture(Recipe::Linear);
    const std::string path = ggui::getEnv("PATH");
    const fs::path oldGit = s.path("old-git");
    fs::create_directories(oldGit);
    s.write(oldGit, "git", "#!/bin/sh\necho 'git version 2.20.0'\n");
    fs::permissions(oldGit / "git", fs::perms::owner_all);
    ggui::setEnv("PATH", oldGit.string() + ":" + path);
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool shown = s.dialogOpen("Git required");
    ggui::setEnv("PATH", path);
    GG_REQUIRE(shown);
    const int quits = s.app.quitRequests();
    s.dialogButton("Git required", "Quit");
    GG_CHECK_EQ(s.app.quitRequests(), quits + 1);
    GG_CHECK(s.app.dialogs().current() == nullptr);
    GG_CHECK(s.session() == nullptr || !s.session()->opened());
    // (The test run keeps the app alive.) The refused open is over: Welcome works again and the
    // repository opens with a good git.
    ctx->Yield(2);
    GG_CHECK((ctx->ItemInfo("//Welcome/###welcome_open").ItemFlags & ImGuiItemFlags_Disabled) == 0);
    GG_CHECK(s.openRepository(repo));
    GG_CHECK(s.waitUntil([&] { return s.session() && s.session()->opened(); }));
    s.settle();
}

} // namespace ggtest
