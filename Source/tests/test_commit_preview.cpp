// The commit preview under a commit input follows the loaded history (GG-124).
#include "panels/HistoryPanel.hpp"
#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "shell/RevResolve.hpp"
#include "shell/Session.hpp"
#include "tests/Harness.hpp"

#include <imgui_internal.h>

#include <string>

namespace ggtest {

GG_TEST("commit_preview", "the preview of a commit that is not loaded yet updates when more history loads")
{
    // More commits than one history page: the root commit is not loaded at first.
    const int count = ggui::HistoryPanel::kPageSize + 50;
    const fs::path repo = s.fixture(Recipe::Empty);
    std::string stream = "reset refs/heads/main\n";
    for (int i = 0; i < count; ++i) {
        stream += "commit refs/heads/main\nmark :" + std::to_string(i + 1) + "\ncommitter T <t@example.com> " +
                  std::to_string(1700000000 + i) + " +0000\ndata 3\nc" + std::to_string(i % 10) + "\n";
        if (i > 0)
            stream += "from :" + std::to_string(i) + "\n";
        stream += "\n";
    }
    s.git(repo, {"fast-import", "--quiet"}, stream);
    s.git(repo, {"symbolic-ref", "HEAD", "refs/heads/main"});
    const std::string root = s.gitOut(repo, {"rev-list", "--max-parents=0", "main"});
    GG_REQUIRE(s.openRepository(repo));
    auto& history = s.session()->history();
    GG_REQUIRE(s.waitUntil([&] { return !history.loading() && !history.rows().empty(); }, 60.0f));
    GG_REQUIRE(history.row(ggui::core::Oid::fromHex(root)) == nullptr);

    ggui::Form f;
    f.title = "Preview probe";
    f.add(ggui::commitField(*s.session(), "rev", "Rev", root));
    f.buttons.push_back({"Close", {}});
    s.app.dialogs().open(std::move(f));
    ctx->Yield(3);
    GG_CHECK(s.textShown("//$FOCUSED", "loaded history"));

    history.showMore();
    ctx->Yield(2);
    GG_REQUIRE(s.waitUntil([&] { return !history.loading(); }, 60.0f));
    GG_REQUIRE(history.row(ggui::core::Oid::fromHex(root)) != nullptr);
    ctx->Yield(3);
    GG_CHECK(!s.textShown("//$FOCUSED", "loaded history"));
    s.app.dialogs().closeAll();
}

} // namespace ggtest
