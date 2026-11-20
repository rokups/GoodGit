// Submodule add refuses a URL with a password (GG-103).
#include "shell/Actions.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

namespace ggtest {

GG_TEST("submodule", "URL with a password is detected")
{
    using ggui::Actions;
    GG_CHECK(!Actions::submoduleUrlProblem("https://user:TOKEN@host/r.git").empty());
    GG_CHECK(!Actions::submoduleUrlProblem("https://user%3ATOKEN@host/r.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("https://user@host/r.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("https://user:@host/r.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("https://host/a@b:c/r.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("git@github.com:x/r.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("ssh://git@host/x.git").empty());
    GG_CHECK(Actions::submoduleUrlProblem("/srv/repos/a:b@c").empty());
}

GG_TEST("submodule", "Add is refused for a URL with a password")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    const std::string before = s.gitOut(repo, {"status", "--porcelain"});
    const std::string modules = s.gitOut(repo, {"show", ":.gitmodules"});
    s.session()->actions().submoduleAdd("https://user:TOKEN@example.invalid/r.git", "other", "");
    GG_REQUIRE(s.dialogOpen("add submodule other"));
    s.dialogButton("add submodule other", "OK");
    GG_REQUIRE(s.settle());
    GG_CHECK_STR_EQ(s.gitOut(repo, {"status", "--porcelain"}), before);
    GG_CHECK_STR_EQ(s.gitOut(repo, {"show", ":.gitmodules"}), modules);
}

GG_TEST("submodule", "Add dialog does not run for a URL with a password")
{
    const fs::path repo = s.fixture(Recipe::Submodules);
    GG_REQUIRE(s.openRepository(repo));
    s.session()->showAddSubmoduleDialog();
    GG_REQUIRE(s.dialogOpen("Add submodule"));
    s.dialogText("Add submodule", "url", "https://user:TOKEN@example.invalid/r.git");
    s.dialogText("Add submodule", "path", "other");
    s.dialogButton("Add submodule", "Add");
    GG_REQUIRE(s.settle());
    // The button is disabled: no action runs and no error dialog opens.
    GG_CHECK(!s.dialogOpen("add submodule other", 1.0f));
}

} // namespace ggtest
